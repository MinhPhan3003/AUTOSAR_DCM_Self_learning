/**
 * @file    PduR_Stub_CanTp.c
 * @brief   PduR stub, CanTp side: a configurable fake upper layer.
 *
 * Return values follow AUTOSAR CP R25-11 SWS_PDURouter 8.3.4:
 *   - StartOfReception: BUFREQ_OK + available buffer, or BUFREQ_E_NOT_OK /
 *     BUFREQ_E_OVFL with bufferSizePtr unchanged [SWS_PduR_00507].
 *   - CopyRxData with SduLength 0 only reports the available buffer
 *     [SWS_PduR_00512].
 *   - CopyTxData returns BUFREQ_E_BUSY when not enough data is available
 *     [SWS_PduR_00518].
 */
#include <stdio.h>
#include "PduR_CanTp.h"
#include "PduR_Stub.h"

#define PDUR_STUB_TRACE_LENGTH  160u

typedef struct
{
    BufReq_ReturnType SorResult;
    BufReq_ReturnType CopyRxResult;
    PduLengthType     RxAvailable;
    PduLengthType     RxLength;
    uint8             RxData[PDUR_STUB_CANTP_BUFFER_SIZE];
    uint32            SorCount;
    PduLengthType     LastTpSduLength;
    uint32            RxIndicationCount;
    Std_ReturnType    LastRxResult;
    PduIdType         LastRxId;
    BufReq_ReturnType CopyTxResult;
    PduLengthType     TxLength;
    PduLengthType     TxPosition;
    uint8             TxData[PDUR_STUB_CANTP_BUFFER_SIZE];
    uint32            TxConfirmationCount;
    Std_ReturnType    LastTxResult;
} PduR_StubCanTpType;

static PduR_StubCanTpType    PduR_StubCanTp;
static PduR_Stub_TraceFnType PduR_StubTrace = NULL_PTR;

/* ======================== Helpers ========================================= */
static const char* PduR_StubBufReqText(BufReq_ReturnType Result)
{
    static const char* const text[] = { "BUFREQ_OK", "BUFREQ_E_NOT_OK", "BUFREQ_E_BUSY", "BUFREQ_E_OVFL" };
    return (Result <= BUFREQ_E_OVFL) ? text[Result] : "BUFREQ_?";
}

static void PduR_StubTraceLine(const char* Text)
{
    if (PduR_StubTrace != NULL_PTR)
    {
        PduR_StubTrace(Text);
    }
}

static void PduR_StubCopy(uint8* Dest, const uint8* Src, PduLengthType Length)
{
    PduLengthType index;
    for (index = 0u; index < Length; index++)
    {
        Dest[index] = Src[index];
    }
}

/* ======================== Accessors ======================================= */
void PduR_Stub_CanTpReset(void)
{
    PduR_StubCanTp = (PduR_StubCanTpType){ 0 };
    PduR_StubCanTp.SorResult    = BUFREQ_OK;
    PduR_StubCanTp.CopyRxResult = BUFREQ_OK;
    PduR_StubCanTp.CopyTxResult = BUFREQ_OK;
    PduR_StubCanTp.RxAvailable  = PDUR_STUB_CANTP_BUFFER_SIZE;
    PduR_StubCanTp.LastRxResult = E_NOT_OK;
    PduR_StubCanTp.LastTxResult = E_NOT_OK;
}

void PduR_Stub_CanTpSetTrace(PduR_Stub_TraceFnType Trace)                { PduR_StubTrace = Trace; }
void PduR_Stub_CanTpSetStartOfReceptionResult(BufReq_ReturnType Result) { PduR_StubCanTp.SorResult = Result; }
void PduR_Stub_CanTpSetCopyRxDataResult(BufReq_ReturnType Result)       { PduR_StubCanTp.CopyRxResult = Result; }
void PduR_Stub_CanTpSetCopyTxDataResult(BufReq_ReturnType Result)       { PduR_StubCanTp.CopyTxResult = Result; }

