/**
 * @file    CanTp_Internal.h
 * @brief   Internal interfaces of CanTp (frame codec, Rx and Tx state
 *          machines, configuration types). Not for other modules.
 *
 * Implementation-specific types are named CanTp_<TypeName>Type and are not
 * visible outside CanTp [SWS_CanTp_00002] [SWS_CanTp_00296].
 */
#ifndef CANTP_INTERNAL_H
#define CANTP_INTERNAL_H

#include "CanTp.h"

/* ======================== DET identifiers ================================= */
/* CanTp module ID = 35 is listed in the AUTOSAR "List of Basic Software
 * Modules" (not among the provided PDFs) -> verify. */
#define CANTP_MODULE_ID                ((uint16)35u)
#define CANTP_INSTANCE_ID              ((uint8)0u)

/* Service IDs (SWS_CanTp sections 8.3, 8.4) */
#define CANTP_API_ID_INIT              ((uint8)0x01u) /* [SWS_CanTp_00208] */
#define CANTP_API_ID_SHUTDOWN          ((uint8)0x02u) /* [SWS_CanTp_00211] */
#define CANTP_API_ID_MAINFUNCTION      ((uint8)0x06u) /* [SWS_CanTp_00213] */
#define CANTP_API_ID_READPARAMETER     ((uint8)0x0Bu) /* [SWS_CanTp_00323] */
#define CANTP_API_ID_TXCONFIRMATION    ((uint8)0x40u) /* [SWS_CanTp_00215] */
#define CANTP_API_ID_RXINDICATION      ((uint8)0x42u) /* [SWS_CanTp_00214] */
#define CANTP_API_ID_TRANSMIT          ((uint8)0x49u) /* [SWS_CanTp_00212] */
#define CANTP_API_ID_CANCELTRANSMIT    ((uint8)0x4Au) /* [SWS_CanTp_00246] */
#define CANTP_API_ID_CHANGEPARAMETER   ((uint8)0x4Bu) /* [SWS_CanTp_00302] */
#define CANTP_API_ID_CANCELRECEIVE     ((uint8)0x4Cu) /* [SWS_CanTp_00257] */

/* ======================== N_PCI layout ====================================
 * SWS_CanTp R25-11 Figure 7.2 "Summary of N_PCI bytes" (normal addressing):
 *   SF : byte#1 = 0000 | SF_DL                         (CAN_DL <= 8)
 *   FF : byte#1 = 0001 | FF_DL[11:8], byte#2 = FF_DL[7:0] (FF_DL <= 4095)
 *        byte#1 = 0001 0000, byte#2 = 0x00, byte#3..6 = FF_DL (FF_DL > 4095)
 *   CF : byte#1 = 0010 | SN
 *   FC : byte#1 = 0011 | FS, byte#2 = BS, byte#3 = STmin                  */
#define CANTP_PCI_TYPE_MASK            ((uint8)0xF0u)
#define CANTP_PCI_TYPE_SHIFT           4u
#define CANTP_PCI_LOW_NIBBLE_MASK      ((uint8)0x0Fu)

#define CANTP_PCI_TYPE_SF              ((uint8)0x0u)
#define CANTP_PCI_TYPE_FF              ((uint8)0x1u)
#define CANTP_PCI_TYPE_CF              ((uint8)0x2u)
#define CANTP_PCI_TYPE_FC              ((uint8)0x3u)

#define CANTP_SF_PCI_LENGTH            ((PduLengthType)1u)
#define CANTP_FF_PCI_LENGTH            ((PduLengthType)2u)
#define CANTP_FF_ESC_PCI_LENGTH        ((PduLengthType)6u)
#define CANTP_CF_PCI_LENGTH            ((PduLengthType)1u)
#define CANTP_FC_PCI_LENGTH            ((PduLengthType)3u)

#define CANTP_FF_DL_12BIT_MAX          ((uint32)0x0FFFu)

