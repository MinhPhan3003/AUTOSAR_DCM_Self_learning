/**
 * @file    CanTp_Sim.c
 * @brief   Interactive console: you play the tester, the program plays the
 *          ECU (CanTp + fake PduR upper layer + fake LSduR).
 *
 * Every frame on the "bus" is printed raw and split into its N_PCI fields.
 * You can set each field yourself (SF_DL, FF_DL, SN, FS, BS, STmin, padding)
 * to provoke the error paths of CanTp. Type "help" for the commands.
 *
 * Build: make sim      Run: ./out/CanTp_Sim [-e]   (-e echoes commands,
 * useful when a script is piped in: ./out/CanTp_Sim -e < script.txt)
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "CanTp.h"
#include "SchM_CanTp.h"
#include "PduR_Stub.h"
#include "LSduR_Stub.h"
#include "Det_Stub.h"
#include "TesterTp.h"

/* ======================== Constants ======================================= */
#define SIM_LINE_LENGTH      512u
#define SIM_MAX_TOKENS       80u
#define SIM_MAX_MESSAGE      512u
#define SIM_CAN_ID_PHYS      0x7E0u   /* CAN IDs are only known by the simulator */
#define SIM_CAN_ID_FUNC      0x7DFu
#define SIM_CAN_ID_RESP      0x7E8u
#define SIM_FF_CAPACITY      6u
#define SIM_CF_CAPACITY      7u
#define SIM_SF_CAPACITY      7u
#define SIM_PATTERN_START    0x00u

/* ======================== Simulator state ================================= */
typedef struct
{
    uint32  TimeMs;                         /* number of CanTp_MainFunction calls  */
    boolean AutoConfirm;                    /* confirm ECU frames automatically    */
    boolean Echo;                           /* print each command (-e)             */
    uint8   Message[SIM_MAX_MESSAGE];       /* tester message started with "ff"    */
    uint32  MessageLength;
    uint32  MessagePosition;                /* next byte for "cf"                  */
    uint8   NextSn;                         /* next SN for "cf"                    */
} Sim_StateType;

/* Options of a frame command: key=value pairs and hex bytes. */
typedef struct
{
    uint8   Bytes[SIM_MAX_MESSAGE];
    uint32  ByteCount;
    boolean HasDl;   uint32 Dl;
    boolean HasSn;   uint8  Sn;
    boolean HasLen;  uint32 Len;
    uint8   Fs;
    uint8   Bs;
    uint8   STmin;
    uint32  Count;
    boolean Padding;
    boolean Valid;
} Sim_OptionsType;

static Sim_StateType Sim;

/* ======================== Output ========================================== */

static void Sim_Print(const char* Who, const char* Text)
{
    printf("[t=%5lu ms] %-14s %s\n", (unsigned long)Sim.TimeMs, Who, Text);
}

static void Sim_PrintFrame(uint32 CanId, const char* Direction, const uint8* Data, uint8 Length)
{
    char fields[TESTERTP_TEXT_LENGTH];
    char raw[3u * TESTERTP_CAN_DL + 1u] = "";
    char who[32];
    uint8 index;

    for (index = 0u; index < Length; index++)
    {
        (void)snprintf(&raw[3u * index], sizeof(raw) - (3u * index), "%02X ", (unsigned)Data[index]);
    }
    TesterTp_Format(Data, Length, fields, sizeof(fields));
    (void)snprintf(who, sizeof(who), "0x%03lX %s", (unsigned long)CanId, Direction);
    printf("[t=%5lu ms] %-14s %-24s| %s\n", (unsigned long)Sim.TimeMs, who, raw, fields);
}

static const char* Sim_ErrorName(uint8 ErrorId)
{
    switch (ErrorId)
    {
        case CANTP_E_PARAM_CONFIG:       return "CANTP_E_PARAM_CONFIG";
        case CANTP_E_PARAM_ID:           return "CANTP_E_PARAM_ID";
        case CANTP_E_PARAM_POINTER:      return "CANTP_E_PARAM_POINTER";
        case CANTP_E_UNINIT:             return "CANTP_E_UNINIT";
        case CANTP_E_INVALID_TX_ID:      return "CANTP_E_INVALID_TX_ID";
        case CANTP_E_INVALID_RX_ID:      return "CANTP_E_INVALID_RX_ID";
        case CANTP_E_PADDING:            return "CANTP_E_PADDING";
        case CANTP_E_INVALID_TATYPE:     return "CANTP_E_INVALID_TATYPE";
        case CANTP_E_OPER_NOT_SUPPORTED: return "CANTP_E_OPER_NOT_SUPPORTED";
        case CANTP_E_COM:                return "CANTP_E_COM";
        case CANTP_E_RX_COM:             return "CANTP_E_RX_COM";
        case CANTP_E_TX_COM:             return "CANTP_E_TX_COM";
        default:                         return "?";
    }
}

