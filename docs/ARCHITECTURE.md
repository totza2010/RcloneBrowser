# สถาปัตยกรรมชั้น (Layer Architecture) — เอกสารมีชีวิต

> **เอกสารนี้ไม่ใช่สภาพปัจจุบัน แต่คือเป้าหมาย** — ตอนนี้โค้ดยังเป็นก้อนเดียว
> วัตถุประสงค์คือ **บันทึกจุดที่แยกได้ระหว่างทำงานอื่น** เพื่อที่ตอนลงมือแยกจริง
> (E2 ในแผนพัฒนา) จะไม่ต้องมาสำรวจใหม่ทั้งหมด
>
> เป้าหมายสุดท้าย: โมเดลแบบ qBittorrent — core ตัวเดียว มี 2 หน้าตา
> `RcloneBrowser` (Qt GUI) และ `rclone-browser-nox` (headless + Web UI)
>
> อัปเดตล่าสุด: 2026-08-03

## สถานะความคืบหน้า

| งาน | สถานะ | ผล |
|---|---|---|
| แยก `JobOptionsListWidgetItem` → `job_options_item.h` | ✅ **เสร็จ** | `job_options.h/.cpp` เป็น core แล้ว |
| แยก `ReadSettings`/`WriteSettings` → `widget_settings.cpp` | ✅ **เสร็จ** | `utils.h/.cpp` เป็น core แล้ว |
| `RedactArgs()` + อุดจุดรั่ว 6 จุด | ✅ **เสร็จ** | ดู §5 |
| แก้ log หายทุก 10,000 บรรทัด | ✅ **เสร็จ** | เปลี่ยนเป็น ring buffer |
| P0 build hardening | ✅ **เสร็จ** | ยืนยันด้วย `dumpbin` แล้ว |
| CI build-check บน push/PR | ✅ **เสร็จ** | `.github/workflows/ci.yml` · V-07 **PASS** — 4 job เขียวบน GitHub |
| §3.3 RC credential ไป env + `bounded()` | ✅ **เสร็จ** | แก้ VIO-3 ไปด้วย · V-08 **PASS** |
| §3.6 Capability registry | ✅ **เสร็จ** | `rclone_capabilities.*` (L0) — รอทดสอบ (V-09) |
| §6.6 Mount script editor ในหน้าต่าง | ✅ **เสร็จ** | `script_editor_dialog.*` (L3) — รอทดสอบ (V-10) |
| แยก `pch_core.h` | ✅ **เสร็จ** | QtCore + QtNetwork เท่านั้น |
| **สร้าง `rbcore` static lib + QTest** | ✅ **เสร็จ** | ลิงก์แค่ `Qt6::Core` `Qt6::Network` — linker บังคับขอบเขตแล้ว |
| §3.5 progress จาก RC API แทน regex | ✅ **เสร็จ** | `job_stats.*` + `rc_client.*` (L0) · ลบ regex 10 ตัว · V-11/V-12 **PASS** |
| §3.4 listing ด้วย `lsjson` | ✅ **เสร็จ** | `lsjson_parser.*` (L0) · ลบ regex 2 ตัวสุดท้าย · ทดสอบมือแล้ว |
| E1 `--run-task` headless | ⬜ ยังไม่ทำ | |
| E2 `rbcore` + `-DNO_GUI=ON` | ⬜ ยังไม่ทำ | |

**ไฟล์ที่ปลอด GUI: 27/67** (เริ่มต้นที่ 21/59) — ตรวจด้วย `python scripts/check_layers.py`

### `rbcore` มีจริงแล้ว
ไฟล์ใน §3.1 ทั้งหมดอยู่ใน target `rbcore` (static lib) ที่ลิงก์แค่ `Qt6::Core` และ
`Qt6::Network` — **ขอบเขตชั้นถูกบังคับด้วย linker แล้ว ไม่ใช่แค่ข้อตกลง**
ถ้าใครดึง widget เข้า core จะ build ไม่ผ่านทันที ไม่ต้องรอ `-DNO_GUI=ON`

