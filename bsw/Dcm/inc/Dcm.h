/**
 * @file    Dcm.h
 * @brief   Public interface of the example Diagnostic Communication Manager.
 *
 * Reference: AUTOSAR CP R25-11, SWS_DiagnosticCommunicationManager
 *   - 8.2  Type definitions
 *   - 8.3  Function definitions
 *   - 8.4  Callback notifications (Dcm <-> PduR)
 *   - 7.10 Development errors
 *
 * Scope of this example: UDS on one DcmDslConnection (physical + functional
 * Rx, one Tx), services 0x10 and 0x3E. See README.md for limitations.
 */
#ifndef DCM_H
#define DCM_H

#include "ComStack_Types.h"
#include "Rte_Dcm_Type.h"
#include "Dcm_Cfg.h"

/* ======================== Development errors [SWS_Dcm_00040] ============== */
#define DCM_E_UNINIT                 ((uint8)0x05u)
#define DCM_E_PARAM                  ((uint8)0x06u)
#define DCM_E_PARAM_POINTER          ((uint8)0x07u)
#define DCM_E_INIT_FAILED            ((uint8)0x08u)

/* ======================== Type definitions (8.2) ========================== */

/* [SWS_Dcm_00982] Implementation specific. Unused in this pre-compile example. */
typedef struct
{
    uint8 Reserved;
} Dcm_ConfigType;

typedef uint8            Dcm_MsgItemType;    /* [SWS_Dcm_00989] */
typedef Dcm_MsgItemType* Dcm_MsgType;        /* [SWS_Dcm_00990] */
typedef uint32           Dcm_MsgLenType;     /* [SWS_Dcm_00991] */
typedef uint8            Dcm_IdContextType;  /* [SWS_Dcm_00993] */

/* [SWS_Dcm_00992] Bitfield: reqType (LSB+0), suppressPosResponse (LSB+1) */
typedef struct
{
    unsigned int reqType             : 1; /* 0 = physical, 1 = functional */
    unsigned int suppressPosResponse : 1; /* 1 = no positive response     */
} Dcm_MsgAddInfoType;

/* [SWS_Dcm_00994] */
typedef struct
{
    Dcm_MsgType        reqData;       /* request data after the SID     */
    Dcm_MsgLenType     reqDataLen;    /* without SID                    */
    Dcm_MsgType        resData;       /* positive response after the SID */
    Dcm_MsgLenType     resDataLen;    /* without SID                    */
    Dcm_MsgAddInfoType msgAddInfo;
    Dcm_MsgLenType     resMaxDataLen;
    Dcm_IdContextType  idContext;
    PduIdType          dcmRxPduId;
} Dcm_MsgContextType;

/* [SWS_Dcm_91015] (subset) */
typedef uint8 Dcm_ExtendedOpStatusType;
#define DCM_INITIAL  ((Dcm_ExtendedOpStatusType)0x00u)
#define DCM_PENDING  ((Dcm_ExtendedOpStatusType)0x01u)
#define DCM_CANCEL   ((Dcm_ExtendedOpStatusType)0x02u)

/* ======================== Functions (8.3) ================================= */

/* [SWS_Dcm_00037] */
void Dcm_Init(const Dcm_ConfigType* ConfigPtr);

/* [SWS_Dcm_00339] */
Std_ReturnType Dcm_GetSesCtrlType(Dcm_SesCtrlType* SesCtrlType);

/* ======================== Callbacks for PduR (8.4) ======================== */

/* [SWS_Dcm_00094] */
BufReq_ReturnType Dcm_StartOfReception(PduIdType id,
                                       const PduInfoType* info,
                                       PduLengthType TpSduLength,
                                       PduLengthType* bufferSizePtr);

/* [SWS_Dcm_00556] */
BufReq_ReturnType Dcm_CopyRxData(PduIdType id,
                                 const PduInfoType* info,
                                 PduLengthType* bufferSizePtr);

/* [SWS_Dcm_00093] */
void Dcm_TpRxIndication(PduIdType id, Std_ReturnType result);

/* [SWS_Dcm_00092] */
BufReq_ReturnType Dcm_CopyTxData(PduIdType id,
                                 const PduInfoType* info,
                                 const RetryInfoType* retry,
                                 PduLengthType* availableDataPtr);

/* [SWS_Dcm_00351] */
void Dcm_TpTxConfirmation(PduIdType id, Std_ReturnType result);

#endif /* DCM_H */
