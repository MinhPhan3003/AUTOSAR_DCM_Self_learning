/**
 * @file    Dcm_Dsd.c
 * @brief   Diagnostic Service Dispatcher (DSD): SID check, verifications,
 *          suppressPosRspMsgIndicationBit, response assembly, confirmation.
 *
 * Reference: AUTOSAR CP R25-11 SWS_DiagnosticCommunicationManager, 7.3.4
 * Order of checks implemented here (subset of [SWS_Dcm_01535] and
 * ISO 14229-1:2013 7.5.2 Figure 5 / 7.5.3.1 Figure 6):
 *   1. SID supported?                       -> NRC 0x11 [SWS_Dcm_00197]
 *   2. SID supported in active session?     -> NRC 0x7F [SWS_Dcm_00211]
 *   3. Minimum length?                      -> NRC 0x13 [SWS_Dcm_00696]
 *   4. Sub-function supported?              -> NRC 0x12 [SWS_Dcm_00273]
 * Not implemented: manufacturer/supplier notification, authentication,
 * security level, mode rules, NRC 0x7E (see README.md).
 */
#include "Dcm_Internal.h"

/* ======================== Local types / data ============================== */
typedef struct
{
    uint8             ServiceId;        /* stored for later use [SWS_Dcm_00198] */
    boolean           NegativeResponse; /* kind of the response being sent      */
    Dcm_IdContextType IdContext;
} Dcm_DsdContextType;

static Dcm_DsdContextType Dcm_DsdContext;

/* ======================== Local helpers =================================== */

/* Request SIDs in the response range are not answered when
 * DcmRespondAllRequest = FALSE [SWS_Dcm_00084]. */
static boolean Dcm_DsdIsIgnoredSid(uint8 Sid)
{
    boolean ignored = FALSE;
#if (DCM_RESPOND_ALL_REQUEST == STD_OFF)
    if (((Sid >= 0x40u) && (Sid <= 0x7Fu)) || (Sid >= 0xC0u))
    {
        ignored = TRUE;
    }
#else
    (void)Sid;
#endif
    return ignored;
}

/* Scan the active "Service Identifier Table" [SWS_Dcm_00193] [SWS_Dcm_00196]. */
static const Dcm_DsdServiceCfgType* Dcm_DsdFindService(uint8 Sid)
{
    const Dcm_DsdServiceCfgType* service = NULL_PTR;
    uint8 index;

    for (index = 0u; (index < DCM_CFG_NUM_SERVICES) && (service == NULL_PTR); index++)
    {
        if (Dcm_DsdServiceTableCfg[index].ServiceId == Sid)
        {
            service = &Dcm_DsdServiceTableCfg[index];
        }
    }
    return service;
}

static boolean Dcm_DsdIsSessionAllowed(const Dcm_DsdServiceCfgType* Service)
{
    const Dcm_SesCtrlType session = DslInternal_GetSesCtrlType();
    boolean allowed = FALSE;
    uint8 index;

    for (index = 0u; (index < Service->NumSessionRefs) && (allowed == FALSE); index++)
    {
        allowed = (Service->SessionRefs[index] == session) ? TRUE : FALSE;
    }
    return allowed;
}

static boolean Dcm_DsdIsSubServiceSupported(const Dcm_DsdServiceCfgType* Service, uint8 SubFunction)
{
    boolean supported = FALSE;
    uint8 index;

    for (index = 0u; (index < Service->NumSubServices) && (supported == FALSE); index++)
    {
        supported = (Service->SubServiceIds[index] == SubFunction) ? TRUE : FALSE;
    }
    return supported;
}

