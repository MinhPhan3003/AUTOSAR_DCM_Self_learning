/**
 * @file    CanTp_Tx.c
 * @brief   CanTp transmission: SF / FF / CF out, FC in.
 *
 * Reference: AUTOSAR CP R25-11 SWS_CANTransportLayer
 *   - 7.1.2 Transmit request, 7.1.3 Transmit cancellation
 *   - 7.3.2 N-SDU Transmission
 *   - 7.3.8 N-PDU padding
 *   - 9.3 / 9.4 sequence diagrams
 *
 * CanTp_Transmit only accepts the request; data are copied with
 * PduR_CanTpCopyTxData and frames are sent from CanTp_MainFunction
 * ("launches an internal transmit task", SWS 9.3.3) [Practice].
 *
 * State machine of one Tx N-SDU:
 *
 *   IDLE --CanTp_Transmit--> SEND_FRAME (N_Cs)
 *   SEND_FRAME --CopyTxData OK + LSduR_CanTpTransmit--> WAIT_CONF (N_As)
 *   WAIT_CONF  --TxConfirmation--> IDLE (all data sent, PduR_CanTpTxConfirmation(E_OK))
 *                              --> WAIT_FC     after FF or after the last CF of a block (N_Bs)
 *                              --> WAIT_STMIN  between two CFs (STmin from the FC)
 *                              --> SEND_FRAME  STmin = 0
 *   WAIT_FC    --FC(CTS)--> SEND_FRAME; --FC(WAIT)--> WAIT_FC (N_Bs reloaded)
 *              --FC(OVFLW) / invalid FS--> IDLE with E_NOT_OK
 *   WAIT_STMIN --STmin elapsed--> SEND_FRAME
 */
#include "CanTp_Internal.h"
#include "PduR_CanTp.h"
#include "LSduR_CanTp.h"

/* ======================== Local types ===================================== */
typedef enum
{
    CANTP_TX_IDLE,         /* CANTP_TX_WAIT in the SWS                       */
    CANTP_TX_SEND_FRAME,   /* copy data and send SF/FF/CF, N_Cs running      */
    CANTP_TX_WAIT_CONF,    /* frame given to LSduR, N_As running             */
    CANTP_TX_WAIT_FC,      /* N_Bs running                                   */
    CANTP_TX_WAIT_STMIN    /* minimum separation time between two CFs       */
} CanTp_TxStateType;

typedef struct
{
    CanTp_TxStateType   State;
    CanTp_FrameKindType FrameKind;       /* SF, FF or CF: next / pending frame  */
    PduLengthType       SduLength;       /* total N-SDU length                  */
    PduLengthType       Sent;            /* confirmed payload bytes             */
    PduLengthType       PendingLength;   /* payload of the frame in WAIT_CONF   */
    uint8               NextSn;
    uint8               Bs;              /* BS of the last FC(CTS)              */
    uint8               BlockCounter;    /* CFs sent in the current block       */
    uint16              StminTicks;      /* STmin of the last FC(CTS)           */
    uint16              Timer;           /* N_As, N_Bs, N_Cs or STmin, in ticks */
} CanTp_TxConnType;

/* ======================== Local data ====================================== */
static CanTp_TxConnType CanTp_TxConn[CANTP_CFG_NUM_TX_NSDU];

/* ======================== Helpers ========================================= */

static void CanTp_TxReset(PduIdType TxNSduId)
{
    CanTp_TxConn[TxNSduId] = (CanTp_TxConnType){ 0 };
    CanTp_TxConn[TxNSduId].State = CANTP_TX_IDLE;
}

/* Always notify the upper layer at the end [SWS_CanTp_00177] [SWS_CanTp_00204]. */
static void CanTp_TxFinish(PduIdType TxNSduId, Std_ReturnType Result)
{
    CanTp_TxReset(TxNSduId);
    PduR_CanTpTxConfirmation(CanTp_TxNSduCfg[TxNSduId].PduRTxId, Result);
}

static void CanTp_TxFail(PduIdType TxNSduId, uint8 ApiId, uint8 ErrorId)
{
    CanTp_ReportRuntimeError(ApiId, ErrorId);
    CanTp_TxFinish(TxNSduId, E_NOT_OK);                            /* [SWS_CanTp_00205] */
}

