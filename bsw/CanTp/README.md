# CanTp: ví dụ CAN Transport Layer (AUTOSAR CP R25-11)

Nguồn dùng để viết code (đã lưu trong `docs/specs/`, tải từ autosar.org):

- AUTOSAR CP R25-11 **SWS_CANTransportLayer** (Document ID 14): nguồn chính
- AUTOSAR CP R25-11 SWS_PDURouter 8.3.4, SWS_LSduRouter 8.3.2.1, SWS_DefaultErrorTracer 8.1.3, SWS_CommunicationStackTypes 8.1.5 cho prototype của module lân cận
- **ISO 15765-2**: **không có** trong project. Các giá trị số mà SWS chỉ tham chiếu sang ISO (FS, mã hóa STmin, SN bắt đầu, FF_DL tối thiểu) được ghi rõ là cần xác minh (xem cuối file).

Nhãn: **[Spec]** có trong tài liệu (kèm mục/ID) · **[Practice]** cách làm phổ biến · **[Inference]** suy ra, tài liệu không viết nguyên văn.

---

## 1. Tại sao cần CanTp

Một CAN frame classic chỉ chở được 8 byte. Request/response UDS thì thường dài hơn, ví dụ `22 F1 90` → `62 F1 90` + 17 byte VIN. CanTp **cắt** N-SDU dài thành nhiều frame khi gửi và **ghép** lại khi nhận. Nó cũng có **flow control** để bên nhận điều chỉnh tốc độ bên gửi [Spec: SWS_CanTp 1, 7].

CanTp **không có buffer chứa dữ liệu**. Nó copy thẳng từ/vào buffer của upper layer qua `PduR_CanTpCopyTxData` / `PduR_CanTpCopyRxData` [Spec: 7.3.3, SWS_CanTp_00298].

## 2. Vị trí trong stack

```
Tester ──CAN──> CanIf ──> LSduR ──CanTp_RxIndication──> CanTp ──PduR_CanTp*──> PduR ──Dcm_*──> Dcm
                  <──────  LSduR <──LSduR_CanTpTransmit── CanTp <──CanTp_Transmit── PduR <──PduR_DcmTransmit── Dcm
```

- [Spec] Từ **R20-11**, lower layer của CanTp là **LSduR** (L-SDU Router), không còn là CanIf (SWS_CanTp, change history, 1, 5.1.7). Sơ đồ trong `README.md` gốc của project (`CanIf → CanTp`) là cách vẽ của các release cũ.
- [Spec] `LSduR_CanTpTransmit` ở R25-11 có trạng thái **DRAFT** (SWS_LSduRouter [CP_SWS_LSduR_91009]).

## 3. Bốn loại frame (N_PCI, normal addressing, classic CAN)

[Spec] SWS_CanTp R25-11 Figure 7.2 "Summary of N_PCI bytes":

