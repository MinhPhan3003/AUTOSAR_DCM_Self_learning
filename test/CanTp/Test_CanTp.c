/**
 * @file    Test_CanTp.c
 * @brief   Host test: the test plays the tester and the LSduR. It builds
 *          SF / FF / CF / FC frames with TesterTp, feeds them through
 *          CanTp_RxIndication, reads the frames CanTp gives to
 *          LSduR_CanTpTransmit and confirms them with CanTp_TxConfirmation.
 *          The PduR stub plays the upper layer (normally PduR -> Dcm).
 */
#include "CanTp.h"
#include "SchM_CanTp.h"
#include "PduR_Stub.h"
#include "LSduR_Stub.h"
#include "Det_Stub.h"
#include "TesterTp.h"
#include "Test_Assert.h"

/* ======================== Test constants ================================== */
#define TEST_PHYS_NPDU     CanTpConf_CanTpRxNPdu_Req7E0      /* SF/FF/CF + FC from tester */
#define TEST_FUNC_NPDU     CanTpConf_CanTpRxNPdu_FuncReq7DF
#define TEST_TX_CONF_ID    CanTpConf_CanTpTxNPdu_Resp7E8     /* = FC confirmation id      */
#define TEST_PHYS_RX       CanTpConf_CanTpRxNSdu_DiagPhysRx
#define TEST_TX            CanTpConf_CanTpTxNSdu_DiagTx

/* Example configuration in gen/CanTp/CanTp_Lcfg.c (1 tick = 1 ms) */
#define TEST_CFG_BS        2u
#define TEST_CFG_STMIN     0x05u
#define TEST_CFG_WFTMAX    3u
#define TEST_NAR_TICKS     70u
#define TEST_NAS_TICKS     70u
#define TEST_NBR_TICKS     20u
#define TEST_NBS_TICKS     150u
#define TEST_NCS_TICKS     20u
#define TEST_NCR_TICKS     150u

#define TEST_MSG_LENGTH    34u     /* FF(6) + 4 x CF(7) */

static uint8 Test_Message[TEST_MSG_LENGTH];

/* ======================== Helpers ========================================= */

static void Test_Reset(void)
{
    uint8 index;

    Det_Stub_Reset();
    LSduR_Stub_Reset();
    PduR_Stub_CanTpReset();
    CanTp_Init(NULL_PTR);
    for (index = 0u; index < TEST_MSG_LENGTH; index++)
    {
        Test_Message[index] = (uint8)(0xA0u + index);
    }
}

static void Test_Tick(uint32 Ticks)
{
    uint32 tick;
    for (tick = 0u; tick < Ticks; tick++)
    {
        CanTp_MainFunction();
    }
}

/* Tester puts a frame on the bus; LSduR forwards it to CanTp. */
static void Test_Send(PduIdType RxNPduId, const TesterTp_FrameType* Frame, boolean Padding)
{
    uint8 raw[TESTERTP_CAN_DL];
    PduInfoType info;

    info.SduLength   = TesterTp_Encode(Frame, Padding, raw);
    info.SduDataPtr  = raw;
    info.MetaDataPtr = NULL_PTR;
    CanTp_RxIndication(RxNPduId, &info);
}

static void Test_SendCf(uint8 Sn, uint32 Offset)
{
    const uint32 remaining = TEST_MSG_LENGTH - Offset;
    const TesterTp_FrameType cf = TesterTp_MakeCf(Sn, &Test_Message[Offset], (uint8)((remaining > 7u) ? 7u : remaining));
    Test_Send(TEST_PHYS_NPDU, &cf, TRUE);
}

/* Reads the next frame CanTp transmitted. Returns FALSE if there is none. */
static boolean Test_ReadEcuFrame(TesterTp_FrameType* Frame, PduLengthType* Length)
{
    LSduR_Stub_FrameType raw;
    boolean found = LSduR_Stub_PopFrame(&raw);

    if (found == TRUE)
    {
        *Length = raw.Length;
        found = TesterTp_Decode(raw.Data, (uint8)raw.Length, Frame);
    }
    return found;
}

/* LSduR confirms the last frame (FC or SF/FF/CF, both on 0x7E8). */
static void Test_Confirm(void)
{
    CanTp_TxConfirmation(TEST_TX_CONF_ID, E_OK);
}

