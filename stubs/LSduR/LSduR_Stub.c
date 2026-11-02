/**
 * @file    LSduR_Stub.c
 * @brief   LSduR stub: records the N-PDUs that CanTp transmits.
 */
#include "LSduR_CanTp.h"
#include "LSduR_Stub.h"

#define LSDUR_STUB_FIFO_SIZE  64u

static LSduR_Stub_FrameType  LSduR_StubFifo[LSDUR_STUB_FIFO_SIZE];
static uint32                LSduR_StubHead;          /* next frame to pop  */
static uint32                LSduR_StubCount;         /* frames in the FIFO */
static uint32                LSduR_StubTransmitCount;
static Std_ReturnType        LSduR_StubTransmitResult = E_OK;
static LSduR_Stub_HookFnType LSduR_StubHook = NULL_PTR;

void LSduR_Stub_Reset(void)
{
    LSduR_StubHead           = 0u;
    LSduR_StubCount          = 0u;
    LSduR_StubTransmitCount  = 0u;
    LSduR_StubTransmitResult = E_OK;
}

void   LSduR_Stub_SetTransmitResult(Std_ReturnType Result) { LSduR_StubTransmitResult = Result; }
void   LSduR_Stub_SetHook(LSduR_Stub_HookFnType Hook)      { LSduR_StubHook = Hook; }
uint32 LSduR_Stub_GetTransmitCount(void)                   { return LSduR_StubTransmitCount; }
uint32 LSduR_Stub_GetPendingFrameCount(void)               { return LSduR_StubCount; }

boolean LSduR_Stub_PopFrame(LSduR_Stub_FrameType* Frame)
{
    boolean found = FALSE;

    if (LSduR_StubCount > 0u)
    {
        *Frame = LSduR_StubFifo[LSduR_StubHead];
        LSduR_StubHead = (LSduR_StubHead + 1u) % LSDUR_STUB_FIFO_SIZE;
        LSduR_StubCount--;
        found = TRUE;
    }
    return found;
}

static void LSduR_StubStore(PduIdType TxPduId, const PduInfoType* PduInfoPtr)
{
    LSduR_Stub_FrameType* slot;
    PduLengthType index;

    if (LSduR_StubCount < LSDUR_STUB_FIFO_SIZE)
    {
        slot = &LSduR_StubFifo[(LSduR_StubHead + LSduR_StubCount) % LSDUR_STUB_FIFO_SIZE];
        slot->TxPduId = TxPduId;
        slot->Length  = (PduInfoPtr->SduLength > LSDUR_STUB_MAX_FRAME_LENGTH) ?
                        (PduLengthType)LSDUR_STUB_MAX_FRAME_LENGTH : PduInfoPtr->SduLength;
        for (index = 0u; index < slot->Length; index++)
        {
            slot->Data[index] = PduInfoPtr->SduDataPtr[index];
        }
        LSduR_StubCount++;
        if (LSduR_StubHook != NULL_PTR)
        {
            LSduR_StubHook(slot);
        }
    }
}

Std_ReturnType LSduR_CanTpTransmit(PduIdType TxPduId, const PduInfoType* PduInfoPtr)
{
    LSduR_StubTransmitCount++;
    if ((LSduR_StubTransmitResult == E_OK) && (PduInfoPtr != NULL_PTR))
    {
        LSduR_StubStore(TxPduId, PduInfoPtr);
    }
    return LSduR_StubTransmitResult;
}