/* ======================== Stub hooks ====================================== */

static void Sim_OnEcuFrame(const LSduR_Stub_FrameType* Frame)
{
    Sim_PrintFrame(SIM_CAN_ID_RESP, "ECU->TESTER", Frame->Data, (uint8)Frame->Length);
}

static void Sim_OnPduR(const char* Text)
{
    Sim_Print("  PduR <-CanTp", Text);
}

static void Sim_OnDet(uint16 ModuleId, uint8 ApiId, uint8 ErrorId, boolean IsRuntime)
{
    char text[128];
    (void)snprintf(text, sizeof(text), "%s error %s (0x%02X), module %u, api 0x%02X",
                   (IsRuntime == TRUE) ? "runtime" : "development", Sim_ErrorName(ErrorId),
                   (unsigned)ErrorId, (unsigned)ModuleId, (unsigned)ApiId);
    Sim_Print("  DET", text);
}

/* LSduR confirms the oldest frame CanTp gave to it. */
static boolean Sim_ConfirmOne(Std_ReturnType Result)
{
    LSduR_Stub_FrameType frame;
    boolean found = LSduR_Stub_PopFrame(&frame);

    if (found == TRUE)
    {
        /* LSduR maps its Tx PDU to the CanTp confirmation id (both 0x7E8 here). */
        Sim_Print("  LSduR->CanTp", (Result == E_OK) ? "CanTp_TxConfirmation(E_OK)" : "CanTp_TxConfirmation(E_NOT_OK)");
        CanTp_TxConfirmation(CanTpConf_CanTpTxNPdu_Resp7E8, Result);
    }
    return found;
}

static void Sim_AutoConfirm(void)
{
    while ((Sim.AutoConfirm == TRUE) && (Sim_ConfirmOne(E_OK) == TRUE))
    {
        /* confirm every frame that is on the bus */
    }
}

/* ======================== Option parsing ================================== */

static boolean Sim_IsWord(const char* Token, const char* Word)
{
    while ((*Token != '\0') && (*Word != '\0') && (tolower((unsigned char)*Token) == *Word))
    {
        Token++;
        Word++;
    }
    return ((*Token == '\0') && (*Word == '\0')) ? TRUE : FALSE;
}

/* "22F190" or "22" -> bytes. Returns FALSE if the token is not hex. */
static boolean Sim_ParseHex(const char* Token, Sim_OptionsType* Options)
{
    const size_t length = strlen(Token);
    boolean valid = ((length > 0u) && ((length % 2u) == 0u)) ? TRUE : FALSE;
    size_t index;

    for (index = 0u; (valid == TRUE) && (index < length); index++)
    {
        valid = (isxdigit((unsigned char)Token[index]) != 0) ? TRUE : FALSE;
    }
    for (index = 0u; (valid == TRUE) && (index < length) && (Options->ByteCount < SIM_MAX_MESSAGE); index += 2u)
    {
        char pair[3] = { Token[index], Token[index + 1u], '\0' };
        Options->Bytes[Options->ByteCount] = (uint8)strtoul(pair, NULL, 16);
        Options->ByteCount++;
    }
    return valid;
}

static uint8 Sim_ParseFs(const char* Value)
{
    uint8 fs;
    if (Sim_IsWord(Value, "cts") == TRUE)        { fs = TESTERTP_FS_CTS; }
    else if (Sim_IsWord(Value, "wait") == TRUE)  { fs = TESTERTP_FS_WAIT; }
    else if (Sim_IsWord(Value, "ovflw") == TRUE) { fs = TESTERTP_FS_OVFLW; }
    else                                         { fs = (uint8)strtoul(Value, NULL, 0); }
    return fs;
}

