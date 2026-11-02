/**
 * @file    CanTp_Rx.c
 * @brief   CanTp reception: SF / FF / CF in, FC out.
 *
 * Reference: AUTOSAR CP R25-11 SWS_CANTransportLayer
 *   - 7.3.1 N-SDU Reception
 *   - 7.3.8 N-PDU padding
 *   - 7.3.9 Handling of unexpected N-PDU arrival (Table 1)
 *   - 9.2 / 9.5 sequence diagrams
 *
 * Context split [Practice], following 9.5: SF/FF/CF are processed inside
 * CanTp_RxIndication; FC N-PDUs are transmitted from CanTp_MainFunction.
 *
 * State machine of one Rx N-SDU:
 *
 *   IDLE --SF--> (StartOfReception, CopyRxData, RxIndication) --> IDLE
 *   IDLE --FF--> StartOfReception --+--> SEND_FC(CTS)   buffer >= next block
 *                                   +--> WAIT_BUFFER    buffer <  next block (N_Br)
 *                                   +--> SEND_FC(OVFLW) BUFREQ_E_OVFL
 *   SEND_FC      --LSduR_CanTpTransmit--> WAIT_FC_CONF (N_Ar)
 *   WAIT_FC_CONF --TxConfirmation--> WAIT_CF (CTS) / WAIT_BUFFER (WAIT) / IDLE (OVFLW)
 *   WAIT_CF      --CF--> WAIT_CF (N_Cr) / end of block -> SEND_FC or WAIT_BUFFER
 *                --last CF--> RxIndication(E_OK) --> IDLE
 *   WAIT_BUFFER  --buffer ok--> SEND_FC(CTS); --N_Br expired--> SEND_FC(WAIT)
 */
#include "CanTp_Internal.h"
#include "PduR_CanTp.h"
#include "LSduR_CanTp.h"

/* ======================== Local types ===================================== */
typedef enum
{
    CANTP_RX_IDLE,           /* CANTP_RX_WAIT in the SWS                          */
    CANTP_RX_SEND_FC,        /* FC decided, sent by the next main function       */
    CANTP_RX_WAIT_FC_CONF,   /* FC given to LSduR, N_Ar running                   */
    CANTP_RX_WAIT_CF,        /* N_Cr running                                      */
    CANTP_RX_WAIT_BUFFER     /* upper layer buffer too small, N_Br running        */
} CanTp_RxStateType;

typedef struct
{
    CanTp_RxStateType State;
    boolean           Locked;        /* StartOfReception returned BUFREQ_OK       */
    PduLengthType     SduLength;     /* FF_DL                                     */
    PduLengthType     Received;      /* bytes given to the upper layer            */
    PduLengthType     BufferSize;    /* last bufferSizePtr from the upper layer   */
    uint8             ExpectedSn;
    uint8             BlockCounter;  /* CFs received in the current block         */
    uint8             FcStatus;      /* FS of the FC to send / being sent          */
    uint16            WftCounter;    /* consecutive FC(WAIT) sent                  */
    uint16            Timer;         /* N_Ar, N_Br or N_Cr, in main function ticks */
} CanTp_RxConnType;

/* Largest SDU length the upper layer API can express (PduLengthType). */
#define CANTP_PDU_LENGTH_MAX  ((uint32)((PduLengthType)~(PduLengthType)0u))

/* ======================== Local data ====================================== */
static CanTp_RxConnType CanTp_RxConn[CANTP_CFG_NUM_RX_NSDU];
static uint8            CanTp_RxBs[CANTP_CFG_NUM_RX_NSDU];     /* current BS    */
static uint8            CanTp_RxSTmin[CANTP_CFG_NUM_RX_NSDU];  /* current STmin */

/* ======================== Helpers ========================================= */

static void CanTp_RxReset(PduIdType RxNSduId)
{
    CanTp_RxConn[RxNSduId] = (CanTp_RxConnType){ 0 };
    CanTp_RxConn[RxNSduId].State = CANTP_RX_IDLE;
}

/* End of a reception: notify the upper layer only if it accepted the
 * connection [SWS_CanTp_00084]; BUFREQ_E_NOT_OK / E_OVFL -> no indication
 * [SWS_CanTp_00081] [SWS_CanTp_00318]. */
