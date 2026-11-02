/**
 * @file    CanTp.h
 * @brief   Public interface of the example CAN Transport Layer (CanTp).
 *
 * Reference: AUTOSAR CP R25-11, SWS_CANTransportLayer (Document ID 14)
 *   - 7.4  Error classification
 *   - 8.2  Type definitions
 *   - 8.3  Function definitions
 *   - 8.4  Callback notifications (CanTp <- LSduR)
 *
 * Scope of this example: classic CAN (CAN_DL <= 8), normal addressing
 * (CANTP_STANDARD), specific connections (no MetaData). See
 * bsw/CanTp/README.md for the full list of limitations.
 */
#ifndef CANTP_H
#define CANTP_H

#include "ComStack_Types.h"
#include "CanTp_Cfg.h"

/* ======================== Development errors [SWS_CanTp_00293] =========== */
#define CANTP_E_PARAM_CONFIG         ((uint8)0x01u)
#define CANTP_E_PARAM_ID             ((uint8)0x02u)
#define CANTP_E_PARAM_POINTER        ((uint8)0x03u)
#define CANTP_E_INIT_FAILED          ((uint8)0x04u)
#define CANTP_E_UNINIT               ((uint8)0x20u)
#define CANTP_E_INVALID_TX_ID        ((uint8)0x30u)
#define CANTP_E_INVALID_RX_ID        ((uint8)0x40u)

/* ======================== Runtime errors [SWS_CanTp_00352] =============== */
#define CANTP_E_PADDING              ((uint8)0x70u)
#define CANTP_E_INVALID_TATYPE       ((uint8)0x90u)
#define CANTP_E_OPER_NOT_SUPPORTED   ((uint8)0xA0u)
#define CANTP_E_COM                  ((uint8)0xB0u)
#define CANTP_E_RX_COM               ((uint8)0xC0u)
#define CANTP_E_TX_COM               ((uint8)0xD0u)

/* ======================== Type definitions (8.2) ========================== */

/* [SWS_CanTp_00340] Implementation specific. Unused in this pre-compile
 * example: the configuration tables are linked in (gen/CanTp). */
typedef struct
{
    uint8 Reserved;
} CanTp_ConfigType;

/* ======================== Functions (8.3) ================================= */

/* [SWS_CanTp_00208] */
void CanTp_Init(const CanTp_ConfigType* CfgPtr);

/* [SWS_CanTp_00211] */
void CanTp_Shutdown(void);

/* [SWS_CanTp_00212] TxPduId = CanTpTxNSduId */
Std_ReturnType CanTp_Transmit(PduIdType TxPduId, const PduInfoType* PduInfoPtr);

/* [SWS_CanTp_00246] TxPduId = CanTpTxNSduId */
Std_ReturnType CanTp_CancelTransmit(PduIdType TxPduId);

/* [SWS_CanTp_00257] RxPduId = CanTpRxNSduId */
Std_ReturnType CanTp_CancelReceive(PduIdType RxPduId);

/* [SWS_CanTp_00302] id = CanTpRxNSduId, parameter = TP_STMIN or TP_BS */
Std_ReturnType CanTp_ChangeParameter(PduIdType id, TPParameterType parameter, uint16 value);

/* [SWS_CanTp_00323] id = CanTpRxNSduId, parameter = TP_STMIN or TP_BS */
Std_ReturnType CanTp_ReadParameter(PduIdType id, TPParameterType parameter, uint16* value);

/* CanTp_MainFunction is declared in SchM_CanTp.h [SWS_CanTp_00213]. */

/* ======================== Callbacks for LSduR (8.4) ======================= */

/* [SWS_CanTp_00214] RxPduId = CanTpRxNPduId (SF/FF/CF) or CanTpRxFcNPduId (FC) */
void CanTp_RxIndication(PduIdType RxPduId, const PduInfoType* PduInfoPtr);

/* [SWS_CanTp_00215] TxPduId = CanTpTxNPduConfirmationPduId or
 * CanTpTxFcNPduConfirmationPduId */
void CanTp_TxConfirmation(PduIdType TxPduId, Std_ReturnType result);

#endif /* CANTP_H */
