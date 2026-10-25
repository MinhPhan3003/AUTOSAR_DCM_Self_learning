/**
 * @file    Dcm_Internal.h
 * @brief   Internal interfaces between DSL, DSD and DSP. Not for other modules.
 *
 * Function names follow the non-normative internal interfaces of
 * AUTOSAR CP R25-11 SWS_Dcm, section 8.10, where such a name exists
 * (DslInternal_SetSesCtrlType, DspInternal_DcmConfirmation). The others are
 * implementation choices of this example.
 */
#ifndef DCM_INTERNAL_H
#define DCM_INTERNAL_H

#include "Dcm.h"

/* ======================== DET identifiers ================================= */
/* Dcm module ID = 53 is listed in the AUTOSAR "List of Basic Software
 * Modules" (not among the provided PDFs). */
#define DCM_MODULE_ID                  ((uint16)53u)
#define DCM_INSTANCE_ID                ((uint8)0u)

/* Service IDs of the APIs (SWS_Dcm sections 8.3, 8.4, 8.6) */
#define DCM_API_ID_INIT                ((uint8)0x01u) /* [SWS_Dcm_00037] */
#define DCM_API_ID_GETSESCTRLTYPE      ((uint8)0x06u) /* [SWS_Dcm_00339] */
#define DCM_API_ID_COPYTXDATA          ((uint8)0x43u) /* [SWS_Dcm_00092] */
#define DCM_API_ID_COPYRXDATA          ((uint8)0x44u) /* [SWS_Dcm_00556] */
#define DCM_API_ID_TPRXINDICATION      ((uint8)0x45u) /* [SWS_Dcm_00093] */
#define DCM_API_ID_STARTOFRECEPTION    ((uint8)0x46u) /* [SWS_Dcm_00094] */
#define DCM_API_ID_TPTXCONFIRMATION    ((uint8)0x48u) /* [SWS_Dcm_00351] */

/* ======================== UDS constants (ISO 14229-1:2013) =============== */
#define DCM_UDS_SID_DIAGNOSTIC_SESSION_CONTROL ((uint8)0x10u) /* 9.2 */
#define DCM_UDS_SID_TESTER_PRESENT             ((uint8)0x3Eu) /* 9.6 */
#define DCM_UDS_SID_NEGATIVE_RESPONSE          ((uint8)0x7Fu) /* Table 21 */
#define DCM_UDS_POSITIVE_RESPONSE_SID_OFFSET   ((uint8)0x40u) /* 0x10->0x50, 0x3E->0x7E */
#define DCM_UDS_SPRMIB_MASK                    ((uint8)0x80u) /* bit 7 of sub-function */
#define DCM_UDS_SUBFUNCTION_MASK               ((uint8)0x7Fu)
#define DCM_UDS_TP_ZERO_SUBFUNCTION            ((uint8)0x00u) /* Table 66 */

/* Negative response: 0x7F, request SID, NRC (ISO 14229-1 Table 21) */
#define DCM_UDS_NEG_RESPONSE_LENGTH            ((PduLengthType)3u)
#define DCM_UDS_NEG_RESPONSE_POS_NRSI          0u
#define DCM_UDS_NEG_RESPONSE_POS_SID           1u
#define DCM_UDS_NEG_RESPONSE_POS_NRC           2u

/* Byte positions inside a request / response buffer */
#define DCM_UDS_POS_SID                        0u
#define DCM_UDS_POS_SUBFUNCTION                1u
#define DCM_UDS_SID_LENGTH                     ((PduLengthType)1u)

/* ======================== Configuration types ============================= */

/* DcmDslProtocolRx/DcmDslProtocolRxAddrType [ECUC_Dcm_00710] */
typedef uint8 Dcm_DslAddrType;
#define DCM_PHYSICAL_TYPE    ((Dcm_DslAddrType)0u)
#define DCM_FUNCTIONAL_TYPE  ((Dcm_DslAddrType)1u)

typedef struct
{
    Dcm_DslAddrType AddrType;          /* DcmDslProtocolRxAddrType */
} Dcm_DslProtocolRxCfgType;