static boolean Test_ExpectFc(uint8 Fs, uint8 Bs, uint8 STmin)
{
    TesterTp_FrameType frame;
    PduLengthType length = 0u;
    return ((Test_ReadEcuFrame(&frame, &length) == TRUE) && (length == TESTERTP_CAN_DL) &&
            (frame.Kind == TESTERTP_FC) && (frame.Fs == Fs) && (frame.Bs == Bs) && (frame.STmin == STmin)) ? TRUE : FALSE;
}

static boolean Test_RxDataEquals(const uint8* Expected, PduLengthType Length)
{
    PduLengthType received = 0u;
    const uint8* data = PduR_Stub_CanTpGetRxData(&received);
    boolean equal = (received == Length) ? TRUE : FALSE;
    PduLengthType index;

    for (index = 0u; (equal == TRUE) && (index < Length); index++)
    {
        equal = (data[index] == Expected[index]) ? TRUE : FALSE;
    }
    return equal;
}

/* FF of TEST_MSG_LENGTH bytes, then wait for FC(CTS) and confirm it. */
static void Test_StartSegmentedRx(void)
{
    const TesterTp_FrameType ff = TesterTp_MakeFf(TEST_MSG_LENGTH, Test_Message, 6u);
    Test_Send(TEST_PHYS_NPDU, &ff, TRUE);
    Test_Tick(1u);
    Test_Check(Test_ExpectFc(TESTERTP_FS_CTS, TEST_CFG_BS, TEST_CFG_STMIN), "FF -> FC(CTS, BS=2, STmin=5)");
    Test_Confirm();
}

/* ======================== Reception tests ================================= */

static void Test_RxSingleFrame(void)
{
    const uint8 request[] = { 0x22u, 0xF1u, 0x90u };
    const TesterTp_FrameType sf = TesterTp_MakeSf(request, 3u);

    Test_Begin("Rx: SF -> StartOfReception, CopyRxData, RxIndication(E_OK)");
    Test_Reset();
    Test_Send(TEST_PHYS_NPDU, &sf, TRUE);
    Test_Check(PduR_Stub_CanTpGetLastTpSduLength() == 3u, "TpSduLength = SF_DL");
    Test_Check(PduR_Stub_CanTpGetRxIndicationCount() == 1u, "one RxIndication");
    Test_Check(PduR_Stub_CanTpGetLastRxResult() == E_OK, "RxIndication E_OK");
    Test_Check(Test_RxDataEquals(request, 3u), "payload copied");
    Test_Check(LSduR_Stub_GetTransmitCount() == 0u, "no FC for SF");
}

static void Test_RxSingleFrameErrors(void)
{
    const uint8 request[] = { 0x22u, 0xF1u, 0x90u };
    const TesterTp_FrameType sf = TesterTp_MakeSf(request, 3u);

    Test_Begin("Rx: SF errors (padding, no buffer, small buffer)");
    Test_Reset();
    Test_Send(TEST_PHYS_NPDU, &sf, FALSE);                       /* 4 bytes, padding expected */
    Test_Check(Det_Stub_GetLastRuntimeErrorId() == CANTP_E_PADDING, "SF < 8 bytes -> CANTP_E_PADDING");
    Test_Check(PduR_Stub_CanTpGetStartOfReceptionCount() == 0u, "SF rejected");

    PduR_Stub_CanTpSetStartOfReceptionResult(BUFREQ_E_NOT_OK);
    Test_Send(TEST_PHYS_NPDU, &sf, TRUE);
    Test_Check(PduR_Stub_CanTpGetRxIndicationCount() == 0u, "BUFREQ_E_NOT_OK -> no RxIndication");

    PduR_Stub_CanTpSetStartOfReceptionResult(BUFREQ_OK);
    PduR_Stub_CanTpSetRxAvailable(2u);
    Test_Send(TEST_PHYS_NPDU, &sf, TRUE);
    Test_Check(PduR_Stub_CanTpGetLastRxResult() == E_NOT_OK, "buffer < SF_DL -> RxIndication(E_NOT_OK)");
}

