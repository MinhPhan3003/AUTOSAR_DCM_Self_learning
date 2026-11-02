# ADR-001: Cấu trúc thư mục và quy tắc đặt file .h / .c

**Status:** Proposed
**Date:** 2026-10-18
**Deciders:** Chủ repo / lead của nhóm BSW
**Mở rộng bởi:** [ADR-002](ADR-002-mcal-can-s32k144.md) (thêm `mcal/`, `platform/`, `app/`; giữ stub cho unit test)

## Context

Cấu trúc trước khi đổi:

```
inc/   Dcm.h, Dcm_Cfg.h
src/   Dcm_Dsl.c, Dcm_Dsd.c, Dcm_Dsp.c, Dcm_Internal.h, Dcm_Lcfg.c
stub/  Std_Types.h, ComStack_Types.h, Rte_Dcm_Type.h, SchM_Dcm.h, PduR_Dcm.h, Det.h, Stub_Neighbors.c/.h
test/  Test_Main.c
*.pdf, build.sh, README.md   (cùng cấp với code)
```

Các vấn đề cụ thể:

1. **Code viết tay lẫn với code generate.** `Dcm_Cfg.h` nằm trong `inc/`, `Dcm_Lcfg.c` nằm trong `src/`. Khi tool cấu hình generate lại, không biết file nào được phép ghi đè.
2. **`stub/` chứa ba loại file của ba "chủ" khác nhau:** type chung (Std/ComStack), output của RTE (`Rte_Dcm_Type.h`, `SchM_Dcm.h`), interface của module lân cận (`PduR_Dcm.h`, `Det.h`). `Stub_Neighbors.c` còn gộp PduR + Det + SchM vào một file. Khi có PduR thật thì không xóa gọn được.
3. **Không có chỗ cho module thứ hai.** Thêm Dem hay FiM thì `inc/` và `src/` sẽ lẫn file của nhiều module.
4. **Header private không được bảo vệ.** Mọi file đều có thể include `Dcm_Internal.h`.
5. **Tài liệu (PDF) lẫn với code.**

Chuẩn liên quan:

- [Spec] AUTOSAR CP R25-11 SWS_TimeSyncOverCAN, mục 5.1, chỉ sang *SWS BSW General* mục 5.1.6 "Code file structure" và 5.1.7 "Header file structure" để biết quy tắc **tên file**. SWS BSW General **không có** trong các PDF được cung cấp, nên các mẫu tên `<Ma>_Cfg.h`, `<Ma>_Lcfg.c`, `<Ma>_PBcfg.c` bên dưới chưa được đối chiếu.
- [Spec] AUTOSAR CP R25-11 SWS_Dcm: API của Dcm "Available via `Dcm.h`" (mục 8.3, 8.4), `Dcm_MainFunction` "Available via `SchM_Dcm.h`" (8.6.1), type RTE "Available via `Rte_Dcm_Type.h`" (8.8.1).
- [Inference] Các SWS được cung cấp chỉ quy định **tên file và nội dung**, không quy định **cấu trúc thư mục**. Thư mục là quyết định của dự án.

## Decision

**Mỗi file nằm trong thư mục của bên sở hữu nó, tức là bên sẽ giao file đó trong một dự án thật.**

```
Autosar Documentation/
├── CLAUDE.md, README.md, Makefile
├── docs/
│   ├── specs/                 PDF: AUTOSAR SWS, ISO
│   └── adr/                   quyết định kiến trúc (file này)
├── bsw/<Module>/              code VIẾT TAY của module mình phát triển
│   ├── inc/                   header PUBLIC (module khác được include)
│   └── src/                   .c + header PRIVATE (chỉ module này dùng)
├── gen/<Module>/              output của TOOL CẤU HÌNH cho module đó (được phép ghi đè)
├── stubs/<Owner>/             thay thế tạm cho thứ mình KHÔNG phát triển
│   ├── General/               Std_Types.h, ComStack_Types.h
│   ├── Rte/                   Rte_<Ma>_Type.h, SchM_<Ma>.h, SchM_<Ma>_Stub.c
│   └── <Ma>/                  <Ma>_<User>.h (interface thật), <Ma>_Stub.c/.h (giả lập + accessor)
├── test/
│   ├── Common/                helper dùng chung cho mọi test
│   └── <Module>/              test của từng module
└── out/                       output build, không đưa vào version control
```

