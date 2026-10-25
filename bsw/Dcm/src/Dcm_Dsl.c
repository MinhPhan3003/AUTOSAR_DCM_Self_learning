/**
 * @file    Dcm_Dsl.c
 * @brief   Diagnostic Session Layer (DSL): reception from PduR, transmission
 *          to PduR, session state and S3Server timer.
 *
 * Reference: AUTOSAR CP R25-11 SWS_DiagnosticCommunicationManager
 *   - 7.2.4.2 Forward requests from the PduR module to the DSD submodule
 *   - 7.2.4.3 Dcm_CopyTxData / Dcm_TpTxConfirmation
 *   - 7.2.4.4 Forward responses from the DSD submodule to the PduR module
 *   - 7.2.4.11 Manage session state, 7.2.4.13 Non-default sessions
 *
 * Request path : PduR -> Dcm_StartOfReception -> Dcm_CopyRxData (n times)
 *                -> Dcm_TpRxIndication -> [Dcm_MainFunction] -> DSD -> DSP
 * Response path: DSL -> PduR_DcmTransmit -> Dcm_CopyTxData (n times)
 *                -> Dcm_TpTxConfirmation -> [Dcm_MainFunction] -> DSD -> DSP
 *
 * The PduR callbacks "might be called in interrupt context" (SWS_Dcm 8.4).
 * They therefore only copy data and change state; request processing and
 * confirmation handling are deferred to Dcm_MainFunction [Practice].
 */
#include "Dcm_Internal.h"
#include "PduR_Dcm.h"
#include "SchM_Dcm.h"
#if (DCM_DEV_ERROR_DETECT == STD_ON)
#include "Det.h"
#endif

/* ======================== Local types ===================================== */
typedef enum
{
    DCM_DSL_STATE_IDLE,            /* connection free                         */
    DCM_DSL_STATE_RECEIVING,       /* between StartOfReception and RxIndication */
    DCM_DSL_STATE_REQUEST_READY,   /* complete request, waiting for MainFunction */
    DCM_DSL_STATE_TX_PENDING,      /* PduR_DcmTransmit accepted               */
    DCM_DSL_STATE_TX_CONFIRMED     /* TpTxConfirmation received               */
} Dcm_DslStateType;

typedef struct
{
    Dcm_DslStateType State;
    PduIdType        RxPduId;
    PduLengthType    RxExpectedLength;
    PduLengthType    RxCopiedLength;
    PduLengthType    TxLength;
    PduLengthType    TxCopiedLength;
    Std_ReturnType   TxResult;
    Dcm_SesCtrlType  Session;
    boolean          S3Running;
    uint32           S3RemainingMs;
} Dcm_DslContextType;

/* ======================== Local data ====================================== */
static boolean            Dcm_Initialized = FALSE;
static Dcm_DslContextType Dcm_DslContext;
static uint8              Dcm_DslRxBuffer[DCM_CFG_DSL_BUFFER_SIZE];
static uint8              Dcm_DslTxBuffer[DCM_CFG_DSL_BUFFER_SIZE];

/* ======================== Local helpers =================================== */
static void Dcm_DslReportDetError(uint8 ApiId, uint8 ErrorId)
{
#if (DCM_DEV_ERROR_DETECT == STD_ON)
    (void)Det_ReportError(DCM_MODULE_ID, DCM_INSTANCE_ID, ApiId, ErrorId);
#else
    (void)ApiId;
    (void)ErrorId;
#endif
}

/* Common development error checks of the PduR callbacks [SWS_Dcm_00040]. */
static Std_ReturnType Dcm_DslCheckApi(uint8 ApiId, boolean IdValid, boolean PtrValid)
{
    Std_ReturnType result = E_NOT_OK;

    if (Dcm_Initialized == FALSE)
    {
        Dcm_DslReportDetError(ApiId, DCM_E_UNINIT);
    }
    else if (IdValid == FALSE)
    {
        Dcm_DslReportDetError(ApiId, DCM_E_PARAM);
    }
    else if (PtrValid == FALSE)
    {
        Dcm_DslReportDetError(ApiId, DCM_E_PARAM_POINTER);
    }
    else
    {
        result = E_OK;
    }
    return result;
}

static boolean Dcm_DslIsRxPduIdValid(PduIdType id)
{
    return (id < DCM_CFG_NUM_PROTOCOL_RX) ? TRUE : FALSE;
}