static void Test_RxMultiFrame(void)
{
    Test_Begin("Rx: FF + FC(CTS) + 4 CF with BS = 2");
    Test_Reset();
    Test_StartSegmentedRx();
    Test_SendCf(1u, 6u);
    Test_SendCf(2u, 13u);                                        /* end of block 1 */
    Test_Tick(1u);
    Test_Check(Test_ExpectFc(TESTERTP_FS_CTS, TEST_CFG_BS, TEST_CFG_STMIN), "FC after BS = 2 CFs");
    Test_Confirm();
    Test_SendCf(3u, 20u);
    Test_Check(PduR_Stub_CanTpGetRxIndicationCount() == 0u, "not finished after CF 3");
    Test_SendCf(4u, 27u);                                        /* last CF */
    Test_Check(PduR_Stub_CanTpGetLastRxResult() == E_OK, "RxIndication(E_OK)");
    Test_Check(Test_RxDataEquals(Test_Message, TEST_MSG_LENGTH), "34 bytes reassembled");
    Test_Tick(1u);
    Test_Check(LSduR_Stub_GetPendingFrameCount() == 0u, "no FC after the last CF");
}

static void Test_RxSequenceErrors(void)
{
    Test_Begin("Rx: wrong SN and N_Cr timeout");
    Test_Reset();
    Test_StartSegmentedRx();
    Test_SendCf(2u, 6u);                                         /* expected SN = 1 */
    Test_Check(PduR_Stub_CanTpGetLastRxResult() == E_NOT_OK, "wrong SN -> RxIndication(E_NOT_OK)");
    Test_Check(Det_Stub_GetLastRuntimeErrorId() == CANTP_E_COM, "wrong SN -> CANTP_E_COM");

    Test_Reset();
    Test_StartSegmentedRx();
    Test_Tick(TEST_NCR_TICKS - 1u);
    Test_Check(PduR_Stub_CanTpGetRxIndicationCount() == 0u, "no timeout before N_Cr");
    Test_Tick(1u);
    Test_Check(PduR_Stub_CanTpGetLastRxResult() == E_NOT_OK, "N_Cr -> RxIndication(E_NOT_OK)");
    Test_Check(Det_Stub_GetLastRuntimeErrorId() == CANTP_E_RX_COM, "N_Cr -> CANTP_E_RX_COM");
}

static void Test_RxOverflow(void)
{
    const TesterTp_FrameType ff = TesterTp_MakeFf(TEST_MSG_LENGTH, Test_Message, 6u);

    Test_Begin("Rx: StartOfReception BUFREQ_E_OVFL -> FC(OVFLW)");
    Test_Reset();
    PduR_Stub_CanTpSetStartOfReceptionResult(BUFREQ_E_OVFL);
    Test_Send(TEST_PHYS_NPDU, &ff, TRUE);
    Test_Tick(1u);
    Test_Check(Test_ExpectFc(TESTERTP_FS_OVFLW, TEST_CFG_BS, TEST_CFG_STMIN), "FC(OVFLW) sent");
    Test_Confirm();
    Test_Check(PduR_Stub_CanTpGetRxIndicationCount() == 0u, "no RxIndication");
}

static void Test_RxWaitThenCts(void)
{
    const TesterTp_FrameType ff = TesterTp_MakeFf(TEST_MSG_LENGTH, Test_Message, 6u);

    Test_Begin("Rx: buffer too small -> FC(WAIT) after N_Br, then FC(CTS)");
    Test_Reset();
    PduR_Stub_CanTpSetRxAvailable(10u);                          /* 6 for FF, 4 left < 14 */
    Test_Send(TEST_PHYS_NPDU, &ff, TRUE);
    Test_Tick(TEST_NBR_TICKS - 1u);
    Test_Check(LSduR_Stub_GetPendingFrameCount() == 0u, "nothing sent before N_Br");
    Test_Tick(1u);
    Test_Tick(1u);
    Test_Check(Test_ExpectFc(TESTERTP_FS_WAIT, TEST_CFG_BS, TEST_CFG_STMIN), "FC(WAIT) after N_Br");
    Test_Confirm();
    PduR_Stub_CanTpSetRxAvailable(100u);
    Test_Tick(2u);
    Test_Check(Test_ExpectFc(TESTERTP_FS_CTS, TEST_CFG_BS, TEST_CFG_STMIN), "FC(CTS) when buffer is free");
}

