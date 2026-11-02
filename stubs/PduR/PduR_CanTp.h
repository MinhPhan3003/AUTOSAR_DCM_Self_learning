/**
 * @file    PduR_CanTp.h
 * @brief   STUB - PduR callbacks used by CanTp.
 *
 * Mandatory interfaces of CanTp, header "PduR_CanTp.h" [SWS_CanTp_00216].
 * Prototypes from AUTOSAR CP R25-11 SWS_PDURouter, section 8.3.4
 * (PduR_<User:LoTp>... with <User:LoTp> = CanTp):
 *   CopyRxData       [SWS_PduR_00512]
 *   RxIndication     [SWS_PduR_00375]
 *   StartOfReception [SWS_PduR_00507]
 *   CopyTxData       [SWS_PduR_00518]
 *   TxConfirmation   [SWS_PduR_00381]
 */
#ifndef PDUR_CANTP_H
#define PDUR_CANTP_H

#include "ComStack_Types.h"

BufReq_ReturnType PduR_CanTpStartOfReception(PduIdType id,
                                             const PduInfoType* info,
                                             PduLengthType TpSduLength,
                                             PduLengthType* bufferSizePtr);

BufReq_ReturnType PduR_CanTpCopyRxData(PduIdType id,
                                       const PduInfoType* info,
                                       PduLengthType* bufferSizePtr);

void PduR_CanTpRxIndication(PduIdType id, Std_ReturnType result);

BufReq_ReturnType PduR_CanTpCopyTxData(PduIdType id,
                                       const PduInfoType* info,
                                       const RetryInfoType* retry,
                                       PduLengthType* availableDataPtr);

void PduR_CanTpTxConfirmation(PduIdType id, Std_ReturnType result);

#endif /* PDUR_CANTP_H */
