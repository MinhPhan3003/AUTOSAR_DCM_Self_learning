/**
 * @file    CanTp_Frame.c
 * @brief   N_PCI encoder / decoder for SF, FF, CF and FC (normal addressing,
 *          classic CAN).
 *
 * Reference: AUTOSAR CP R25-11 SWS_CANTransportLayer
 *   - Figure 7.2 "Summary of N_PCI bytes"
 *   - 7.3.8 N-PDU padding [SWS_CanTp_00059] [SWS_CanTp_00347] [SWS_CanTp_00348]
 * Field values and validity limits come from ISO 15765-2, which the SWS
 * follows [SWS_CanTp_00033] but which is not among the provided PDFs.
 */
#include "CanTp_Internal.h"

/* Byte positions inside an N-PDU (normal addressing). */
#define CANTP_POS_PCI          0u
#define CANTP_POS_FF_DL_LOW    1u
#define CANTP_POS_FF_ESC_DL    2u   /* first of 4 bytes, MSB first */
#define CANTP_POS_FC_BS        1u
#define CANTP_POS_FC_STMIN     2u

#define CANTP_BYTE_MASK        0xFFu
#define CANTP_BITS_PER_BYTE    8u
#define CANTP_FF_ESC_DL_BYTES  4u

/* ======================== Decoder ========================================= */

static void CanTp_FrameDecodeSf(const PduInfoType* NPdu, CanTp_NPciType* Pci)
{
    const PduLengthType sfDl = (PduLengthType)(NPdu->SduDataPtr[CANTP_POS_PCI] & CANTP_PCI_LOW_NIBBLE_MASK);

    /* SF_DL = 0 is the CAN FD escape sequence: CAN FD not supported here. */
    if ((sfDl > 0u) && (sfDl <= CANTP_SF_MAX_DATA_LENGTH) &&
        (sfDl <= (PduLengthType)(NPdu->SduLength - CANTP_SF_PCI_LENGTH)))
    {
        Pci->Kind          = CANTP_FRAME_SF;
        Pci->DataLength    = sfDl;
        Pci->PayloadOffset = CANTP_SF_PCI_LENGTH;
    }
}

static uint32 CanTp_FrameReadEscapeFfDl(const uint8* Data)
{
    uint32 value = 0u;
    uint8 index;

    for (index = 0u; index < CANTP_FF_ESC_DL_BYTES; index++)
    {
        value = (value << CANTP_BITS_PER_BYTE) | Data[CANTP_POS_FF_ESC_DL + index];
    }
    return value;
}

static void CanTp_FrameDecodeFf(const PduInfoType* NPdu, CanTp_NPciType* Pci)
{
    const uint8* data = NPdu->SduDataPtr;
    uint32 ffDl = ((uint32)(data[CANTP_POS_PCI] & CANTP_PCI_LOW_NIBBLE_MASK) << CANTP_BITS_PER_BYTE) |
                  data[CANTP_POS_FF_DL_LOW];
    PduLengthType offset = CANTP_FF_PCI_LENGTH;
    uint32 minimum = CANTP_SF_MAX_DATA_LENGTH;   /* FF_DL must not fit in an SF */

    if (ffDl == 0u)
    {
        ffDl    = CanTp_FrameReadEscapeFfDl(data);
        offset  = CANTP_FF_ESC_PCI_LENGTH;
        minimum = CANTP_FF_DL_12BIT_MAX;         /* escape only for FF_DL > 4095 */
    }
    /* An FF always fills the whole CAN frame [Inference from ISO 15765-2]. */
    if ((NPdu->SduLength >= CANTP_CAN20_DL) && (ffDl > minimum))
    {
        Pci->Kind          = CANTP_FRAME_FF;
        Pci->DataLength    = ffDl;
        Pci->PayloadOffset = offset;
    }
}

void CanTp_FrameDecode(const PduInfoType* NPdu, CanTp_NPciType* Pci)
{
    const uint8 type = (uint8)((NPdu->SduDataPtr[CANTP_POS_PCI] & CANTP_PCI_TYPE_MASK) >> CANTP_PCI_TYPE_SHIFT);
    const uint8 lowNibble = (uint8)(NPdu->SduDataPtr[CANTP_POS_PCI] & CANTP_PCI_LOW_NIBBLE_MASK);

    Pci->Kind = CANTP_FRAME_INVALID;
    if (type == CANTP_PCI_TYPE_SF)
    {
        CanTp_FrameDecodeSf(NPdu, Pci);
    }
    else if (type == CANTP_PCI_TYPE_FF)
    {
        CanTp_FrameDecodeFf(NPdu, Pci);
    }
    else if ((type == CANTP_PCI_TYPE_CF) && (NPdu->SduLength > CANTP_CF_PCI_LENGTH))
    {
        Pci->Kind          = CANTP_FRAME_CF;
        Pci->Sn            = lowNibble;
        Pci->PayloadOffset = CANTP_CF_PCI_LENGTH;
    }
    else if ((type == CANTP_PCI_TYPE_FC) && (NPdu->SduLength >= CANTP_FC_PCI_LENGTH))
    {
        Pci->Kind          = CANTP_FRAME_FC;
        Pci->Fs            = lowNibble;
        Pci->Bs            = NPdu->SduDataPtr[CANTP_POS_FC_BS];
        Pci->STmin         = NPdu->SduDataPtr[CANTP_POS_FC_STMIN];
        Pci->PayloadOffset = CANTP_FC_PCI_LENGTH;
    }
    else
    {
        /* unknown N-PDU or too short: CANTP_FRAME_INVALID -> ignored */
    }
}