static void Test_RxWftMax(void)
{
    const TesterTp_FrameType ff = TesterTp_MakeFf(TEST_MSG_LENGTH, Test_Message, 6u);
    uint32 waits = 0u;
    uint32 round;

    Test_Begin("Rx: at most WFTmax FC(WAIT), then abort");
    Test_Reset();
    PduR_Stub_CanTpSetRxAvailable(10u);
    Test_Send(TEST_PHYS_NPDU, &ff, TRUE);
    for (round = 0u; round < (TEST_CFG_WFTMAX + 1u); round++)
    {
        Test_Tick(TEST_NBR_TICKS + 1u);
        if (Test_ExpectFc(TESTERTP_FS_WAIT, TEST_CFG_BS, TEST_CFG_STMIN) == TRUE)
        {
            waits++;
            Test_Confirm();
        }
    }
    Test_Check(waits == TEST_CFG_WFTMAX, "exactly WFTmax FC(WAIT)");
    Test_Check(PduR_Stub_CanTpGetLastRxResult() == E_NOT_OK, "then RxIndication(E_NOT_OK)");
}

static void Test_RxFunctionalAndUnexpected(void)
{
    const uint8 request[] = { 0x3Eu, 0x00u };
    const TesterTp_FrameType sf = TesterTp_MakeSf(request, 2u);
    const TesterTp_FrameType ff = TesterTp_MakeFf(TEST_MSG_LENGTH, Test_Message, 6u);

    Test_Begin("Rx: functional FF rejected, new SF aborts a reception");
    Test_Reset();
    Test_Send(TEST_FUNC_NPDU, &ff, TRUE);
    Test_Check(Det_Stub_GetLastRuntimeErrorId() == CANTP_E_INVALID_TATYPE, "functional FF -> CANTP_E_INVALID_TATYPE");
    Test_Send(TEST_FUNC_NPDU, &sf, TRUE);
    Test_Check(PduR_Stub_CanTpGetLastRxId() == CANTP_CFG_PDUR_DIAG_FUNC_RX, "functional SF accepted");

    Test_Reset();
    Test_StartSegmentedRx();
    Test_Send(TEST_PHYS_NPDU, &sf, TRUE);
    Test_Check(PduR_Stub_CanTpGetRxIndicationCount() == 2u, "old reception ended + new SF indicated");
    Test_Check(PduR_Stub_CanTpGetLastRxResult() == E_OK, "new SF received");
    Test_SendCf(1u, 6u);
    Test_Check(PduR_Stub_CanTpGetRxIndicationCount() == 2u, "CF after abort is ignored");
}

/* ======================== Transmission tests ============================== */

/* CanTp_Transmit + first main function. Returns the first frame. */
static TesterTp_FrameType Test_StartTx(PduLengthType Length, PduLengthType* FrameLength)
{
    TesterTp_FrameType frame = TesterTp_MakeSf(NULL_PTR, 0u);
    const PduInfoType info = { NULL_PTR, NULL_PTR, Length };

    PduR_Stub_CanTpSetTxData(Test_Message, Length);
    Test_Check(CanTp_Transmit(TEST_TX, &info) == E_OK, "CanTp_Transmit accepted");
    Test_Tick(1u);
    Test_Check(Test_ReadEcuFrame(&frame, FrameLength), "first frame on the bus");
    return frame;
}

static void Test_TxSingleFrame(void)
{
    PduLengthType length = 0u;
    TesterTp_FrameType frame;

    Test_Begin("Tx: SF, padded to 8 bytes, TxConfirmation(E_OK)");
    Test_Reset();
    frame = Test_StartTx(3u, &length);
    Test_Check((frame.Kind == TESTERTP_SF) && (frame.Dl == 3u), "SF with SF_DL = 3");
    Test_Check(length == TESTERTP_CAN_DL, "padding to 8 bytes");
    Test_Check(frame.Data[3] == TESTERTP_PADDING_BYTE, "padding byte 0xCC");
    Test_Confirm();
    Test_Check(PduR_Stub_CanTpGetLastTxResult() == E_OK, "PduR_CanTpTxConfirmation(E_OK)");
}

static boolean Test_ExpectCf(uint8 Sn)
{
    TesterTp_FrameType frame;
    PduLengthType length = 0u;
    return ((Test_ReadEcuFrame(&frame, &length) == TRUE) && (frame.Kind == TESTERTP_CF) && (frame.Sn == Sn)) ? TRUE : FALSE;
}

