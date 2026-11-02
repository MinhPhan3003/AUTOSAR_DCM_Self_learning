/**
 * @file    Det_Stub.h
 * @brief   Test accessors of the Det stub (host test only).
 */
#ifndef DET_STUB_H
#define DET_STUB_H

#include "Std_Types.h"

/* Optional hook, called on every report (used by the CanTp simulator). */
typedef void (*Det_Stub_HookFnType)(uint16 ModuleId, uint8 ApiId, uint8 ErrorId, boolean IsRuntime);

void   Det_Stub_Reset(void);
void   Det_Stub_SetHook(Det_Stub_HookFnType Hook);

/* Development errors (Det_ReportError) */
uint32 Det_Stub_GetErrorCount(void);
uint8  Det_Stub_GetLastApiId(void);
uint8  Det_Stub_GetLastErrorId(void);

/* Runtime errors (Det_ReportRuntimeError) */
uint32 Det_Stub_GetRuntimeErrorCount(void);
uint8  Det_Stub_GetLastRuntimeApiId(void);
uint8  Det_Stub_GetLastRuntimeErrorId(void);

#endif /* DET_STUB_H */