void PduR_Stub_CanTpSetRxAvailable(PduLengthType Available)
{
    const PduLengthType space = (PduLengthType)(PDUR_STUB_CANTP_BUFFER_SIZE - PduR_StubCanTp.RxLength);
    PduR_StubCanTp.RxAvailable = (Available > space) ? space : Available;
}

uint32         PduR_Stub_CanTpGetStartOfReceptionCount(void) { return PduR_StubCanTp.SorCount; }
PduLengthType  PduR_Stub_CanTpGetLastTpSduLength(void)       { return PduR_StubCanTp.LastTpSduLength; }
uint32         PduR_Stub_CanTpGetRxIndicationCount(void)     { return PduR_StubCanTp.RxIndicationCount; }
Std_ReturnType PduR_Stub_CanTpGetLastRxResult(void)          { return PduR_StubCanTp.LastRxResult; }
PduIdType      PduR_Stub_CanTpGetLastRxId(void)              { return PduR_StubCanTp.LastRxId; }
uint32         PduR_Stub_CanTpGetTxConfirmationCount(void)   { return PduR_StubCanTp.TxConfirmationCount; }
Std_ReturnType PduR_Stub_CanTpGetLastTxResult(void)          { return PduR_StubCanTp.LastTxResult; }

const uint8* PduR_Stub_CanTpGetRxData(PduLengthType* Length)
{
    *Length = PduR_StubCanTp.RxLength;
    return PduR_StubCanTp.RxData;
}

void PduR_Stub_CanTpSetTxData(const uint8* Data, PduLengthType Length)
{
    const PduLengthType length = (Length > PDUR_STUB_CANTP_BUFFER_SIZE) ? PDUR_STUB_CANTP_BUFFER_SIZE : Length;
    PduR_StubCopy(PduR_StubCanTp.TxData, Data, length);
    PduR_StubCanTp.TxLength   = length;
    PduR_StubCanTp.TxPosition = 0u;
}

/* ======================== Rx callbacks ==================================== */
BufReq_ReturnType PduR_CanTpStartOfReception(PduIdType id, const PduInfoType* info,
                                             PduLengthType TpSduLength, PduLengthType* bufferSizePtr)
{
    char line[PDUR_STUB_TRACE_LENGTH];
    const BufReq_ReturnType result = PduR_StubCanTp.SorResult;

    (void)info;
    PduR_StubCanTp.SorCount++;
    PduR_StubCanTp.LastTpSduLength = TpSduLength;
    if (result == BUFREQ_OK)
    {
        PduR_StubCanTp.RxLength = 0u;
        PduR_Stub_CanTpSetRxAvailable(PduR_StubCanTp.RxAvailable);
        *bufferSizePtr = PduR_StubCanTp.RxAvailable;
    }
    if (result == BUFREQ_OK)
    {
        (void)snprintf(line, sizeof(line), "PduR_CanTpStartOfReception(id=%u, TpSduLength=%u) -> %s, bufferSize=%u",
                       (unsigned)id, (unsigned)TpSduLength, PduR_StubBufReqText(result),
                       (unsigned)PduR_StubCanTp.RxAvailable);
    }
    else
    {
        /* bufferSizePtr remains unchanged [SWS_PduR_00507] */
        (void)snprintf(line, sizeof(line), "PduR_CanTpStartOfReception(id=%u, TpSduLength=%u) -> %s",
                       (unsigned)id, (unsigned)TpSduLength, PduR_StubBufReqText(result));
    }
    PduR_StubTraceLine(line);
    return result;
}