/* Classic CAN: TX_DL = RX_DL = 8 [SWS_CanTp_00350]. */
#define CANTP_CAN20_DL                 ((PduLengthType)8u)
#define CANTP_SF_MAX_DATA_LENGTH       ((PduLengthType)(CANTP_CAN20_DL - CANTP_SF_PCI_LENGTH))  /* 7 */
#define CANTP_FF_DATA_LENGTH           ((PduLengthType)(CANTP_CAN20_DL - CANTP_FF_PCI_LENGTH))  /* 6 */
#define CANTP_CF_MAX_DATA_LENGTH       ((PduLengthType)(CANTP_CAN20_DL - CANTP_CF_PCI_LENGTH))  /* 7 */

/* SN of the first CF after an FF, and SN wrap 0xF -> 0x0.
 * ISO 15765-2 (not among the provided PDFs) -> verify. */
#define CANTP_SN_FIRST_CF              ((uint8)1u)
#define CANTP_SN_MASK                  ((uint8)0x0Fu)

/* Flow status values. SWS_CanTp uses the names CTS / WT / OVFLW; the numeric
 * values are defined in ISO 15765-2 (not among the provided PDFs) -> verify. */
#define CANTP_FS_CTS                   ((uint8)0x0u)
#define CANTP_FS_WAIT                  ((uint8)0x1u)
#define CANTP_FS_OVFLW                 ((uint8)0x2u)

/* STmin encoding, ISO 15765-2 (not among the provided PDFs) -> verify:
 * 0x00..0x7F = 0..127 ms, 0xF1..0xF9 = 100..900 us, other values reserved. */
#define CANTP_STMIN_MS_MAX             ((uint8)0x7Fu)
#define CANTP_STMIN_US_FIRST           ((uint8)0xF1u)
#define CANTP_STMIN_US_LAST            ((uint8)0xF9u)

#define CANTP_BS_MAX                   ((uint16)0xFFu)  /* CanTpBs range 0..255 */

/* ======================== Configuration types ============================= */

/* CanTpRxTaType / CanTpTxTaType */
typedef uint8 CanTp_TaTypeType;
#define CANTP_PHYSICAL                 ((CanTp_TaTypeType)0u)
#define CANTP_FUNCTIONAL               ((CanTp_TaTypeType)1u)

/* CanTpRxNSdu (+ CanTpRxNPdu, CanTpTxFcNPdu). Index = CanTpRxNSduId. */
typedef struct
{
    PduIdType        RxNPduId;              /* CanTpRxNPdu/CanTpRxNPduId                     */
    PduIdType        PduRRxId;              /* CanTpRxNSduRef -> PduR handle                  */
    PduIdType        TxFcLSduRId;           /* CanTpTxFcNPdu/CanTpTxFcNPduRef -> LSduR handle */
    PduIdType        TxFcConfirmationPduId; /* CanTpTxFcNPdu/CanTpTxFcNPduConfirmationPduId   */
    CanTp_TaTypeType TaType;                /* CanTpRxTaType                                  */
    boolean          PaddingActive;         /* CanTpRxPaddingActivation == CANTP_ON           */
    uint8            Bs;                    /* CanTpBs                                        */
    uint8            STmin;                 /* CanTpSTmin, already encoded as in the FC       */
    uint16           WftMax;                /* CanTpRxWftMax                                  */
    uint16           NarTicks;              /* CanTpNar                                       */
    uint16           NbrTicks;              /* CanTpNbr                                       */
    uint16           NcrTicks;              /* CanTpNcr                                       */
} CanTp_RxNSduCfgType;

/* CanTpTxNSdu (+ CanTpTxNPdu, CanTpRxFcNPdu). Index = CanTpTxNSduId. */
typedef struct
{
    PduIdType        LSduRTxId;             /* CanTpTxNPdu/CanTpTxNPduRef -> LSduR handle   */
    PduIdType        TxConfirmationPduId;   /* CanTpTxNPdu/CanTpTxNPduConfirmationPduId     */
    PduIdType        RxFcNPduId;            /* CanTpRxFcNPdu/CanTpRxFcNPduId                */
    PduIdType        PduRTxId;              /* CanTpTxNSduRef -> PduR handle                */
    CanTp_TaTypeType TaType;                /* CanTpTxTaType                                */
    boolean          PaddingActive;         /* CanTpTxPaddingActivation == CANTP_ON         */
    boolean          CancelTransmitEnabled; /* CanTpTc                                      */
    uint16           NasTicks;              /* CanTpNas                                     */
    uint16           NbsTicks;              /* CanTpNbs                                     */
    uint16           NcsTicks;              /* CanTpNcs                                     */
} CanTp_TxNSduCfgType;