static void CanTp_RxFinish(PduIdType RxNSduId, Std_ReturnType Result)
{
    const boolean notify = CanTp_RxConn[RxNSduId].Locked;

    CanTp_RxReset(RxNSduId);   /* free before the callback: the upper layer may react at once */
    if (notify == TRUE)
    {
        PduR_CanTpRxIndication(CanTp_RxNSduCfg[RxNSduId].PduRRxId, Result);
    }
}

static void CanTp_RxFail(PduIdType RxNSduId, uint8 ApiId, uint8 ErrorId)
{
    CanTp_ReportRuntimeError(ApiId, ErrorId);
    CanTp_RxFinish(RxNSduId, E_NOT_OK);
}

/* Bytes still needed before the next FC: one block, or the rest if BS = 0. */
static PduLengthType CanTp_RxNextBlockLength(PduIdType RxNSduId)
{
    const CanTp_RxConnType* conn = &CanTp_RxConn[RxNSduId];
    const uint32 remaining = (uint32)conn->SduLength - conn->Received;
    const uint32 block = (uint32)CanTp_RxBs[RxNSduId] * CANTP_CF_MAX_DATA_LENGTH;

    return (PduLengthType)(((CanTp_RxBs[RxNSduId] == 0u) || (block > remaining)) ? remaining : block);
}

/* After FF or after the last CF of a block: FC(CTS) if the buffer is large
 * enough for the next block [SWS_CanTp_00224], otherwise wait with N_Br
 * running [SWS_CanTp_00082] [SWS_CanTp_00325]. */
static void CanTp_RxEvaluateBuffer(PduIdType RxNSduId)
{
    CanTp_RxConnType* conn = &CanTp_RxConn[RxNSduId];

    if (conn->BufferSize >= CanTp_RxNextBlockLength(RxNSduId))
    {
        conn->FcStatus = CANTP_FS_CTS;
        conn->State    = CANTP_RX_SEND_FC;
    }
    else
    {
        conn->State = CANTP_RX_WAIT_BUFFER;
    }
}

/* SF / FF while a reception is running: terminate it with E_NOT_OK and
 * start the new one [SWS_CanTp_00124], Table 1. */
static void CanTp_RxAbortOngoing(PduIdType RxNSduId)
{
    if (CanTp_RxConn[RxNSduId].State != CANTP_RX_IDLE)
    {
        CanTp_RxFail(RxNSduId, CANTP_API_ID_RXINDICATION, CANTP_E_COM);
    }
}

/* ======================== Single Frame ==================================== */

static void CanTp_RxStartSf(PduIdType RxNSduId, const CanTp_NPciType* Pci, const PduInfoType* NPdu)
{
    const PduIdType pduRId = CanTp_RxNSduCfg[RxNSduId].PduRRxId;
    PduLengthType bufferSize = 0u;
    PduInfoType info;
    BufReq_ReturnType result;

    info.SduDataPtr  = &NPdu->SduDataPtr[Pci->PayloadOffset];
    info.MetaDataPtr = NULL_PTR;
    info.SduLength   = (PduLengthType)Pci->DataLength;            /* SF_DL */
    result = PduR_CanTpStartOfReception(pduRId, &info, info.SduLength, &bufferSize); /* [SWS_CanTp_00079] [SWS_CanTp_00329] */
    if (result == BUFREQ_OK)
    {
        if ((bufferSize >= info.SduLength) &&
            (PduR_CanTpCopyRxData(pduRId, &info, &bufferSize) == BUFREQ_OK))
        {
            PduR_CanTpRxIndication(pduRId, E_OK);
        }
        else
        {
            PduR_CanTpRxIndication(pduRId, E_NOT_OK);           /* [SWS_CanTp_00339] */
        }
    }
    /* BUFREQ_E_NOT_OK / BUFREQ_E_OVFL: abort, no indication [SWS_CanTp_00081] [SWS_CanTp_00353] */
}

