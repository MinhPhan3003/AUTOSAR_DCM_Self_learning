/**
 * @file    TesterTp.c
 * @brief   Tester-side ISO-TP frame builder / parser (host test only).
 *          N_PCI layout: SWS_CanTp R25-11 Figure 7.2 (normal addressing).
 */
#include <stdio.h>
#include "TesterTp.h"

#define TESTERTP_SHIFT_TYPE      4u
#define TESTERTP_NIBBLE          0x0Fu
#define TESTERTP_BYTE            0xFFu
#define TESTERTP_FF_DL_12BIT_MAX 0x0FFFu
#define TESTERTP_PCI_SF          1u
#define TESTERTP_PCI_FF          2u
#define TESTERTP_PCI_FF_ESC      6u
#define TESTERTP_PCI_CF          1u
#define TESTERTP_PCI_FC          3u
#define TESTERTP_STMIN_MS_MAX    0x7Fu
#define TESTERTP_STMIN_US_FIRST  0xF1u
#define TESTERTP_STMIN_US_LAST   0xF9u
#define TESTERTP_US_PER_STEP     100u

/* ======================== Constructors ==================================== */

static void TesterTp_SetData(TesterTp_FrameType* Frame, const uint8* Data, uint8 DataLength)
{
    const uint8 capacity = TesterTp_Capacity(Frame);
    uint8 index;

    Frame->DataLength = (DataLength > capacity) ? capacity : DataLength;
    for (index = 0u; index < Frame->DataLength; index++)
    {
        Frame->Data[index] = Data[index];
    }
}

TesterTp_FrameType TesterTp_MakeSf(const uint8* Data, uint8 DataLength)
{
    TesterTp_FrameType frame = { TESTERTP_SF, 0u, 0u, 0u, 0u, 0u, { 0u }, 0u };
    frame.Dl = DataLength;
    TesterTp_SetData(&frame, Data, DataLength);
    return frame;
}

TesterTp_FrameType TesterTp_MakeFf(uint32 FfDl, const uint8* Data, uint8 DataLength)
{
    TesterTp_FrameType frame = { TESTERTP_FF, 0u, 0u, 0u, 0u, 0u, { 0u }, 0u };
    frame.Dl = FfDl;
    TesterTp_SetData(&frame, Data, DataLength);
    return frame;
}

TesterTp_FrameType TesterTp_MakeCf(uint8 Sn, const uint8* Data, uint8 DataLength)
{
    TesterTp_FrameType frame = { TESTERTP_CF, 0u, 0u, 0u, 0u, 0u, { 0u }, 0u };
    frame.Sn = Sn;
    TesterTp_SetData(&frame, Data, DataLength);
    return frame;
}

TesterTp_FrameType TesterTp_MakeFc(uint8 Fs, uint8 Bs, uint8 STmin)
{
    TesterTp_FrameType frame = { TESTERTP_FC, 0u, 0u, 0u, 0u, 0u, { 0u }, 0u };
    frame.Fs    = Fs;
    frame.Bs    = Bs;
    frame.STmin = STmin;
    return frame;
}

/* ======================== Encoder ========================================= */

static uint8 TesterTp_PciLength(const TesterTp_FrameType* Frame)
{
    uint8 length = 0u;

    switch (Frame->Kind)
    {
        case TESTERTP_SF: length = TESTERTP_PCI_SF; break;
        case TESTERTP_FF: length = (Frame->Dl > TESTERTP_FF_DL_12BIT_MAX) ? TESTERTP_PCI_FF_ESC : TESTERTP_PCI_FF; break;
        case TESTERTP_CF: length = TESTERTP_PCI_CF; break;
        case TESTERTP_FC: length = TESTERTP_PCI_FC; break;
        default:          break;
    }
    return length;
}

uint8 TesterTp_Capacity(const TesterTp_FrameType* Frame)
{
    const uint8 pci = TesterTp_PciLength(Frame);
    return ((pci == 0u) || (Frame->Kind == TESTERTP_FC)) ? 0u : (uint8)(TESTERTP_CAN_DL - pci);
}