static void Dcm_DslCopyBytes(uint8* Dest, const uint8* Src, PduLengthType Length)
{
    PduLengthType index;

    for (index = 0u; index < Length; index++)
    {
        Dest[index] = Src[index];
    }
}

static void Dcm_DslStopS3Timer(void)
{
    Dcm_DslContext.S3Running = FALSE;
}

/* S3Server runs only in a non-default session [SWS_Dcm_01668].
 * Exact start/stop points are defined in ISO 14229-2 (not provided):
 * here S3 is restarted when the connection becomes idle [Inference]. */
static void Dcm_DslRestartS3Timer(void)
{
    if (Dcm_DslContext.Session != DCM_DEFAULT_SESSION)
    {
        Dcm_DslContext.S3RemainingMs = DCM_CFG_S3_SERVER_TIMEOUT_MS;
        Dcm_DslContext.S3Running     = TRUE;
    }
    else
    {
        Dcm_DslStopS3Timer();
    }
}

/* Free the connection: the DcmPduId is blocked from RxIndication until the
 * Tx confirmation (or until processing ends without response)
 * [SWS_Dcm_00241]. */
static void Dcm_DslReleaseConnection(void)
{
    SchM_Enter_Dcm_DCM_EXCLUSIVE_AREA_0();
    Dcm_DslContext.State          = DCM_DSL_STATE_IDLE;
    Dcm_DslContext.RxCopiedLength = 0u;
    Dcm_DslContext.TxCopiedLength = 0u;
    Dcm_DslRestartS3Timer();
    SchM_Exit_Dcm_DCM_EXCLUSIVE_AREA_0();
}

/* ======================== Init / session API ============================== */
void Dcm_Init(const Dcm_ConfigType* ConfigPtr)
{
    (void)ConfigPtr; /* pre-compile variant: no post-build data */

    Dcm_DslContext.State            = DCM_DSL_STATE_IDLE;
    Dcm_DslContext.RxPduId          = 0u;
    Dcm_DslContext.RxExpectedLength = 0u;
    Dcm_DslContext.RxCopiedLength   = 0u;
    Dcm_DslContext.TxLength         = 0u;
    Dcm_DslContext.TxCopiedLength   = 0u;
    Dcm_DslContext.TxResult         = E_NOT_OK;
    Dcm_DslContext.Session          = DCM_DEFAULT_SESSION; /* [SWS_Dcm_00034] */
    Dcm_DslStopS3Timer();
    Dcm_DslContext.S3RemainingMs    = 0u;

    DsdInternal_Init();
    DspInternal_Init();
    Dcm_Initialized = TRUE;
}

/* [SWS_Dcm_00339] */
Std_ReturnType Dcm_GetSesCtrlType(Dcm_SesCtrlType* SesCtrlType)
{
    Std_ReturnType result = Dcm_DslCheckApi(DCM_API_ID_GETSESCTRLTYPE, TRUE,
                                            (SesCtrlType != NULL_PTR) ? TRUE : FALSE);
    if (result == E_OK)
    {
        *SesCtrlType = Dcm_DslContext.Session;
    }
    return result;
}

/* [SWS_Dcm_00022] */
void DslInternal_SetSesCtrlType(Dcm_SesCtrlType SesCtrlType)
{
    Dcm_DslContext.Session = SesCtrlType;
}

Dcm_SesCtrlType DslInternal_GetSesCtrlType(void)
{
    return Dcm_DslContext.Session;
}

/* ======================== Reception (PduR -> Dcm) ========================= */
static BufReq_ReturnType Dcm_DslAcceptReception(PduIdType id,
                                                PduLengthType TpSduLength,
                                                PduLengthType* bufferSizePtr)
{
    BufReq_ReturnType result = BUFREQ_E_NOT_OK;

    SchM_Enter_Dcm_DCM_EXCLUSIVE_AREA_0();
    /* A new request on a busy connection is declined [SWS_Dcm_01678]. */
    if (Dcm_DslContext.State == DCM_DSL_STATE_IDLE)
    {
        Dcm_DslContext.State            = DCM_DSL_STATE_RECEIVING;
        Dcm_DslContext.RxPduId          = id;
        Dcm_DslContext.RxExpectedLength = TpSduLength;
        Dcm_DslContext.RxCopiedLength   = 0u;
        Dcm_DslStopS3Timer();
        *bufferSizePtr = DCM_CFG_DSL_BUFFER_SIZE;
        result = BUFREQ_OK;
    }
    SchM_Exit_Dcm_DCM_EXCLUSIVE_AREA_0();
    return result;
}