static void Test_TxMultiFrame(void)
{
    PduLengthType length = 0u;
    TesterTp_FrameType frame;
    const TesterTp_FrameType fcBlock = TesterTp_MakeFc(TESTERTP_FS_CTS, 2u, 3u);
    const TesterTp_FrameType fcAll   = TesterTp_MakeFc(TESTERTP_FS_CTS, 0u, 0u);

    Test_Begin("Tx: FF + FC(BS=2, STmin=3) + CF + CF + FC(BS=0) + CF + CF");
    Test_Reset();
    frame = Test_StartTx(TEST_MSG_LENGTH, &length);
    Test_Check((frame.Kind == TESTERTP_FF) && (frame.Dl == TEST_MSG_LENGTH), "FF with FF_DL = 34");
    Test_Confirm();
    Test_Tick(5u);
    Test_Check(LSduR_Stub_GetPendingFrameCount() == 0u, "waits for FC after FF");
    Test_Send(TEST_PHYS_NPDU, &fcBlock, TRUE);
    Test_Tick(1u);
    Test_Check(Test_ExpectCf(1u), "CF SN=1 right after FC");
    Test_Confirm();
    Test_Tick(2u);
    Test_Check(LSduR_Stub_GetPendingFrameCount() == 0u, "STmin = 3 ms not elapsed");
    Test_Tick(1u);
    Test_Check(Test_ExpectCf(2u), "CF SN=2 after STmin");
    Test_Confirm();
    Test_Tick(10u);
    Test_Check(LSduR_Stub_GetPendingFrameCount() == 0u, "waits for FC after BS = 2 CFs");
    Test_Send(TEST_PHYS_NPDU, &fcAll, TRUE);
    Test_Tick(1u);
    Test_Check(Test_ExpectCf(3u), "CF SN=3");
    Test_Confirm();
    Test_Tick(1u);
    Test_Check(Test_ExpectCf(4u), "CF SN=4 (BS=0, STmin=0)");
    Test_Confirm();
    Test_Check(PduR_Stub_CanTpGetLastTxResult() == E_OK, "PduR_CanTpTxConfirmation(E_OK)");
}

static void Test_TxFlowControlErrors(void)
{
    PduLengthType length = 0u;
    const TesterTp_FrameType fcWait  = TesterTp_MakeFc(TESTERTP_FS_WAIT, 0u, 0u);
    const TesterTp_FrameType fcOvflw = TesterTp_MakeFc(TESTERTP_FS_OVFLW, 0u, 0u);
    const TesterTp_FrameType fcBadFs = TesterTp_MakeFc(5u, 0u, 0u);

    Test_Begin("Tx: FC(WAIT) restarts N_Bs, N_Bs timeout, FC(OVFLW), invalid FS, FC padding");
    Test_Reset();
    (void)Test_StartTx(TEST_MSG_LENGTH, &length);
    Test_Confirm();
    Test_Tick(TEST_NBS_TICKS - 1u);
    Test_Send(TEST_PHYS_NPDU, &fcWait, TRUE);
    Test_Tick(TEST_NBS_TICKS - 1u);
    Test_Check(PduR_Stub_CanTpGetTxConfirmationCount() == 0u, "FC(WAIT) restarted N_Bs");
    Test_Tick(1u);
    Test_Check(PduR_Stub_CanTpGetLastTxResult() == E_NOT_OK, "N_Bs -> TxConfirmation(E_NOT_OK)");
    Test_Check(Det_Stub_GetLastRuntimeErrorId() == CANTP_E_TX_COM, "N_Bs -> CANTP_E_TX_COM");

    Test_Reset();
    (void)Test_StartTx(TEST_MSG_LENGTH, &length);
    Test_Confirm();
    Test_Send(TEST_PHYS_NPDU, &fcOvflw, TRUE);
    Test_Check(PduR_Stub_CanTpGetLastTxResult() == E_NOT_OK, "FC(OVFLW) -> E_NOT_OK");

    Test_Reset();
    (void)Test_StartTx(TEST_MSG_LENGTH, &length);
    Test_Confirm();
    Test_Send(TEST_PHYS_NPDU, &fcBadFs, TRUE);
    Test_Check(PduR_Stub_CanTpGetLastTxResult() == E_NOT_OK, "invalid FS -> E_NOT_OK");

    Test_Reset();
    (void)Test_StartTx(TEST_MSG_LENGTH, &length);
    Test_Confirm();
    Test_Send(TEST_PHYS_NPDU, &fcWait, FALSE);                   /* 3 bytes */
    Test_Check(Det_Stub_GetLastRuntimeErrorId() == CANTP_E_PADDING, "FC < 8 bytes -> CANTP_E_PADDING");
}