static Dcm_NegativeResponseCodeType Dcm_DsdVerifyRequest(const Dcm_DsdServiceCfgType* Service,
                                                         const Dcm_DsdRequestType* Request)
{
    Dcm_NegativeResponseCodeType nrc = DCM_POS_RESP;

    if (Dcm_DsdIsSessionAllowed(Service) == FALSE)
    {
        nrc = DCM_E_SERVICENOTSUPPORTEDINACTIVESESSION;      /* [SWS_Dcm_00211] */
    }
    else if (Request->Length < Service->MinReqLength)
    {
        nrc = DCM_E_INCORRECTMESSAGELENGTHORINVALIDFORMAT;   /* [SWS_Dcm_00696] */
    }
    else if ((Service->SubfuncAvail == TRUE) &&
             (Dcm_DsdIsSubServiceSupported(Service,
                  (uint8)(Request->Data[DCM_UDS_POS_SUBFUNCTION] & DCM_UDS_SUBFUNCTION_MASK)) == FALSE))
    {
        nrc = DCM_E_SUBFUNCTIONNOTSUPPORTED;                 /* [SWS_Dcm_00273] */
    }
    else
    {
        /* all verifications passed */
    }
    return nrc;
}

/* Fill Dcm_MsgContextType; pointers point behind the SID [SWS_Dcm_00763]. */
static void Dcm_DsdBuildMsgContext(const Dcm_DsdServiceCfgType* Service,
                                   const Dcm_DsdRequestType* Request,
                                   uint8* ResBuffer, PduLengthType ResBufferSize,
                                   Dcm_MsgContextType* MsgContext)
{
    MsgContext->reqData       = &Request->Data[DCM_UDS_SID_LENGTH];
    MsgContext->reqDataLen    = (Dcm_MsgLenType)Request->Length - DCM_UDS_SID_LENGTH;
    MsgContext->resData       = &ResBuffer[DCM_UDS_SID_LENGTH];
    MsgContext->resDataLen    = 0u;
    MsgContext->resMaxDataLen = (Dcm_MsgLenType)ResBufferSize - DCM_UDS_SID_LENGTH;
    MsgContext->idContext     = Dcm_DsdContext.IdContext;
    MsgContext->dcmRxPduId    = Request->RxPduId;
    MsgContext->msgAddInfo.reqType = (Request->AddrType == DCM_FUNCTIONAL_TYPE) ? 1u : 0u;
    MsgContext->msgAddInfo.suppressPosResponse = 0u;

    /* SPRMIB handling only for services with sub-function [SWS_Dcm_00204] */
    if ((Service->SubfuncAvail == TRUE) &&
        ((Request->Data[DCM_UDS_POS_SUBFUNCTION] & DCM_UDS_SPRMIB_MASK) != 0u))
    {
        MsgContext->msgAddInfo.suppressPosResponse = 1u;         /* [SWS_Dcm_00200] */
        Request->Data[DCM_UDS_POS_SUBFUNCTION] &= DCM_UDS_SUBFUNCTION_MASK; /* [SWS_Dcm_00201] */
    }
}

/* Negative responses suppressed for functional requests [SWS_Dcm_00001]. */
static boolean Dcm_DsdIsNrcSuppressedOnFunctional(Dcm_NegativeResponseCodeType Nrc)
{
    return ((Nrc == DCM_E_SERVICENOTSUPPORTED) ||
            (Nrc == DCM_E_SUBFUNCTIONNOTSUPPORTED) ||
            (Nrc == DCM_E_REQUESTOUTOFRANGE) ||
            (Nrc == DCM_E_SUBFUNCTIONNOTSUPPORTEDINACTIVESESSION) ||
            (Nrc == DCM_E_SERVICENOTSUPPORTEDINACTIVESESSION)) ? TRUE : FALSE;
}

/* Processing finished: always confirm to DSP [SWS_Dcm_00236] [SWS_Dcm_00240]. */
static void Dcm_DsdFinish(Dcm_ConfirmationStatusType Status)
{
    DspInternal_DcmConfirmation(Dcm_DsdContext.IdContext, 0u, Status);
    Dcm_DsdContext.IdContext++;
}

static boolean Dcm_DsdHandleNegative(Dcm_NegativeResponseCodeType Nrc, Dcm_DslAddrType AddrType,
                                     uint8* ResBuffer, PduLengthType* ResLength)
{
    boolean sendResponse = FALSE;

    if ((AddrType == DCM_FUNCTIONAL_TYPE) && (Dcm_DsdIsNrcSuppressedOnFunctional(Nrc) == TRUE))
    {
        Dcm_DsdFinish(DCM_RES_NEG_OK);   /* suppressed negative response */
    }
    else
    {
        ResBuffer[DCM_UDS_NEG_RESPONSE_POS_NRSI] = DCM_UDS_SID_NEGATIVE_RESPONSE;
        ResBuffer[DCM_UDS_NEG_RESPONSE_POS_SID]  = Dcm_DsdContext.ServiceId;
        ResBuffer[DCM_UDS_NEG_RESPONSE_POS_NRC]  = Nrc;
        *ResLength = DCM_UDS_NEG_RESPONSE_LENGTH;
        Dcm_DsdContext.NegativeResponse = TRUE;
        sendResponse = TRUE;
    }
    return sendResponse;
}

