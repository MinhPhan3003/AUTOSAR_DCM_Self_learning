/**
 * @file    Det.h
 * @brief   STUB - Default Error Tracer interface.
 *
 * Prototypes from AUTOSAR CP R25-11 SWS_DefaultErrorTracer:
 *   - Det_ReportError        [SWS_Det_00009] (section 8.1.3.2)
 *   - Det_ReportRuntimeError [SWS_Det_01001] (section 8.1.3.4)
 */
#ifndef DET_H
#define DET_H

#include "Std_Types.h"

Std_ReturnType Det_ReportError(uint16 ModuleId, uint8 InstanceId, uint8 ApiId, uint8 ErrorId);

Std_ReturnType Det_ReportRuntimeError(uint16 ModuleId, uint8 InstanceId, uint8 ApiId, uint8 ErrorId);

#endif /* DET_H */