/* [SWS_Dcm_00094] */
BufReq_ReturnType Dcm_StartOfReception(PduIdType id,
                                       const PduInfoType* info,
                                       PduLengthType TpSduLength,
                                       PduLengthType* bufferSizePtr)
{
    BufReq_ReturnType result = BUFREQ_E_NOT_OK;

    /* info only carries MetaData for generic connections, which this
     * example does not support (SWS_Dcm 7.2.4.5). The payload is copied by
     * the following Dcm_CopyRxData calls [SWS_Dcm_00443]. */
    (void)info;

    if (Dcm_DslCheckApi(DCM_API_ID_STARTOFRECEPTION, Dcm_DslIsRxPduIdValid(id),
                        (bufferSizePtr != NULL_PTR) ? TRUE : FALSE) == E_OK)
    {
        if (TpSduLength == 0u)
        {
            result = BUFREQ_E_NOT_OK;                     /* [SWS_Dcm_00642] */
        }
        else if (TpSduLength > DCM_CFG_DSL_BUFFER_SIZE)
        {
            result = BUFREQ_E_OVFL;                       /* [SWS_Dcm_00444] */
        }
        else
        {
            result = Dcm_DslAcceptReception(id, TpSduLength, bufferSizePtr);
        }
    }
    return result;
}

static BufReq_ReturnType Dcm_DslCopyFromLowerLayer(const PduInfoType* info,
                                                   PduLengthType* bufferSizePtr)
{
    BufReq_ReturnType result = BUFREQ_OK;
    const PduLengthType freeSize =
        (PduLengthType)(DCM_CFG_DSL_BUFFER_SIZE - Dcm_DslContext.RxCopiedLength);

    if (info->SduLength == 0u)
    {
        *bufferSizePtr = freeSize;                        /* [SWS_Dcm_00996] */
    }
    else if ((info->SduLength > freeSize) || (info->SduDataPtr == NULL_PTR))
    {
        result = BUFREQ_E_NOT_OK;
    }
    else
    {
        Dcm_DslCopyBytes(&Dcm_DslRxBuffer[Dcm_DslContext.RxCopiedLength],
                         info->SduDataPtr, info->SduLength);
        Dcm_DslContext.RxCopiedLength += info->SduLength;
        *bufferSizePtr = (PduLengthType)(freeSize - info->SduLength); /* [SWS_Dcm_00443] */
    }
    return result;
}

/* [SWS_Dcm_00556] */
BufReq_ReturnType Dcm_CopyRxData(PduIdType id,
                                 const PduInfoType* info,
                                 PduLengthType* bufferSizePtr)
{
    BufReq_ReturnType result = BUFREQ_E_NOT_OK;

    if (Dcm_DslCheckApi(DCM_API_ID_COPYRXDATA, Dcm_DslIsRxPduIdValid(id),
                        ((info != NULL_PTR) && (bufferSizePtr != NULL_PTR)) ? TRUE : FALSE) == E_OK)
    {
        SchM_Enter_Dcm_DCM_EXCLUSIVE_AREA_0();
        if ((Dcm_DslContext.State == DCM_DSL_STATE_RECEIVING) && (Dcm_DslContext.RxPduId == id))
        {
            result = Dcm_DslCopyFromLowerLayer(info, bufferSizePtr);
        }
        SchM_Exit_Dcm_DCM_EXCLUSIVE_AREA_0();
    }
    return result;
}