static boolean Dcm_DsdHandlePositive(const Dcm_MsgContextType* MsgContext,
                                     uint8* ResBuffer, PduLengthType* ResLength)
{
    boolean sendResponse = FALSE;

    if (MsgContext->msgAddInfo.suppressPosResponse == 1u)
    {
        Dcm_DsdFinish(DCM_RES_POS_OK);   /* suppressed positive response [SWS_Dcm_00231] */
    }
    else
    {
        /* response SID at first byte [SWS_Dcm_00223] [SWS_Dcm_00224] */
        ResBuffer[DCM_UDS_POS_SID] =
            (uint8)(Dcm_DsdContext.ServiceId + DCM_UDS_POSITIVE_RESPONSE_SID_OFFSET);
        *ResLength = (PduLengthType)(MsgContext->resDataLen + DCM_UDS_SID_LENGTH);
        Dcm_DsdContext.NegativeResponse = FALSE;
        sendResponse = TRUE;
    }
    return sendResponse;
}

/* ======================== Internal interface ============================== */
void DsdInternal_Init(void)
{
    Dcm_DsdContext.ServiceId        = 0u;
    Dcm_DsdContext.NegativeResponse = FALSE;
    Dcm_DsdContext.IdContext        = 0u;
}

boolean DsdInternal_ProcessRequest(const Dcm_DsdRequestType* Request,
                                   uint8* ResBuffer,
                                   PduLengthType ResBufferSize,
                                   PduLengthType* ResLength)
{
    const Dcm_DsdServiceCfgType* service;
    Dcm_NegativeResponseCodeType nrc;
    Dcm_MsgContextType msgContext;
    boolean sendResponse = FALSE;

    Dcm_DsdContext.ServiceId = Request->Data[DCM_UDS_POS_SID];
    if (Dcm_DsdIsIgnoredSid(Dcm_DsdContext.ServiceId) == FALSE)
    {
        service = Dcm_DsdFindService(Dcm_DsdContext.ServiceId);
        nrc = (service == NULL_PTR) ? DCM_E_SERVICENOTSUPPORTED     /* [SWS_Dcm_00197] */
                                    : Dcm_DsdVerifyRequest(service, Request);
        if (nrc == DCM_POS_RESP)
        {
            Dcm_DsdBuildMsgContext(service, Request, ResBuffer, ResBufferSize, &msgContext);
            if (service->ServiceFnc(DCM_INITIAL, &msgContext, &nrc) == E_OK)  /* [SWS_Dcm_00732] */
            {
                sendResponse = Dcm_DsdHandlePositive(&msgContext, ResBuffer, ResLength);
            }
            else if (nrc == DCM_POS_RESP)
            {
                nrc = DCM_E_GENERALREJECT;  /* E_NOT_OK without NRC [Inference] */
            }
            else
            {
                /* nrc set by the service */
            }
        }
        if (nrc != DCM_POS_RESP)
        {
            sendResponse = Dcm_DsdHandleNegative(nrc, Request->AddrType, ResBuffer, ResLength);
        }
    }
    return sendResponse;
}

/* Map the transmission result to Dcm_ConfirmationStatusType and confirm. */
void DsdInternal_TxConfirmation(Std_ReturnType Result)
{
    Dcm_ConfirmationStatusType status;

    if (Dcm_DsdContext.NegativeResponse == TRUE)
    {
        status = (Result == E_OK) ? DCM_RES_NEG_OK : DCM_RES_NEG_NOT_OK;
    }
    else
    {
        status = (Result == E_OK) ? DCM_RES_POS_OK : DCM_RES_POS_NOT_OK;
    }
    Dcm_DsdFinish(status);
}