```bash
cmake --build build --config Release          # rbcore + GUI + tests
ctest --test-dir build -C Release --output-on-failure
```

---

## 1. ชั้นที่ต้องการ

```
┌─────────────────────────────────────────────────────────┐
│ L3  Clients                                             │
│     Qt Widgets GUI  ·  Web UI (static)  ·  CLI          │
│     → แสดงผลอย่างเดียว ไม่มี business logic              │
├─────────────────────────────────────────────────────────┤
│ L2  Transport                                           │
│     HTTP API (QHttpServer/QTcpServer)                   │
│     → มีเฉพาะตอน build ที่เปิด API                        │
├─────────────────────────────────────────────────────────┤
│ L1  Orchestration        [rbcore]                       │
│     JobEngine · queue · scheduler · task store          │
├─────────────────────────────────────────────────────────┤
│ L0  rclone invocation    [rbcore]                       │
│     สร้าง args · spawn process · parse output · log      │
└─────────────────────────────────────────────────────────┘
       L0+L1 ลิงก์ได้แค่ Qt6::Core และ Qt6::Network
```

### กฎที่ห้ามละเมิด

1. **L0/L1 ห้าม `#include` อะไรก็ตามจาก QtWidgets/QtGui** — บังคับด้วย target `rbcore` ที่ไม่ลิงก์ `Qt6::Widgets` · ปลายทางคือ `cmake -DNO_GUI=ON` build ทั้งแอปได้
2. **L0/L1 ห้ามเปิด dialog หรือ `QMessageBox`** — ต้องคืน error กลับขึ้นไปให้ client ตัดสินใจแทน
3. **L3 ห้ามสร้าง rclone args เอง** — ต้องเรียกผ่าน L0 เท่านั้น (ตอนนี้ยังละเมิดอยู่ ดู §4)
4. **การสื่อสารขึ้นบน ใช้ signal เท่านั้น** — L1 ห้ามรู้จักชนิดของ client
5. **ทุกอย่างที่ออกจาก L0/L1 ต้องผ่าน `RedactArgs()`** — เพราะ args มี `--rc-pass` (ดู §5)

---

## 2. วิธีบันทึกจุดที่แยกได้ระหว่างทำงาน

เมื่อกำลังแก้อย่างอื่นอยู่แล้วเจอโค้ดที่ควรอยู่ใน core **อย่าหยุดไปแยกทันที** ให้ทิ้ง marker ไว้:

```cpp
// CORE: สร้าง args ของ mount -- ควรย้ายไป JobArgsBuilder (L0), ไม่พึ่ง widget เลย
// LAYER: ฟังก์ชันนี้ผสม parse ผลลัพธ์ (L0) กับอัปเดต progress bar (L3)
```

| marker | ความหมาย |
|---|---|
| `// CORE:` | โค้ดก้อนนี้ควรอยู่ใน L0/L1 ตามสภาพ ย้ายได้เลยตอน E2 |
| `// LAYER:` | โค้ดก้อนนี้ปนกันอยู่ ต้องผ่าครึ่งก่อนย้าย |
| `// SECURITY:` | จุดที่ข้อมูลลับรั่วออกได้ ต้องระวังทุกครั้งที่เพิ่มทางออกใหม่ |
| `// TEST:` | **ต้องมีคนทดสอบด้วยมือ** — เขียนว่าทดสอบอย่างไรและเกณฑ์ผ่านคืออะไร พร้อมรหัส `(V-nn)` |

รวบรวม marker ทั้งหมด (จัดกลุ่มตามชนิด):
```bash
python scripts/check_layers.py --markers
```

### `TEST:` ต้องคู่กับ `docs/VERIFY.md` เสมอ