static boolean Sim_ParseKeyValue(const char* Key, const char* Value, Sim_OptionsType* Options)
{
    const uint32 number = (uint32)strtoul(Value, NULL, 0);
    boolean known = TRUE;

    if (Sim_IsWord(Key, "dl") == TRUE)          { Options->HasDl = TRUE; Options->Dl = number; }
    else if (Sim_IsWord(Key, "sn") == TRUE)     { Options->HasSn = TRUE; Options->Sn = (uint8)number; }
    else if (Sim_IsWord(Key, "len") == TRUE)    { Options->HasLen = TRUE; Options->Len = number; }
    else if (Sim_IsWord(Key, "fs") == TRUE)     { Options->Fs = Sim_ParseFs(Value); }
    else if (Sim_IsWord(Key, "bs") == TRUE)     { Options->Bs = (uint8)number; }
    else if (Sim_IsWord(Key, "st") == TRUE)     { Options->STmin = (uint8)number; }
    else if (Sim_IsWord(Key, "n") == TRUE)      { Options->Count = number; }
    else if (Sim_IsWord(Key, "pad") == TRUE)    { Options->Padding = Sim_IsWord(Value, "off") ? FALSE : TRUE; }
    else                                        { known = FALSE; }
    return known;
}

static Sim_OptionsType Sim_ParseOptions(int Argc, char** Argv)
{
    Sim_OptionsType options;
    int index;

    (void)memset(&options, 0, sizeof(options));
    options.Padding = TRUE;
    options.Count   = 1u;
    options.Valid   = TRUE;
    for (index = 1; (index < Argc) && (options.Valid == TRUE); index++)
    {
        char* equal = strchr(Argv[index], '=');
        if (equal != NULL)
        {
            *equal = '\0';
            options.Valid = Sim_ParseKeyValue(Argv[index], equal + 1, &options);
        }
        else
        {
            options.Valid = Sim_ParseHex(Argv[index], &options);
        }
        if (options.Valid == FALSE)
        {
            printf("  ? cannot read \"%s\" (hex bytes or key=value expected)\n", Argv[index]);
        }
    }
    return options;
}

/* len=N without bytes: generate 00 01 02 ... */
static void Sim_ApplyLength(Sim_OptionsType* Options)
{
    uint32 index;

    if ((Options->HasLen == TRUE) && (Options->ByteCount == 0u))
    {
        Options->ByteCount = (Options->Len > SIM_MAX_MESSAGE) ? SIM_MAX_MESSAGE : Options->Len;
        for (index = 0u; index < Options->ByteCount; index++)
        {
            Options->Bytes[index] = (uint8)(SIM_PATTERN_START + index);
        }
    }
}

/* ======================== Tester frames =================================== */

static void Sim_TesterSend(uint32 CanId, PduIdType RxNPduId, const TesterTp_FrameType* Frame, boolean Padding)
{
    uint8 raw[TESTERTP_CAN_DL];
    PduInfoType info;

    info.SduLength   = TesterTp_Encode(Frame, Padding, raw);
    info.SduDataPtr  = raw;
    info.MetaDataPtr = NULL_PTR;
    Sim_PrintFrame(CanId, "TESTER->ECU", raw, (uint8)info.SduLength);
    CanTp_RxIndication(RxNPduId, &info);
}

static void Sim_CmdSf(int Argc, char** Argv, boolean Functional)
{
    Sim_OptionsType options = Sim_ParseOptions(Argc, Argv);
    TesterTp_FrameType frame;

    Sim_ApplyLength(&options);
    if (options.Valid == TRUE)
    {
        frame = TesterTp_MakeSf(options.Bytes, (uint8)((options.ByteCount > SIM_SF_CAPACITY) ? SIM_SF_CAPACITY : options.ByteCount));
        frame.Dl = (options.HasDl == TRUE) ? options.Dl : options.ByteCount;
        Sim_TesterSend((Functional == TRUE) ? SIM_CAN_ID_FUNC : SIM_CAN_ID_PHYS,
                       (Functional == TRUE) ? CanTpConf_CanTpRxNPdu_FuncReq7DF : CanTpConf_CanTpRxNPdu_Req7E0,
                       &frame, options.Padding);
    }
}

