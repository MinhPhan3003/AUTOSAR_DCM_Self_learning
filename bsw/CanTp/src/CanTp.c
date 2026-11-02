/**
 * @file    CanTp.c
 * @brief   CanTp API entry points: development error checks, dispatch of
 *          received N-PDUs and confirmations, main function, shared helpers.
 *
 * Reference: AUTOSAR CP R25-11 SWS_CANTransportLayer
 *   - 7.1.1 Initialization and shutdown
 *   - 7.4   Error classification
 *   - 8.3   Function definitions, 8.4 Callback notifications
 */
#include "CanTp_Internal.h"
#include "SchM_CanTp.h"
#include "Det.h"

/* ======================== Local data ====================================== */
/* CANTP_OFF after power up [SWS_CanTp_00168]; status variable is 0 before
 * any API is called [SWS_CanTp_00161]. */
static boolean CanTp_Initialized = FALSE;

/* ======================== Error reporting ================================= */
static void CanTp_ReportDevError(uint8 ApiId, uint8 ErrorId)
{
#if (CANTP_DEV_ERROR_DETECT == STD_ON)
    (void)Det_ReportError(CANTP_MODULE_ID, CANTP_INSTANCE_ID, ApiId, ErrorId);
#else
    (void)ApiId;
    (void)ErrorId;
#endif
}

/* Runtime errors are always reported (Det_ReportRuntimeError is a mandatory
 * interface of CanTp [SWS_CanTp_00216]). */
void CanTp_ReportRuntimeError(uint8 ApiId, uint8 ErrorId)
{
    (void)Det_ReportRuntimeError(CANTP_MODULE_ID, CANTP_INSTANCE_ID, ApiId, ErrorId);
}

/* Common development error checks in the order: init, id, pointer. */
static Std_ReturnType CanTp_CheckApi(uint8 ApiId, boolean IdValid, uint8 IdErrorId, boolean PtrValid)
{
    Std_ReturnType result = E_NOT_OK;

    if (CanTp_Initialized == FALSE)
    {
        CanTp_ReportDevError(ApiId, CANTP_E_UNINIT);               /* [SWS_CanTp_00031] */
    }
    else if (IdValid == FALSE)
    {
        CanTp_ReportDevError(ApiId, IdErrorId);
    }
    else if (PtrValid == FALSE)
    {
        CanTp_ReportDevError(ApiId, CANTP_E_PARAM_POINTER);
    }
    else
    {
        result = E_OK;
    }
    return result;
}

/* ======================== Shared helpers ================================== */

/* Counts one main function tick. Returns TRUE when the timer reaches 0. */
boolean CanTp_TimerElapsed(uint16* Timer)
{
    if (*Timer > 0u)
    {
        (*Timer)--;
    }
    return (*Timer == 0u) ? TRUE : FALSE;
}

/* An N-PDU shared by several connections stays unavailable until its
 * TxConfirmation [SWS_CanTp_00075] [SWS_CanTp_00248]. */
boolean CanTp_IsLSduBusy(PduIdType LSduRTxId)
{
    return ((CanTp_RxIsLSduBusy(LSduRTxId) == TRUE) || (CanTp_TxIsLSduBusy(LSduRTxId) == TRUE)) ? TRUE : FALSE;
}

/* Rx N-SDU that receives SF/FF/CF on this N-PDU. */
static boolean CanTp_FindRxNSdu(PduIdType RxNPduId, PduIdType* RxNSduId)
{
    boolean found = FALSE;
    PduIdType index;

    for (index = 0u; (index < CANTP_CFG_NUM_RX_NSDU) && (found == FALSE); index++)
    {
        if (CanTp_RxNSduCfg[index].RxNPduId == RxNPduId)
        {
            *RxNSduId = index;
            found = TRUE;
        }
    }
    return found;
}

/* Tx N-SDU that receives its FC on this N-PDU. */
static boolean CanTp_FindTxNSduByFc(PduIdType RxFcNPduId, PduIdType* TxNSduId)
{
    boolean found = FALSE;
    PduIdType index;

    for (index = 0u; (index < CANTP_CFG_NUM_TX_NSDU) && (found == FALSE); index++)
    {
        if (CanTp_TxNSduCfg[index].RxFcNPduId == RxFcNPduId)
        {
            *TxNSduId = index;
            found = TRUE;
        }
    }
    return found;
}

static boolean CanTp_IsRxPduIdValid(PduIdType RxPduId)
{
    PduIdType dummy;
    return ((CanTp_FindRxNSdu(RxPduId, &dummy) == TRUE) ||
            (CanTp_FindTxNSduByFc(RxPduId, &dummy) == TRUE)) ? TRUE : FALSE;
}