marker บอกแค่ *ว่าต้องทดสอบอะไร* ส่วน **ผลการทดสอบ** ให้ผู้ทดสอบกรอกใน
[`docs/VERIFY.md`](VERIFY.md) ซึ่งมีตาราง PASS/FAIL + ชื่อผู้ทดสอบ + วันที่

```cpp
// TEST: (V-03) mount remote ที่ตั้ง RC port -> hover ปุ่ม output ต้องเห็น
// "--rc-pass=***" ไม่ใช่ค่าจริง แล้วกด copy วางใน Notepad ต้องได้ *** เช่นกัน
```
รหัส `V-nn` ต้องไม่ซ้ำ และไม่นำกลับมาใช้ใหม่แม้รายการเก่าจะเลิกใช้แล้ว

---

## 3. ทะเบียนสถานะไฟล์

ตรวจอัตโนมัติด้วย:
```bash
python scripts/check_layers.py
```

> ⚠️ **ข้อจำกัดของสคริปต์:** เป็นการตรวจระดับ `#include` เท่านั้น และ `src/pch.h`
> ยังดึง `<QtGui>` เข้าทุกไฟล์ของ target GUI ทำให้บางไฟล์ "ดูสะอาด" ทั้งที่ยังลิงก์ไม่ผ่าน
> **ตัวตัดสินจริงคือการ build target `rbcore`** ซึ่งลิงก์แค่ Qt Core/Network
> สคริปต์นี้ใช้ติดตามความคืบหน้าของไฟล์ที่*ยังไม่ได้*ย้ายเข้า `rbcore`

### 3.1 🟢 อยู่ใน `rbcore` แล้ว (ลิงก์ Qt Core/Network เท่านั้น)

| ไฟล์ | บรรทัด | บทบาทเป้าหมาย | หมายเหตุ |
|---|---|---|---|
| `qcron.cpp/.h` | 251 | L1 — scheduler engine | สะอาดมาแต่เดิม |
| `qcronfield.cpp/.h` | 242 | L1 | สะอาดมาแต่เดิม |
| `qcronnode.cpp/.h` | 229 | L1 | สะอาดมาแต่เดิม |
| `list_of_job_options.cpp/.h` | 271 | L1 — task store | สะอาดมาแต่เดิม |
| `global.h` | — | L1 | สะอาดมาแต่เดิม |
| `job_options.h/.cpp` | 355 | L1 — นิยาม task | ✅ **แยกแล้ว** |
| `utils.h/.cpp` | 327 | L0 — rclone invocation | ✅ **แยกแล้ว** |
| `rclone_capabilities.h/.cpp` | 120 | L0 — ถาม backend ว่าทำอะไรได้ | ✅ **เขียนใหม่เป็น core ตั้งแต่ต้น** |
| `job_stats.h/.cpp` | 230 | L0 — แปลง `core/stats` เป็นตัวเลข | ✅ **ใหม่** |
| `rc_client.h/.cpp` | 130 | L0 — poll RC ของ job แบบ async | ✅ **ใหม่** |
| `lsjson_parser.h/.cpp` | 190 | L0 — streaming parser ของ `lsjson` | ✅ **ใหม่** |

รวม **~1,985 บรรทัดอยู่ใน `rbcore`** และมี unit test ครอบแล้ว 6 ชุด (`tests/`)

### 3.2 🟡 แยกได้ด้วยงานเล็ก

| ไฟล์ | ปัญหา | วิธีแก้ | สถานะ |
|---|---|---|---|
| ~~`job_options.h:2`~~ | ~~`#include <QListWidget>`~~ | ย้าย `JobOptionsListWidgetItem` ไป [`job_options_item.h`](../src/job_options_item.h) | ✅ **เสร็จ** |
| ~~`utils.cpp`~~ | ~~widget include 6 ตัว~~ | ย้าย `ReadSettings`/`WriteSettings` ไป [`widget_settings.cpp`](../src/widget_settings.cpp) | ✅ **เสร็จ** |
| ~~`src/pch.h`~~ | ~~ดึง `<QtGui>` ให้ทั้งโปรเจกต์~~ | [`pch_core.h`](../src/pch_core.h) สำหรับ `rbcore` | ✅ **เสร็จ** |
| [`item_model.cpp`](../src/item_model.cpp) | `QApplication` `QStyle` (ใช้ทำไอคอน) | ส่วน parse ย้ายเข้า `lsjson_parser` (L0) แล้ว เหลือแต่ตัว model กับไอคอนที่เป็น L3 | ⬜ ทำตอน E2 |