static void Sim_CmdFf(int Argc, char** Argv)
{
    Sim_OptionsType options = Sim_ParseOptions(Argc, Argv);
    TesterTp_FrameType frame;
    uint32 capacity;

    Sim_ApplyLength(&options);
    if (options.Valid == TRUE)
    {
        (void)memcpy(Sim.Message, options.Bytes, options.ByteCount);
        Sim.MessageLength = options.ByteCount;
        frame    = TesterTp_MakeFf((options.HasDl == TRUE) ? options.Dl : options.ByteCount, NULL_PTR, 0u);
        capacity = TesterTp_Capacity(&frame);
        frame    = TesterTp_MakeFf(frame.Dl, Sim.Message, (uint8)((options.ByteCount > capacity) ? capacity : options.ByteCount));
        Sim.MessagePosition = frame.DataLength;
        Sim.NextSn          = 1u;
        Sim_TesterSend(SIM_CAN_ID_PHYS, CanTpConf_CanTpRxNPdu_Req7E0, &frame, TRUE);
    }
}

/* One CF: explicit bytes, or the next part of the message started by "ff". */
static void Sim_SendOneCf(const Sim_OptionsType* Options)
{
    const uint32 remaining = Sim.MessageLength - Sim.MessagePosition;
    const uint8 sn = (Options->HasSn == TRUE) ? Options->Sn : Sim.NextSn;
    TesterTp_FrameType frame;

    if (Options->ByteCount > 0u)
    {
        frame = TesterTp_MakeCf(sn, Options->Bytes, (uint8)Options->ByteCount);
    }
    else
    {
        frame = TesterTp_MakeCf(sn, &Sim.Message[Sim.MessagePosition],
                                (uint8)((remaining > SIM_CF_CAPACITY) ? SIM_CF_CAPACITY : remaining));
        Sim.MessagePosition += frame.DataLength;
    }
    Sim.NextSn = (uint8)((sn + 1u) & 0x0Fu);
    Sim_TesterSend(SIM_CAN_ID_PHYS, CanTpConf_CanTpRxNPdu_Req7E0, &frame, Options->Padding);
}

static void Sim_CmdCf(int Argc, char** Argv)
{
    Sim_OptionsType options = Sim_ParseOptions(Argc, Argv);
    uint32 index;

    for (index = 0u; (options.Valid == TRUE) && (index < options.Count); index++)
    {
        if ((options.ByteCount == 0u) && (Sim.MessagePosition >= Sim.MessageLength))
        {
            printf("  ? no data left from \"ff\": give the CF bytes yourself\n");
            break;
        }
        Sim_SendOneCf(&options);
        options.HasSn = FALSE;   /* sn= only applies to the first CF */
    }
}

static void Sim_CmdFc(int Argc, char** Argv)
{
    const Sim_OptionsType options = Sim_ParseOptions(Argc, Argv);
    TesterTp_FrameType frame;

    if (options.Valid == TRUE)
    {
        frame = TesterTp_MakeFc(options.Fs, options.Bs, options.STmin);
        Sim_TesterSend(SIM_CAN_ID_PHYS, CanTpConf_CanTpRxFcNPdu_Req7E0, &frame, options.Padding);
    }
}

static void Sim_CmdRaw(int Argc, char** Argv)
{
    const Sim_OptionsType options = Sim_ParseOptions(Argc, Argv);
    PduInfoType info;

    if ((options.Valid == TRUE) && (options.ByteCount > 0u) && (options.ByteCount <= TESTERTP_CAN_DL))
    {
        info.SduDataPtr  = (uint8*)options.Bytes;
        info.SduLength   = (PduLengthType)options.ByteCount;
        info.MetaDataPtr = NULL_PTR;
        Sim_PrintFrame(SIM_CAN_ID_PHYS, "TESTER->ECU", options.Bytes, (uint8)options.ByteCount);
        CanTp_RxIndication(CanTpConf_CanTpRxNPdu_Req7E0, &info);
    }
    else
    {
        printf("  ? raw needs 1..8 bytes\n");
    }
}

/* ======================== ECU side commands =============================== */

static void Sim_CmdSend(int Argc, char** Argv)
{
    Sim_OptionsType options = Sim_ParseOptions(Argc, Argv);
    PduInfoType info = { NULL_PTR, NULL_PTR, 0u };
    char text[96];

    Sim_ApplyLength(&options);
    if ((options.Valid == TRUE) && (options.ByteCount > 0u))
    {
        PduR_Stub_CanTpSetTxData(options.Bytes, (PduLengthType)options.ByteCount);
        info.SduLength = (PduLengthType)options.ByteCount;
        (void)snprintf(text, sizeof(text), "CanTp_Transmit(DiagTx, SduLength=%u) -> %s", (unsigned)info.SduLength,
                       (CanTp_Transmit(CanTpConf_CanTpTxNSdu_DiagTx, &info) == E_OK) ? "E_OK" : "E_NOT_OK");
        Sim_Print("  PduR->CanTp", text);
        Sim_Print("", "(frames are sent by CanTp_MainFunction: use \"tick\")");
    }
}

