/**
 * @file    LSduR_CanTp.h
 * @brief   STUB - L-SDU Router interface used by CanTp.
 *
 * Since R20-11 the lower layer of CanTp is the L-SDU Router, not CanIf
 * (AUTOSAR CP R25-11 SWS_CANTransportLayer, change history and 5.1.7).
 * LSduR_CanTpTransmit is a mandatory interface of CanTp, header
 * "LSduR_<module>.h" [SWS_CanTp_00216].
 * Prototype: LSduR_<User:Up>Transmit, AUTOSAR CP R25-11 SWS_LSduRouter
 * 8.3.2.1 [CP_SWS_LSduR_91009]. Status of this API in R25-11: DRAFT.
 */
#ifndef LSDUR_CANTP_H
#define LSDUR_CANTP_H

#include "ComStack_Types.h"

Std_ReturnType LSduR_CanTpTransmit(PduIdType TxPduId, const PduInfoType* PduInfoPtr);

#endif /* LSDUR_CANTP_H */
