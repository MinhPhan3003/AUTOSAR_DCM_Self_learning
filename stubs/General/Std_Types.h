/**
 * @file    Std_Types.h
 * @brief   STUB - minimal AUTOSAR standard types for host build.
 *
 * NOTE: Not derived from the provided PDFs. Real content is defined in
 *       AUTOSAR_CP_SWS_StandardTypes and AUTOSAR_CP_SWS_PlatformTypes.
 *       Replace this file with the one delivered by your BSW stack.
 */
#ifndef STD_TYPES_H
#define STD_TYPES_H

#include <stdint.h>

typedef uint8_t  uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint8    boolean;

#ifndef TRUE
#define TRUE   ((boolean)1u)
#endif
#ifndef FALSE
#define FALSE  ((boolean)0u)
#endif

#define NULL_PTR  ((void *)0)

typedef uint8 Std_ReturnType;

#define E_OK      ((Std_ReturnType)0x00u)
#define E_NOT_OK  ((Std_ReturnType)0x01u)

#define STD_ON    1u
#define STD_OFF   0u

#endif /* STD_TYPES_H */