static void Sim_CmdTick(int Argc, char** Argv)
{
    const uint32 ticks = (Argc > 1) ? (uint32)strtoul(Argv[1], NULL, 0) : 1u;
    uint32 tick;

    for (tick = 0u; tick < ticks; tick++)
    {
        Sim.TimeMs++;
        CanTp_MainFunction();
        Sim_AutoConfirm();
    }
}

static void Sim_CmdConf(int Argc, char** Argv)
{
    const Std_ReturnType result = ((Argc > 1) && (Sim_IsWord(Argv[1], "fail") == TRUE)) ? E_NOT_OK : E_OK;

    if (Sim_ConfirmOne(result) == FALSE)
    {
        printf("  ? no ECU frame waits for confirmation\n");
    }
}

static BufReq_ReturnType Sim_ParseBufReq(const char* Word)
{
    BufReq_ReturnType result = BUFREQ_OK;
    if (Sim_IsWord(Word, "notok") == TRUE)     { result = BUFREQ_E_NOT_OK; }
    else if (Sim_IsWord(Word, "busy") == TRUE) { result = BUFREQ_E_BUSY; }
    else if (Sim_IsWord(Word, "ovfl") == TRUE) { result = BUFREQ_E_OVFL; }
    else                                       { result = BUFREQ_OK; }
    return result;
}

static void Sim_CmdParam(int Argc, char** Argv)
{
    char text[96];
    TPParameterType parameter;
    uint16 value;

    if (Argc < 3)
    {
        printf("  ? usage: param bs|stmin <value>\n");
        return;
    }
    parameter = (Sim_IsWord(Argv[1], "stmin") == TRUE) ? TP_STMIN : TP_BS;
    value     = (uint16)strtoul(Argv[2], NULL, 0);
    (void)snprintf(text, sizeof(text), "CanTp_ChangeParameter(DiagPhysRx, %s, 0x%02X) -> %s",
                   (parameter == TP_STMIN) ? "TP_STMIN" : "TP_BS", (unsigned)value,
                   (CanTp_ChangeParameter(CanTpConf_CanTpRxNSdu_DiagPhysRx, parameter, value) == E_OK) ? "E_OK" : "E_NOT_OK");
    Sim_Print("  App->CanTp", text);
}

static void Sim_CmdRead(void)
{
    char text[96];
    uint16 bs = 0u;
    uint16 stmin = 0u;

    (void)CanTp_ReadParameter(CanTpConf_CanTpRxNSdu_DiagPhysRx, TP_BS, &bs);
    (void)CanTp_ReadParameter(CanTpConf_CanTpRxNSdu_DiagPhysRx, TP_STMIN, &stmin);
    (void)snprintf(text, sizeof(text), "CanTp_ReadParameter(DiagPhysRx): BS=%u STmin=0x%02X", (unsigned)bs, (unsigned)stmin);
    Sim_Print("  App->CanTp", text);
}

static void Sim_CmdCancel(int Argc, char** Argv)
{
    char text[64];
    const boolean tx = ((Argc > 1) && (Sim_IsWord(Argv[1], "tx") == TRUE)) ? TRUE : FALSE;
    const Std_ReturnType result = (tx == TRUE) ? CanTp_CancelTransmit(CanTpConf_CanTpTxNSdu_DiagTx)
                                               : CanTp_CancelReceive(CanTpConf_CanTpRxNSdu_DiagPhysRx);

    (void)snprintf(text, sizeof(text), "%s -> %s", (tx == TRUE) ? "CanTp_CancelTransmit(DiagTx)"
                   : "CanTp_CancelReceive(DiagPhysRx)", (result == E_OK) ? "E_OK" : "E_NOT_OK");
    Sim_Print("  App->CanTp", text);
}

