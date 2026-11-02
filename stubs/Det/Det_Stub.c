/**
 * @file    Det_Stub.c
 * @brief   Det stub: records the last reported development / runtime error.
 */
#include "Det.h"
#include "Det_Stub.h"

typedef struct
{
    uint32 Count;
    uint8  LastApiId;
    uint8  LastErrorId;
} Det_StubRecordType;

static Det_StubRecordType  Det_StubDevErrors;
static Det_StubRecordType  Det_StubRuntimeErrors;
static Det_Stub_HookFnType Det_StubHook = NULL_PTR;

static void Det_StubClear(Det_StubRecordType* Record)
{
    Record->Count       = 0u;
    Record->LastApiId   = 0u;
    Record->LastErrorId = 0u;
}

static void Det_StubRecord(Det_StubRecordType* Record, uint16 ModuleId,
                           uint8 ApiId, uint8 ErrorId, boolean IsRuntime)
{
    Record->Count++;
    Record->LastApiId   = ApiId;
    Record->LastErrorId = ErrorId;
    if (Det_StubHook != NULL_PTR)
    {
        Det_StubHook(ModuleId, ApiId, ErrorId, IsRuntime);
    }
}

void Det_Stub_Reset(void)
{
    Det_StubClear(&Det_StubDevErrors);
    Det_StubClear(&Det_StubRuntimeErrors);
}

void   Det_Stub_SetHook(Det_Stub_HookFnType Hook) { Det_StubHook = Hook; }

uint32 Det_Stub_GetErrorCount(void)         { return Det_StubDevErrors.Count; }
uint8  Det_Stub_GetLastApiId(void)          { return Det_StubDevErrors.LastApiId; }
uint8  Det_Stub_GetLastErrorId(void)        { return Det_StubDevErrors.LastErrorId; }

uint32 Det_Stub_GetRuntimeErrorCount(void)  { return Det_StubRuntimeErrors.Count; }
uint8  Det_Stub_GetLastRuntimeApiId(void)   { return Det_StubRuntimeErrors.LastApiId; }
uint8  Det_Stub_GetLastRuntimeErrorId(void) { return Det_StubRuntimeErrors.LastErrorId; }

Std_ReturnType Det_ReportError(uint16 ModuleId, uint8 InstanceId, uint8 ApiId, uint8 ErrorId)
{
    (void)InstanceId;
    Det_StubRecord(&Det_StubDevErrors, ModuleId, ApiId, ErrorId, FALSE);
    return E_OK;
}

Std_ReturnType Det_ReportRuntimeError(uint16 ModuleId, uint8 InstanceId, uint8 ApiId, uint8 ErrorId)
{
    (void)InstanceId;
    Det_StubRecord(&Det_StubRuntimeErrors, ModuleId, ApiId, ErrorId, TRUE);
    return E_OK;
}