static void CanTp_RxOnSf(PduIdType RxNSduId, const CanTp_NPciType* Pci, const PduInfoType* NPdu)
{
    /* Padding active: only SF with 8 bytes are accepted [SWS_CanTp_00344] [SWS_CanTp_00345] */
    if ((CanTp_RxNSduCfg[RxNSduId].PaddingActive == TRUE) && (NPdu->SduLength < CANTP_CAN20_DL))
    {
        CanTp_ReportRuntimeError(CANTP_API_ID_RXINDICATION, CANTP_E_PADDING);
    }
    else
    {
        CanTp_RxAbortOngoing(RxNSduId);
        CanTp_RxStartSf(RxNSduId, Pci, NPdu);
    }
}

/* ======================== First Frame ===================================== */

static void CanTp_RxCopyFirstData(PduIdType RxNSduId, const PduInfoType* Info, PduLengthType BufferSize)
{
    CanTp_RxConnType* conn = &CanTp_RxConn[RxNSduId];

    conn->Locked     = TRUE;
    conn->BufferSize = BufferSize;
    if ((BufferSize < Info->SduLength) ||                          /* [SWS_CanTp_00339] */
        (PduR_CanTpCopyRxData(CanTp_RxNSduCfg[RxNSduId].PduRRxId, Info, &conn->BufferSize) != BUFREQ_OK))
    {
        CanTp_RxFinish(RxNSduId, E_NOT_OK);
    }
    else
    {
        conn->Received     = Info->SduLength;
        conn->ExpectedSn   = CANTP_SN_FIRST_CF;
        conn->BlockCounter = 0u;
        conn->WftCounter   = 0u;
        CanTp_RxEvaluateBuffer(RxNSduId);
    }
}

/* Called with FC(OVFLW) already prepared, so every path that does not
 * change the state ends with FC(OVFLW). */
static void CanTp_RxStartFf(PduIdType RxNSduId, const CanTp_NPciType* Pci, const PduInfoType* NPdu)
{
    const CanTp_RxNSduCfgType* cfg = &CanTp_RxNSduCfg[RxNSduId];
    CanTp_RxConnType* conn = &CanTp_RxConn[RxNSduId];
    PduLengthType bufferSize = 0u;
    PduInfoType info;
    BufReq_ReturnType result;

    conn->SduLength  = (PduLengthType)Pci->DataLength;
    info.SduDataPtr  = &NPdu->SduDataPtr[Pci->PayloadOffset];
    info.MetaDataPtr = NULL_PTR;
    info.SduLength   = (PduLengthType)(CANTP_CAN20_DL - Pci->PayloadOffset);
    result = PduR_CanTpStartOfReception(cfg->PduRRxId, &info, conn->SduLength, &bufferSize);
    if (result == BUFREQ_OK)
    {
        CanTp_RxCopyFirstData(RxNSduId, &info, bufferSize);
    }
    else if (result != BUFREQ_E_OVFL)
    {
        CanTp_RxReset(RxNSduId);   /* BUFREQ_E_NOT_OK: no FC, no indication [SWS_CanTp_00081] */
    }
    else
    {
        /* BUFREQ_E_OVFL: FC(OVFLW) already prepared [SWS_CanTp_00318] */
    }
}

static void CanTp_RxOnFf(PduIdType RxNSduId, const CanTp_NPciType* Pci, const PduInfoType* NPdu)
{
    CanTp_RxConnType* conn = &CanTp_RxConn[RxNSduId];

    if (CanTp_RxNSduCfg[RxNSduId].TaType == CANTP_FUNCTIONAL)      /* [SWS_CanTp_00093] */
    {
        CanTp_ReportRuntimeError(CANTP_API_ID_RXINDICATION, CANTP_E_INVALID_TATYPE);
    }
    else
    {
        CanTp_RxAbortOngoing(RxNSduId);
        conn->Timer    = CanTp_RxNSduCfg[RxNSduId].NbrTicks;       /* N_Br starts at FF [SWS_CanTp_00166] */
        conn->FcStatus = CANTP_FS_OVFLW;
        conn->State    = CANTP_RX_SEND_FC;
        /* FF_DL that PduLengthType cannot express -> FC(OVFLW) [Inference] */
        if (Pci->DataLength <= CANTP_PDU_LENGTH_MAX)
        {
            CanTp_RxStartFf(RxNSduId, Pci, NPdu);
        }
    }
}

/* ======================== Consecutive Frame =============================== */