### 3.3 🔴 ต้องผ่าจริง (งานหลักของ E2)

| จุด | ขนาด | หมายเหตุ |
|---|---|---|
| [`MainWindow::runItem()`](../src/main_window.cpp) `main_window.cpp:3702-3988` | ~286 บรรทัด | **หัวใจ** — ประกอบ args + dispatch ทุก operation → กลายเป็น `JobArgsBuilder` (L0) + `JobEngine::run()` (L1) |
| `MainWindow::addTransfer()` `:4251-4607` | ~356 บรรทัด | ปนกัน: สร้าง job (L1) + สร้าง `JobWidget` (L3) |
| `MainWindow::addNewMount()` `:4636-4739` | ~103 บรรทัด | เหมือนกัน |
| `MainWindow::addTasksToQueue()` `:3296` · `saveQueueFile()` `:4073` · `saveSchedulerFile()` `:4116` · `restoreSchedulersFromFile()` `:3258` | ~400 บรรทัด | คิว + persistence — logic เป็น L1 แต่ผูกกับ `QListWidget` |
| `MainWindow::rcloneListRemotes()` `:2953` · `rcloneGetVersion()` `:2520` | ~600 บรรทัด | เรียก rclone (L0) ปนกับอัปเดต UI (L3) |
| progress parsing ใน [`job_widget.cpp:136-260`](../src/job_widget.cpp) | ~125 บรรทัด | อยู่ใน widget ทั้งที่เป็น L0 แท้ๆ — **§3.5 ของแผนจะเขียนใหม่อยู่แล้ว ให้ย้ายออกมาเป็น `JobRunner` (L0) ในรอบเดียวกัน อย่าทำสองครั้ง** |

**ส่วนที่เหลือของ `main_window.cpp` (~3,500 บรรทัด) เป็น UI แท้ — ไม่ต้องแตะ**

---

## 4. การละเมิดชั้นที่รู้อยู่แล้ว (ยังไม่แก้)

| # | จุด | อาการ | แก้ตอน |
|---|---|---|---|
| VIO-1 | `main_window.cpp:3880-3900` | L3 สร้าง rclone args เอง (ส่วนสุ่ม RC credential ย้ายออกไป `addNewMount()` แล้ว) | E2 |
| ~~VIO-2~~ | ~~`job_widget.cpp`~~ | ~~L3 ทำหน้าที่ parse output~~ | ✅ **แก้แล้ว** — regex ทั้ง 10 ตัวถูกลบ เหลือแค่อ่านบรรทัดประกาศ port |
| ~~VIO-3~~ | ~~`mount_widget.cpp`~~ | ~~L3 อ่าน credential กลับจาก args ด้วย regex~~ | ✅ **แก้แล้ว** — credential ส่งผ่าน constructor และ env |
| ~~VIO-4~~ | ~~`item_model.cpp`~~ | ~~สร้าง args ของ `lsd`/`lsl` ในชั้น model~~ | ✅ **แก้แล้ว** — parse ย้ายไป `lsjson_parser` (L0) |
| VIO-5 | ทั่วทั้งโค้ด | error จาก rclone แสดงด้วย `QMessageBox` ทันทีในจุดที่เกิด → headless จะค้าง | E2 |

---

## 5. ข้อบังคับด้านความปลอดภัยที่ผูกกับชั้น

### 5.1 RC credential — ✅ ไม่อยู่ใน command line แล้ว

