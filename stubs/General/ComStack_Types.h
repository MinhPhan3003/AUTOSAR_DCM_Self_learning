/**
 * @file    ComStack_Types.h
 * @brief   STUB - communication stack types imported by Dcm.
 *
 * The list of imported types is given in AUTOSAR CP R25-11
 * SWS_DiagnosticCommunicationManager, section 8.1 [SWS_Dcm_00333].
 * Their definition (width, members, literal values) is in
 * AUTOSAR_CP_SWS_CommunicationStackTypes, which is NOT among the provided
 * PDFs -> verify against that document / your BSW delivery.
 */
#ifndef COMSTACK_TYPES_H
#define COMSTACK_TYPES_H

#include "Std_Types.h"

/* Width is configurable in a real stack (uint8/uint16/uint32). */
typedef uint16 PduIdType;
typedef uint16 PduLengthType;

typedef struct
{
    uint8*        SduDataPtr;
    uint8*        MetaDataPtr;
    PduLengthType SduLength;
} PduInfoType;

typedef uint8 BufReq_ReturnType;
#define BUFREQ_OK        ((BufReq_ReturnType)0x00u)
#define BUFREQ_E_NOT_OK  ((BufReq_ReturnType)0x01u)
#define BUFREQ_E_BUSY    ((BufReq_ReturnType)0x02u)
#define BUFREQ_E_OVFL    ((BufReq_ReturnType)0x03u)

typedef uint8 TpDataStateType;
#define TP_DATACONF      ((TpDataStateType)0x00u)
#define TP_DATARETRY     ((TpDataStateType)0x01u)
#define TP_CONFPENDING   ((TpDataStateType)0x02u)

typedef struct
{
    TpDataStateType TpDataState;
    PduLengthType   TxTpDataCnt;
} RetryInfoType;

/* Imported by CanTp (SWS_CanTp R25-11 [SWS_CanTp_00209]). Values from
 * AUTOSAR CP R25-11 SWS_CommunicationStackTypes [SWS_COMTYPE_00031]. */
typedef uint8 TPParameterType;
#define TP_STMIN         ((TPParameterType)0x00u)
#define TP_BS            ((TPParameterType)0x01u)
#define TP_BC            ((TPParameterType)0x02u)

#endif /* COMSTACK_TYPES_H */