static void CanTp_TxEnterSendFrame(PduIdType TxNSduId)
{
    CanTp_TxConn[TxNSduId].State = CANTP_TX_SEND_FRAME;
    CanTp_TxConn[TxNSduId].Timer = CanTp_TxNSduCfg[TxNSduId].NcsTicks; /* N_Cs before CopyTxData [SWS_CanTp_00167] */
}

/* Writes the N_PCI of the next frame. Returns the PCI length and the number
 * of payload bytes this frame carries. */
static PduLengthType CanTp_TxEncodePci(const CanTp_TxConnType* Conn, uint8* Frame, PduLengthType* DataLength)
{
    PduLengthType pciLength;
    const PduLengthType remaining = (PduLengthType)(Conn->SduLength - Conn->Sent);

    if (Conn->FrameKind == CANTP_FRAME_SF)
    {
        pciLength   = CanTp_FrameEncodeSf(Frame, Conn->SduLength);           /* [SWS_CanTp_00231] */
        *DataLength = Conn->SduLength;
    }
    else if (Conn->FrameKind == CANTP_FRAME_FF)
    {
        pciLength   = CanTp_FrameEncodeFf(Frame, Conn->SduLength);           /* [SWS_CanTp_00232] */
        *DataLength = (PduLengthType)(CANTP_CAN20_DL - pciLength);
    }
    else
    {
        pciLength   = CanTp_FrameEncodeCf(Frame, Conn->NextSn);
        *DataLength = (remaining > CANTP_CF_MAX_DATA_LENGTH) ? CANTP_CF_MAX_DATA_LENGTH : remaining;
    }
    return pciLength;
}

/* Copies the payload of the next frame and gives it to LSduR.
 * Returns FALSE if nothing happened (LSduR N-PDU busy or BUFREQ_E_BUSY). */
static boolean CanTp_TxTrySendFrame(PduIdType TxNSduId)
{
    const CanTp_TxNSduCfgType* cfg = &CanTp_TxNSduCfg[TxNSduId];
    CanTp_TxConnType* conn = &CanTp_TxConn[TxNSduId];
    uint8 frame[CANTP_CAN20_DL];
    PduLengthType pciLength;
    PduLengthType available = 0u;
    PduInfoType info;
    BufReq_ReturnType result = BUFREQ_E_BUSY;

    if (CanTp_IsLSduBusy(cfg->LSduRTxId) == FALSE)                 /* [SWS_CanTp_00248] */
    {
        pciLength        = CanTp_TxEncodePci(conn, frame, &info.SduLength);
        info.SduDataPtr  = &frame[pciLength];
        info.MetaDataPtr = NULL_PTR;
        /* retry = NULL_PTR: no recovery in ISO 15765-2 [SWS_CanTp_00272] */
        result = PduR_CanTpCopyTxData(cfg->PduRTxId, &info, NULL_PTR, &available); /* [SWS_CanTp_00086] */
        if (result == BUFREQ_OK)
        {
            conn->PendingLength = info.SduLength;
            conn->State         = CANTP_TX_WAIT_CONF;
            conn->Timer         = cfg->NasTicks;                   /* N_As [SWS_CanTp_00075] */
            info.SduDataPtr     = frame;
            info.SduLength      = CanTp_FrameFinalize(frame, (PduLengthType)(pciLength + info.SduLength),
                                                      cfg->PaddingActive);       /* [SWS_CanTp_00348] */
            if (LSduR_CanTpTransmit(cfg->LSduRTxId, &info) != E_OK)
            {
                CanTp_TxFail(TxNSduId, CANTP_API_ID_MAINFUNCTION, CANTP_E_COM); /* [SWS_CanTp_00343] */
            }
        }
        else if (result != BUFREQ_E_BUSY)
        {
            CanTp_TxFail(TxNSduId, CANTP_API_ID_MAINFUNCTION, CANTP_E_COM);     /* [SWS_CanTp_00087] */
        }
        else
        {
            /* BUFREQ_E_BUSY: retry in the next main function [SWS_CanTp_00184] */
        }
    }
    return (result != BUFREQ_E_BUSY) ? TRUE : FALSE;
}