BufReq_ReturnType PduR_CanTpCopyRxData(PduIdType id, const PduInfoType* info, PduLengthType* bufferSizePtr)
{
    char line[PDUR_STUB_TRACE_LENGTH];
    BufReq_ReturnType result = PduR_StubCanTp.CopyRxResult;

    if ((result == BUFREQ_OK) && (info->SduLength > PduR_StubCanTp.RxAvailable))
    {
        result = BUFREQ_E_NOT_OK;   /* CanTp copied more than it was allowed to */
    }
    if ((result == BUFREQ_OK) && (info->SduLength > 0u))
    {
        PduR_StubCopy(&PduR_StubCanTp.RxData[PduR_StubCanTp.RxLength], info->SduDataPtr, info->SduLength);
        PduR_StubCanTp.RxLength    = (PduLengthType)(PduR_StubCanTp.RxLength + info->SduLength);
        PduR_StubCanTp.RxAvailable = (PduLengthType)(PduR_StubCanTp.RxAvailable - info->SduLength);
    }
    *bufferSizePtr = PduR_StubCanTp.RxAvailable;
    (void)snprintf(line, sizeof(line), "PduR_CanTpCopyRxData(id=%u, SduLength=%u) -> %s, bufferSize=%u",
                   (unsigned)id, (unsigned)info->SduLength, PduR_StubBufReqText(result),
                   (unsigned)PduR_StubCanTp.RxAvailable);
    PduR_StubTraceLine(line);
    return result;
}

void PduR_CanTpRxIndication(PduIdType id, Std_ReturnType result)
{
    char line[PDUR_STUB_TRACE_LENGTH];

    PduR_StubCanTp.RxIndicationCount++;
    PduR_StubCanTp.LastRxResult = result;
    PduR_StubCanTp.LastRxId     = id;
    (void)snprintf(line, sizeof(line), "PduR_CanTpRxIndication(id=%u, result=%s), %u byte(s) received",
                   (unsigned)id, (result == E_OK) ? "E_OK" : "E_NOT_OK", (unsigned)PduR_StubCanTp.RxLength);
    PduR_StubTraceLine(line);
}

/* ======================== Tx callbacks ==================================== */
BufReq_ReturnType PduR_CanTpCopyTxData(PduIdType id, const PduInfoType* info,
                                       const RetryInfoType* retry, PduLengthType* availableDataPtr)
{
    char line[PDUR_STUB_TRACE_LENGTH];
    const PduLengthType remaining = (PduLengthType)(PduR_StubCanTp.TxLength - PduR_StubCanTp.TxPosition);
    BufReq_ReturnType result = PduR_StubCanTp.CopyTxResult;

    (void)retry;
    if ((result == BUFREQ_OK) && (info->SduLength > remaining))
    {
        result = BUFREQ_E_BUSY;     /* not enough data [SWS_PduR_00518] */
    }
    if (result == BUFREQ_OK)
    {
        PduR_StubCopy(info->SduDataPtr, &PduR_StubCanTp.TxData[PduR_StubCanTp.TxPosition], info->SduLength);
        PduR_StubCanTp.TxPosition = (PduLengthType)(PduR_StubCanTp.TxPosition + info->SduLength);
    }
    *availableDataPtr = (PduLengthType)(PduR_StubCanTp.TxLength - PduR_StubCanTp.TxPosition);
    (void)snprintf(line, sizeof(line), "PduR_CanTpCopyTxData(id=%u, SduLength=%u) -> %s, availableData=%u",
                   (unsigned)id, (unsigned)info->SduLength, PduR_StubBufReqText(result),
                   (unsigned)*availableDataPtr);
    PduR_StubTraceLine(line);
    return result;
}

void PduR_CanTpTxConfirmation(PduIdType id, Std_ReturnType result)
{
    char line[PDUR_STUB_TRACE_LENGTH];

    PduR_StubCanTp.TxConfirmationCount++;
    PduR_StubCanTp.LastTxResult = result;
    (void)snprintf(line, sizeof(line), "PduR_CanTpTxConfirmation(id=%u, result=%s)",
                   (unsigned)id, (result == E_OK) ? "E_OK" : "E_NOT_OK");
    PduR_StubTraceLine(line);
}
