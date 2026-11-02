/**
 * @file    PduR_Stub.c
 * @brief   PduR stub: only records PduR_DcmTransmit. The test plays the role
 *          of PduR/CanTp and calls Dcm_CopyTxData / Dcm_TpTxConfirmation itself.
 */
#include "PduR_Dcm.h"
#include "PduR_Stub.h"

static Std_ReturnType PduR_StubTransmitResult = E_OK;
static uint32         PduR_StubTransmitCount  = 0u;
static PduLengthType  PduR_StubLastLength     = 0u;
static PduIdType      PduR_StubLastPduId      = 0u;

void PduR_Stub_Reset(void)
{
    PduR_StubTransmitResult = E_OK;
    PduR_StubTransmitCount  = 0u;
    PduR_StubLastLength     = 0u;
    PduR_StubLastPduId      = 0u;
}

void          PduR_Stub_SetTransmitResult(Std_ReturnType Result) { PduR_StubTransmitResult = Result; }
uint32        PduR_Stub_GetTransmitCount(void)      { return PduR_StubTransmitCount; }
PduLengthType PduR_Stub_GetLastTransmitLength(void) { return PduR_StubLastLength; }
PduIdType     PduR_Stub_GetLastTransmitPduId(void)  { return PduR_StubLastPduId; }

Std_ReturnType PduR_DcmTransmit(PduIdType TxPduId, const PduInfoType* PduInfoPtr)
{
    PduR_StubTransmitCount++;
    PduR_StubLastPduId  = TxPduId;
    PduR_StubLastLength = (PduInfoPtr != NULL_PTR) ? PduInfoPtr->SduLength : 0u;
    return PduR_StubTransmitResult;
}
