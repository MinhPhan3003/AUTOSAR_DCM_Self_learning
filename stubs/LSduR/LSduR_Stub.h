/**
 * @file    LSduR_Stub.h
 * @brief   Test accessors of the LSduR stub (host test only).
 *
 * Every frame CanTp passes to LSduR_CanTpTransmit is stored in a FIFO, so a
 * test (or the simulator) can read the frames that "went on the bus".
 * The test decides when to call CanTp_TxConfirmation.
 */
#ifndef LSDUR_STUB_H
#define LSDUR_STUB_H

#include "ComStack_Types.h"

#define LSDUR_STUB_MAX_FRAME_LENGTH  8u    /* classic CAN only */

typedef struct
{
    PduIdType     TxPduId;
    PduLengthType Length;
    uint8         Data[LSDUR_STUB_MAX_FRAME_LENGTH];
} LSduR_Stub_FrameType;

/* Optional hook, called inside LSduR_CanTpTransmit (used by the simulator). */
typedef void (*LSduR_Stub_HookFnType)(const LSduR_Stub_FrameType* Frame);

void    LSduR_Stub_Reset(void);
void    LSduR_Stub_SetTransmitResult(Std_ReturnType Result);
void    LSduR_Stub_SetHook(LSduR_Stub_HookFnType Hook);
uint32  LSduR_Stub_GetTransmitCount(void);
uint32  LSduR_Stub_GetPendingFrameCount(void);

/* Removes the oldest frame from the FIFO. Returns FALSE if the FIFO is empty. */
boolean LSduR_Stub_PopFrame(LSduR_Stub_FrameType* Frame);

#endif /* LSDUR_STUB_H */