/* ======================== Encoder ========================================= */

PduLengthType CanTp_FrameEncodeSf(uint8* Frame, PduLengthType SfDl)
{
    Frame[CANTP_POS_PCI] = (uint8)((CANTP_PCI_TYPE_SF << CANTP_PCI_TYPE_SHIFT) | (SfDl & CANTP_PCI_LOW_NIBBLE_MASK));
    return CANTP_SF_PCI_LENGTH;
}

PduLengthType CanTp_FrameEncodeFf(uint8* Frame, PduLengthType FfDl)
{
    PduLengthType pciLength = CANTP_FF_PCI_LENGTH;
    uint8 index;

    if ((uint32)FfDl <= CANTP_FF_DL_12BIT_MAX)
    {
        Frame[CANTP_POS_PCI]       = (uint8)((CANTP_PCI_TYPE_FF << CANTP_PCI_TYPE_SHIFT) |
                                             ((FfDl >> CANTP_BITS_PER_BYTE) & CANTP_PCI_LOW_NIBBLE_MASK));
        Frame[CANTP_POS_FF_DL_LOW] = (uint8)(FfDl & CANTP_BYTE_MASK);
    }
    else
    {
        Frame[CANTP_POS_PCI]       = (uint8)(CANTP_PCI_TYPE_FF << CANTP_PCI_TYPE_SHIFT);
        Frame[CANTP_POS_FF_DL_LOW] = 0u;
        for (index = 0u; index < CANTP_FF_ESC_DL_BYTES; index++)
        {
            const uint8 shift = (uint8)((CANTP_FF_ESC_DL_BYTES - 1u - index) * CANTP_BITS_PER_BYTE);
            Frame[CANTP_POS_FF_ESC_DL + index] = (uint8)(((uint32)FfDl >> shift) & CANTP_BYTE_MASK);
        }
        pciLength = CANTP_FF_ESC_PCI_LENGTH;
    }
    return pciLength;
}

PduLengthType CanTp_FrameEncodeCf(uint8* Frame, uint8 Sn)
{
    Frame[CANTP_POS_PCI] = (uint8)((CANTP_PCI_TYPE_CF << CANTP_PCI_TYPE_SHIFT) | (Sn & CANTP_SN_MASK));
    return CANTP_CF_PCI_LENGTH;
}

PduLengthType CanTp_FrameEncodeFc(uint8* Frame, uint8 Fs, uint8 Bs, uint8 STmin)
{
    Frame[CANTP_POS_PCI]      = (uint8)((CANTP_PCI_TYPE_FC << CANTP_PCI_TYPE_SHIFT) | (Fs & CANTP_PCI_LOW_NIBBLE_MASK));
    Frame[CANTP_POS_FC_BS]    = Bs;
    Frame[CANTP_POS_FC_STMIN] = STmin;
    return CANTP_FC_PCI_LENGTH;
}

/* Padding: fill the unused bytes with CANTP_PADDING_BYTE and send 8 bytes
 * [SWS_CanTp_00347] [SWS_CanTp_00348]. Without padding the frame keeps
 * its used length. Returns the CAN_DL to transmit. */
PduLengthType CanTp_FrameFinalize(uint8* Frame, PduLengthType UsedLength, boolean PaddingActive)
{
    PduLengthType length = UsedLength;

    if (PaddingActive == TRUE)
    {
        for (length = UsedLength; length < CANTP_CAN20_DL; length++)
        {
            Frame[length] = CANTP_PADDING_BYTE;
        }
    }
    return length;
}

/* ======================== STmin =========================================== */

boolean CanTp_FrameIsStminValid(uint16 STmin)
{
    return ((STmin <= CANTP_STMIN_MS_MAX) ||
            ((STmin >= CANTP_STMIN_US_FIRST) && (STmin <= CANTP_STMIN_US_LAST))) ? TRUE : FALSE;
}

/* STmin received in an FC -> number of main function ticks to wait. */
uint16 CanTp_FrameStminToTicks(uint8 STmin)
{
    uint16 ticks;

    if (STmin <= CANTP_STMIN_MS_MAX)
    {
        ticks = CANTP_MS_TO_TICKS((uint16)STmin);
    }
    else if ((STmin >= CANTP_STMIN_US_FIRST) && (STmin <= CANTP_STMIN_US_LAST))
    {
        ticks = 1u;   /* 100..900 us, rounded up to one main function period [Inference] */
    }
    else
    {
        /* Reserved value: use the longest STmin (127 ms). ISO 15765-2 -> verify. */
        ticks = CANTP_MS_TO_TICKS((uint16)CANTP_STMIN_MS_MAX);
    }
    return ticks;
}