### Quy tắc đặt file

| Loại file | Đặt ở | Ai được include | Ví dụ hiện tại | Cơ sở |
|---|---|---|---|---|
| Header public của module | `bsw/<M>/inc/<M>.h` | Mọi file | `Dcm.h` | [Spec] "Available via Dcm.h" |
| Code của submodule | `bsw/<M>/src/<M>_<Sub>.c` | – | `Dcm_Dsl.c`, `Dcm_Dsd.c`, `Dcm_Dsp.c` | [Practice] |
| Header private | `bsw/<M>/src/<M>_Internal.h` | Chỉ file trong `bsw/<M>/` và `gen/<M>/` | `Dcm_Internal.h` | [Practice] |
| Cấu hình pre-compile | `gen/<M>/<M>_Cfg.h` | Mọi file (thông qua `<M>.h`) | `Dcm_Cfg.h` | Tên file: SWS BSW General 5.1.7 (cần đối chiếu) |
| Cấu hình link-time | `gen/<M>/<M>_Lcfg.c` | – | `Dcm_Lcfg.c` | Tên file: SWS BSW General 5.1.6 (cần đối chiếu) |
| Cấu hình post-build (sau này) | `gen/<M>/<M>_PBcfg.c` | – | – | Tên file: SWS BSW General 5.1.6 (cần đối chiếu) |
| Header RTE/SchM | `stubs/Rte/` | Mọi file | `Rte_Dcm_Type.h`, `SchM_Dcm.h` | [Spec] tên file theo SWS_Dcm |
| Interface của module lân cận | `stubs/<Owner>/<Owner>_<User>.h` | Mọi file | `PduR_Dcm.h`, `Det.h` | [Spec] SWS_Dcm_91001 |
| Giả lập của module lân cận | `stubs/<Owner>/<Owner>_Stub.c` + `<Owner>_Stub.h` (accessor cho test) | Chỉ test | `PduR_Stub.c`, `Det_Stub.c` | [Practice] |
| Test | `test/<M>/Test_<M>.c` | – | `Test_Dcm.c` | [Practice] |

### Quy tắc include (Makefile bắt buộc tuân theo)

- Include path **public**, dùng chung cho mọi file: `bsw/*/inc`, `gen/*`, `stubs/*`, `test/Common`.
- Include path **private** `bsw/<M>/src` chỉ được thêm khi biên dịch file nằm trong `bsw/<M>/` hoặc `gen/<M>/`. Nếu test hay module khác `#include "Dcm_Internal.h"` thì build sẽ báo lỗi (đã kiểm tra).
- `gen/<M>/<M>_Lcfg.c` được include header private của chính module `<M>`, vì file này mô tả cấu hình bằng type nội bộ của module.
- Code trong `bsw/` không bao giờ include `*_Stub.h`. Chỉ `test/` mới được include.

### Thêm module mới (ví dụ Dem)

1. Tạo `bsw/Dem/inc/Dem.h`, `bsw/Dem/src/Dem_*.c`, `gen/Dem/Dem_Cfg.h`, `gen/Dem/Dem_Lcfg.c`, `test/Dem/Test_Dem.c`.
2. Thêm `Dem` vào `MODULES` trong `Makefile`.
3. Khi đã có Dem thật, xóa stub Dem (nếu có) trong `stubs/Dem/` và bỏ `Dem` khỏi `STUBS`.

## Options Considered

### Option A: Phẳng `inc/`, `src/`, `stub/` (trước khi đổi)

| Dimension | Assessment |
|---|---|
| Complexity | Low |
| Mở rộng thêm module | Kém: file của mọi module lẫn trong một thư mục |
| Tách code generate | Không có |
| Thay stub bằng code thật | Phải tách tay từng file |
| Bảo vệ header private | Không |