static void CanTp_TxSendOrWait(PduIdType TxNSduId)
{
    if ((CanTp_TxTrySendFrame(TxNSduId) == FALSE) &&
        (CanTp_TimerElapsed(&CanTp_TxConn[TxNSduId].Timer) == TRUE))
    {
        CanTp_TxFail(TxNSduId, CANTP_API_ID_MAINFUNCTION, CANTP_E_TX_COM); /* N_Cs [SWS_CanTp_00280] */
    }
}

/* A frame was confirmed by LSduR: decide what comes next. */
static void CanTp_TxAfterConfirmation(PduIdType TxNSduId)
{
    CanTp_TxConnType* conn = &CanTp_TxConn[TxNSduId];
    const uint16 nbsTicks = CanTp_TxNSduCfg[TxNSduId].NbsTicks;

    conn->Sent = (PduLengthType)(conn->Sent + conn->PendingLength);
    if (conn->Sent >= conn->SduLength)
    {
        CanTp_TxFinish(TxNSduId, E_OK);                            /* [SWS_CanTp_00090] */
    }
    else if (conn->FrameKind == CANTP_FRAME_FF)
    {
        conn->NextSn = CANTP_SN_FIRST_CF;
        conn->State  = CANTP_TX_WAIT_FC;
        conn->Timer  = nbsTicks;                                   /* N_Bs after FF [SWS_CanTp_00315] */
    }
    else
    {
        conn->NextSn = (uint8)((conn->NextSn + 1u) & CANTP_SN_MASK);
        conn->BlockCounter++;
        if ((conn->Bs != 0u) && (conn->BlockCounter >= conn->Bs))
        {
            conn->State = CANTP_TX_WAIT_FC;
            conn->Timer = nbsTicks;                                /* N_Bs after a block [SWS_CanTp_00315] */
        }
        else if (conn->StminTicks > 0u)
        {
            conn->State = CANTP_TX_WAIT_STMIN;
            conn->Timer = conn->StminTicks;
        }
        else
        {
            CanTp_TxEnterSendFrame(TxNSduId);
        }
    }
}

static void CanTp_TxProcessConnection(PduIdType TxNSduId)
{
    CanTp_TxConnType* conn = &CanTp_TxConn[TxNSduId];

    switch (conn->State)
    {
        case CANTP_TX_SEND_FRAME:
            CanTp_TxSendOrWait(TxNSduId);
            break;
        case CANTP_TX_WAIT_STMIN:
            if (CanTp_TimerElapsed(&conn->Timer) == TRUE)
            {
                CanTp_TxEnterSendFrame(TxNSduId);
                CanTp_TxSendOrWait(TxNSduId);
            }
            break;
        case CANTP_TX_WAIT_CONF:   /* N_As timeout [SWS_CanTp_00310] */
        case CANTP_TX_WAIT_FC:     /* N_Bs timeout [SWS_CanTp_00316] */
            if (CanTp_TimerElapsed(&conn->Timer) == TRUE)
            {
                CanTp_TxFail(TxNSduId, CANTP_API_ID_MAINFUNCTION, CANTP_E_TX_COM); /* [SWS_CanTp_00229] */
            }
            break;
        default:
            break;
    }
}

/* ======================== Internal interface ============================== */

void CanTp_TxInit(void)
{
    PduIdType index;

    for (index = 0u; index < CANTP_CFG_NUM_TX_NSDU; index++)
    {
        CanTp_TxReset(index);
    }
}

Std_ReturnType CanTp_TxStart(PduIdType TxNSduId, PduLengthType SduLength)
{
    CanTp_TxConnType* conn = &CanTp_TxConn[TxNSduId];
    Std_ReturnType result = E_NOT_OK;

    if ((conn->State != CANTP_TX_IDLE) || (SduLength == 0u))
    {
        /* N-SDU already in use [SWS_CanTp_00206] [SWS_CanTp_00123]; empty SDU [Inference] */
    }
    else if ((CanTp_TxNSduCfg[TxNSduId].TaType == CANTP_FUNCTIONAL) && (SduLength > CANTP_SF_MAX_DATA_LENGTH))
    {
        CanTp_ReportRuntimeError(CANTP_API_ID_TRANSMIT, CANTP_E_INVALID_TATYPE);   /* [SWS_CanTp_00093] */
    }
    else
    {
        CanTp_TxReset(TxNSduId);
        conn->SduLength = SduLength;   /* only the length is used [SWS_CanTp_00225] [SWS_CanTp_00299] */
        conn->FrameKind = (SduLength <= CANTP_SF_MAX_DATA_LENGTH) ? CANTP_FRAME_SF : CANTP_FRAME_FF;
        CanTp_TxEnterSendFrame(TxNSduId);
        result = E_OK;
    }
    return result;
}