static boolean CanTp_IsTxConfirmationIdValid(PduIdType TxPduId)
{
    boolean valid = FALSE;
    PduIdType index;

    for (index = 0u; index < CANTP_CFG_NUM_TX_NSDU; index++)
    {
        valid = (CanTp_TxNSduCfg[index].TxConfirmationPduId == TxPduId) ? TRUE : valid;
    }
    for (index = 0u; index < CANTP_CFG_NUM_RX_NSDU; index++)
    {
        valid = ((CanTp_RxNSduCfg[index].TaType == CANTP_PHYSICAL) &&
                 (CanTp_RxNSduCfg[index].TxFcConfirmationPduId == TxPduId)) ? TRUE : valid;
    }
    return valid;
}

/* ======================== Initialization and shutdown (8.3.1, 8.3.3) ===== */

void CanTp_Init(const CanTp_ConfigType* CfgPtr)
{
    (void)CfgPtr;          /* pre-compile configuration: tables are linked in */
    CanTp_RxInit();        /* all connections idle, previous ones lost        */
    CanTp_TxInit();        /* [SWS_CanTp_00030] [SWS_CanTp_00111] [SWS_CanTp_00273] */
    CanTp_Initialized = TRUE;                                      /* CANTP_ON [SWS_CanTp_00170] */
}

void CanTp_Shutdown(void)
{
    if (CanTp_CheckApi(CANTP_API_ID_SHUTDOWN, TRUE, 0u, TRUE) == E_OK)
    {
        /* close all connections without notification [SWS_CanTp_00200] [SWS_CanTp_00202] */
        CanTp_RxInit();
        CanTp_TxInit();
        CanTp_Initialized = FALSE;                                 /* CANTP_OFF */
    }
}

/* ======================== Transmission API (8.3.4, 8.3.5) ================= */

Std_ReturnType CanTp_Transmit(PduIdType TxPduId, const PduInfoType* PduInfoPtr)
{
    Std_ReturnType result = E_NOT_OK;

    if (CanTp_CheckApi(CANTP_API_ID_TRANSMIT, (TxPduId < CANTP_CFG_NUM_TX_NSDU) ? TRUE : FALSE,
                       CANTP_E_INVALID_TX_ID,                      /* [SWS_CanTp_00356] */
                       (PduInfoPtr != NULL_PTR) ? TRUE : FALSE) == E_OK) /* [SWS_CanTp_00321] */
    {
        SchM_Enter_CanTp_CANTP_EXCLUSIVE_AREA_0();                 /* check-and-claim of the N-SDU */
        result = CanTp_TxStart(TxPduId, PduInfoPtr->SduLength);
        SchM_Exit_CanTp_CANTP_EXCLUSIVE_AREA_0();
    }
    return result;
}

Std_ReturnType CanTp_CancelTransmit(PduIdType TxPduId)
{
    Std_ReturnType result = E_NOT_OK;

    if (CanTp_CheckApi(CANTP_API_ID_CANCELTRANSMIT, (TxPduId < CANTP_CFG_NUM_TX_NSDU) ? TRUE : FALSE,
                       CANTP_E_INVALID_TX_ID, TRUE) == E_OK)       /* [SWS_CanTp_00254] */
    {
        result = CanTp_TxCancel(TxPduId);
    }
    return result;
}

/* ======================== Reception API (8.3.6 - 8.3.8) =================== */

Std_ReturnType CanTp_CancelReceive(PduIdType RxPduId)
{
    Std_ReturnType result = E_NOT_OK;

    if (CanTp_CheckApi(CANTP_API_ID_CANCELRECEIVE, (RxPduId < CANTP_CFG_NUM_RX_NSDU) ? TRUE : FALSE,
                       CANTP_E_INVALID_RX_ID, TRUE) == E_OK)       /* [SWS_CanTp_00260] */
    {
        result = CanTp_RxCancel(RxPduId);
    }
    return result;
}