extern const CanTp_RxNSduCfgType CanTp_RxNSduCfg[CANTP_CFG_NUM_RX_NSDU];
extern const CanTp_TxNSduCfgType CanTp_TxNSduCfg[CANTP_CFG_NUM_TX_NSDU];

/* ======================== Frame codec (CanTp_Frame.c) ===================== */
typedef enum
{
    CANTP_FRAME_SF,
    CANTP_FRAME_FF,
    CANTP_FRAME_CF,
    CANTP_FRAME_FC,
    CANTP_FRAME_INVALID
} CanTp_FrameKindType;

/* Decoded N_PCI of one received N-PDU. */
typedef struct
{
    CanTp_FrameKindType Kind;
    uint32              DataLength;    /* SF_DL or FF_DL                   */
    uint8               Sn;            /* CF                               */
    uint8               Fs;            /* FC                               */
    uint8               Bs;            /* FC                               */
    uint8               STmin;         /* FC                               */
    PduLengthType       PayloadOffset; /* index of the first N_Data byte   */
} CanTp_NPciType;

void          CanTp_FrameDecode(const PduInfoType* NPdu, CanTp_NPciType* Pci);
PduLengthType CanTp_FrameEncodeSf(uint8* Frame, PduLengthType SfDl);
PduLengthType CanTp_FrameEncodeFf(uint8* Frame, PduLengthType FfDl);
PduLengthType CanTp_FrameEncodeCf(uint8* Frame, uint8 Sn);
PduLengthType CanTp_FrameEncodeFc(uint8* Frame, uint8 Fs, uint8 Bs, uint8 STmin);
PduLengthType CanTp_FrameFinalize(uint8* Frame, PduLengthType UsedLength, boolean PaddingActive);
boolean       CanTp_FrameIsStminValid(uint16 STmin);
uint16        CanTp_FrameStminToTicks(uint8 STmin);

/* ======================== Common helpers (CanTp.c) ======================== */
boolean CanTp_TimerElapsed(uint16* Timer);
boolean CanTp_IsLSduBusy(PduIdType LSduRTxId);
void    CanTp_ReportRuntimeError(uint8 ApiId, uint8 ErrorId);

/* ======================== Reception (CanTp_Rx.c) ========================== */
void           CanTp_RxInit(void);
void           CanTp_RxProcessNPdu(PduIdType RxNSduId, const CanTp_NPciType* Pci, const PduInfoType* NPdu);
void           CanTp_RxMainFunction(void);
boolean        CanTp_RxConfirmFc(PduIdType ConfirmationPduId, Std_ReturnType Result);
boolean        CanTp_RxIsLSduBusy(PduIdType LSduRTxId);
boolean        CanTp_RxIsBusy(PduIdType RxNSduId);
Std_ReturnType CanTp_RxCancel(PduIdType RxNSduId);
void           CanTp_RxSetParameter(PduIdType RxNSduId, TPParameterType Parameter, uint8 Value);
uint8          CanTp_RxGetParameter(PduIdType RxNSduId, TPParameterType Parameter);

/* ======================== Transmission (CanTp_Tx.c) ======================= */
void           CanTp_TxInit(void);
Std_ReturnType CanTp_TxStart(PduIdType TxNSduId, PduLengthType SduLength);
void           CanTp_TxProcessFc(PduIdType TxNSduId, const CanTp_NPciType* Pci, const PduInfoType* NPdu);
void           CanTp_TxMainFunction(void);
boolean        CanTp_TxConfirmFrame(PduIdType ConfirmationPduId, Std_ReturnType Result);
boolean        CanTp_TxIsLSduBusy(PduIdType LSduRTxId);
Std_ReturnType CanTp_TxCancel(PduIdType TxNSduId);

#endif /* CANTP_INTERNAL_H */