/* [SWS_Dcm_00093] */
void Dcm_TpRxIndication(PduIdType id, Std_ReturnType result)
{
    boolean receptionFailed = FALSE;

    if (Dcm_DslCheckApi(DCM_API_ID_TPRXINDICATION, Dcm_DslIsRxPduIdValid(id), TRUE) == E_OK)
    {
        SchM_Enter_Dcm_DCM_EXCLUSIVE_AREA_0();
        if ((Dcm_DslContext.State == DCM_DSL_STATE_RECEIVING) && (Dcm_DslContext.RxPduId == id))
        {
            /* Forward to DSD only after RxIndication with E_OK [SWS_Dcm_00111].
             * Length cross-check is a defensive addition [Inference]. */
            if ((result == E_OK) &&
                (Dcm_DslContext.RxCopiedLength == Dcm_DslContext.RxExpectedLength))
            {
                Dcm_DslContext.State = DCM_DSL_STATE_REQUEST_READY;
            }
            else
            {
                receptionFailed = TRUE; /* buffer not evaluated [SWS_Dcm_00344] */
            }
        }
        SchM_Exit_Dcm_DCM_EXCLUSIVE_AREA_0();
    }

    if (receptionFailed == TRUE)
    {
        Dcm_DslReleaseConnection();
    }
}

/* ======================== Transmission (Dcm -> PduR) ====================== */
static void Dcm_DslTransmitResponse(PduLengthType ResLength)
{
    PduInfoType pduInfo;

    /* Only the length is given to PduR; the data is fetched later by
     * Dcm_CopyTxData (SWS_Dcm 7.2.4.4). */
    pduInfo.SduDataPtr  = NULL_PTR;
    pduInfo.MetaDataPtr = NULL_PTR;
    pduInfo.SduLength   = ResLength;

    SchM_Enter_Dcm_DCM_EXCLUSIVE_AREA_0();
    Dcm_DslContext.TxLength       = ResLength;
    Dcm_DslContext.TxCopiedLength = 0u;
    Dcm_DslContext.State          = DCM_DSL_STATE_TX_PENDING;
    SchM_Exit_Dcm_DCM_EXCLUSIVE_AREA_0();

    /* [SWS_Dcm_00115] */
    if (PduR_DcmTransmit(DCM_CFG_PDUR_DIAG_TX_PDU_ID, &pduInfo) != E_OK)
    {
        /* No TpTxConfirmation will follow; no repetition [SWS_Dcm_00118]. */
        DsdInternal_TxConfirmation(E_NOT_OK);
        Dcm_DslReleaseConnection();
    }
}

/* Handle the retry parameter of Dcm_CopyTxData (SWS_Dcm 8.4.4). The whole
 * response stays in the Tx buffer until TpTxConfirmation, so TP_DATACONF and
 * TP_CONFPENDING need no action. TP_DATARETRY rewinds the copy position. */
static Std_ReturnType Dcm_DslApplyTxRetry(const RetryInfoType* retry)
{
    Std_ReturnType result = E_OK;

    if ((retry != NULL_PTR) && (retry->TpDataState == TP_DATARETRY))
    {
        if (retry->TxTpDataCnt <= Dcm_DslContext.TxCopiedLength)
        {
            Dcm_DslContext.TxCopiedLength -= retry->TxTpDataCnt;
        }
        else
        {
            result = E_NOT_OK;
        }
    }
    return result;
}

static BufReq_ReturnType Dcm_DslCopyToLowerLayer(const PduInfoType* info,
                                                 PduLengthType* availableDataPtr)
{
    BufReq_ReturnType result = BUFREQ_OK;
    const PduLengthType remaining =
        (PduLengthType)(Dcm_DslContext.TxLength - Dcm_DslContext.TxCopiedLength);

    if (info->SduLength == 0u)
    {
        *availableDataPtr = remaining;
    }
    else if (info->SduLength > remaining)
    {
        result = BUFREQ_E_BUSY;          /* not enough Tx data, nothing copied */
    }
    else if (info->SduDataPtr == NULL_PTR)
    {
        result = BUFREQ_E_NOT_OK;
    }
    else
    {
        Dcm_DslCopyBytes(info->SduDataPtr,
                         &Dcm_DslTxBuffer[Dcm_DslContext.TxCopiedLength], info->SduLength);
        Dcm_DslContext.TxCopiedLength += info->SduLength;
        /* never more than the bytes still to be sent [SWS_Dcm_00350] */
        *availableDataPtr = (PduLengthType)(remaining - info->SduLength);
    }
    return result;                                   /* [SWS_Dcm_00346] */
}