static void Sim_CmdRxData(void)
{
    PduLengthType length = 0u;
    const uint8* data = PduR_Stub_CanTpGetRxData(&length);
    PduLengthType index;

    printf("  upper layer Rx buffer (%u bytes):", (unsigned)length);
    for (index = 0u; index < length; index++)
    {
        printf("%s%02X", ((index % 16u) == 0u) ? "\n   " : " ", (unsigned)data[index]);
    }
    printf("\n");
}

static void Sim_Reset(void)
{
    Sim.TimeMs = 0u;
    Sim.MessageLength = 0u;
    Sim.MessagePosition = 0u;
    Sim.NextSn = 1u;
    Det_Stub_Reset();
    LSduR_Stub_Reset();
    PduR_Stub_CanTpReset();
    CanTp_Init(NULL_PTR);
    Sim_Print("  EcuM->CanTp", "CanTp_Init()");
}

/* ======================== Help ============================================ */

static void Sim_PrintHelp(void)
{
    printf("\n"
        "You are the TESTER. Frames go on 0x7E0 (physical) / 0x7DF (functional).\n"
        "The ECU answers on 0x7E8. Bytes are hex: \"22 F1 90\" or \"22F190\".\n"
        "Options are key=value; numbers are decimal or 0x.. hex.\n\n"
        " Tester -> ECU (each N_PCI field can be set)\n"
        "  sf  <bytes> [dl=N] [pad=off]       Single Frame (SF_DL = byte count unless dl=)\n"
        "  func <bytes>                       Single Frame on the functional ID 0x7DF\n"
        "  ff  <bytes>|len=N [dl=N]           First Frame (FF_DL = byte count unless dl=)\n"
        "                                     the rest of the bytes is kept for \"cf\"\n"
        "  cf  [n=N] [sn=N] [<bytes>] [pad=off]  next N Consecutive Frames of the \"ff\" message\n"
        "  fc  [fs=cts|wait|ovflw|N] [bs=N] [st=N] [pad=off]   Flow Control for the ECU's transmission\n"
        "  raw <1..8 bytes>                   any frame, no N_PCI help\n\n"
        " ECU side\n"
        "  send <bytes>|len=N                 upper layer calls CanTp_Transmit (ECU response)\n"
        "  tick [N]                           run CanTp_MainFunction N times (1 tick = 1 ms)\n"
        "  auto on|off                        LSduR confirms ECU frames automatically (default on)\n"
        "  conf [ok|fail]                     confirm the oldest ECU frame (when auto is off)\n"
        "  buf N                              free Rx buffer of the upper layer (FC WAIT / CTS)\n"
        "  sor ok|notok|ovfl                  result of PduR_CanTpStartOfReception\n"
        "  copytx ok|busy|notok               result of PduR_CanTpCopyTxData\n"
        "  param bs|stmin N                   CanTp_ChangeParameter on the physical Rx N-SDU\n"
        "  read                               CanTp_ReadParameter BS / STmin\n"
        "  cancel rx|tx                       CanTp_CancelReceive / CanTp_CancelTransmit\n"
        "  rxdata                             data received by the upper layer\n"
        "  init                               CanTp_Init (all connections lost)\n"
        "  help | quit                        # starts a comment\n\n"
        " Example: ff 2E F1 90 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10\n"
        "          tick          (ECU sends FC)\n"
        "          cf n=2        (BS=2 -> ECU wants a new FC after 2 CFs)\n"
        "          tick\n"
        "          cf\n\n");
}

/* ======================== Command loop ==================================== */

static int Sim_Tokenize(char* Line, char** Argv)
{
    int argc = 0;
    char* token = strtok(Line, " \t\r\n");

    while ((token != NULL) && (token[0] != '#') && (argc < (int)SIM_MAX_TOKENS))
    {
        Argv[argc] = token;
        argc++;
        token = strtok(NULL, " \t\r\n");
    }
    return argc;
}

static void Sim_SetFlag(int Argc, char** Argv, boolean* Flag, const char* Name)
{
    *Flag = ((Argc > 1) && (Sim_IsWord(Argv[1], "off") == TRUE)) ? FALSE : TRUE;
    printf("  %s %s\n", Name, (*Flag == TRUE) ? "on" : "off");
}

