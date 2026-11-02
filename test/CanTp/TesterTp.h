/**
 * @file    TesterTp.h
 * @brief   Tester-side ISO-TP frame builder / parser (host test only).
 *
 * Plays the role of the tester's transport layer (e.g. CANoe or python
 * udsoncan). It is written independently of bsw/CanTp so that the test
 * checks CanTp against a second implementation of the N_PCI layout
 * (SWS_CanTp R25-11 Figure 7.2, normal addressing, classic CAN).
 *
 * Every N_PCI field is a plain struct member. Build a frame by filling the
 * fields, or decode a received frame and read the fields. The encoder writes
 * the fields exactly as given, so malformed frames (wrong SN, wrong SF_DL,
 * invalid FS, ...) can be produced on purpose.
 */
#ifndef TESTERTP_H
#define TESTERTP_H

#include "Std_Types.h"

#define TESTERTP_CAN_DL        8u      /* classic CAN frame length            */
#define TESTERTP_PADDING_BYTE  0xCCu   /* same example value as the ECU       */
#define TESTERTP_TEXT_LENGTH   160u    /* size of a buffer for TesterTp_Format */

/* N_PCItype, high nibble of byte #1 */
typedef enum
{
    TESTERTP_SF = 0,
    TESTERTP_FF = 1,
    TESTERTP_CF = 2,
    TESTERTP_FC = 3,
    TESTERTP_UNKNOWN
} TesterTp_KindType;

/* Flow status (ISO 15765-2, not among the provided PDFs -> verify) */
#define TESTERTP_FS_CTS    0u
#define TESTERTP_FS_WAIT   1u
#define TESTERTP_FS_OVFLW  2u

typedef struct
{
    TesterTp_KindType Kind;
    uint32 Dl;                          /* SF: SF_DL (0..15), FF: FF_DL         */
    uint8  Sn;                          /* CF: sequence number (0..15)          */
    uint8  Fs;                          /* FC: flow status                      */
    uint8  Bs;                          /* FC: block size                       */
    uint8  STmin;                       /* FC: separation time (raw byte)       */
    uint8  Data[TESTERTP_CAN_DL];       /* N_Data after the N_PCI               */
    uint8  DataLength;                  /* number of valid bytes in Data        */
} TesterTp_FrameType;

/* Convenience constructors. Data may be NULL_PTR when DataLength is 0. */
TesterTp_FrameType TesterTp_MakeSf(const uint8* Data, uint8 DataLength);
TesterTp_FrameType TesterTp_MakeFf(uint32 FfDl, const uint8* Data, uint8 DataLength);
TesterTp_FrameType TesterTp_MakeCf(uint8 Sn, const uint8* Data, uint8 DataLength);
TesterTp_FrameType TesterTp_MakeFc(uint8 Fs, uint8 Bs, uint8 STmin);

/* Number of N_Data bytes that fit behind the N_PCI of this frame. */
uint8 TesterTp_Capacity(const TesterTp_FrameType* Frame);

/* Writes the frame into Out. Unused bytes are padded when Padding is TRUE.
 * Returns the CAN_DL (number of bytes to put on the bus). */
uint8 TesterTp_Encode(const TesterTp_FrameType* Frame, boolean Padding, uint8 Out[TESTERTP_CAN_DL]);

/* Splits a raw frame into its fields. Returns FALSE for an unknown N_PCItype
 * or a frame too short for its N_PCI. */
boolean TesterTp_Decode(const uint8* Data, uint8 Length, TesterTp_FrameType* Frame);

/* One line, e.g. "FF  FF_DL=20  data: 62 F1 90 41 42 43". */
void TesterTp_Format(const uint8* Data, uint8 Length, char* Text, uint32 TextSize);

#endif /* TESTERTP_H */