static void Test_TxTimeouts(void)
{
    PduLengthType length = 0u;
    const PduInfoType info = { NULL_PTR, NULL_PTR, 3u };

    Test_Begin("Tx: N_As timeout, N_Cs timeout (CopyTxData busy)");
    Test_Reset();
    (void)Test_StartTx(3u, &length);
    Test_Tick(TEST_NAS_TICKS);
    Test_Check(PduR_Stub_CanTpGetLastTxResult() == E_NOT_OK, "N_As -> TxConfirmation(E_NOT_OK)");

    Test_Reset();
    PduR_Stub_CanTpSetCopyTxDataResult(BUFREQ_E_BUSY);
    (void)CanTp_Transmit(TEST_TX, &info);
    Test_Tick(TEST_NCS_TICKS - 1u);
    Test_Check(PduR_Stub_CanTpGetTxConfirmationCount() == 0u, "retries while BUFREQ_E_BUSY");
    Test_Tick(1u);
    Test_Check(PduR_Stub_CanTpGetLastTxResult() == E_NOT_OK, "N_Cs -> TxConfirmation(E_NOT_OK)");
    Test_Check(LSduR_Stub_GetTransmitCount() == 0u, "nothing sent");
}

static void Test_TxBusyAndCancel(void)
{
    const PduInfoType info = { NULL_PTR, NULL_PTR, TEST_MSG_LENGTH };

    Test_Begin("Tx: N-SDU busy, CanTp_CancelTransmit");
    Test_Reset();
    PduR_Stub_CanTpSetTxData(Test_Message, TEST_MSG_LENGTH);
    Test_Check(CanTp_Transmit(TEST_TX, &info) == E_OK, "first request accepted");
    Test_Check(CanTp_Transmit(TEST_TX, &info) == E_NOT_OK, "second request rejected");
    Test_Check(CanTp_CancelTransmit(TEST_TX) == E_OK, "cancel accepted");
    Test_Check(PduR_Stub_CanTpGetLastTxResult() == E_NOT_OK, "TxConfirmation(E_NOT_OK) after cancel");
    Test_Check(CanTp_CancelTransmit(TEST_TX) == E_NOT_OK, "nothing to cancel");
    Test_Check(Det_Stub_GetLastRuntimeErrorId() == CANTP_E_OPER_NOT_SUPPORTED, "CANTP_E_OPER_NOT_SUPPORTED");
}

/* The response N-PDU 0x7E8 carries both the ECU's SF/FF/CF and its FCs:
 * only one may wait for TxConfirmation at a time [SWS_CanTp_00248]. */
static void Test_SharedNPdu(void)
{
    PduLengthType length = 0u;
    const TesterTp_FrameType ff = TesterTp_MakeFf(TEST_MSG_LENGTH, Test_Message, 6u);

    Test_Begin("Full duplex: FC waits until the pending FF on 0x7E8 is confirmed");
    Test_Reset();
    (void)Test_StartTx(TEST_MSG_LENGTH, &length);                /* FF pending */
    Test_Send(TEST_PHYS_NPDU, &ff, TRUE);                        /* tester request */
    Test_Tick(3u);
    Test_Check(LSduR_Stub_GetPendingFrameCount() == 0u, "FC held back");
    Test_Confirm();                                              /* confirms the FF */
    Test_Tick(1u);
    Test_Check(Test_ExpectFc(TESTERTP_FS_CTS, TEST_CFG_BS, TEST_CFG_STMIN), "FC sent after confirmation");
}

/* ======================== API tests ======================================= */

static void Test_Parameters(void)
{
    uint16 value = 0u;

    Test_Begin("CanTp_ReadParameter / CanTp_ChangeParameter");
    Test_Reset();
    Test_Check((CanTp_ReadParameter(TEST_PHYS_RX, TP_BS, &value) == E_OK) && (value == TEST_CFG_BS), "read BS");
    Test_Check((CanTp_ReadParameter(TEST_PHYS_RX, TP_STMIN, &value) == E_OK) && (value == TEST_CFG_STMIN), "read STmin");
    Test_Check(CanTp_ChangeParameter(TEST_PHYS_RX, TP_BS, 4u) == E_OK, "change BS to 4");
    Test_Check(CanTp_ChangeParameter(TEST_PHYS_RX, TP_STMIN, 0x80u) == E_NOT_OK, "STmin 0x80 rejected");
    Test_Check(Det_Stub_GetLastErrorId() == CANTP_E_PARAM_CONFIG, "CANTP_E_PARAM_CONFIG");
    Test_Check(CanTp_ChangeParameter(TEST_PHYS_RX, TP_BC, 1u) == E_NOT_OK, "TP_BC rejected");
    Test_Check(Det_Stub_GetLastErrorId() == CANTP_E_PARAM_ID, "CANTP_E_PARAM_ID");

    {
        const TesterTp_FrameType ff = TesterTp_MakeFf(TEST_MSG_LENGTH, Test_Message, 6u);
        Test_Send(TEST_PHYS_NPDU, &ff, TRUE);
        Test_Tick(1u);
        Test_Check(Test_ExpectFc(TESTERTP_FS_CTS, 4u, TEST_CFG_STMIN), "FC uses the new BS");
        Test_Check(CanTp_ChangeParameter(TEST_PHYS_RX, TP_BS, 8u) == E_NOT_OK, "no change during reception");
    }
}