/* Returns FALSE on "quit". */
static boolean Sim_Execute(int Argc, char** Argv)
{
    const char* cmd = Argv[0];
    boolean running = TRUE;

    if (Sim_IsWord(cmd, "sf") == TRUE)            { Sim_CmdSf(Argc, Argv, FALSE); }
    else if (Sim_IsWord(cmd, "func") == TRUE)     { Sim_CmdSf(Argc, Argv, TRUE); }
    else if (Sim_IsWord(cmd, "ff") == TRUE)       { Sim_CmdFf(Argc, Argv); }
    else if (Sim_IsWord(cmd, "cf") == TRUE)       { Sim_CmdCf(Argc, Argv); }
    else if (Sim_IsWord(cmd, "fc") == TRUE)       { Sim_CmdFc(Argc, Argv); }
    else if (Sim_IsWord(cmd, "raw") == TRUE)      { Sim_CmdRaw(Argc, Argv); }
    else if (Sim_IsWord(cmd, "send") == TRUE)     { Sim_CmdSend(Argc, Argv); }
    else if (Sim_IsWord(cmd, "tick") == TRUE)     { Sim_CmdTick(Argc, Argv); }
    else if (Sim_IsWord(cmd, "conf") == TRUE)     { Sim_CmdConf(Argc, Argv); }
    else if (Sim_IsWord(cmd, "auto") == TRUE)     { Sim_SetFlag(Argc, Argv, &Sim.AutoConfirm, "auto confirmation"); }
    else if (Sim_IsWord(cmd, "buf") == TRUE)      { PduR_Stub_CanTpSetRxAvailable((PduLengthType)((Argc > 1) ? strtoul(Argv[1], NULL, 0) : 0u)); }
    else if (Sim_IsWord(cmd, "sor") == TRUE)      { PduR_Stub_CanTpSetStartOfReceptionResult(Sim_ParseBufReq((Argc > 1) ? Argv[1] : "ok")); }
    else if (Sim_IsWord(cmd, "copytx") == TRUE)   { PduR_Stub_CanTpSetCopyTxDataResult(Sim_ParseBufReq((Argc > 1) ? Argv[1] : "ok")); }
    else if (Sim_IsWord(cmd, "param") == TRUE)    { Sim_CmdParam(Argc, Argv); }
    else if (Sim_IsWord(cmd, "read") == TRUE)     { Sim_CmdRead(); }
    else if (Sim_IsWord(cmd, "cancel") == TRUE)   { Sim_CmdCancel(Argc, Argv); }
    else if (Sim_IsWord(cmd, "rxdata") == TRUE)   { Sim_CmdRxData(); }
    else if (Sim_IsWord(cmd, "init") == TRUE)     { Sim_Reset(); }
    else if (Sim_IsWord(cmd, "help") == TRUE)     { Sim_PrintHelp(); }
    else if (Sim_IsWord(cmd, "quit") == TRUE)     { running = FALSE; }
    else                                          { printf("  ? unknown command \"%s\" (type help)\n", cmd); }
    Sim_AutoConfirm();
    return running;
}

int main(int argc, char** argv)
{
    char line[SIM_LINE_LENGTH];
    char* tokens[SIM_MAX_TOKENS];
    boolean running = TRUE;

    Sim.Echo        = ((argc > 1) && (strcmp(argv[1], "-e") == 0)) ? TRUE : FALSE;
    Sim.AutoConfirm = TRUE;
    LSduR_Stub_SetHook(&Sim_OnEcuFrame);
    PduR_Stub_CanTpSetTrace(&Sim_OnPduR);
    Det_Stub_SetHook(&Sim_OnDet);
    printf("CanTp simulator - AUTOSAR CP R25-11 CanTp example (classic CAN, normal addressing)\n"
           "ECU config: BS=2 STmin=5ms WFTmax=3 padding=0xCC N_Ar/N_As=70 N_Br/N_Cs=20 N_Bs/N_Cr=150 ms\n"
           "Type \"help\" for the commands.\n");
    Sim_Reset();
    while (running == TRUE)
    {
        int count;
        if (Sim.Echo == FALSE)
        {
            printf("cantp> ");
            (void)fflush(stdout);
        }
        if (fgets(line, sizeof(line), stdin) == NULL)
        {
            break;
        }
        if (Sim.Echo == TRUE)
        {
            printf("\n> %s", line);
        }
        count = Sim_Tokenize(line, tokens);
        if (count > 0)
        {
            running = Sim_Execute(count, tokens);
        }
        (void)fflush(stdout);
    }
    return 0;
}