| Frame | Byte #1 bit 7–4 | Byte #1 bit 3–0 | Byte #2 | Byte #3 | Dữ liệu | Simulator: trường chỉnh được |
|---|---|---|---|---|---|---|
| **SF** Single Frame | `0000` | SF_DL | data | data | 1..7 byte | `sf <bytes> dl=N pad=off` |
| **FF** First Frame (FF_DL ≤ 4095) | `0001` | FF_DL[11:8] | FF_DL[7:0] | data | 6 byte | `ff <bytes> dl=N` |
| **FF** (FF_DL > 4095) | `0001` | `0000` | `0x00` | FF_DL 32 bit (byte #3..#6) | 2 byte | `ff len=5000` |
| **CF** Consecutive Frame | `0010` | SN | data | data | 7 byte (CF cuối: phần còn lại) | `cf sn=N n=N <bytes> pad=off` |
| **FC** Flow Control | `0011` | FS | BS | STmin | không có | `fc fs=cts\|wait\|ovflw\|N bs=N st=N` |

Giá trị các trường (ISO 15765-2, **cần xác minh**): FS `0`=CTS, `1`=WAIT, `2`=OVFLW. STmin `0x00..0x7F` = 0..127 ms, `0xF1..0xF9` = 100..900 µs, giá trị khác là reserved. SN của CF đầu tiên = 1, sau `0xF` quay về `0x0`.

## 4. Năm điểm cho từng chức năng

| | Nhận SF | Nhận multi-frame (FF/CF vào, FC ra) | Gửi SF | Gửi multi-frame (FF/CF ra, FC vào) |
|---|---|---|---|---|
| **1. Trigger** | LSduR nhận frame có PCI `0x0_` | LSduR nhận FF `0x1_`, rồi các CF `0x2_` | Upper layer gọi `CanTp_Transmit`, SduLength ≤ 7 | `CanTp_Transmit`, SduLength > 7 |
| **2. Module nhận → gọi tiếp** | LSduR → CanTp → PduR | LSduR → CanTp → PduR; CanTp → LSduR (FC) | PduR → CanTp → PduR (copy) → LSduR | như cột trái; FC: LSduR → CanTp |
| **3. Interface** | Callback `CanTp_RxIndication`; `PduR_CanTpStartOfReception`, `PduR_CanTpCopyRxData`, `PduR_CanTpRxIndication` | như SF + `LSduR_CanTpTransmit` (FC) và callback `CanTp_TxConfirmation` (FC) | `CanTp_Transmit`, `PduR_CanTpCopyTxData`, `LSduR_CanTpTransmit`, `CanTp_TxConfirmation`, `PduR_CanTpTxConfirmation` | như SF + `CanTp_RxIndication` (FC) |
| **4. Dữ liệu / kết quả** | Không lưu trong CanTp; data vào buffer của upper layer. Kết quả `E_OK` / `E_NOT_OK` qua `PduR_CanTpRxIndication` | RAM: trạng thái kết nối (`CanTp_RxConn`). Lỗi: SN sai, N_Cr, N_Ar, WFTmax → `E_NOT_OK` | RAM: `CanTp_TxConn`. Kết quả qua `PduR_CanTpTxConfirmation` | FC(OVFLW), FS sai, N_Bs, N_As, N_Cs → `E_NOT_OK` |
| **5. Cấu hình** | `CanTpRxNSdu` (`CanTpRxPaddingActivation`, `CanTpRxTaType`), `CanTpRxNPdu/CanTpRxNPduId` | + `CanTpBs`, `CanTpSTmin`, `CanTpRxWftMax`, `CanTpNar`, `CanTpNbr`, `CanTpNcr`, `CanTpTxFcNPdu` | `CanTpTxNSdu` (`CanTpTxPaddingActivation`, `CanTpTxTaType`, `CanTpNas`, `CanTpNcs`), `CanTpTxNPdu` | + `CanTpNbs`, `CanTpRxFcNPdu`, `CanTpTc` |

Ngữ cảnh chạy [Practice, theo SWS 9.2 / 9.5]: SF/FF/CF được xử lý ngay trong `CanTp_RxIndication` (có thể là ngữ cảnh ngắt, SWS_CanTp_00235). FC và các frame Tx được gửi từ `CanTp_MainFunction`.

## 5. Các timer (1 tick = 1 ms, giá trị ví dụ trong `gen/CanTp/CanTp_Lcfg.c`)

| Timer | Phía | Bắt đầu khi | Dừng khi | Hết giờ → | Requirement |
|---|---|---|---|---|---|
| N_As (70) | gửi | đưa SF/FF/CF cho LSduR | `CanTp_TxConfirmation` | `PduR_CanTpTxConfirmation(E_NOT_OK)`, `CANTP_E_TX_COM` | 00075, 00310 |
| N_Bs (150) | gửi | FF/CF cuối block được confirm, hoặc nhận FC(WAIT) | nhận FC(CTS) | `E_NOT_OK`, `CANTP_E_TX_COM` | 00315, 00316 |
| N_Cs (20) | gửi | trước `PduR_CanTpCopyTxData` | data được copy | `E_NOT_OK`, `CANTP_E_TX_COM` | 00167, 00280 |
| N_Ar (70) | nhận | đưa FC cho LSduR | `CanTp_TxConfirmation` của FC | `PduR_CanTpRxIndication(E_NOT_OK)`, `CANTP_E_RX_COM` | 00311 |
| N_Br (20) | nhận | nhận FF / CF cuối block | gửi FC | gửi FC(WAIT), tối đa WFTmax lần, sau đó abort | 00166, 00341, 00223 |
| N_Cr (150) | nhận | FC(CTS) được confirm / nhận CF | nhận CF tiếp | `E_NOT_OK`, `CANTP_E_RX_COM` | 00312, 00313 |

Giá trị phải thỏa (N_Br + N_Ar) < 0.9·N_Bs và (N_Cs + N_As) < 0.9·N_Cr [Spec: ghi chú trong SWS 7.3.1, 7.3.2, trích ISO 15765-2].

## 6. Cấu trúc file

| File | Vai trò |
|---|---|
| `bsw/CanTp/inc/CanTp.h` | API public, mã lỗi DET (SWS 7.4, 8.2–8.4) |
| `bsw/CanTp/src/CanTp.c` | Kiểm tra DET, dispatch `RxIndication` / `TxConfirmation`, `MainFunction` |
| `bsw/CanTp/src/CanTp_Rx.c` | State machine nhận: SF / FF / CF vào, FC ra |
| `bsw/CanTp/src/CanTp_Tx.c` | State machine gửi: SF / FF / CF ra, FC vào |
| `bsw/CanTp/src/CanTp_Frame.c` | Encode / decode N_PCI, padding, STmin |
| `bsw/CanTp/src/CanTp_Internal.h` | Type cấu hình, hằng số N_PCI, interface nội bộ (private) |
| `gen/CanTp/CanTp_Cfg.h`, `CanTp_Lcfg.c` | Cấu hình (bình thường do tool generate) |
| `stubs/LSduR/` | `LSduR_CanTp.h` + stub ghi lại frame ECU gửi |
| `stubs/PduR/PduR_CanTp.h`, `PduR_Stub_CanTp.c` | Upper layer giả: chỉnh được buffer, giá trị trả về |
| `stubs/Rte/SchM_CanTp.h` | `CanTp_MainFunction`, exclusive area |
| `test/CanTp/TesterTp.c/.h` | Codec phía **tester**, viết độc lập với CanTp, mỗi trường N_PCI là một member của struct |
| `test/CanTp/Test_CanTp.c` | 17 test, 106 check |
| `test/CanTp/sim/CanTp_Sim.c` | Simulator tương tác |
| `test/CanTp/sim/scripts/*.txt` | Script demo |

## 7. Chạy

```sh
make test                                  # Test_Dcm + Test_CanTp
make test MODULES=CanTp                    # chỉ CanTp (khi checkout chưa có gen/Dcm)
make sim                                   # build out/CanTp_Sim
./out/CanTp_Sim                            # gõ lệnh trực tiếp, "help" để xem lệnh
./out/CanTp_Sim -e < test/CanTp/sim/scripts/01_rx_multiframe.txt
```

Bạn đóng vai **tester**. Mỗi frame được in dạng raw và tách theo từng trường:

```
> ff 2E F1 90 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 12 13 14 15 16 17 18
[t=    0 ms] 0x7E0 TESTER->ECU 10 1B 2E F1 90 01 02 03 | FF  FF_DL=27  data: 2E F1 90 01 02 03
[t=    0 ms]   PduR <-CanTp PduR_CanTpStartOfReception(id=0, TpSduLength=27) -> BUFREQ_OK, bufferSize=512
> tick
[t=    1 ms] 0x7E8 ECU->TESTER 30 02 05 CC CC CC CC CC | FC  FS=0(CTS) BS=2 STmin=0x05(5 ms)
```

| Lệnh | Tác dụng |
|---|---|
| `sf`, `func`, `ff`, `cf`, `fc`, `raw` | Tester gửi frame; set từng trường bằng `dl=`, `sn=`, `fs=`, `bs=`, `st=`, `pad=off` |
| `send <bytes>` / `send len=N` | Upper layer của ECU gọi `CanTp_Transmit` |
| `tick N` | Chạy `CanTp_MainFunction` N lần (N ms) |
| `auto off`, `conf ok\|fail` | Tự xác nhận frame thay cho LSduR (để thử N_As / N_Ar) |
| `buf N`, `sor ...`, `copytx ...` | Thay đổi phản hồi của upper layer (để thử FC.WAIT, FC.OVFLW, N_Cs) |
| `param bs\|stmin N`, `read`, `cancel rx\|tx` | `CanTp_ChangeParameter`, `CanTp_ReadParameter`, `CanTp_CancelReceive/Transmit` |

### Bài tập (tự làm với simulator, chưa có lời giải)

1. Gửi FF 27 byte, nhận FC, rồi gửi `cf sn=2`. CanTp báo lỗi gì, ở API nào, upper layer nhận gì? *Gợi ý:* tìm `SWS_CanTp_00314` trong `CanTp_Rx.c`.
2. `auto off`, rồi `send len=3` và `tick 80`. Timer nào hết hạn? Vì sao lỗi là `CANTP_E_TX_COM` chứ không phải `CANTP_E_COM`? *Gợi ý:* SWS_CanTp_00229.
3. `buf 8`, gửi FF 27 byte, `tick 100`. Đếm số FC(WAIT). Số đó lấy từ tham số cấu hình nào?
4. Gửi `send len=20`, `tick`, rồi trả lời `fc pad=off`. Vì sao bị từ chối? Nếu tắt `CanTpTxPaddingActivation` thì sao?
5. Tester gửi `fc bs=0 st=0xF3` cho một response 40 byte. ECU chờ bao lâu giữa hai CF trong ví dụ này, và vì sao (gợi ý: độ phân giải của `CanTp_MainFunction`)?

## 8. Truy vết requirement → code

| Requirement | Nội dung | Vị trí |
|---|---|---|
| SWS_CanTp_00030, 00111, 00170, 00273 | Init: mọi kết nối về idle, chuyển sang CANTP_ON | `CanTp_Init` |
| SWS_CanTp_00200, 00202 | Shutdown, không báo cho upper layer | `CanTp_Shutdown` |
| SWS_CanTp_00079, 00329 | SF/FF → `PduR_CanTpStartOfReception` kèm dữ liệu SF/FF | `CanTp_RxStartSf`, `CanTp_RxStartFf` |
| SWS_CanTp_00081, 00318, 00353, 00339 | Kết quả `StartOfReception`: NOT_OK / OVFL (FC.OVFLW) / buffer nhỏ | `CanTp_Rx.c` |
| SWS_CanTp_00082, 00222, 00224, 00325, 00341, 00223 | Đợi buffer, FC(WAIT), WFTmax, FC(CTS) | `CanTp_RxEvaluateBuffer`, `CanTp_RxPollBuffer` |
| SWS_CanTp_00166, 00312, 00313, 00311 | N_Br, N_Cr, N_Ar | `CanTp_Rx.c` |
| SWS_CanTp_00269, 00271, 00314, 00084 | Copy CF, lỗi copy, SN sai, kết thúc nhận | `CanTp_RxOnCf` |
| SWS_CanTp_00067, 00278 | Cùng BS/STmin trong mọi FC, FC sau mỗi block | `CanTp_RxSendFc`, `CanTp_RxContinueBlock` |
| SWS_CanTp_00124, Table 1 | SF/FF trong lúc đang nhận → abort, nhận cái mới; CF/FC không chờ → bỏ qua | `CanTp_RxAbortOngoing`, `CanTp_RxOnCf`, `CanTp_TxProcessFc` |
| SWS_CanTp_00092, 00093 | Functional chỉ SF; multi-frame → `CANTP_E_INVALID_TATYPE` | `CanTp_RxOnFf`, `CanTp_TxStart` |
| SWS_CanTp_00225, 00231, 00232, 00206, 00123 | `CanTp_Transmit`: chỉ dùng length; SF/FF; từ chối khi N-SDU đang bận | `CanTp_TxStart` |
| SWS_CanTp_00086, 00087, 00184, 00272, 00167, 00280 | `CopyTxData` cho mỗi frame, retry=NULL, BUSY → thử lại, N_Cs | `CanTp_TxTrySendFrame` |
| SWS_CanTp_00075, 00310, 00315, 00316, 00090 | N_As, N_Bs, kết thúc gửi | `CanTp_Tx.c` |
| SWS_CanTp_00309, 00317 | FC(OVFLW), FS sai → abort | `CanTp_TxProcessFc` |
| SWS_CanTp_00342, 00343, 00355 | `LSduR_CanTpTransmit` lỗi / TxConfirmation lỗi → abort | `CanTp_RxSendFc`, `CanTp_TxTrySendFrame`, `CanTp_*Confirm*` |
| SWS_CanTp_00344–00349 | Padding Rx/Tx, `CANTP_E_PADDING` | `CanTp_RxOnSf`, `CanTp_RxOnCf`, `CanTp_FrameFinalize`, `CanTp_TxProcessFc` |
| SWS_CanTp_00248, 00075 | Một N-PDU chỉ có một frame chờ confirmation (0x7E8 dùng chung cho response và FC) | `CanTp_IsLSduBusy` |
| SWS_CanTp_00242, 00255, 00256 | `CanTp_CancelTransmit` | `CanTp_TxCancel` |
| SWS_CanTp_00261, 00262, 00263 | `CanTp_CancelReceive` | `CanTp_RxCancel` |
| SWS_CanTp_00303, 00304, 00305, 00324 | `ChangeParameter` / `ReadParameter` | `CanTp.c` |
| SWS_CanTp_00293, 00352, 00229 | Mã lỗi development / runtime | `CanTp.h` |

## 9. Giới hạn (cố ý giản lược)

- Chỉ classic CAN (CAN_DL ≤ 8), chỉ **normal addressing** (`CANTP_STANDARD`), không MetaData / generic connection, không CAN FD (`CanTpFlexibleDataRateSupport = false`).
- Mỗi N-SDU có kết nối riêng. Chưa làm channel dùng chung và `CanTpPendingTxNSduSupport` (SWS 7.2.3).
- Chưa làm: `CanTp_GetVersionInfo`, Extended Production Errors về Dem (SWS 7.4.4), security event về IdsM (SWS 7.5), post-build configuration.
- Exclusive area chỉ bảo vệ bước check-and-claim trong `CanTp_Transmit`. Chưa phân tích đầy đủ khi `CanTp_RxIndication` / `CanTp_TxConfirmation` chạy trong ngắt.
- STmin 100–900 µs được làm tròn lên 1 tick (1 ms) [Inference].
- Simulator chưa nối với DCM thật. Upper layer là stub PduR.

## 10. Điểm cần xác minh

1. **ISO 15765-2** (không có file): giá trị FS (0/1/2), mã hóa STmin và cách xử lý giá trị reserved (ở đây dùng 127 ms), SN của CF đầu = 1, điều kiện FF_DL > 7 và FF escape chỉ khi FF_DL > 4095, FF luôn đủ 8 byte, sender dùng BS/STmin của FC(CTS) nào.
2. Module ID của CanTp = **35**: lấy từ AUTOSAR "List of Basic Software Modules", không có trong các PDF được cung cấp.
3. SWS_CanTp_00229 gọi `CANTP_E_RX_COM` / `CANTP_E_TX_COM` là "development error", nhưng bảng SWS_CanTp_00352 xếp chúng vào **runtime errors**. Code dùng `Det_ReportRuntimeError` theo bảng.
4. `CanTpNbr` được SWS mô tả là "performance requirement for (N_Br + N_Ar)". Code dùng trực tiếp làm timer N_Br [Inference].
5. Mã lỗi khi abort vì upper layer trả `BUFREQ_E_NOT_OK`, hoặc vì SF/FF mới cắt ngang một lần nhận đang chạy: SWS không nêu riêng. Code báo `CANTP_E_COM` theo SWS_CanTp_00229 ("any other protocol error") [Inference].
6. `CLAUDE.md` liệt kê R22-11 / R23-11 / R24-11, nhưng code DCM và CanTp viết theo **R25-11** để cùng release với SWS_Dcm đang có trong project.
7. Giá trị timing, BS = 2, STmin = 5 ms, WFTmax = 3, padding `0xCC`, CAN ID 0x7E0/0x7DF/0x7E8 đều là **giá trị ví dụ**. Giá trị thật lấy từ OEM spec.