/* One DcmDspSessionRow. ECUC values are in seconds (float); the
 * configuration tool converts them to integer milliseconds. */
typedef struct
{
    Dcm_SesCtrlType SessionLevel;      /* DcmDspSessionLevel           */
    uint32          P2ServerMaxMs;     /* DcmDspSessionP2ServerMax     */
    uint32          P2StarServerMaxMs; /* DcmDspSessionP2StarServerMax */
} Dcm_DspSessionRowCfgType;

/* Same prototype as <Module>_<DiagnosticService> [SWS_Dcm_00763] */
typedef Std_ReturnType (*Dcm_DsdServiceFncType)(Dcm_ExtendedOpStatusType OpStatus,
                                                Dcm_MsgContextType* pMsgContext,
                                                Dcm_NegativeResponseCodeType* ErrorCode);

/* One DcmDsdService of the active DcmDsdServiceTable */
typedef struct
{
    uint8                  ServiceId;      /* DcmDsdSidTabServiceId         */
    boolean                SubfuncAvail;   /* DcmDsdSidTabSubfuncAvail      */
    PduLengthType          MinReqLength;   /* incl. SID [Practice]          */
    const Dcm_SesCtrlType* SessionRefs;    /* DcmDsdSidTabSessionLevelRef   */
    uint8                  NumSessionRefs;
    const uint8*           SubServiceIds;  /* DcmDsdSubService/DcmDsdSubServiceId */
    uint8                  NumSubServices;
    Dcm_DsdServiceFncType  ServiceFnc;     /* DcmDsdSidTabFnc or internal   */
} Dcm_DsdServiceCfgType;

extern const Dcm_DslProtocolRxCfgType Dcm_DslProtocolRxCfg[DCM_CFG_NUM_PROTOCOL_RX];
extern const Dcm_DspSessionRowCfgType Dcm_DspSessionRowCfg[DCM_CFG_NUM_SESSION_ROWS];
extern const Dcm_DsdServiceCfgType    Dcm_DsdServiceTableCfg[DCM_CFG_NUM_SERVICES];

/* ======================== DSL -> DSD ====================================== */
typedef struct
{
    uint8*          Data;      /* request incl. SID, owned by DSL */
    PduLengthType   Length;
    PduIdType       RxPduId;
    Dcm_DslAddrType AddrType;
} Dcm_DsdRequestType;

void DsdInternal_Init(void);

/* Returns TRUE if a response was written to ResBuffer and must be sent. */
boolean DsdInternal_ProcessRequest(const Dcm_DsdRequestType* Request,
                                   uint8* ResBuffer,
                                   PduLengthType ResBufferSize,
                                   PduLengthType* ResLength);

/* DSL forwards the Tx confirmation to DSD [SWS_Dcm_00117] */
void DsdInternal_TxConfirmation(Std_ReturnType Result);

/* ======================== DSL session state [SWS_Dcm_00022] =============== */
void            DslInternal_SetSesCtrlType(Dcm_SesCtrlType SesCtrlType);
Dcm_SesCtrlType DslInternal_GetSesCtrlType(void);

/* ======================== DSP ============================================= */
void DspInternal_Init(void);

/* SWS_Dcm 8.10.3 (non-normative) */
void DspInternal_DcmConfirmation(Dcm_IdContextType idContext,
                                 uint16 ConnectionId,
                                 Dcm_ConfirmationStatusType status);

Std_ReturnType DspInternal_DiagnosticSessionControl(Dcm_ExtendedOpStatusType OpStatus,
                                                    Dcm_MsgContextType* pMsgContext,
                                                    Dcm_NegativeResponseCodeType* ErrorCode);

Std_ReturnType DspInternal_TesterPresent(Dcm_ExtendedOpStatusType OpStatus,
                                         Dcm_MsgContextType* pMsgContext,
                                         Dcm_NegativeResponseCodeType* ErrorCode);

#endif /* DCM_INTERNAL_H */