เดิม `--rc-user=`/`--rc-pass=` ถูกต่อเข้า args ⇒ อ่านได้จาก process list ของทั้งเครื่อง
ตอนนี้:
- สุ่มด้วย `GenerateRcCredential()` ที่ `MainWindow::addNewMount()` (ใช้ `bounded()` ไม่ใช่ `%` ที่มี modulo bias)
- ส่งให้ rclone ผ่าน env `RCLONE_RC_USER`/`RCLONE_RC_PASS` ด้วย `UseRcCredentials()`
- `MountWidget` เก็บไว้ใน member (`mRcUser`/`mRcPass`) สำหรับ mount script และ `core/quit` ตอน unmount
- **ไม่ถูกบันทึกลง `tasks.bin`** เพราะ task เก็บเป็น field ของ `JobOptions` แล้วประกอบ args ใหม่ทุกครั้ง

> ยืนยันกับ rclone v1.72.1 แล้วว่าอ่าน env ได้ทั้งฝั่ง server (`mount --rc`, `rcd`)
> และฝั่ง client (`rc`) — ทดสอบด้วย HTTP 401 เมื่อไม่ส่ง credential และ HTTP 200 เมื่อส่ง

### 5.2 argument อื่นยังต้องผ่าน `RedactArgs()`

ช่อง free-form rclone options ให้ผู้ใช้ใส่ `--drive-token=`, `--sftp-pass=` ฯลฯ ได้
`RedactArgs()` ใน [`utils.h`](../src/utils.h) ครอบทั้ง 6 จุด:

| ไฟล์ | ทางออก |
|---|---|
| `job_widget.cpp:18` / `:133` | tooltip / clipboard |
| `mount_widget.cpp:23` / `:186` | tooltip / clipboard |
| `stream_widget.cpp:20` / `:81` | tooltip / clipboard |

> `stream_widget` เป็นจุดที่พบเพิ่มตอนลงมือจริง — ไม่ได้อยู่ในรายการสำรวจครั้งแรก

**ทุกครั้งที่เพิ่มทางออกใหม่ของข้อมูล (log ไฟล์, API response, Web UI, diagnostics)
ต้องผ่าน `RedactArgs()` เสมอ** — ยิ่งขยับไปทาง Web UI ยิ่งอันตราย เพราะทางออกเพิ่มขึ้นเรื่อยๆ
ตอนทำ E3 ควรเพิ่ม test ที่ scan หา `mArgs.join` แบบดิบๆ แล้ว fail (NG ทำแนวนี้กับ RC auth)

---

## 6. ลำดับที่ตั้งใจไว้

งานนี้อยู่**ท้ายสุด**เพราะใหญ่และเปลี่ยนโครงมากที่สุด ระหว่างนี้:

```
P0/P1  (build hardening, RC auth, lsjson, JSON progress, capability registry)
       └─ ระหว่างทำ: ทิ้ง // CORE: marker ทุกครั้งที่เจอ
       └─ ถ้าต้นทุนไม่เกินครึ่งชั่วโมง แยกเลยตามข้อ 3.2
เฟส 2  (log, autocomplete, docker, --run-task)
       └─ E1 --run-task บังคับให้ต้องแยกชุดแรกจริง
E2     ผ่าตัดจริงตามข้อ 3.3
```

### เกณฑ์ผ่านของ E2
```bash
cmake -S . -B build-nox -DNO_GUI=ON && cmake --build build-nox
# ต้องคอมไพล์และลิงก์ผ่านโดยไม่มี Qt6::Widgets
```

---

## 7. อ้างอิง

- แผนพัฒนาเต็ม: `RcloneBrowser-Dev-Plan-v2.md` (§6.3 คือส่วนของงานนี้)
- สคริปต์ตรวจ: [`scripts/check_layers.py`](../scripts/check_layers.py)
- โมเดลอ้างอิง: qBittorrent — `qbittorrent` / `qbittorrent-nox` ใช้ core เดียวกัน