static void CanTp_RxContinueBlock(PduIdType RxNSduId)
{
    CanTp_RxConnType* conn = &CanTp_RxConn[RxNSduId];
    const CanTp_RxNSduCfgType* cfg = &CanTp_RxNSduCfg[RxNSduId];

    conn->BlockCounter++;
    if ((CanTp_RxBs[RxNSduId] != 0u) && (conn->BlockCounter >= CanTp_RxBs[RxNSduId]))
    {
        conn->BlockCounter = 0u;                     /* FC only after every block [SWS_CanTp_00278] */
        conn->Timer        = cfg->NbrTicks;          /* [SWS_CanTp_00166] */
        CanTp_RxEvaluateBuffer(RxNSduId);            /* [SWS_CanTp_00325] */
    }
    else
    {
        conn->Timer = cfg->NcrTicks;                 /* [SWS_CanTp_00312] */
    }
}

static void CanTp_RxOnCf(PduIdType RxNSduId, const CanTp_NPciType* Pci, const PduInfoType* NPdu)
{
    CanTp_RxConnType* conn = &CanTp_RxConn[RxNSduId];
    const PduLengthType remaining = (PduLengthType)(conn->SduLength - conn->Received);
    const boolean isLast = (remaining <= CANTP_CF_MAX_DATA_LENGTH) ? TRUE : FALSE;
    PduInfoType info;

    info.SduDataPtr  = &NPdu->SduDataPtr[Pci->PayloadOffset];
    info.MetaDataPtr = NULL_PTR;
    info.SduLength   = (isLast == TRUE) ? remaining : CANTP_CF_MAX_DATA_LENGTH;

    if (conn->State != CANTP_RX_WAIT_CF)
    {
        /* CF not awaited: ignore (Table 1) */
    }
    else if (Pci->Sn != conn->ExpectedSn)                          /* [SWS_CanTp_00314] */
    {
        CanTp_RxFail(RxNSduId, CANTP_API_ID_RXINDICATION, CANTP_E_COM);
    }
    else if ((isLast == TRUE) && (CanTp_RxNSduCfg[RxNSduId].PaddingActive == TRUE) &&
             (NPdu->SduLength < CANTP_CAN20_DL))                   /* [SWS_CanTp_00346] */
    {
        CanTp_RxFail(RxNSduId, CANTP_API_ID_RXINDICATION, CANTP_E_PADDING);
    }
    else if (NPdu->SduLength < (PduLengthType)(Pci->PayloadOffset + info.SduLength))
    {
        /* CF shorter than the data it must carry: ignore [Inference] */
    }
    else if (PduR_CanTpCopyRxData(CanTp_RxNSduCfg[RxNSduId].PduRRxId, &info, &conn->BufferSize) != BUFREQ_OK)
    {
        CanTp_RxFail(RxNSduId, CANTP_API_ID_RXINDICATION, CANTP_E_COM); /* [SWS_CanTp_00271] */
    }
    else
    {
        conn->Received   = (PduLengthType)(conn->Received + info.SduLength);
        conn->ExpectedSn = (uint8)((conn->ExpectedSn + 1u) & CANTP_SN_MASK);
        if (isLast == TRUE)
        {
            CanTp_RxFinish(RxNSduId, E_OK);                        /* [SWS_CanTp_00084] */
        }
        else
        {
            CanTp_RxContinueBlock(RxNSduId);
        }
    }
}

/* ======================== Main function parts ============================= */

static void CanTp_RxSendFc(PduIdType RxNSduId)
{
    const CanTp_RxNSduCfgType* cfg = &CanTp_RxNSduCfg[RxNSduId];
    CanTp_RxConnType* conn = &CanTp_RxConn[RxNSduId];
    uint8 frame[CANTP_CAN20_DL];
    PduInfoType info;

    if (CanTp_IsLSduBusy(cfg->TxFcLSduRId) == FALSE)               /* [SWS_CanTp_00248] */
    {
        /* Same BS and STmin in every FC of one reception [SWS_CanTp_00067] */
        info.SduLength   = CanTp_FrameEncodeFc(frame, conn->FcStatus, CanTp_RxBs[RxNSduId], CanTp_RxSTmin[RxNSduId]);
        info.SduLength   = CanTp_FrameFinalize(frame, info.SduLength, cfg->PaddingActive); /* [SWS_CanTp_00347] */
        info.SduDataPtr  = frame;
        info.MetaDataPtr = NULL_PTR;
        conn->WftCounter = (conn->FcStatus == CANTP_FS_CTS) ? 0u : conn->WftCounter;
        conn->State      = CANTP_RX_WAIT_FC_CONF;
        conn->Timer      = cfg->NarTicks;
        if (LSduR_CanTpTransmit(cfg->TxFcLSduRId, &info) != E_OK)
        {
            CanTp_RxFail(RxNSduId, CANTP_API_ID_MAINFUNCTION, CANTP_E_COM); /* [SWS_CanTp_00342] */
        }
    }
}