static void Test_CancelReceive(void)
{
    Test_Begin("CanTp_CancelReceive");
    Test_Reset();
    Test_Check(CanTp_CancelReceive(TEST_PHYS_RX) == E_NOT_OK, "nothing to cancel");
    Test_Check(Det_Stub_GetLastRuntimeErrorId() == CANTP_E_OPER_NOT_SUPPORTED, "CANTP_E_OPER_NOT_SUPPORTED");
    Test_StartSegmentedRx();
    Test_Check(CanTp_CancelReceive(TEST_PHYS_RX) == E_OK, "cancel accepted");
    Test_Check(PduR_Stub_CanTpGetLastRxResult() == E_NOT_OK, "RxIndication(E_NOT_OK)");

    Test_Reset();
    Test_StartSegmentedRx();
    Test_SendCf(1u, 6u);
    Test_SendCf(2u, 13u);
    Test_Tick(1u);
    (void)Test_ExpectFc(TESTERTP_FS_CTS, TEST_CFG_BS, TEST_CFG_STMIN);
    Test_Confirm();
    Test_SendCf(3u, 20u);                                        /* only the last CF is missing */
    Test_Check(CanTp_CancelReceive(TEST_PHYS_RX) == E_NOT_OK, "cancel rejected before the last CF");
}

static void Test_DevErrors(void)
{
    const PduInfoType info = { NULL_PTR, NULL_PTR, 3u };

    Test_Begin("Development errors");
    Test_Reset();
    Test_Check(CanTp_Transmit(5u, &info) == E_NOT_OK, "invalid TxPduId");
    Test_Check(Det_Stub_GetLastErrorId() == CANTP_E_INVALID_TX_ID, "CANTP_E_INVALID_TX_ID");
    Test_Check(CanTp_Transmit(TEST_TX, NULL_PTR) == E_NOT_OK, "NULL PduInfoPtr");
    Test_Check(Det_Stub_GetLastErrorId() == CANTP_E_PARAM_POINTER, "CANTP_E_PARAM_POINTER");
    CanTp_RxIndication(9u, &info);
    Test_Check(Det_Stub_GetLastErrorId() == CANTP_E_INVALID_RX_ID, "CANTP_E_INVALID_RX_ID");
    CanTp_TxConfirmation(9u, E_OK);
    Test_Check(Det_Stub_GetLastErrorId() == CANTP_E_INVALID_TX_ID, "TxConfirmation: CANTP_E_INVALID_TX_ID");
    CanTp_Shutdown();
    Test_Check(CanTp_Transmit(TEST_TX, &info) == E_NOT_OK, "rejected after shutdown");
    Test_Check(Det_Stub_GetLastErrorId() == CANTP_E_UNINIT, "CANTP_E_UNINIT");
}

int main(void)
{
    Test_RxSingleFrame();
    Test_RxSingleFrameErrors();
    Test_RxMultiFrame();
    Test_RxSequenceErrors();
    Test_RxOverflow();
    Test_RxWaitThenCts();
    Test_RxWftMax();
    Test_RxFunctionalAndUnexpected();
    Test_TxSingleFrame();
    Test_TxMultiFrame();
    Test_TxFlowControlErrors();
    Test_TxTimeouts();
    Test_TxBusyAndCancel();
    Test_SharedNPdu();
    Test_Parameters();
    Test_CancelReceive();
    Test_DevErrors();

    Test_PrintSummary();
    return (Test_GetFailureCount() == 0u) ? 0 : 1;
}