#if (CANTP_CHANGE_PARAMETER_API == STD_ON)
Std_ReturnType CanTp_ChangeParameter(PduIdType id, TPParameterType parameter, uint16 value)
{
    Std_ReturnType result = E_NOT_OK;
    const boolean valueValid = (parameter == TP_STMIN) ? CanTp_FrameIsStminValid(value) :
                               ((value <= CANTP_BS_MAX) ? TRUE : FALSE);

    if (CanTp_CheckApi(CANTP_API_ID_CHANGEPARAMETER, (id < CANTP_CFG_NUM_RX_NSDU) ? TRUE : FALSE,
                       CANTP_E_INVALID_RX_ID, TRUE) != E_OK)       /* [SWS_CanTp_00357] */
    {
        /* error already reported */
    }
    else if ((parameter != TP_STMIN) && (parameter != TP_BS))
    {
        CanTp_ReportDevError(CANTP_API_ID_CHANGEPARAMETER, CANTP_E_PARAM_ID);     /* [SWS_CanTp_00305] */
    }
    else if (valueValid == FALSE)
    {
        CanTp_ReportDevError(CANTP_API_ID_CHANGEPARAMETER, CANTP_E_PARAM_CONFIG); /* [SWS_CanTp_00305] */
    }
    else if (CanTp_RxIsBusy(id) == TRUE)
    {
        /* reception in progress: reject [SWS_CanTp_00303] [SWS_CanTp_00304] */
    }
    else
    {
        CanTp_RxSetParameter(id, parameter, (uint8)value);
        result = E_OK;
    }
    return result;
}
#endif

#if (CANTP_READ_PARAMETER_API == STD_ON)
Std_ReturnType CanTp_ReadParameter(PduIdType id, TPParameterType parameter, uint16* value)
{
    Std_ReturnType result = E_NOT_OK;

    if (CanTp_CheckApi(CANTP_API_ID_READPARAMETER, (id < CANTP_CFG_NUM_RX_NSDU) ? TRUE : FALSE,
                       CANTP_E_INVALID_RX_ID,                      /* [SWS_CanTp_00358] */
                       (value != NULL_PTR) ? TRUE : FALSE) != E_OK)
    {
        /* error already reported */
    }
    else if ((parameter != TP_STMIN) && (parameter != TP_BS))
    {
        CanTp_ReportDevError(CANTP_API_ID_READPARAMETER, CANTP_E_PARAM_ID);       /* [SWS_CanTp_00324] */
    }
    else
    {
        *value = CanTp_RxGetParameter(id, parameter);
        result = E_OK;
    }
    return result;
}
#endif

/* ======================== Main function (8.3.9) =========================== */

void CanTp_MainFunction(void)
{
    /* No DET check: CanTp_MainFunction is excluded from CANTP_E_UNINIT
     * [SWS_CanTp_00293]. Segmentation only in CANTP_ON [SWS_CanTp_00238]. */
    if (CanTp_Initialized == TRUE)
    {
        CanTp_RxMainFunction();   /* N_Ar, N_Br, N_Cr, FC transmission       */
        CanTp_TxMainFunction();   /* N_As, N_Bs, N_Cs, STmin, SF/FF/CF       */
    }
}

/* ======================== Callbacks from LSduR (8.4) ====================== */

void CanTp_RxIndication(PduIdType RxPduId, const PduInfoType* PduInfoPtr)
{
    CanTp_NPciType pci;
    PduIdType nSduId = 0u;

    if (CanTp_CheckApi(CANTP_API_ID_RXINDICATION, CanTp_IsRxPduIdValid(RxPduId), CANTP_E_INVALID_RX_ID, /* [SWS_CanTp_00359] */
                       ((PduInfoPtr != NULL_PTR) && (PduInfoPtr->SduDataPtr != NULL_PTR)) ? TRUE : FALSE) /* [SWS_CanTp_00322] */
        == E_OK)
    {
        pci.Kind = CANTP_FRAME_INVALID;
        if (PduInfoPtr->SduLength > 0u)
        {
            CanTp_FrameDecode(PduInfoPtr, &pci);
        }
        if (pci.Kind == CANTP_FRAME_FC)
        {
            if (CanTp_FindTxNSduByFc(RxPduId, &nSduId) == TRUE)
            {
                CanTp_TxProcessFc(nSduId, &pci, PduInfoPtr);
            }
        }
        else if ((pci.Kind != CANTP_FRAME_INVALID) && (CanTp_FindRxNSdu(RxPduId, &nSduId) == TRUE))
        {
            CanTp_RxProcessNPdu(nSduId, &pci, PduInfoPtr);
        }
        else
        {
            /* unknown or malformed N-PDU: ignore (Table 1) */
        }
    }
}

void CanTp_TxConfirmation(PduIdType TxPduId, Std_ReturnType result)
{
    if (CanTp_CheckApi(CANTP_API_ID_TXCONFIRMATION, CanTp_IsTxConfirmationIdValid(TxPduId),
                       CANTP_E_INVALID_TX_ID, TRUE) == E_OK)       /* [SWS_CanTp_00360] */
    {
        /* One N-PDU has at most one transmission pending [SWS_CanTp_00248]:
         * the first connection waiting on this id owns the confirmation. */
        if (CanTp_TxConfirmFrame(TxPduId, result) == FALSE)
        {
            (void)CanTp_RxConfirmFc(TxPduId, result);
        }
    }
}