**Pros:** ít thư mục, phù hợp với một module duy nhất.
**Cons:** gặp đủ 5 vấn đề nêu ở phần Context.

### Option B: Theo loại file cho toàn dự án (`include/`, `source/`, `config/`, `stub/`)

| Dimension | Assessment |
|---|---|
| Complexity | Low–Med |
| Mở rộng thêm module | Trung bình: vẫn một `include/` cho mọi module |
| Tách code generate | Có (`config/`) |
| Thay stub bằng code thật | Trung bình |
| Bảo vệ header private | Không, vì header private và public cùng nằm trong `include/` |

**Pros:** đơn giản, tách được phần generate.
**Cons:** không biết header nào public; xóa một module phải tìm file trong nhiều thư mục.

### Option C: Theo module và owner (đã chọn)

| Dimension | Assessment |
|---|---|
| Complexity | Med: nhiều thư mục hơn |
| Mở rộng thêm module | Tốt: một module = một thư mục `bsw/<M>` + `gen/<M>` + `test/<M>` |
| Tách code generate | Có (`gen/<M>`) |
| Thay stub bằng code thật | Xóa đúng một thư mục `stubs/<Owner>` |
| Bảo vệ header private | Có, Makefile bảo đảm |

**Pros:** nhìn đường dẫn là biết file của ai, có được sửa tay không, có được include không. Giống cách vendor BSW giao code theo từng module [Practice].
**Cons:** đường dẫn dài hơn; cần Makefile có quy tắc theo module.

## Trade-off Analysis

Option A tốn ít công nhất nhưng chỉ chạy được khi có một module. Lộ trình học trong CLAUDE.md có thêm Dem, FiM, Det và các module lân cận, nên A sẽ phải đổi lại sớm. Option B giải quyết được việc tách code generate nhưng không tách được public với private, mà đây là yêu cầu "Layering" và "Module structure" trong CLAUDE.md. Option C tốn thêm vài thư mục và một Makefile có quy tắc. Đổi lại, mỗi module độc lập, và quy tắc include được build bảo đảm chứ không phải dựa vào việc mọi người tự nhớ.

## Consequences

- **Dễ hơn:** thêm hoặc xóa module; generate lại cấu hình mà không sợ ghi đè code viết tay; thay stub bằng module thật; review đúng phạm vi.
- **Khó hơn:** đường dẫn dài hơn; mọi file mới phải đặt đúng quy tắc (dùng bảng ở trên khi review).
- **Cần xem lại:**
  - Đối chiếu tên `<Ma>_Cfg.h`, `<Ma>_Lcfg.c`, `<Ma>_PBcfg.c`, `<Ma>_MemMap.h` với SWS BSW General 5.1.6 / 5.1.7 khi có file.
  - Khi chạy trên target thật: thêm `<Ma>_MemMap.h` và thư mục cho code phụ thuộc compiler/platform.

## Action Items

1. [x] Di chuyển code vào `bsw/Dcm`, `gen/Dcm`, `stubs/<Owner>`, `test/Dcm`, `test/Common`.
2. [x] Tách `Stub_Neighbors.c/.h` thành `PduR_Stub`, `Det_Stub`, `SchM_Dcm_Stub`.
3. [x] Thay `build.sh` bằng `Makefile` có include path public/private theo module và tự theo dõi dependency header.
4. [x] Kiểm tra: `make test` cho 59/59 check pass; build lần hai không biên dịch lại gì; include `Dcm_Internal.h` từ `test/` bị build báo lỗi.
5. [ ] Chuyển `AUTOSAR_CP_SWS_DiagnosticCommunicationManager.pdf` và `ISO_14229-1_2013.en.PDF.pdf` vào `docs/specs/` (lần này bị khóa vì đang mở trong trình xem PDF).
6. [ ] Bổ sung SWS BSW General (CP R25-11) vào `docs/specs/` để đối chiếu quy tắc tên file.
7. [ ] Khởi tạo git để theo dõi thay đổi (`out/` đã nằm trong `.gitignore`).