static void TesterTp_EncodePci(const TesterTp_FrameType* Frame, uint8* Out)
{
    const uint8 type = (uint8)((uint8)Frame->Kind << TESTERTP_SHIFT_TYPE);
    uint8 index;

    switch (Frame->Kind)
    {
        case TESTERTP_SF:
            Out[0] = (uint8)(type | (Frame->Dl & TESTERTP_NIBBLE));
            break;
        case TESTERTP_FF:
            if (Frame->Dl <= TESTERTP_FF_DL_12BIT_MAX)
            {
                Out[0] = (uint8)(type | ((Frame->Dl >> 8u) & TESTERTP_NIBBLE));
                Out[1] = (uint8)(Frame->Dl & TESTERTP_BYTE);
            }
            else
            {
                Out[0] = type;          /* escape: FF_DL = 0 in 12 bits, then 32 bits */
                Out[1] = 0u;
                for (index = 0u; index < 4u; index++)
                {
                    Out[2u + index] = (uint8)((Frame->Dl >> (24u - (8u * index))) & TESTERTP_BYTE);
                }
            }
            break;
        case TESTERTP_CF:
            Out[0] = (uint8)(type | (Frame->Sn & TESTERTP_NIBBLE));
            break;
        default:   /* FC */
            Out[0] = (uint8)(type | (Frame->Fs & TESTERTP_NIBBLE));
            Out[1] = Frame->Bs;
            Out[2] = Frame->STmin;
            break;
    }
}

uint8 TesterTp_Encode(const TesterTp_FrameType* Frame, boolean Padding, uint8 Out[TESTERTP_CAN_DL])
{
    const uint8 pci = TesterTp_PciLength(Frame);
    uint8 length = 0u;
    uint8 index;

    if (pci > 0u)
    {
        TesterTp_EncodePci(Frame, Out);
        length = pci;
        for (index = 0u; (index < Frame->DataLength) && (length < TESTERTP_CAN_DL); index++)
        {
            Out[length] = Frame->Data[index];
            length++;
        }
        while ((Padding == TRUE) && (length < TESTERTP_CAN_DL))
        {
            Out[length] = TESTERTP_PADDING_BYTE;
            length++;
        }
    }
    return length;
}

/* ======================== Decoder ========================================= */

static uint8 TesterTp_DecodePci(const uint8* Data, uint8 Length, TesterTp_FrameType* Frame)
{
    uint8 pci = 0u;
    uint8 index;

    switch (Frame->Kind)
    {
        case TESTERTP_SF:
            Frame->Dl = Data[0] & TESTERTP_NIBBLE;
            pci = TESTERTP_PCI_SF;
            break;
        case TESTERTP_FF:
            Frame->Dl = (Length >= TESTERTP_PCI_FF) ? (((uint32)(Data[0] & TESTERTP_NIBBLE) << 8u) | Data[1]) : 0u;
            pci = TESTERTP_PCI_FF;
            if ((Frame->Dl == 0u) && (Length >= TESTERTP_PCI_FF_ESC))
            {
                for (index = 0u; index < 4u; index++)
                {
                    Frame->Dl = (Frame->Dl << 8u) | Data[2u + index];
                }
                pci = TESTERTP_PCI_FF_ESC;
            }
            break;
        case TESTERTP_CF:
            Frame->Sn = Data[0] & TESTERTP_NIBBLE;
            pci = TESTERTP_PCI_CF;
            break;
        default:   /* FC */
            Frame->Fs    = Data[0] & TESTERTP_NIBBLE;
            Frame->Bs    = (Length > 1u) ? Data[1] : 0u;
            Frame->STmin = (Length > 2u) ? Data[2] : 0u;
            pci = TESTERTP_PCI_FC;
            break;
    }
    return pci;
}