void CanTp_TxProcessFc(PduIdType TxNSduId, const CanTp_NPciType* Pci, const PduInfoType* NPdu)
{
    CanTp_TxConnType* conn = &CanTp_TxConn[TxNSduId];

    if (conn->State != CANTP_TX_WAIT_FC)
    {
        /* FC not awaited: ignore (Table 1) */
    }
    else if ((CanTp_TxNSduCfg[TxNSduId].PaddingActive == TRUE) && (NPdu->SduLength < CANTP_CAN20_DL))
    {
        CanTp_TxFail(TxNSduId, CANTP_API_ID_RXINDICATION, CANTP_E_PADDING);       /* [SWS_CanTp_00349] */
    }
    else if (Pci->Fs == CANTP_FS_CTS)
    {
        conn->Bs           = Pci->Bs;
        conn->StminTicks   = CanTp_FrameStminToTicks(Pci->STmin);
        conn->BlockCounter = 0u;
        conn->FrameKind    = CANTP_FRAME_CF;
        CanTp_TxEnterSendFrame(TxNSduId);
    }
    else if (Pci->Fs == CANTP_FS_WAIT)
    {
        conn->Timer = CanTp_TxNSduCfg[TxNSduId].NbsTicks;          /* restart N_Bs [SWS_CanTp_00315] */
    }
    else
    {
        /* FC(OVFLW) [SWS_CanTp_00309] or invalid FS [SWS_CanTp_00317] */
        CanTp_TxFail(TxNSduId, CANTP_API_ID_RXINDICATION, CANTP_E_COM);
    }
}

void CanTp_TxMainFunction(void)
{
    PduIdType index;

    for (index = 0u; index < CANTP_CFG_NUM_TX_NSDU; index++)
    {
        CanTp_TxProcessConnection(index);
    }
}

boolean CanTp_TxConfirmFrame(PduIdType ConfirmationPduId, Std_ReturnType Result)
{
    boolean consumed = FALSE;
    PduIdType index;

    for (index = 0u; (index < CANTP_CFG_NUM_TX_NSDU) && (consumed == FALSE); index++)
    {
        if ((CanTp_TxConn[index].State == CANTP_TX_WAIT_CONF) &&
            (CanTp_TxNSduCfg[index].TxConfirmationPduId == ConfirmationPduId))
        {
            consumed = TRUE;
            if (Result != E_OK)
            {
                CanTp_TxFail(index, CANTP_API_ID_TXCONFIRMATION, CANTP_E_COM);    /* [SWS_CanTp_00355] */
            }
            else
            {
                CanTp_TxAfterConfirmation(index);
            }
        }
    }
    return consumed;
}

boolean CanTp_TxIsLSduBusy(PduIdType LSduRTxId)
{
    boolean busy = FALSE;
    PduIdType index;

    for (index = 0u; index < CANTP_CFG_NUM_TX_NSDU; index++)
    {
        if ((CanTp_TxConn[index].State == CANTP_TX_WAIT_CONF) &&
            (CanTp_TxNSduCfg[index].LSduRTxId == LSduRTxId))
        {
            busy = TRUE;
        }
    }
    return busy;
}

Std_ReturnType CanTp_TxCancel(PduIdType TxNSduId)
{
    Std_ReturnType result = E_NOT_OK;

    if (CanTp_TxNSduCfg[TxNSduId].CancelTransmitEnabled == FALSE)
    {
        /* CanTpTc = false: feature deactivated [SWS_CanTp_00242] */
    }
    else if (CanTp_TxConn[TxNSduId].State == CANTP_TX_IDLE)
    {
        CanTp_ReportRuntimeError(CANTP_API_ID_CANCELTRANSMIT, CANTP_E_OPER_NOT_SUPPORTED);
    }
    else
    {
        CanTp_TxFinish(TxNSduId, E_NOT_OK);                        /* [SWS_CanTp_00255] [SWS_CanTp_00256] */
        result = E_OK;
    }
    return result;
}