/* N_Br running: ask the upper layer for buffer each main function
 * [SWS_CanTp_00222]; on N_Br expiry send FC(WAIT) [SWS_CanTp_00341], at most
 * WFTmax times [SWS_CanTp_00223]. */
static void CanTp_RxPollBuffer(PduIdType RxNSduId)
{
    CanTp_RxConnType* conn = &CanTp_RxConn[RxNSduId];
    PduInfoType info = { NULL_PTR, NULL_PTR, 0u };

    if (PduR_CanTpCopyRxData(CanTp_RxNSduCfg[RxNSduId].PduRRxId, &info, &conn->BufferSize) != BUFREQ_OK)
    {
        CanTp_RxFail(RxNSduId, CANTP_API_ID_MAINFUNCTION, CANTP_E_COM);
    }
    else if (conn->BufferSize >= CanTp_RxNextBlockLength(RxNSduId))
    {
        conn->FcStatus = CANTP_FS_CTS;                             /* [SWS_CanTp_00224] */
        conn->State    = CANTP_RX_SEND_FC;
    }
    else if (CanTp_TimerElapsed(&conn->Timer) == FALSE)
    {
        /* keep waiting */
    }
    else if (conn->WftCounter >= CanTp_RxNSduCfg[RxNSduId].WftMax)
    {
        CanTp_RxFail(RxNSduId, CANTP_API_ID_MAINFUNCTION, CANTP_E_RX_COM);
    }
    else
    {
        conn->WftCounter++;
        conn->FcStatus = CANTP_FS_WAIT;
        conn->State    = CANTP_RX_SEND_FC;
    }
}

static void CanTp_RxProcessConnection(PduIdType RxNSduId)
{
    CanTp_RxConnType* conn = &CanTp_RxConn[RxNSduId];

    switch (conn->State)
    {
        case CANTP_RX_SEND_FC:
            CanTp_RxSendFc(RxNSduId);
            break;
        case CANTP_RX_WAIT_BUFFER:
            CanTp_RxPollBuffer(RxNSduId);
            break;
        case CANTP_RX_WAIT_FC_CONF:   /* N_Ar timeout [SWS_CanTp_00311] */
        case CANTP_RX_WAIT_CF:        /* N_Cr timeout [SWS_CanTp_00313] */
            if (CanTp_TimerElapsed(&conn->Timer) == TRUE)
            {
                CanTp_RxFail(RxNSduId, CANTP_API_ID_MAINFUNCTION, CANTP_E_RX_COM); /* [SWS_CanTp_00229] */
            }
            break;
        default:
            break;
    }
}

/* ======================== Internal interface ============================== */

void CanTp_RxInit(void)
{
    PduIdType index;

    for (index = 0u; index < CANTP_CFG_NUM_RX_NSDU; index++)
    {
        CanTp_RxReset(index);
        CanTp_RxBs[index]    = CanTp_RxNSduCfg[index].Bs;
        CanTp_RxSTmin[index] = CanTp_RxNSduCfg[index].STmin;
    }
}

void CanTp_RxProcessNPdu(PduIdType RxNSduId, const CanTp_NPciType* Pci, const PduInfoType* NPdu)
{
    switch (Pci->Kind)
    {
        case CANTP_FRAME_SF: CanTp_RxOnSf(RxNSduId, Pci, NPdu); break;
        case CANTP_FRAME_FF: CanTp_RxOnFf(RxNSduId, Pci, NPdu); break;
        case CANTP_FRAME_CF: CanTp_RxOnCf(RxNSduId, Pci, NPdu); break;
        default:             break;   /* FC is handled by the Tx side */
    }
}

