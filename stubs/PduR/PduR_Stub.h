/**
 * @file    PduR_Stub.h
 * @brief   Test accessors of the PduR stub (host test only).
 */
#ifndef PDUR_STUB_H
#define PDUR_STUB_H

#include "ComStack_Types.h"

/* ======================== Dcm side (PduR_Stub.c) ========================== */
void          PduR_Stub_Reset(void);
void          PduR_Stub_SetTransmitResult(Std_ReturnType Result);
uint32        PduR_Stub_GetTransmitCount(void);
PduLengthType PduR_Stub_GetLastTransmitLength(void);
PduIdType     PduR_Stub_GetLastTransmitPduId(void);

/* ======================== CanTp side (PduR_Stub_CanTp.c) ==================
 * The stub plays the upper layer of CanTp (normally PduR -> Dcm/Com).
 * Rx: it owns one receive buffer. "Available" is the free space that it
 *     reports in bufferSizePtr; a test lowers it to force FC(WAIT).
 * Tx: it owns the N-SDU payload that CanTp_Transmit announced.          */
#define PDUR_STUB_CANTP_BUFFER_SIZE  ((PduLengthType)512u)

/* Optional hook, receives one text line per callback (used by the simulator). */
typedef void (*PduR_Stub_TraceFnType)(const char* Text);

void              PduR_Stub_CanTpReset(void);
void              PduR_Stub_CanTpSetTrace(PduR_Stub_TraceFnType Trace);

/* Rx behaviour */
void              PduR_Stub_CanTpSetStartOfReceptionResult(BufReq_ReturnType Result);
void              PduR_Stub_CanTpSetCopyRxDataResult(BufReq_ReturnType Result);
void              PduR_Stub_CanTpSetRxAvailable(PduLengthType Available);

/* Rx observation */
uint32            PduR_Stub_CanTpGetStartOfReceptionCount(void);
PduLengthType     PduR_Stub_CanTpGetLastTpSduLength(void);
uint32            PduR_Stub_CanTpGetRxIndicationCount(void);
Std_ReturnType    PduR_Stub_CanTpGetLastRxResult(void);
PduIdType         PduR_Stub_CanTpGetLastRxId(void);
const uint8*      PduR_Stub_CanTpGetRxData(PduLengthType* Length);

/* Tx behaviour */
void              PduR_Stub_CanTpSetTxData(const uint8* Data, PduLengthType Length);
void              PduR_Stub_CanTpSetCopyTxDataResult(BufReq_ReturnType Result);

/* Tx observation */
uint32            PduR_Stub_CanTpGetTxConfirmationCount(void);
Std_ReturnType    PduR_Stub_CanTpGetLastTxResult(void);

#endif /* PDUR_STUB_H */