boolean TesterTp_Decode(const uint8* Data, uint8 Length, TesterTp_FrameType* Frame)
{
    const TesterTp_FrameType empty = { TESTERTP_UNKNOWN, 0u, 0u, 0u, 0u, 0u, { 0u }, 0u };
    boolean valid = FALSE;
    uint8 pci;
    uint8 index;

    *Frame = empty;
    if ((Length > 0u) && ((Data[0] >> TESTERTP_SHIFT_TYPE) <= (uint8)TESTERTP_FC))
    {
        Frame->Kind = (TesterTp_KindType)(Data[0] >> TESTERTP_SHIFT_TYPE);
        pci = TesterTp_DecodePci(Data, Length, Frame);
        valid = (Length >= pci) ? TRUE : FALSE;
        for (index = pci; (valid == TRUE) && (index < Length) && (Frame->Kind != TESTERTP_FC); index++)
        {
            Frame->Data[Frame->DataLength] = Data[index];
            Frame->DataLength++;
        }
    }
    return valid;
}

/* ======================== Formatting ====================================== */

static void TesterTp_FormatStmin(uint8 STmin, char* Text, uint32 TextSize)
{
    if (STmin <= TESTERTP_STMIN_MS_MAX)
    {
        (void)snprintf(Text, TextSize, "%u ms", (unsigned)STmin);
    }
    else if ((STmin >= TESTERTP_STMIN_US_FIRST) && (STmin <= TESTERTP_STMIN_US_LAST))
    {
        (void)snprintf(Text, TextSize, "%u us",
                       (unsigned)((STmin - TESTERTP_STMIN_US_FIRST + 1u) * TESTERTP_US_PER_STEP));
    }
    else
    {
        (void)snprintf(Text, TextSize, "reserved");
    }
}

static const char* TesterTp_FsText(uint8 Fs)
{
    static const char* const text[] = { "CTS", "WAIT", "OVFLW" };
    return (Fs <= TESTERTP_FS_OVFLW) ? text[Fs] : "INVALID";
}

static uint32 TesterTp_FormatFields(const TesterTp_FrameType* Frame, char* Text, uint32 TextSize)
{
    char stmin[16];
    int written = 0;

    switch (Frame->Kind)
    {
        case TESTERTP_SF: written = snprintf(Text, TextSize, "SF  SF_DL=%u ", (unsigned)Frame->Dl); break;
        case TESTERTP_FF: written = snprintf(Text, TextSize, "FF  FF_DL=%lu ", (unsigned long)Frame->Dl); break;
        case TESTERTP_CF: written = snprintf(Text, TextSize, "CF  SN=%u ", (unsigned)Frame->Sn); break;
        default:
            TesterTp_FormatStmin(Frame->STmin, stmin, sizeof(stmin));
            written = snprintf(Text, TextSize, "FC  FS=%u(%s) BS=%u STmin=0x%02X(%s)", (unsigned)Frame->Fs,
                               TesterTp_FsText(Frame->Fs), (unsigned)Frame->Bs, (unsigned)Frame->STmin, stmin);
            break;
    }
    return (written > 0) ? (uint32)written : 0u;
}

void TesterTp_Format(const uint8* Data, uint8 Length, char* Text, uint32 TextSize)
{
    TesterTp_FrameType frame;
    uint32 used;
    uint8 index;

    if (TesterTp_Decode(Data, Length, &frame) == FALSE)
    {
        (void)snprintf(Text, TextSize, "??  unknown or too short N_PCI");
        return;
    }
    used = TesterTp_FormatFields(&frame, Text, TextSize);
    if ((frame.Kind != TESTERTP_FC) && (used < TextSize))
    {
        used += (uint32)snprintf(&Text[used], TextSize - used, " data:");
    }
    for (index = 0u; (frame.Kind != TESTERTP_FC) && (index < frame.DataLength) && (used < TextSize); index++)
    {
        used += (uint32)snprintf(&Text[used], TextSize - used, " %02X", (unsigned)frame.Data[index]);
    }
}