/* [SWS_Dcm_00092] */
BufReq_ReturnType Dcm_CopyTxData(PduIdType id,
                                 const PduInfoType* info,
                                 const RetryInfoType* retry,
                                 PduLengthType* availableDataPtr)
{
    BufReq_ReturnType result = BUFREQ_E_NOT_OK;

    if (Dcm_DslCheckApi(DCM_API_ID_COPYTXDATA,
                        (id == DcmConf_DcmDslProtocolTx_DiagTx) ? TRUE : FALSE,
                        ((info != NULL_PTR) && (availableDataPtr != NULL_PTR)) ? TRUE : FALSE) == E_OK)
    {
        SchM_Enter_Dcm_DCM_EXCLUSIVE_AREA_0();
        if ((Dcm_DslContext.State == DCM_DSL_STATE_TX_PENDING) &&
            (Dcm_DslApplyTxRetry(retry) == E_OK))
        {
            result = Dcm_DslCopyToLowerLayer(info, availableDataPtr);
        }
        SchM_Exit_Dcm_DCM_EXCLUSIVE_AREA_0();
    }
    return result;
}

/* [SWS_Dcm_00351] */
void Dcm_TpTxConfirmation(PduIdType id, Std_ReturnType result)
{
    if (Dcm_DslCheckApi(DCM_API_ID_TPTXCONFIRMATION,
                        (id == DcmConf_DcmDslProtocolTx_DiagTx) ? TRUE : FALSE, TRUE) == E_OK)
    {
        SchM_Enter_Dcm_DCM_EXCLUSIVE_AREA_0();
        if (Dcm_DslContext.State == DCM_DSL_STATE_TX_PENDING)
        {
            /* Tx buffer is unlocked from here on [SWS_Dcm_00352]. */
            Dcm_DslContext.TxResult = result;
            Dcm_DslContext.State    = DCM_DSL_STATE_TX_CONFIRMED;
        }
        SchM_Exit_Dcm_DCM_EXCLUSIVE_AREA_0();
    }
}

/* ======================== Main function =================================== */
static void Dcm_DslProcessRequest(void)
{
    Dcm_DsdRequestType request;
    PduLengthType      resLength = 0u;

    request.Data     = Dcm_DslRxBuffer;
    request.Length   = Dcm_DslContext.RxCopiedLength;
    request.RxPduId  = Dcm_DslContext.RxPduId;
    request.AddrType = Dcm_DslProtocolRxCfg[Dcm_DslContext.RxPduId].AddrType;

    if (DsdInternal_ProcessRequest(&request, Dcm_DslTxBuffer,
                                   DCM_CFG_DSL_BUFFER_SIZE, &resLength) == TRUE)
    {
        Dcm_DslTransmitResponse(resLength);   /* [SWS_Dcm_00237] */
    }
    else
    {
        Dcm_DslReleaseConnection();           /* no response [SWS_Dcm_00238] */
    }
}

static void Dcm_DslProcessTxConfirmation(void)
{
    DsdInternal_TxConfirmation(Dcm_DslContext.TxResult);  /* [SWS_Dcm_00117] */
    Dcm_DslReleaseConnection();
}

/* Back to default session when S3Server elapses [SWS_Dcm_01670]. */
static void Dcm_DslProcessS3Timer(void)
{
    if ((Dcm_DslContext.S3Running == TRUE) && (Dcm_DslContext.State == DCM_DSL_STATE_IDLE))
    {
        if (Dcm_DslContext.S3RemainingMs > DCM_CFG_TASK_TIME_MS)
        {
            Dcm_DslContext.S3RemainingMs -= DCM_CFG_TASK_TIME_MS;
        }
        else
        {
            Dcm_DslStopS3Timer();
            DslInternal_SetSesCtrlType(DCM_DEFAULT_SESSION);
        }
    }
}

/* [SWS_Dcm_00053] */
void Dcm_MainFunction(void)
{
    if (Dcm_Initialized == TRUE)
    {
        /* State is read without lock: REQUEST_READY and TX_CONFIRMED are only
         * left by this function, and callbacks do not modify them. */
        if (Dcm_DslContext.State == DCM_DSL_STATE_REQUEST_READY)
        {
            Dcm_DslProcessRequest();
        }
        else if (Dcm_DslContext.State == DCM_DSL_STATE_TX_CONFIRMED)
        {
            Dcm_DslProcessTxConfirmation();
        }
        else
        {
            /* nothing to process */
        }
        Dcm_DslProcessS3Timer();
    }
}