void CanTp_RxMainFunction(void)
{
    PduIdType index;

    for (index = 0u; index < CANTP_CFG_NUM_RX_NSDU; index++)
    {
        CanTp_RxProcessConnection(index);
    }
}

boolean CanTp_RxConfirmFc(PduIdType ConfirmationPduId, Std_ReturnType Result)
{
    boolean consumed = FALSE;
    PduIdType index;

    for (index = 0u; (index < CANTP_CFG_NUM_RX_NSDU) && (consumed == FALSE); index++)
    {
        CanTp_RxConnType* conn = &CanTp_RxConn[index];
        if ((conn->State == CANTP_RX_WAIT_FC_CONF) &&
            (CanTp_RxNSduCfg[index].TxFcConfirmationPduId == ConfirmationPduId))
        {
            consumed = TRUE;
            if (Result != E_OK)
            {
                CanTp_RxFail(index, CANTP_API_ID_TXCONFIRMATION, CANTP_E_COM); /* [SWS_CanTp_00355] */
            }
            else if (conn->FcStatus == CANTP_FS_CTS)
            {
                conn->State = CANTP_RX_WAIT_CF;
                conn->Timer = CanTp_RxNSduCfg[index].NcrTicks;     /* [SWS_CanTp_00312] */
            }
            else if (conn->FcStatus == CANTP_FS_WAIT)
            {
                conn->State = CANTP_RX_WAIT_BUFFER;
                conn->Timer = CanTp_RxNSduCfg[index].NbrTicks;     /* reload N_Br [SWS_CanTp_00341] */
            }
            else
            {
                CanTp_RxReset(index);                              /* FC(OVFLW) sent: aborted [SWS_CanTp_00318] */
            }
        }
    }
    return consumed;
}

boolean CanTp_RxIsLSduBusy(PduIdType LSduRTxId)
{
    boolean busy = FALSE;
    PduIdType index;

    for (index = 0u; index < CANTP_CFG_NUM_RX_NSDU; index++)
    {
        if ((CanTp_RxConn[index].State == CANTP_RX_WAIT_FC_CONF) &&
            (CanTp_RxNSduCfg[index].TxFcLSduRId == LSduRTxId))
        {
            busy = TRUE;
        }
    }
    return busy;
}

boolean CanTp_RxIsBusy(PduIdType RxNSduId)
{
    return (CanTp_RxConn[RxNSduId].State != CANTP_RX_IDLE) ? TRUE : FALSE;
}

Std_ReturnType CanTp_RxCancel(PduIdType RxNSduId)
{
    const CanTp_RxConnType* conn = &CanTp_RxConn[RxNSduId];
    Std_ReturnType result = E_NOT_OK;

    if ((conn->State == CANTP_RX_IDLE) || (conn->Locked == FALSE))
    {
        CanTp_ReportRuntimeError(CANTP_API_ID_CANCELRECEIVE, CANTP_E_OPER_NOT_SUPPORTED);
    }
    else if ((conn->State == CANTP_RX_WAIT_CF) &&
             ((PduLengthType)(conn->SduLength - conn->Received) <= CANTP_CF_MAX_DATA_LENGTH))
    {
        /* last CF expected: reject [SWS_CanTp_00262] */
    }
    else
    {
        CanTp_RxFinish(RxNSduId, E_NOT_OK);                        /* [SWS_CanTp_00261] [SWS_CanTp_00263] */
        result = E_OK;
    }
    return result;
}

void CanTp_RxSetParameter(PduIdType RxNSduId, TPParameterType Parameter, uint8 Value)
{
    if (Parameter == TP_STMIN)
    {
        CanTp_RxSTmin[RxNSduId] = Value;
    }
    else
    {
        CanTp_RxBs[RxNSduId] = Value;
    }
}

uint8 CanTp_RxGetParameter(PduIdType RxNSduId, TPParameterType Parameter)
{
    return (Parameter == TP_STMIN) ? CanTp_RxSTmin[RxNSduId] : CanTp_RxBs[RxNSduId];
}
