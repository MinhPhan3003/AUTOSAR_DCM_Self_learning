/**
 * @file    PduR_Dcm.h
 * @brief   STUB - PduR interface used by Dcm.
 *
 * PduR_DcmTransmit is a mandatory interface of Dcm
 * (AUTOSAR CP R25-11 SWS_Dcm, section 8.7.1 [SWS_Dcm_91001]).
 * The prototype itself is defined in AUTOSAR_CP_SWS_PDURouter, which is NOT
 * among the provided PDFs -> verify against that document.
 */
#ifndef PDUR_DCM_H
#define PDUR_DCM_H

#include "ComStack_Types.h"

Std_ReturnType PduR_DcmTransmit(PduIdType TxPduId, const PduInfoType* PduInfoPtr);

#endif /* PDUR_DCM_H */
