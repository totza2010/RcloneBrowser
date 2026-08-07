# แผนพัฒนา totza2010/RcloneBrowser

> เขียนครั้งแรก 2026-08-03 จากการตรวจ repo จริงและซอร์สจริง (base `67af990`, VERSION 3.1.1)
> ทับฉบับ v1 ที่เขียนจาก CHANGELOG ของ fork อื่นโดยไม่ได้เทียบกับโค้ดเรา (ดู §0)
> **ย้ายเข้า repo เมื่อ 2026-08-06** — ก่อนหน้านี้อยู่นอก version control ทำให้แผนกับโค้ดเดินคนละทาง
>
> เอกสารคู่กัน: [`ARCHITECTURE.md`](ARCHITECTURE.md) (ทะเบียนชั้น) · [`VERIFY.md`](VERIFY.md) (ผลทดสอบ) · [`DEV-SETUP.md`](DEV-SETUP.md) (สภาพแวดล้อม)

## สถานะ ณ 2026-08-06

**24 commits** จาก `67af990` ถึง `5077c39` · branch `modernize/rc-progress-and-hardening` · CI + Docker เขียว

### P0 / P1 — เสร็จทั้งหมด

| § | งาน | สถานะ | ทดสอบ |
|---|---|---|---|
| §3.1 | Build hardening (`/GS`, `/guard:cf`, `/CETCOMPAT`, CMP0091, Multimedia optional) | ✅ | V-01 ⬜ |
| §3.2 | CI build-check บน push/PR | ✅ | V-07 **PASS** |
| §3.3 | RC credential ไป environment + `bounded()` | ✅ | V-08 **PASS** (ข้อ 4–6 ⬜) |
| §3.4 | listing ด้วย `lsjson` | ✅ | ทดสอบมือแล้ว |
| §3.5 | progress จาก RC API แทน regex | ✅ | V-11 / V-12 **PASS** |
| §3.6 | Backend capability registry | ✅ | V-09 ⬜ |

**ลบ regex ที่ parse output ของ rclone ออกครบทั้ง 12 ตัว** — นี่คือผลลัพธ์ที่ใหญ่ที่สุดของ P1
ทุกตัวเลขบนการ์ดมาจาก `core/stats` และทุกรายการไฟล์มาจาก `lsjson` ซึ่งเป็น API ไม่ใช่รูปแบบการแสดงผล

### เฟส 2 — เสร็จหมดยกเว้นสาย Web UI

| § | งาน | สถานะ | ทดสอบ |
|---|---|---|---|
| §6.0 | `RedactArgs()` + อุดจุดรั่ว `--rc-pass` | ✅ | V-03 ⬜ |
| §6.1 | เก็บ log ของแต่ละงานเป็นไฟล์ | ✅ | ทดสอบมือแล้ว · ⚠️ ค้าง: ทบทวนรูปแบบชื่อไฟล์ |
| §6.2 | ย้าย Dockerfile เข้า repo | ✅ | build จริงผ่าน 2 flavour |
| §6.4 | Autocomplete | ✅ | V-14 / V-15 ⬜ |
| §6.6 | Mount script editor ในหน้าต่าง | ✅ | V-10 ⬜ |
| §6.7 | การแสดงผลการ์ด job | ✅ | V-13 ⬜ |
| — | ตรวจ repo ของ rclone เอง (นอกแผนเดิม) | ✅ | V-16 ⬜ |
| §6.8 | **เก็บทุกอย่างลง DB + ประวัติการรัน** | ⬜ **ใหม่** | = S13 |
| §6.3 E1 | `--run-task` headless | ✅ | V-17 🟡 core ผ่าน |
| §6.3 E2 | `rbcore` + `-DNO_GUI=ON` | ⬜ | |
| §6.3 E3 | HTTP API | ⬜ | |
| §6.3 E4 | Web UI + Docker ไม่มี X11 | ⬜ | |

### ของที่ได้มาระหว่างทางโดยไม่ได้อยู่ในแผน

- **`rbcore` static library** — ลิงก์แค่ `Qt6::Core` และ `Qt6::Network` ขอบเขตชั้นจึงถูกบังคับด้วย linker ไม่ใช่ข้อตกลง · ~2,345 บรรทัด
- **QTest 8 ชุด** — แผนบอกว่าต้องมีก่อนเริ่ม P1 ตอนนี้มีจริงและรันใน CI
- **`scripts/check_layers.py`** — ไฟล์ปลอด GUI 38/79 (เริ่มที่ 21/59)
- **Docker workflow** — 2 flavour (tgdrive / mainline) แท็กตามเวอร์ชัน rclone อัตโนมัติ + cron เฝ้า release ของทั้งสอง repo
- **ตรวจ repo ของ rclone จาก backend** — เอาช่องกรอกออกได้ (V-16)

### ค้างไว้ตั้งใจ

| เรื่อง | ที่มาร์คไว้ |
|---|---|
| ทบทวนรูปแบบชื่อไฟล์ log | `REVISIT` ใน `src/job_log.cpp` |
| เติมชื่อ remote ในช่อง source/dest (§6.4 #3) | ยังไม่ทำ |
| autocomplete path บน remote (§6.4 #4) | ยังไม่ทำ |
| VIO-1 `main_window.cpp` ประกอบ args เอง | `LAYER:` marker · แก้ตอน E2 |

### ถัดไป

**E1 `--run-task` headless** คือข้อต่อไปตามลำดับใน §6.5 และเป็นตัวบังคับให้แยก core ชุดแรกจริง
รีโปอ้างอิงสำหรับ E3/E4 บันทึกไว้ที่ [`ARCHITECTURE.md` §7](ARCHITECTURE.md)

---

## 0. สรุปสิ่งที่แผน v1 เข้าใจผิด (อ่านก่อน)

แผน v1 เขียนจาก CHANGELOG ของ NG โดยไม่ได้เทียบกับโค้ดเรา จึงสั่งให้ทำสิ่งที่ **ทำไปแล้ว** หลายข้อ:

| หัวข้อใน v1 | สถานะจริงในโค้ดเรา | สรุป |
|---|---|---|
| §2.1 อัปเป็น Qt6 / `QRegExp`→`QRegularExpression` | ทำแล้วตั้งแต่ `669c36e` — ไม่มี `QRegExp` เหลือแม้แต่ตัวเดียว, `find_package(Qt6 REQUIRED)`, บังคับ 64-bit | ❌ ตัดทิ้ง |
| §2.3.1 RC endpoint ไม่มี auth | **มี auth แล้ว** — `main_window.cpp:3891-3927` และ `mount_dialog.cpp:433-472` สุ่ม user 10 ตัว/pass 22 ตัวต่อ mount | ⚠️ แก้ไขรายละเอียด ไม่ใช่สร้างใหม่ |
| §2.4.3 post-transfer verification (`check`/`cryptcheck`) | มี `check_dialog.cpp/.h/.ui` อยู่แล้ว | ⚠️ เหลือแค่เพิ่ม checkbox "verify หลัง transfer" |
| §2.4.2 Task scheduling | มี in-app scheduler เต็มรูปแบบแล้ว: `scheduler_widget.cpp` (1,077 บรรทัด) + `qcron.cpp/qcronfield.cpp/qcronnode.cpp` (722 บรรทัด) | ⚠️ ช่องว่างจริงคือ **native OS scheduler + headless mode** เท่านั้น |
| §2.1 "ดึง build system จาก Alkl58" | CMake ของ Alkl58 **ไม่ได้ทันสมัยกว่าเรา** — โครงเกือบเหมือนกัน | ⚠️ ดึงเฉพาะ 3 จุด (ดู §2.1) |
| §4 "ตรวจ license ก่อน redistribute" | NG = MIT, เรา = MIT, kapitainsky = MIT ทั้งสาย | ✅ **คัดลอกโค้ดตรงๆ ได้** ไม่ต้องเขียนใหม่ (แค่คง copyright header) |

**ผลกระทบต่อกลยุทธ์:** ข้อสุดท้ายสำคัญที่สุด — v1 บอกให้ "port ทีละส่วนโดยเขียนใหม่ตาม CHANGELOG" ซึ่งเสียเวลาโดยไม่จำเป็น ในเมื่อ **ทั้งระบบนิเวศเป็น MIT หมด** เราสามารถ `git remote add ng` แล้ว `git show ng/master:src/rclone_capabilities.cpp` คัดลอกไฟล์มาตรงๆ พร้อมใส่ attribution ได้เลย

---

## 1. สถานะจริงของระบบนิเวศ (ข้อมูล 2026-08-03)

| Repo | push ล่าสุด | ⭐ | License | ประเมิน |
|---|---|---|---|---|
| `SysAdminDoc/RcloneBrowserNG` | **2026-08-03** (วันนี้) | 1 | MIT | 🟢 active มาก, feature superset, 623 commits |
| `Alkl58/RcloneBrowser` | 2026-07-06 | 120 | MIT | 🟢 active, สาย build/Qt6 |
| `totza2010/RcloneBrowser` (เรา) | 2025-11-10 | 15 | MIT | 🟡 นิ่งมา ~9 เดือน |
| `roshanconnor123/RcloneBrowser` | 2026-07-04 | 0 | MIT | 🔴 base ปี 2023 + AI commit แก้ CMake 1 ตัว — **ตัดทิ้ง** |
| `ubikore/RcloneBrowser` | 2026-05-10 | 0 | MIT | 🟡 **fork ของเราเอง** มี patch จริง 2 ชิ้น (ดู §2.5) |
| `RyoKamui/RcloneBrowser` | 2026-05-11 | 0 | NOASSERTION | 🔴 มี commit เดียว "Initial commit" (squash copy) — **ตัดทิ้ง** |
| `downer08tutti/RcloneBrowser2` | 2026-04-21 | 1 | MIT | 🔴 dump ซอร์ส + แก้ README — **ตัดทิ้ง** |
| `kapitainsky/RcloneBrowser` | 2024-03-11 | 2,940 | MIT | ⚫ ต้นสาย, หยุดแล้ว |

### ข้อค้นพบที่ v1 พลาด: `ubikore` คือ fork ของเรา ไม่ใช่ fork ของ kapitainsky
`ubikore/master` มี commit `Bump version to 3.1.1` และ `Remove Win32 configuration from AppVeyor` ตรงกับของเราทุกตัว แล้วต่อยอดไป `+387/-3` ใน `src/utils.cpp` — นี่คือ fork เดียวในกลุ่ม "แหล่งรอง" ที่มีค่า

---

## 2. ข้อดี/ข้อเสียของแต่ละสาย และสิ่งที่ควรดึงมา

### 2.1 `Alkl58/RcloneBrowser` — สาย build system

**ข้อดี:** stars มากสุดในกลุ่ม active (120), CI เป็น GitHub Actions (เราใช้ AppVeyor ที่ตายไปแล้วในทางปฏิบัติ), ดูแล macOS Apple Silicon จริง
**ข้อเสีย:** ฟีเจอร์แอปเกือบไม่ต่างจาก kapitainsky เลย — เป็น "maintenance fork" ล้วน, commit ล่าสุดหลายตัวขึ้นต้นด้วย `(AI)` = generate แล้ว merge โดยรีวิวน้อย

**ควรดึงมา 3 จุดเท่านั้น:**

1. **`CMP0091` + MSVC runtime** — เราไม่มี ทำให้เสี่ยง `LNK4098` เวลา link กับ Qt prebuilt
   ```cmake
   # CMakeLists.txt — เพิ่มใน if(WIN32) block ก่อน project()
   cmake_policy(SET CMP0091 NEW)
   set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
   ```
2. **Multimedia เป็น optional** — ของเราเป็น `REQUIRED` ทำให้ build ไม่ผ่านบน Qt kit ที่ไม่มี module นี้ (พบบ่อยบน Linux distro packaging)
   ```cmake
   find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
   find_package(Qt6 QUIET OPTIONAL_COMPONENTS Multimedia)
   ```
   ⚠️ ต้องครอบโค้ดที่เรียก `QSoundEffect`/`QMediaPlayer` ด้วย `#ifdef` ควบคู่กัน
3. **macOS frameworks ที่ขาด** — `UserNotifications`, `UniformTypeIdentifiers` (เรามีแค่ Cocoa + IOKit) → notification บน macOS 11+ จะพังเงียบ
4. **commit `(AI) Fix parsing speed of rclone - fixes #10` (2026-07-06)** — ควร diff ดูก่อน เพราะเราเพิ่งเขียน `rxProgress` ใหม่เองใน `6b6cbe5` อาจแก้คนละปัญหาหรือปัญหาเดียวกัน

**⚠️ ห้ามดึง:** `/GS-` ใน `CMAKE_CXX_FLAGS_RELEASE` — Alkl58 ยังมี flag นี้ (ปิด stack protection) เหมือนเรา ซึ่งเป็นของเสียที่ NG แก้ไปแล้ว (ดู §3.1)

---

### 2.2 `SysAdminDoc/RcloneBrowserNG` — สาย feature

**ข้อดี:** feature set กว้างที่สุดในระบบนิเวศแบบทิ้งห่าง — dual-pane, bookmarks, cross-remote search, file preview, versioning browser, bisync UI, serve UI, pause/resume, job history, capability registry, native scheduler, RC engine, i18n scaffolding, 17 QTest targets

**ข้อเสีย (สำคัญ — v1 ไม่ได้พูดถึงเลย):**
- ⭐ **1 ดาว** = แทบไม่มีใครรีวิวหรือใช้จริง ความเสี่ยง regression สูงมาก
- **ไม่มี CI** — commit `Remove GitHub Actions workflows — local builds only` (2026-06-26) แปลว่าไม่มีใครยืนยันว่า build ผ่านบนเครื่องอื่น
- **AI-generated หนัก** — CHANGELOG ยาวผิดปกติ, commit หลายสิบตัวต่อวัน, ข้ออ้าง CVE ที่ต้องตรวจสอบ (ดู §3.3)
- **โครงไฟล์แยกทางกับเราแรงมาก** — NG **ไม่มี**: `dedupe_dialog`, `check_dialog`, `file_dialog`, `delete_progress_dialog`, `remote_folder_dialog`, `scheduler_widget`, `qcron*`, `hours_spinbox`, `minutes_spinbox`
  → **เรามีฟีเจอร์ที่ NG ไม่มี**: dedupe UI, check/cryptcheck dialog, in-app cron scheduler
  → merge ตรงๆ = พัง; ต้อง port รายไฟล์เท่านั้น (ข้อนี้ v1 พูดถูก)

**สรุปท่าที:** ใช้ NG เป็น **แหล่งโค้ด** ไม่ใช่ **แหล่ง truth** — ทุกไฟล์ที่ดึงมาต้องอ่านเองก่อน merge

---

### 2.3 `ubikore/RcloneBrowser` — patch ที่ v1 ตกหล่น

diff จาก `totza2010:master`:
```
+387/-3   src/utils.cpp        ← ของหลัก
+16/-0    src/utils.h
+50/-11   src/transfer_dialog.cpp
+37/-26   .github/workflows/build.yml
added     hosts
+5/-0     src/file_dialog.cpp
```

สองสิ่งที่มีค่า:
1. **hosts-file proxy สำหรับ rclone HTTPS** (`Support project-local hosts for rclone HTTPS requests`) — ให้ระบุ hosts ต่อโปรเจกต์เพื่อข้ามปัญหา DNS/GFW **ข้อนี้ตรงกับ use case teldrive มาก** เพราะ Telegram API ถูกบล็อกใน DNS หลายประเทศ
2. **Native Windows file/folder dialog** (`修改Windows打开文件/文件夹为原生界面`) — แทน Qt non-native dialog, UX ดีขึ้นชัดบน Windows

3. **GitHub Actions build.yml ที่แก้แล้ว** — เราจะย้ายจาก AppVeyor อยู่แล้ว ดึงมาเทียบได้

---

## 3. แผนงานเรียงตามลำดับ (ROI สูง → ต่ำ)

### P0 — ทำก่อน (คุ้มที่สุด, เสี่ยงต่ำ)

#### 3.1 Build hardening + CMake fixes
เราปิด stack protection อยู่จริง: `CMakeLists.txt:32` มี `/GS-`

```cmake
# ก่อน (ทั้ง totza2010 และ Alkl58 — เป็นของตกทอดจาก mmozeiko ปี 2017)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} /GF /Gy /GS- /GR /GL")
#                                                     ^^^^ ปิด stack cookie

# หลัง
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /GF /Gy /GS /GR /GL /guard:cf")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG /OPT:ICF /OPT:REF /CETCOMPAT /DYNAMICBASE /NXCOMPAT")
```
> หมายเหตุ: บรรทัดเดิมยังมีบั๊ก — ใช้ `${CMAKE_CXX_FLAGS}` (ไม่ใช่ `_RELEASE`) ทำให้ flag ซ้ำซ้อน; Alkl58 แก้จุดนี้ไปแล้ว

พร้อมกันนี้: CMP0091, Multimedia optional, macOS frameworks (§2.1)

**ต้นทุน:** ~1 วัน · **ความเสี่ยง:** ต่ำ (แค่ต้อง benchmark ว่า `/GS` ไม่ทำให้ hash/parse ช้าลงผิดสังเกต)

#### 3.2 เก็บกวาด CI (แก้จากฉบับร่างแรก — เรามี GitHub Actions อยู่แล้ว)
`.github/workflows/build.yml` มีอยู่แล้วและใช้ Qt 6.9.3 / `win64_msvc2022_64` / `qtmultimedia` งานที่เหลือคือ:
- **ลบ `appveyor.yml`** ที่ซ้ำซ้อน (แก้ล่าสุด `67af990` แต่ Actions ทำงานแทนหมดแล้ว)
- **workflow trigger เป็น `tags: v*` เท่านั้น** → ไม่มี CI บน push/PR เลย ควรเพิ่ม job `build-check` ที่รันบน push/PR (ไม่ต้อง package/release) เพื่อกัน regression ตอนเริ่มงาน P1
- Qt path ถูก hardcode ที่ [`scripts/release_windows.cmd:6`](../scripts/release_windows.cmd) (`C:\Qt\6.9.3\msvc2022_64`) → ทำให้เป็น env var เพื่อไม่ต้องแก้ 2 ที่ตอนอัป Qt
- ดึง matrix/ARM64 จาก `alkl58/master:.github/workflows/build.yml` มาเทียบ

**ต้นทุน:** ~1 วัน · **ความเสี่ยง:** ต่ำ

#### 3.3 แก้จุดอ่อนจริงของ RC auth (ไม่ใช่ "เพิ่ม auth" ตามที่ v1 เข้าใจ)
โค้ดปัจจุบัน (`main_window.cpp:3891`) มี 3 ปัญหา:

```cpp
// ปัญหา 1: ส่ง password ผ่าน command line → เห็นได้ใน Task Manager / ps aux
args << "--rc-pass=" + rcPass;

// ปัญหา 2: modulo bias
int index = QRandomGenerator::global()->generate() % possibleCharacters.length();

// ปัญหา 3: port มาจาก jo->mountRcPort (ผู้ใช้กำหนด) → ชนกันได้ระหว่าง mount หลายตัว
args << "--rc-addr" << "localhost:" + jo->mountRcPort;
```

แก้:
```cpp
// 1. ใช้ bounded() ตัด bias
int index = QRandomGenerator::global()->bounded(possibleCharacters.length());

// 2. ส่ง credential ผ่าน env แทน command line
QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
env.insert("RCLONE_RC_USER", rcUser);
env.insert("RCLONE_RC_PASS", rcPass);
process->setProcessEnvironment(env);
// แล้วลบ --rc-user/--rc-pass ออกจาก args

// 3. ถ้าผู้ใช้ไม่ระบุ port ให้ใช้ :0 แล้วอ่าน port จริงจาก stderr ของ rclone
```

> **สถานะ (ทำจริงแล้ว):** ข้อ 1 และ 2 เสร็จ — สุ่มด้วย `GenerateRcCredential()` ที่ใช้ `bounded()`
> และส่งผ่าน env `RCLONE_RC_USER`/`RCLONE_RC_PASS` (ยืนยันกับ rclone v1.72.1 แล้วว่ารองรับ
> ทั้งฝั่ง server และ client) · `MountWidget` รับ credential ผ่าน constructor แทนการ regex
> ย้อนอ่านจาก args ⇒ **แก้ VIO-3 ไปพร้อมกัน**
>
> **ข้อ 3 (port `:0`) ยังไม่ทำ โดยตั้งใจ** — port มาจากช่องที่ผู้ใช้กรอกในหน้า mount และมี
> `global.usedRcPorts` คอยกันชนอยู่ การเปลี่ยนเป็น `:0` ต้อง parse port จริงจาก output ของ
> rclone ก่อนจึงจะ unmount ได้ ซึ่งกระทบ UI และ flow ของ mount ทั้งชุด
> **ควรทำตอน §3.5 (เปลี่ยนไป `--use-json-log`)** เพราะตอนนั้นจะมี parser ที่เชื่อถือได้อยู่แล้ว
> `mount_widget.cpp:260` parse `--rc-user` กลับจาก args อยู่ — ต้องแก้ให้อ่านจาก member variable แทนเมื่อย้ายไป env

**ต้นทุน:** ~1-2 วัน · **ความเสี่ยง:** กลาง (กระทบ mount flow ทั้งหมด ต้องทดสอบจริง)

> ⚠️ **ข้ออ้าง CVE ใน v1 §2.3.5 ยังไม่ได้ยืนยัน** — `CVE-2026-3006` (WinFsp), `CVE-2026-6210` (Qt SVG), "rclone < 1.74.3 advisory" ทั้งหมดมาจาก CHANGELOG ของ NG ที่ AI เขียน **ห้ามใส่คำเตือนเหล่านี้ใน UI จนกว่าจะเช็คกับ NVD/GitHub Advisory เอง** — การเตือนผิดสร้างความเสียหายต่อความน่าเชื่อถือมากกว่าไม่เตือน

---

### P1 — คุณค่าสูงสุดในระยะยาว

#### 3.4 เปลี่ยน listing เป็น `lsjson` (แก้ที่รากของปัญหาซ้ำซาก)
ข้อนี้ v1 พูดถูกที่สุด และตรงกับประวัติ repo เราเอง — commit `6b6cbe5 rewrite rxProgress for v1.39 - v1.71.2` และ `89d9910 fix label in progress bar over 100%` คือหลักฐานว่า regex parsing แตกทุกครั้งที่ rclone เปลี่ยน output

**สภาพปัจจุบัน:**
- `item_model.cpp:579-591` — spawn **2 process ต่อโฟลเดอร์** (`lsd` + `lsl`) แล้ว regex แยกคอลัมน์
- `job_widget.cpp:138-148` — **10 regex** ครอบ rclone 1.39–1.71 ซ้อนกัน

**เป้าหมาย:** `lsd`+`lsl` → `lsjson` ตัวเดียว (ลด process ลงครึ่ง = เร็วขึ้นทันทีบน remote ที่ latency สูงอย่าง teldrive)

**⚠️ จุดที่ v1 ให้โค้ดผิด:** ตัวอย่าง streaming parser ใน v1 §2.2 มีบั๊ก — `if (depth == 0) m_buffer.clear();` จะล้าง buffer ทั้งก้อนรวมถึง fragment ที่ยังไม่จบ และตัวแปร `inString` ไม่ถูก carry ข้าม chunk ให้ใช้ approach นี้แทน:

```cpp
// เก็บ state ข้าม chunk เป็น member: m_depth, m_inString, m_escaped, m_objStart
void ItemModel::onLsjsonReadyRead() {
  m_buffer += m_process->readAllStandardOutput();
  int consumed = 0;
  for (int i = m_scanPos; i < m_buffer.size(); ++i) {
    char c = m_buffer[i];
    if (m_escaped)      { m_escaped = false; continue; }
    if (c == '\\' && m_inString) { m_escaped = true; continue; }
    if (c == '"')       { m_inString = !m_inString; continue; }
    if (m_inString)     continue;
    if (c == '{')       { if (m_depth++ == 0) m_objStart = i; }
    else if (c == '}' && --m_depth == 0) {
      emitParsedEntry(QJsonDocument::fromJson(
          m_buffer.mid(m_objStart, i - m_objStart + 1)).object());
      consumed = i + 1;
    }
  }
  m_buffer.remove(0, consumed);        // ตัดเฉพาะที่ parse จบแล้ว
  m_scanPos = m_buffer.size();         // ไม่สแกนซ้ำ
  m_objStart -= consumed;
}
```

**สำหรับ teldrive โดยเฉพาะ:** ต้องมี fallback เมื่อ field หาย — teldrive เป็น custom backend อาจไม่ส่ง `Hashes`, `ID`, `MimeType`
```cpp
QString name = obj.value("Name").toString();
if (name.isEmpty()) return;                          // ข้าม entry เสีย ไม่ crash
qint64 size = obj.value("Size").toVariant().toLongLong();   // -1 = unknown
bool isDir  = obj.value("IsDir").toBool(false);
QDateTime mod = QDateTime::fromString(obj.value("ModTime").toString(), Qt::ISODateWithMs);
if (!mod.isValid()) mod = QDateTime();               // teldrive อาจไม่มี ModTime
```

**ต้นทุน:** ~1 สัปดาห์ · **ความเสี่ยง:** สูง — `item_model.cpp` เป็นหัวใจ ต้องเขียน golden-file test ก่อนแตะ (NG มี test ชุดนี้อยู่ ดึงมาได้)

#### 3.5 Progress parsing — **แก้แนวทาง: ใช้ RC API แทน ไม่ใช่ JSON log**

> เปลี่ยนข้อสรุปหลังข้อเสนอของเจ้าของโปรเจกต์ (2026-08-03) — **ข้อเสนอนี้ถูกกว่าแผนเดิม**

### เทียบ 3 ทางเลือก

| | (a) regex บน text (ตอนนี้) | (b) `--use-json-log` (แผนเดิม) | (c) **`--rc` + `core/stats`** |
|---|---|---|---|
| ต้อง parse ไหม | ใช่ 10 regex | ใช่ (JSON lines) | **ไม่ต้องเลย** |
| พังเมื่อ rclone เปลี่ยน output | **พังตลอด** | พังน้อยลง | **ไม่พัง** — RC เป็น API สัญญาไว้ |
| ข้อมูลต่อไฟล์ | `rxProgress` เดาเอา | มี | **มี `transferring[]` พร้อม percentage/speed/eta** |
| ดึงข้อมูลย้อนหลังได้ | ไม่ได้ | ไม่ได้ | **ได้ — query เมื่อไรก็ได้** |
| ป้อน Web UI (§6.3) ได้ | ต้องแปลงอีกชั้น | ต้องแปลงอีกชั้น | **ตรงเลย** |
| ต้องจัดการ port/auth | ไม่ต้อง | ไม่ต้อง | ต้อง — **แต่เราทำ infra เสร็จแล้วใน §3.3** |

### ข้อมูลจริงจาก `core/stats` (rclone 1.72.1)

```json
{ "bytes":0, "checks":0, "deletedDirs":0, "deletes":0, "elapsedTime":0,
  "errors":0, "eta":null, "fatalError":false, "listed":0, "renames":0,
  "serverSideCopies":0, "serverSideCopyBytes":0, "serverSideMoveBytes":0,
  "serverSideMoves":0, "speed":0, "totalBytes":0, "totalChecks":0,
  "totalTransfers":0, "transferTime":0, "transfers":0 }
```

ครอบคลุม **ทุกฟิลด์ที่ 10 regex ปัจจุบันพยายามดึง** (`bytes`/`totalBytes`/`speed`/`eta`/
`checks`/`totalChecks`/`transfers`/`totalTransfers`/`errors`/`elapsedTime`)
และแถม `listed`, `renames`, `serverSide*`, `fatalError`, `retryError` ที่ตอนนี้ไม่มี
ตอนมีงานวิ่งจะมี `transferring[]` ให้ต่อ progress bar รายไฟล์แทน `rxProgress`

⇒ **ลบ regex ได้ทั้ง 10 ตัว และแก้บั๊ก >100% หายไปเองเพราะคำนวณจากตัวเลขจริง**

### สถาปัตยกรรมที่แนะนำ: RC ต่อ job ไม่ใช่ rcd ตัวเดียว

| แบบ | ข้อดี | ข้อเสีย |
|---|---|---|
| **rcd ตัวเดียวรันทุกงาน** (แผนเดิม §2.4.1) | process เดียว, ประหยัด | 🔴 **shared fate** — rcd ตายทีเดียวงานหายหมด · cancel เปลี่ยนเป็น `job/stop` · isolation หาย |
| **`--rc --rc-addr localhost:<free port>` ต่อ job** ⭐ | ยังคง 1 process ต่อ 1 job (isolation เดิม) · kill process เดิมยังใช้ได้ · **ใช้ `GenerateRcCredential`/`UseRcCredentials` ที่มีอยู่แล้ว** | ต้องจัดสรร port (มี `global.usedRcPorts` อยู่แล้ว) |

**เลือกแบบที่สอง** — เปลี่ยนน้อยที่สุด เสี่ยงน้อยที่สุด และได้ผลเท่ากัน

```cpp
// JobRunner (L0) -- แทน readyRead+regex ใน job_widget.cpp
void JobRunner::start() {
  quint16 port = AllocateRcPort();               // ต่อยอด global.usedRcPorts
  mRcUser = GenerateRcCredential(10);            // มีแล้วจาก §3.3
  mRcPass = GenerateRcCredential(22);
  args << "--rc" << "--rc-addr=localhost:" + QString::number(port);
  UseRcCredentials(mProcess, mRcUser, mRcPass);  // มีแล้วจาก §3.3
  mProcess->start(GetRclone(), args);
  mPoll.start(500);                              // ยิง core/stats ทุก 0.5 วิ
}
```
- ยิงผ่าน `QNetworkAccessManager` แบบ async (ห้าม blocking)
- **fallback ต้องคงไว้:** ถ้า RC bind ไม่ได้ ให้กลับไปใช้ regex เดิม — ยังต้องเก็บโค้ดเก่าไว้
- output ดิบยังต้องอ่านต่อ เพราะช่อง log และ §6.1 ต้องใช้

### ผลพลอยได้ที่ใหญ่กว่าตัวฟีเจอร์
`core/stats` คือ **รูปแบบข้อมูลเดียวกับที่ `/api/v1/jobs` ต้องส่งให้ Web UI** (§6.3)
ทำข้อนี้ = ได้ payload ของ API ฟรีไปเลย ไม่ต้องออกแบบใหม่

> ⚠️ **teldrive:** ต้องทดสอบว่า `core/stats` รายงาน `totalBytes` ถูกต้องไหม
> backend ที่ไม่รู้ขนาดล่วงหน้าอาจคืน 0 หรือ -1 → ต้องกัน div-by-zero (ดูโค้ดด้านล่าง)

---

## 3.5-เดิม (เก็บไว้อ้างอิง) `--use-json-log` (ลบ 10 regex)
```cpp
// job_widget.cpp — แทน rxSize/rxSize2/rxSize3/rxChecks{,2,3}/rxTransferred{,2}
QJsonObject stats = obj.value("stats").toObject();
if (!stats.isEmpty()) {
  qint64 bytes = stats.value("bytes").toVariant().toLongLong();
  qint64 total = stats.value("totalBytes").toVariant().toLongLong();
  // total อาจเป็น 0 ตอนเริ่ม หรือ -1 เมื่อ rclone ยังนับไม่จบ → กัน div-by-zero
  int pct = (total > 0) ? qBound(0, int(bytes * 100 / total), 100) : 0;  // แก้บั๊ก >100% ถาวร
}
```
**เก็บ regex เดิมไว้เป็น fallback** สำหรับ rclone เก่ากว่า 1.50 (`--use-json-log` เพิ่มใน 1.50) — ตรวจเวอร์ชันจาก `RcloneCapabilities` ที่จะทำในข้อถัดไป

**ต้นทุน:** ~3 วัน · **ความเสี่ยง:** กลาง

#### 3.6 Backend capability registry
ดึงจาก NG: `src/rclone_capabilities.cpp` / `.h` (MIT, คัดลอกตรงได้)

**ทำไมข้อนี้สำคัญกับเราเป็นพิเศษ:** teldrive เป็น backend นอก mainline → มี capability ไม่ครบ ตอนนี้ผู้ใช้กดปุ่มแล้วเจอ rclone error ดิบๆ

```cpp
// เก็บผลจริงของ teldrive ไว้เป็น test fixture — สำคัญเพราะไม่มี repo ไหนมีข้อมูลนี้
// $ rclone backend features teldrive: > tests/fixtures/teldrive_features.json
ui->publicLinkAction->setEnabled(caps.supportsPublicLink);
ui->storageUsageAction->setEnabled(caps.supportsAbout);   // remote_widget.cpp:1566 เรียก "about" อยู่
ui->dedupeAction->setEnabled(caps.supportsDuplicateFiles);
```
⚠️ ต้องใช้ `QPointer` guard ใน callback (NG เจอ use-after-free จุดนี้มาแล้ว — อยู่ใน CHANGELOG)

**ต้นทุน:** ~2-3 วัน · **ความเสี่ยง:** ต่ำ

##### ผล `rclone backend features teldrive:` ของจริง (เก็บเมื่อ 2026-08-03, rclone v1.72.1)

ข้อมูลชุดนี้ไม่มีใน repo ไหนของระบบนิเวศ — และเปลี่ยนข้อสรุปหลายข้อ:

| Feature | ค่า | ผลกระทบต่อ UI ของเรา |
|---|---|---|
| `Hashes: []` | **ว่างเปล่า** | ⚠️ **สำคัญที่สุด** — teldrive ไม่มี hash เลย `rclone check` จะเทียบได้แค่ size (ต้องใส่ `--size-only` ไม่งั้น check รายงาน error ทุกไฟล์) และ **cryptcheck ใช้ไม่ได้** → ฟีเจอร์ "verify after transfer" ต้อง gate ด้วยเงื่อนไขนี้ |
| `DuplicateFiles: false` | ไม่รองรับ | **ต้องปิดปุ่ม dedupe** — เรามี `dedupe_dialog` ที่ตอนนี้กดได้แล้ว rclone error |
| `ListR: false` | ไม่รองรับ | `lsjson -R` จะ fallback เป็น walk ทีละ dir → cross-remote search จะช้ามาก ควรเตือนผู้ใช้ |
| `CleanUp: false` | ไม่รองรับ | ปิดปุ่ม cleanup |
| `Command: false` | ไม่รองรับ | `rclone backend <cmd> teldrive:` ใช้ไม่ได้ |
| `PutStream: false` | ไม่รองรับ | upload แบบไม่รู้ขนาดล่วงหน้าไม่ได้ |
| `About: true` | ✅ | storage usage ใช้ได้ ([remote_widget.cpp:1566](../src/remote_widget.cpp:1566)) |
| `PublicLink: true` | ✅ | getLink ใช้ได้ |
| `Copy/Move/DirMove/Purge: true` | ✅ | server-side ops ครบ — remote-to-remote transfer ทำได้ |
| `ChangeNotify: true` | ✅ | mount แบบ VFS จะเห็นการเปลี่ยนแปลงจากฝั่ง Telegram |
| `Precision: 1e9` | 1 วินาที | ต้องใช้ `--modify-window 1s` เวลา sync กับ backend ที่ละเอียดกว่า |

> เก็บไฟล์เต็มไว้เป็น fixture ที่ `tests/fixtures/teldrive_features.json` ก่อนเริ่มงาน capability registry

---

### P2 — ฟีเจอร์ที่ควรเพิ่ม (เลือกตามความต้องการจริง)

| ฟีเจอร์ | แหล่ง | เหตุผล / หมายเหตุ |
|---|---|---|
| **Native OS scheduler + `--run-task`** | NG `schedule_manager.cpp` | เรามี in-app scheduler แล้ว **แต่ต้องเปิดแอปค้าง** — ข้อนี้คือส่วนต่างที่แท้จริง NG มี golden test 20+ ตัวสำหรับ XML/systemd/plist ดึงมาด้วย |
| **hosts-file proxy** | ubikore `utils.cpp` | ⭐ **แนะนำสูงสุดสำหรับ teldrive** — แก้ปัญหา DNS/บล็อกที่ผู้ใช้ Telegram เจอบ่อย เป็นของที่ไม่มีใน fork อื่นเลย |
| **Verify after transfer checkbox** | NG | เรามี `check_dialog` แล้ว เหลือแค่เชื่อม checkbox → รัน `check`/`cryptcheck` อัตโนมัติ ต้นทุนต่ำมาก |
| **Bisync + conflict resolve** | NG | ต้อง gate ด้วย capability (rclone ≥ 1.58) + ปุ่ม `--resync` recovery |
| **Dual-pane (QSplitter)** | NG | UX ที่คู่แข่ง (rclone-ui, RClone Manager) มีหมดแล้ว |
| **Typeahead filter บน remote list** | NG | quick win จริง ~ครึ่งวัน |
| **Native Windows file dialog** | ubikore `file_dialog.cpp` | +5 บรรทัด ได้ UX ดีขึ้นทันที |
| **Remote Health panel** | NG | ช่วย debug teldrive/Telegram API มาก |
| **tasks.bin → tasks.json** | NG `job_options_store.cpp` | `list_of_job_options.cpp` เรายังใช้ binary — JSON แก้มือได้ + git-diff ได้ |
| **Pause/resume transfer** | NG | `NtSuspendProcess`/`SIGSTOP` — มีประโยชน์กับ transfer ยาวๆ บน teldrive |

### P3 — ไม่แนะนำ / ตัดทิ้ง
- **RC Engine แบบ long-lived (v1 §2.4.1)** — v1 ให้ priority สูงเกินจริง ประโยชน์คือลด process spawn overhead ซึ่งไม่ใช่คอขวดของเรา (คอขวดคือ Telegram API rate limit) และเพิ่ม failure mode ใหม่ (rcd ตาย = ทุกอย่างพัง) **ทำหลัง P1 เสร็จหมดแล้วเท่านั้น**
- **Merge จาก roshanconnor123 / RyoKamui / downer08tutti** — ยืนยันแล้วว่าไม่มีอะไร

---

## 4. ขั้นตอนเริ่มงาน

```bash
git remote add ng https://github.com/SysAdminDoc/RcloneBrowserNG.git
git remote add alkl58 https://github.com/Alkl58/RcloneBrowser.git
git remote add ubikore https://github.com/ubikore/RcloneBrowser.git
git fetch --all
```

ดูโค้ดที่จะดึงโดยไม่ต้อง checkout:
```bash
git show ng/master:src/rclone_capabilities.cpp
git show ubikore/master:src/utils.cpp | head -400
git diff HEAD alkl58/master -- CMakeLists.txt
```

**license:** ทุก repo เป็น MIT → คัดลอกได้ ขอแค่คง copyright notice เดิมและเพิ่มบรรทัด attribution ใน `NOTICE` หรือหัวไฟล์
(ยกเว้น `RyoKamui` ที่เป็น NOASSERTION — ห้ามแตะ)

---

## 5. ลำดับที่แนะนำให้ลงมือ

```
✅ P0: build hardening + CI build-check job
✅ P0: RC auth ผ่าน env + bounded() + port :0
✅ P1: capability registry + teldrive fixture
✅ P1: lsjson listing
✅ P1: progress จาก RC API (ลบ regex ครบ 12 ตัว ไม่ใช่ 10)
⬜ P2: ยังไม่แตะเลย — hosts proxy (teldrive) → native scheduler → dual-pane
```

> P0/P1 เสร็จหมดแล้ว **แต่ไปทำเฟส 2 ต่อก่อน P2** เพราะเป้าหมายจริงคือ Web UI
> ซึ่งต้องผ่าน E1–E4 ส่วน P2 เป็นฟีเจอร์ที่เพิ่มทีหลังเมื่อไหร่ก็ได้

**จุดยืนของ repo เรา:** จุดขายคือ **teldrive/tgdrive + custom rclone repo UI** (`045ee4f`) ซึ่ง **ไม่มี fork ไหนในระบบนิเวศมี** — งาน P0/P1 ทั้งหมดคือการทำให้ฐานแข็งพอที่จะรองรับจุดขายนั้นได้ยาวๆ ไม่ใช่การไล่ตาม feature count ของ NG

---

# ส่วนที่ 6 — เฟส 2: Logging, Headless, API, Web UI, Autocomplete

> วิเคราะห์เพิ่มเติมตามโจทย์ 4 ข้อ · ทำ **หลังจบ P0–P1** เท่านั้น
> ข้อ 6.1–6.3 ไม่ใช่ 3 ฟีเจอร์แยกกัน แต่เป็น **ฟีเจอร์เดียวกันที่มองจากคนละมุม** — ควรออกแบบรวมกันตั้งแต่แรก

## 6.0 บั๊กความปลอดภัยที่ต้องแก้ก่อน (blocker ของ 6.1 และ 6.3)

พบระหว่างสำรวจโค้ด — **`--rc-pass` รั่วอยู่แล้วในปัจจุบัน**:

```cpp
// job_widget.cpp:18  และ  mount_widget.cpp:20
ui.showOutput->setToolTip(mArgs.join(" "));      // ← แสดง password ใน tooltip

// mount_widget.cpp:182-183  (ปุ่ม "copy rclone command")
clipboard->setText(mArgs.join(" "));             // ← copy password ลง clipboard
```
`mArgs` ของ mount job มี `--rc-user=...` และ `--rc-pass=...` เต็มๆ (สร้างที่ `main_window.cpp:3926`)

**ตอนนี้ผลกระทบจำกัด** (แค่หน้าจอ/clipboard) **แต่พอเพิ่ม log-to-file (6.1) จะกลายเป็น password ค้างถาวรบนดิสก์ และพอเพิ่ม API (6.3) จะกลายเป็น password ส่งออกทางเน็ตเวิร์ก**

```cpp
// utils.cpp — ต้องมีก่อนทำ 6.1/6.3 ทุกกรณี
static const QRegularExpression kSecretArg(
    R"(^--(rc-pass|rc-user|.*-token|.*-key|.*-secret|.*password.*)=)",
    QRegularExpression::CaseInsensitiveOption);

QStringList RedactArgs(const QStringList &args) {
  QStringList out;
  for (const QString &a : args) {
    auto m = kSecretArg.match(a);
    out << (m.hasMatch() ? m.captured(0) + "***REDACTED***" : a);
  }
  return out;
}
```
แล้วเปลี่ยนทุกจุดที่ `mArgs.join(" ")` เป็น `RedactArgs(mArgs).join(" ")`
> ถ้าทำ §3.3 (ย้าย credential ไป env) ไปแล้ว ปัญหานี้หายไปครึ่งหนึ่งโดยอัตโนมัติ — **ยิ่งเป็นเหตุผลให้ทำ §3.3 ก่อน**

---

## 6.1 ระบบเก็บ log เป็นไฟล์

### สภาพปัจจุบัน
| จุด | สภาพ |
|---|---|
| output ของ job | เก็บใน `ui.output` (`QPlainTextEdit`) **ในหน่วยความจำอย่างเดียว** |
| **`job_widget.cpp:143-146`** | `if (++mLines == 10000) { ui.output->clear(); mLines = 1; }` → **ล้าง log ทิ้งทั้งหมดทุก 10,000 บรรทัด** งานยาวๆ = log หายเกลี้ยง |
| log ของตัวแอป | `qInstallMessageHandler` เขียนลง stderr **เฉพาะ Windows** (`main.cpp:10-46`) ไม่มีไฟล์ |
| log ของ job ที่จบแล้ว | หายทันทีที่ปิด job widget |

สรุป: **ไม่มี log ถาวรเลย** — โจทย์นี้ตรงจุดจริง

### ออกแบบ

```cpp
// log_writer.h — เขียนแบบ tee: ลงไฟล์ + ลง UI พร้อมกัน
class JobLogWriter {
public:
  JobLogWriter(const QString &jobId, const QStringList &args);
  void appendLine(const QString &line);   // ทุกบรรทัดที่อ่านจาก QProcess
  QString filePath() const { return mFile.fileName(); }
private:
  QFile mFile;
  QTextStream mStream;
  qint64 mBytes = 0;
};
```

**จุดที่ต้องตัดสินใจ:**

| ประเด็น | ข้อเสนอ |
|---|---|
| ตำแหน่งไฟล์ | `QStandardPaths::AppDataLocation/logs/` · โหมด portable → `<exe>/logs/` (repo เรามีแนวคิด portable อยู่แล้ว) |
| ชื่อไฟล์ | `<yyyyMMdd-HHmmss>-<op>-<jobid>.log` — เรียงตามเวลาได้เอง |
| header | เขียน `RedactArgs(mArgs)` + rclone version + timestamp ไว้บรรทัดแรก (§6.0 **บังคับ**) |
| rotation | ต่อ job อยู่แล้ว → ไม่ต้อง rotate ระหว่างทาง แต่ต้อง **cap ขนาดต่อไฟล์** (เช่น 50 MB แล้วตัด พร้อมเขียน `... truncated`) กัน sync ล้านไฟล์กินดิสก์ |
| retention | preference: เก็บ N วัน / N ไฟล์ / ไม่จำกัด — ลบตอน startup |
| UI | ปุ่ม "Open log" ต่อ job + เมนู Help > Open log folder |
| ปัญหา 10,000 บรรทัด | เมื่อมีไฟล์แล้ว การ clear UI ไม่ทำให้ข้อมูลหาย — แต่ควรเปลี่ยนเป็น **ring buffer** (`setMaximumBlockCount(10000)`) แทน `clear()` เพื่อไม่ให้จอกระพริบว่างเปล่า |

```cpp
// job_widget.cpp — แก้ 2 บรรทัด ได้พฤติกรรมที่ถูกต้อง
ui.output->setMaximumBlockCount(10000);   // ตั้งครั้งเดียวใน constructor
// แล้วลบ if (++mLines == 10000) { ui.output->clear(); ... } ทิ้งทั้งบล็อก
```

### ⚠️ ลำดับที่สำคัญ: ทำ **หลัง** §3.5 (JSON progress log)
ถ้าทำหลัง §3.5 log ที่ได้จะเป็น **JSONL** ที่มีโครงสร้าง — parse ได้ กรองตาม level ได้ และ **ส่งให้ web UI แสดงผลได้ทันที (6.3/6.4)**
ถ้าทำก่อน จะได้ text ดิบที่ต้อง regex ซ้ำอีกรอบ = ทำงานสองครั้ง

**ต้นทุน:** ~3-4 วัน (รวม §6.0) · **ความเสี่ยง:** ต่ำ · **มูลค่า:** สูง (เป็นฐานของ 6.3 ด้วย)

---

## 6.2 Docker / Web — วิเคราะห์ของเดิม

### `totza2010/rclonebrowser-docker` ทำอะไรอยู่
`FROM jlesage/baseimage-gui:alpine-3.16-v4` → รัน X11 + openbox + **noVNC** เสิร์ฟที่พอร์ต 5800
คือการ **สตรีมพิกเซลของ GUI เดิมผ่านเบราว์เซอร์** ไม่ใช่เว็บแอปจริง

### ข้อดี
- ได้ฟีเจอร์ครบ 100% ทันทีโดยไม่ต้องเขียนโค้ดใหม่เลย
- ใช้ `tgdrive/rclone` fork อยู่แล้ว → teldrive ใช้ได้ใน container

### ข้อเสีย (เรียงตามความรุนแรง)
| ปัญหา | รายละเอียด |
|---|---|
| 🔴 **Alpine 3.16 EOL** | หมดอายุตั้งแต่ พ.ค. 2024 — ไม่มี security update แล้ว (`jlesage` มี base image v5 บน Alpine ใหม่กว่า) |
| 🔴 **build ไม่ reproducible** | `git clone https://github.com/totza2010/RcloneBrowser.git /tmp` **ไม่ pin commit/tag** → build วันนี้กับพรุ่งนี้ได้คนละ binary |
| 🔴 **clone ทับ WORKDIR** | `WORKDIR /tmp` แล้ว `git clone ... /tmp` — เปราะมาก พังทันทีถ้ามีไฟล์ค้าง |
| 🟠 **compose แนะนำ root** | `USER_ID=0 / GROUP_ID=0` + `VNC_PASSWORD=password` เป็นค่าเริ่มต้นในตัวอย่าง |
| 🟠 **noVNC ใช้จริงลำบาก** | กิน CPU ฝั่ง server, ใช้บนมือถือแทบไม่ได้, ไม่มี API, automate ไม่ได้, clipboard/drag-drop พิการ |
| 🟡 **VERSION ซ้ำ 2 repo** | ต้องแก้ 2 ที่ทุกรอบ release, ไม่มีอะไรบังคับให้ตรงกัน |
| 🟡 **rclone pin ที่ 1.71.0** | ล้าหลังของที่ใช้จริง (1.72.1) |

### ควรรวมเข้า repo นี้ไหม? — **ควร และทำได้เลย**
เหตุผล: VERSION แหล่งเดียว, CI ที่ trigger ด้วย tag `v*` อยู่แล้วสามารถ build image ในรอบเดียวกัน, Dockerfile จะ `COPY . /src` แทน `git clone` (แก้ทั้งปัญหา pin และ WORKDIR ในทีเดียว)

```dockerfile
# docker/Dockerfile ใน repo นี้ — build จาก source ในคอนเท็กซ์ ไม่ต้อง clone
FROM alpine:3.21 AS build
RUN apk add --no-cache build-base cmake qt6-qtbase-dev qt6-qtmultimedia-dev
COPY . /src
RUN cmake -S /src -B /build -DCMAKE_BUILD_TYPE=Release && cmake --build /build -j$(nproc)

FROM jlesage/baseimage-gui:alpine-3.21-v5
ARG RCLONE_VERSION=1.72.1
COPY --from=build /build/build/rclone-browser /usr/bin/
...
```
> multi-stage ทำให้ image เล็กลงมาก (ไม่มี toolchain ติดมา) และ `COPY . /src` ทำให้ image ตรงกับ commit ที่ build เสมอ

**ต้นทุน (แค่ย้าย+ปรับปรุง):** ~2 วัน · **ความเสี่ยง:** ต่ำ

### แต่ noVNC คือทางตัน — คำตอบระยะยาวอยู่ที่ 6.3

> **อัปเดตหลังตัดสินใจทิศทาง (ดู §6.3):** เมื่อเป้าหมายคือ Web UI จริงแบบ qBittorrent
> Docker image จะไม่ต้องมี X11/openbox/noVNC/xterm เลย → เหลือแค่ binary + rclone
> **ขนาด image ลดลงหลายร้อย MB และตัด attack surface ของ VNC ทิ้งทั้งหมด**
> ดังนั้น §6.2 ควรทำแค่ "ย้าย Dockerfile เข้า repo + pin + non-root" ไปก่อน
> **อย่าเพิ่งลงแรงปรับปรุง noVNC เพราะมันจะถูกลบทิ้งทั้งหมดใน E4**

---

## 6.3 API / เว็บจริง — ตอบโจทย์ "ดูสถานะออนไลน์ + สั่งงาน + trigger"

### ทำไม Qt WebEngine ถึงไปต่อไม่ได้ (สาเหตุที่งานค้าง)
**QtWebEngine = ยัด Chromium เข้าไปในแอป** (ราว 150 MB) เพื่อให้แอปเดสก์ท็อป *แสดงเว็บ* ได้
แต่สิ่งที่โจทย์ต้องการคือ **ทิศทางตรงกันข้าม** — ให้ *คนอื่นเข้าถึงแอปเราจากเน็ตเวิร์ก*
QtWebEngine แก้ปัญหานี้ไม่ได้เลยแม้แต่น้อย จึงไม่แปลกที่ทำไปแล้วรู้สึกว่าไม่ไปไหน **ตัดทิ้งได้เลย ไม่ต้องเสียดาย**

### สิ่งที่ต้องการจริงคือ headless core + HTTP API + web client บาง

```
┌──────────────────────────────────────────┐
│  JobEngine  (ไม่มี GUI)                   │
│  - spawn/จัดคิว rclone process            │
│  - task store (tasks.json)                │
│  - scheduler (qcron ที่มีอยู่)             │
│  - JobLogWriter (6.1)                     │
└────────┬───────────────┬──────────────────┘
         │               │
   ┌─────▼─────┐   ┌─────▼──────────┐
   │ Qt GUI    │   │ HTTP API       │
   │ (ที่มีอยู่) │   │ (QHttpServer)  │
   └───────────┘   └─────┬──────────┘
                         │
                   ┌─────▼──────┐
                   │ web UI     │  ← แทน noVNC
                   │ (static)   │
                   └────────────┘
```

### อุปสรรคหลัก: logic ผูกกับ GUI แน่นมาก
`main_window.cpp` = **5,281 บรรทัด** และมีทั้งการสร้าง args (`main_window.cpp:3880-3930`), การ spawn process, การจัดคิว — **รันงานโดยไม่เปิดหน้าต่างไม่ได้เลยตอนนี้** นี่คือต้นทุนจริงของฟีเจอร์นี้ ไม่ใช่ตัว HTTP server

### แผนแบ่งเป็น 4 ขั้น (แต่ละขั้นมีของส่งมอบใช้ได้จริง)

| ขั้น | งาน | ได้อะไร | ต้นทุน |
|---|---|---|---|
| **E1** | `--run-task <name>` / `--list-tasks` แบบ headless (= P2 เดิม) | บังคับให้แยก logic ชุดแรกออกจาก GUI + ได้ native scheduler ใช้งานจริงไปด้วย | ~1 สัปดาห์ |
| **E2** | แยก `JobEngine` ออกจาก `main_window` | GUI กลายเป็น client ตัวหนึ่ง, ทดสอบด้วย QTest ได้ | ~2-3 สัปดาห์ ⚠️ ก้อนใหญ่สุด |
| **E3** | HTTP JSON API บน `QHttpServer` | ดูสถานะ/trigger จากที่ไหนก็ได้, ต่อ Home Assistant / n8n / cron ภายนอกได้ | ~1-2 สัปดาห์ |
| **E4** | web UI static เสิร์ฟจาก API | **เลิกใช้ noVNC** — ใช้บนมือถือได้จริง | ~2 สัปดาห์ |

### ความพร้อมด้านเทคนิค (ตรวจแล้ว)
- `qthttpserver` **มีให้ติดตั้งผ่าน aqt สำหรับ Qt 6.9.3** (`python -m aqt install-qt ... -m qthttpserver qtwebsockets`)
- ⚠️ ต้องเช็ค distro package ก่อนคอมมิต: Debian/Ubuntu = `qt6-httpserver-dev`, Alpine อาจไม่มี → **ทำเป็น optional component** เหมือนที่แนะนำกับ Multimedia ใน §2.1
  ```cmake
  find_package(Qt6 QUIET OPTIONAL_COMPONENTS HttpServer)
  if(Qt6HttpServer_FOUND)
    target_compile_definitions(RcloneBrowser PRIVATE RB_HTTP_API)
  endif()
  ```
- **ทางเลือกถ้า packaging มีปัญหา:** เขียนเองบน `QTcpServer` (อยู่ใน Qt Network ที่เป็น dependency อยู่แล้ว) — API เล็กๆ ~10 endpoint ใช้ราว 300 บรรทัด ไม่เพิ่ม dependency เลย

### 🔴 ความปลอดภัยเป็นเงื่อนไขบังคับ ไม่ใช่ของแถม
**แอปนี้ spawn process ด้วย argument ที่ผู้ใช้กำหนดได้ → API ที่ไม่มี auth = RCE เต็มรูปแบบ**

- bind `127.0.0.1` เป็นค่าเริ่มต้น การเปิด `0.0.0.0` ต้องเป็น opt-in ที่มีคำเตือนชัดเจน
- bearer token สร้างตอน first run เก็บด้วย `protectSecret()` ตัวเดียวกับ §2.3.3
- **ห้ามให้ API รับ rclone flag ดิบจากภายนอกเด็ดขาด** — ให้ trigger ได้เฉพาะ *saved task ที่มีอยู่แล้ว* เท่านั้น (allowlist by name) ข้อนี้สำคัญที่สุด
- ปิด CORS เป็นค่าเริ่มต้น
- response ทุกตัวผ่าน `RedactArgs()` (§6.0)
- ถ้าจะเปิดออกอินเทอร์เน็ต → บอกให้ใช้ reverse proxy + TLS อย่าให้แอปทำ TLS เอง

```
GET  /api/v1/status                 → version, rclone version, uptime, mount ที่ active
GET  /api/v1/remotes                → รายชื่อ remote + capability (จาก §3.6)
GET  /api/v1/jobs                   → job ที่รันอยู่ + progress (จาก JSON stats §3.5)
GET  /api/v1/jobs/{id}/log          → SSE stream จาก JobLogWriter (§6.1)
POST /api/v1/jobs/{id}/stop
GET  /api/v1/tasks                  → saved task ทั้งหมด
POST /api/v1/tasks/{name}/run       → trigger  ← โจทย์หลักของข้อนี้
GET  /api/v1/mounts  POST /api/v1/mounts/{name}/{mount|unmount}
```

### ✅ คำถามเชิงกลยุทธ์ — ตอบแล้ว (2026-08-03)

**ทิศทางที่เลือก: โมเดล qBittorrent** — RcloneBrowser ทั้งก้อนรันเป็น daemon ใน Docker แล้วเปิด Web UI แดชบอร์ดที่ **สั่งงานได้เหมือนนั่งหน้าโปรแกรม**

ข้อกังวลเรื่อง "ซ้ำกับ `rclone rcd`" **ตกไป** เพราะขอบเขตไม่ใช่การ proxy คำสั่ง rclone แต่คือการเปิด **ชั้นของ RcloneBrowser เอง** ที่ `rclone rcd` ไม่มี:

| ชั้น | `rclone rcd` มีไหม | เรามี |
|---|---|---|
| saved tasks + แก้ไข/รันซ้ำ | ❌ | ✅ `list_of_job_options` |
| คิวงาน + จัดลำดับ | ❌ | ✅ queue ใน `main_window` |
| scheduler (cron) | ❌ | ✅ `qcron*` + `scheduler_widget` |
| ประวัติ job + log ย้อนหลัง | ❌ | ✅ หลัง §6.1 |
| จัดการ mount หลายตัวพร้อมกัน | บางส่วน | ✅ `mount_widget` |
| teldrive | ❌ | ✅ จุดขายเรา |

⇒ **E2–E4 อยู่ในขอบเขตแน่นอน ไม่ต้องตัดสินใจซ้ำ**

### โมเดล qBittorrent แม็ปกับเราอย่างไร

| qBittorrent | RcloneBrowser |
|---|---|
| `qbittorrent` (GUI) | `RcloneBrowser` — build ปัจจุบัน |
| **`qbittorrent-nox`** (headless, ไม่ลิงก์ QtWidgets เลย) | **`rclone-browser-nox`** — build ใหม่ด้วย `-DNO_GUI=ON` |
| core เดียวกันทั้งคู่ (`base/`) | `JobEngine` (E2) |
| Web UI เสิร์ฟจากตัวโปรแกรม, static ฝังใน Qt resource | เหมือนกัน — ฝังใน `resources.qrc` ที่มีอยู่แล้ว |
| REST API `/api/v2/...` | `/api/v1/...` |
| GUI มีสวิตช์ "Web UI" ใน Options | เหมือนกัน — เปิด API จาก Preferences ได้บนเดสก์ท็อป |

**จุดสำคัญที่ต้องยึด:** `-DNO_GUI=ON` แปลว่า **`JobEngine` ห้ามพึ่ง QtWidgets เด็ดขาด** — ต้องใช้แค่ Qt Core/Network เท่านั้น นี่คือข้อจำกัดที่ควบคุมการ refactor ทั้งหมด และเป็นตัววัดว่าแยกสำเร็จหรือยัง (ถ้า `-DNO_GUI=ON` คอมไพล์ผ่าน = แยกสำเร็จ)

### 🟢 ผลตรวจความเป็นไปได้จริง — ง่ายกว่าที่ประเมินไว้มาก

ตรวจ dependency ของไฟล์ที่จะเป็น core แล้วพบว่า **ส่วนใหญ่เป็น Core-only อยู่แล้ว**:

| ไฟล์ | บรรทัด | dependency | สรุป |
|---|---|---|---|
| `qcron.cpp` `qcronfield.cpp` `qcronnode.cpp` | 722 | `QDateTime QObject QList QString QTimer` | 🟢 **Core-only อยู่แล้ว** ยกมาได้ทันที |
| `global.h` | — | `QList` | 🟢 Core-only |
| `list_of_job_options.cpp` | 271 | `QDataStream` + `job_options.h` | 🟢 Core-only ทันทีที่แก้ `job_options.h` |
| `job_options.h/.cpp` | 355 | `QListWidget` ← **ตัวเดียว** | 🟡 include นี้มีไว้ให้ `JobOptionsListWidgetItem` ที่ [`job_options.h:108`](../src/job_options.h:108) เท่านั้น — **แยกคลาสนั้นออกเป็นไฟล์ต่างหาก แล้ว `JobOptions` เป็น Core ทันที (~15 นาที)** |
| `utils.h/.cpp` | 426 | `QRadioButton QCheckBox QComboBox QSpinBox QLineEdit QPlainTextEdit` | 🟡 แต่ **มีแค่ 2 ฟังก์ชันที่ใช้จริง** คือ `ReadSettings`/`WriteSettings(QSettings*, QObject*)` — อีก 12 ฟังก์ชัน (`GetRclone`, `GetRcloneConf`, `GetRcloneCmd`, `UseRclonePassword`, `IsPortableMode`, `GetConfigDir`, `compareVersion`, ...) เป็น Core ล้วน → แยกเป็น `utils.cpp` + `widget_settings.cpp` |
| `pch.h` | — | `<QtGui> <QMessageBox> <QPushButton>` | 🟡 ต้องมี PCH แยกสำหรับ core target |

**แปลว่า scheduler engine, task store, และ rclone invocation layer แยกได้เกือบฟรี** — งานหนักจริงเหลือแค่ 2 จุด:

1. **`MainWindow::runItem()`** [`main_window.cpp:3702-3988`](../src/main_window.cpp:3702) (~286 บรรทัด) — เป็นตัวประกอบ args + dispatch ทั้งหมด **นี่คือหัวใจที่ต้องดึงออกมา**
2. **progress parsing อยู่ใน `job_widget.cpp`** (ซึ่งเป็น widget) — แต่ **§3.5 จะเขียนส่วนนี้ใหม่อยู่แล้ว** ⇒ **ทำ §3.5 กับการดึง parser ออกมาเป็น `JobRunner` (Core) ในรอบเดียวกัน อย่าทำสองรอบ**

ที่เหลือของ `main_window.cpp` (~4,000 บรรทัด) เป็น UI แท้ๆ — tab, ปุ่ม, tray, sort — **ไม่ต้องแตะ**

⇒ **ปรับประมาณการ E2 ลงจาก 2-3 สัปดาห์ เหลือ ~1.5-2 สัปดาห์** และความเสี่ยงลดลงมาก เพราะไม่ใช่การรื้อ 5,281 บรรทัด แต่เป็นการดึงออกมาราว 1,200 บรรทัดที่ระบุตำแหน่งได้ชัด

### โครงสร้างเป้าหมาย

```
src/core/          ← ลิงก์แค่ Qt Core + Network  (ทดสอบด้วย QTest ได้เต็มที่)
  job_options.*        (ย้าย JobOptionsListWidgetItem ออก)
  list_of_job_options.*
  qcron*.*
  utils.*              (ตัดส่วน widget ออก)
  job_engine.*         ← ใหม่: คิว + spawn + lifecycle
  job_runner.*         ← ใหม่: 1 process + JSON progress (§3.5) + JobLogWriter (§6.1)
  rclone_capabilities.*  (§3.6)
  api_server.*         ← ใหม่ (E3)
src/gui/           ← โค้ดเดิมทั้งหมด เป็น client ของ core
src/web/           ← static assets ฝังใน resources.qrc (E4)
```

```cmake
option(NO_GUI "Build headless daemon only (rclone-browser-nox)" OFF)
add_library(rbcore STATIC ${CORE_SOURCES})
target_link_libraries(rbcore PUBLIC Qt6::Core Qt6::Network)   # ← ห้ามมี Qt6::Widgets

if(NO_GUI)
  add_executable(rclone-browser-nox src/core/main_nox.cpp)
  target_link_libraries(rclone-browser-nox PRIVATE rbcore)
else()
  add_executable(RcloneBrowser ${GUI_SOURCES})
  target_link_libraries(RcloneBrowser PRIVATE rbcore Qt6::Widgets)
endif()
```

### Docker หลัง E4 — ไม่มี X11 อีกต่อไป

```dockerfile
FROM alpine:3.21 AS build
RUN apk add --no-cache build-base cmake qt6-qtbase-dev
COPY . /src
RUN cmake -S /src -B /build -DNO_GUI=ON -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /build -j$(nproc)

FROM alpine:3.21
ARG RCLONE_VERSION=1.72.1
RUN apk add --no-cache qt6-qtbase fuse3 ca-certificates \
    && adduser -D -u 1000 rb                       # ← ไม่รันเป็น root
COPY --from=build /build/rclone-browser-nox /usr/bin/
COPY --from=... /usr/bin/rclone /usr/bin/
USER rb
EXPOSE 8080
VOLUME ["/config"]
ENTRYPOINT ["rclone-browser-nox", "--config-dir=/config", "--api-addr=0.0.0.0:8080"]
```
ไม่มี `xterm`, `openbox`, `dbus`, `qt6-qtbase-x11`, noVNC, `qt6-qtmultimedia` → image เล็กลงมาก
> ⚠️ `rclone mount` ใน container ต้องมี `--cap-add SYS_ADMIN --device /dev/fuse` และควรใช้ `--security-opt apparmor:unconfined` — ต้องเขียนไว้ใน README ให้ชัด

### API — ออกแบบตามชั้นของเรา ไม่ใช่ proxy rclone

```
# สถานะ (แดชบอร์ด)
GET  /api/v1/status              → app/rclone version, uptime, จำนวน job/mount ที่ active
GET  /api/v1/jobs                → job ที่รันอยู่: progress %, speed, ETA, ไฟล์ปัจจุบัน (จาก §3.5)
GET  /api/v1/jobs/{id}/log       → SSE stream สดจาก JobLogWriter (§6.1)
GET  /api/v1/history             → job ที่จบแล้ว + exit code
GET  /api/v1/remotes             → remote + capability (§3.6)
GET  /api/v1/mounts

# สั่งงาน
POST /api/v1/tasks/{name}/run    → trigger saved task   ← โจทย์หลัก
POST /api/v1/tasks/{name}/dry-run
POST /api/v1/jobs/{id}/stop
POST /api/v1/queue/reorder
POST /api/v1/mounts/{name}/mount | /unmount

# เรียกดู/แก้ไขงาน (เท่าที่จำเป็นสำหรับแดชบอร์ด)
GET|PUT /api/v1/tasks[/{name}]
GET  /api/v1/schedules
```

**Web UI (E4):** static HTML/JS ฝังใน `resources.qrc` ไม่ใช้ framework หนัก — โพลล์ `/jobs` ทุก 1-2 วิ + SSE สำหรับ log ก็พอสำหรับแดชบอร์ด (qBittorrent ก็ใช้แนวนี้) **ไม่ต้องใช้ QtWebEngine เลยแม้แต่นิดเดียว**

### 🔴 ความปลอดภัย — เข้มกว่าเดิมเพราะตอนนี้ตั้งใจเปิดออกเน็ตเวิร์ก

แอปนี้ spawn process ด้วย argument ที่ผู้ใช้กำหนดได้ ⇒ **API ที่หลุด = RCE** และตอนนี้ไม่ใช่แค่ loopback แล้ว ต้องทำครบทุกข้อ:

- [ ] **auth บังคับเสมอ** — ไม่มีโหมด no-auth แม้บน loopback; บังคับตั้ง password ตอน first run (qBittorrent เคยมีปัญหาจาก default credential — อย่าเดินซ้ำรอย)
- [ ] **ตรวจ `Host` header** ให้ตรงกับ allowlist → กัน **DNS rebinding** (เว็บภายนอกสั่ง API ใน LAN ของเหยื่อได้ถ้าไม่ตรวจ) ข้อนี้คนลืมบ่อยที่สุด
- [ ] **CSRF token** + ตรวจ `Origin`/`Referer` บนทุก request ที่เปลี่ยนสถานะ
- [ ] `X-Frame-Options: DENY` + CSP → กัน clickjacking
- [ ] **ห้ามรับ rclone flag ดิบจาก API เด็ดขาด** — trigger ได้เฉพาะ task ที่ saved ไว้แล้ว (allowlist by name) นี่คือกำแพงสุดท้ายที่กัน RCE ต่อให้ auth หลุด
- [ ] rate limit + ban ชั่วคราวเมื่อ login ผิดซ้ำ
- [ ] response ทุกตัวผ่าน `RedactArgs()` (§6.0)
- [ ] เก็บ password ด้วย PBKDF2/Argon2 ไม่ใช่ plaintext/base64
- [ ] **ไม่ทำ TLS เอง** — บอกให้ใช้ reverse proxy (Caddy/nginx/Traefik) ใน README

### ปรับประมาณการ E1–E4

| ขั้น | งาน | ต้นทุน | หมายเหตุ |
|---|---|---|---|
| **E1** | `--run-task` / `--list-tasks` headless | ~4-5 วัน | ทำ `job_options` split + `utils` split ในขั้นนี้เลย |
| **E2** | `rbcore` + `JobEngine` + `-DNO_GUI=ON` | ~1.5-2 สัปดาห์ | ⚠️ ทำรวมกับ §3.5 · เกณฑ์ผ่าน = `-DNO_GUI=ON` คอมไพล์ได้ |
| **E3** | HTTP API + auth + security checklist | ~1.5-2 สัปดาห์ | security กินเวลาพอๆ กับตัว API |
| **E4** | Web UI static + Docker nox | ~2-3 สัปดาห์ | ปลด noVNC ทิ้ง |

รวม ~6-8 สัปดาห์ **แต่ E1 และ E2 มีของส่งมอบใช้ได้จริงในตัวเอง** (headless task runner + native scheduler) ต่อให้หยุดกลางทางก็ไม่เสียเปล่า

---

## 6.4 Autocomplete ในการตั้งค่า

**สภาพปัจจุบัน: ไม่มี `QCompleter` ในโค้ดเลยแม้แต่ตัวเดียว** — เป็นงาน greenfield ทั้งหมด

เรียงตาม (คุณค่า ÷ ต้นทุน):

| # | เป้าหมาย | วิธี | สถานะ |
|---|---|---|---|
| 1 | **ช่องโฟลเดอร์ในเครื่อง** — `defaultDownloadDir`, `defaultUploadDir` | `QCompleter` + `QFileSystemModel` | ✅ **เสร็จ** · ครอบช่อง path ใน transfer/mount ด้วย |
| 2 | **ช่อง rclone options** ทั้งใน Preferences และ transfer/mount dialog | parse `rclone help flags` | ✅ **เสร็จ** — `rclone_flags.*` (L0) + `completers.*` (L3) |
| 3 | **ชื่อ remote** ใน transfer/mount dialog | มีรายชื่ออยู่ในหน่วยความจำแล้ว | ⬜ **เหลืออยู่ข้อเดียว** ~3 ชม. |
| 4 | **path ปลายทางบน remote** | `lsjson` ของ dir แม่ + debounce | ⬜ ~3 วัน |
| 5 | **mount point** | Windows: drive letter ว่าง · Unix: `QFileSystemModel` | 🟡 ฝั่ง Unix เสร็จ · Windows ยังไม่ทำ |

> **ที่ประเมินไว้ผิดในข้อ 2:** นับได้ 1,081 flag ตอนวางแผน ของจริงคือ **1,078 flag ที่ parse ได้ครบ**
> แต่ regex ที่ร่างไว้ในแผน (`(--[a-z0-9][a-z0-9-]*)`) อ่านได้แค่ชื่อ ไม่รู้ว่า flag ไหนต้องมีค่า
> และตกไป 2 ตัวที่ชื่อ type มีเว้นวรรค — ของจริงต้องอ่าน type กับคำอธิบายด้วย ดู V-14

```cpp
// #1 — quick win จริงๆ
auto *fsModel = new QFileSystemModel(this);
fsModel->setRootPath("");
fsModel->setFilter(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Drives);
auto *c = new QCompleter(fsModel, this);
c->setCaseSensitivity(Qt::CaseInsensitive);
ui.defaultDownloadDir->setCompleter(c);
ui.defaultUploadDir->setCompleter(c);
```

```cpp
// #2 — ดึง flag จาก rclone ที่ผู้ใช้ตั้งไว้จริง แล้ว cache
//     สำคัญ: ห้าม hardcode รายการ flag เพราะเราใช้ rclone fork ของ tgdrive
//     ซึ่งมี flag ของ teldrive เพิ่มมาที่ mainline ไม่มี → ต้อง query จาก binary จริง
//     cache key = rclone version string (เชื่อมกับ RcloneCapabilities §3.6)
QStringList ParseRcloneFlags(const QByteArray &helpOutput) {
  static const QRegularExpression rx(R"(^\s+(?:-\w,\s+)?(--[a-z0-9][a-z0-9-]*))");
  QStringList flags;
  for (const QByteArray &l : helpOutput.split('\n')) {
    auto m = rx.match(QString::fromUtf8(l));
    if (m.hasMatch()) flags << m.captured(1);
  }
  flags.removeDuplicates();
  return flags;
}
// ช่อง options มีหลาย flag ในบรรทัดเดียว → ต้องใช้ completer แบบ multi-token
completer->setCompletionMode(QCompleter::PopupCompletion);
// override QLineEdit ให้ complete เฉพาะ token หลังช่องว่างสุดท้าย
```

**ต้นทุนรวม #1+#2+#3:** ~3 วัน ได้ผลลัพธ์ที่ผู้ใช้รู้สึกได้ทันที
**หมายเหตุ:** ถ้าทำ §3.6 (capability registry) ไปแล้ว จะมีที่เก็บ cache ต่อ rclone binary อยู่แล้ว → #2 ถูกลงมาก

---

## 6.6 Mount script — เขียนในหน้าต่างได้เลย

**สภาพปัจจุบัน:** [`mount_dialog.ui:651`](../src/mount_dialog.ui) มีแค่ `QLineEdit` + ปุ่ม browse
ผู้ใช้ต้องไปสร้างไฟล์ script เองข้างนอก ตั้ง exec bit เอง แล้วค่อยกลับมาเลือกไฟล์
([`mount_dialog.cpp:229`](../src/mount_dialog.cpp) ปฏิเสธไฟล์ที่ไม่ใช่ executable)

**สิ่งที่ต้องเพิ่ม:**

| ส่วน | รายละเอียด |
|---|---|
| ปุ่ม **Edit / New** ข้างปุ่ม browse | เปิด dialog ที่มี `QPlainTextEdit` |
| เขียนไฟล์ให้อัตโนมัติ | `GetConfigDir()/mount-scripts/<taskName>.<sh\|cmd>` |
| ตั้ง exec bit | `QFile::setPermissions(... ExeOwner)` บน unix — แก้ปัญหาที่ผู้ใช้เจอบ่อย |
| template ตั้งต้น | เติม header ที่อธิบาย `$1..$5` ให้เลย ตามที่ tooltip บอกไว้ที่ `mount_dialog.ui:763` |
| syntax ขั้นต่ำ | monospace font + ไม่ต้องมี highlighter ก็พอในรอบแรก |
| ยังต้องเลือกไฟล์ข้างนอกได้ | ปุ่ม browse เดิมคงไว้ ไม่บังคับให้ทุกคนใช้ editor |

```cpp
// argument ที่ script ได้รับ (ตาม mount_dialog.ui:763) -- ใส่เป็น comment ใน template
// $1 = rclone executable   $2 = RC port   $3 = RC user
// $4 = RC password         $5 = mount point
```
> ⚠️ script ได้รับ **RC password เป็น argument** ⇒ อยู่ในขอบเขต §6.0 ด้วย
> ตอนทำ log ไฟล์ (§6.1) ต้องไม่บันทึก argument ของ script process ลงไฟล์แบบดิบ

**ต้นทุน:** ~1-2 วัน · **ความเสี่ยง:** ต่ำ (เพิ่มของใหม่ ไม่แตะทางเดิม)

---

## 6.7 ปรับการแสดงผลของการ์ด job

> เสนอหลังทดสอบ §3.5 จริง (2026-08-04) — ทั้งหมดเป็น **presentation layer**
> ไม่แตะ logic ที่ทดสอบผ่านแล้ว จึงเสี่ยงต่ำ
>
> หลายข้อทำได้เพราะ `core/stats` ให้ข้อมูลที่ regex เดิมไม่เคยมี

### สภาพปัจจุบัน
ตรวจ `job_widget.ui` แล้ว: การ์ดมี `progress_info` ที่เป็น **`QLabel`** แสดงข้อความ `(0%)`
และ `progress` (`QFormLayout`) สำหรับแถบรายไฟล์ — **ไม่มี `QProgressBar` ระดับทั้งงานเลย**

| # | งาน | ทำไม | ต้นทุน |
|---|---|---|---|
| **1** | **แถบ progress รวมทั้งงาน** ⭐ | ต้องอ่านตัวเลขเล็กๆ ในหัวการ์ด ทั้งที่มี `bytes`/`totalBytes` แม่นอยู่แล้ว | ~1 ชม. |
| **2** | ETA อ่านออก + ซ่อนตอนยังเชื่อไม่ได้ | `5329h27m12s` = 222 วัน · จาก log จริง ETA แกว่ง `11w1d10h` → `27w4d13h` ใน 30 วิ แล้วหัวการ์ดโชว์ "Finished (ETA): 15 มีนาคม 2570" ซึ่งทำให้เข้าใจผิด | ~2 ชม. |
| **3** | บอกสถานะช่วง scan | ช่วงที่ `totalBytes == 0` การ์ดนิ่งสนิท (จริง 10 วิ ในเทสต์) ดูเหมือนค้าง — ใช้ `listed` แสดง "Scanning… N items" | ~2 ชม. |
| **6** | ข้อความแถบรายไฟล์ | `0% of 11.3 GiB` ค้างนานดูเหมือนตาย → `12.4 MiB of 11.3 GiB` | ~30 นาที |
| 4 | ใช้ฟิลด์ที่ยังทิ้งไว้ | `fatalError`/`retryError` → สีเตือน · `renames`/`deletes` → สำคัญกับ `sync` · `serverSideCopies` → บอกว่าไม่กินแบนด์วิดท์ · `speedAvg` นิ่งกว่า `speed` | ~3 ชม. |
| 5 | ตัดช่องซ้ำ | `Transferred: X / Y` กับ `Total size: Y` ซ้ำกัน — เอาที่ว่างไปใส่ข้อ 4 | ~1 ชม. |
| 7 | Sparkline ความเร็ว | สวยแต่ไม่ได้แก้ปัญหา ทำหลังสุด | ~1 วัน |

**ชุดที่แนะนำ: 1 + 2 + 3 + 6** (~1 วัน) ได้ผลที่รู้สึกได้ทันที

---

## 6.8 เก็บทุกอย่างลงฐานข้อมูล — รวมประวัติงานที่รันไปแล้ว

> เพิ่มเข้าแผน 2026-08-07 · ระบบ **S13** ใน [`API.md` §12](API.md)

### ปัญหา

**ปิดโปรแกรมแล้วประวัติงานหายหมด** — ไม่มีที่ไหนเก็บเลยว่างานไหนเคยรัน เมื่อไหร่ ผลเป็นอย่างไร
โอนไปกี่ไบต์ ใช้เวลาเท่าไหร่ การ์ดใน UI คือที่เดียวที่ข้อมูลนั้นเคยมีอยู่ และมันอยู่ในหน่วยความจำล้วนๆ

สภาพจริงตอนนี้แยกเป็นสามแบบ:

| ข้อมูล | เก็บที่ไหน | รอดตอนปิดโปรแกรมไหม |
|---|---|---|
| Task | `tasks.bin` (QDataStream binary v8) | ✅ |
| Queue | `queue.conf` (`uniqueId,requestId` บรรทัดละคู่) | ✅ |
| Scheduler | `scheduler.conf` (`key,value` คั่น comma ค่าเป็น base64) | ✅ |
| **ประวัติการรัน** | **ไม่มีที่ไหนเลย** | ❌ **หายทั้งหมด** |
| Log ของแต่ละงาน | `logs/<เวลา>-<operation>-<id ย่อ>.log` | ✅ แต่**ไม่มีอะไรผูกกับ task** |

ข้อสุดท้ายคือหัวใจ: ไฟล์ log **ไม่ได้หาย** แต่ไม่มีอะไรบอกว่าไฟล์ไหนมาจาก task ไหน รันเมื่อไหร่
สำเร็จหรือไม่ — ต้องเปิดอ่านทีละไฟล์ (ซึ่งตรงกับ `REVISIT` ที่มาร์คไว้ใน `job_log.cpp` พอดี)

### รูปแบบไฟล์ทั้งสามแบบเป็นของทำมือคนละแบบ

`.bin` ผูกกับลำดับฟิลด์ · `.conf` สองไฟล์เป็นรูปแบบที่เขียนเองไม่มี schema version
ทุกครั้งที่เพิ่มฟิลด์ต้องแก้ตัวอ่าน/เขียนเองทั้งหมด และ **ไม่มีอันไหนรองรับการค้นหา**
ซึ่งเป็นสิ่งที่ประวัติต้องการเป็นอย่างแรก ("เดือนที่แล้ว task นี้ fail กี่ครั้ง")

### เลือก SQLite ผ่าน QtSql

**ตรวจแล้วว่ามีจริงทั้งสองแพลตฟอร์ม** ไม่ต้องเพิ่ม dependency จากข้างนอก:

| | |
|---|---|
| Windows (Qt 6.9.3 msvc2022_64) | `plugins/sqldrivers/qsqlite.dll` มาพร้อม Qt |
| Alpine (คอนเทนเนอร์) | แพ็กเกจ `qt6-qtbase-sqlite` มีใน repo |

`Qt6::Sql` **ไม่ใช่ GUI** จึงลิงก์เข้า `rbcore` ได้โดยไม่เสียกฎข้อ 1 ของ
[`ARCHITECTURE.md`](ARCHITECTURE.md) — แต่ต้องบันทึกเพิ่มในทะเบียนว่า core ลิงก์อะไรได้บ้าง
เพราะปัจจุบันเขียนไว้ว่า "แค่ `Qt6::Core` และ `Qt6::Network`"

ทางเลือกอื่นที่พิจารณาแล้วไม่เอา:

| ทางเลือก | ทำไมไม่ |
|---|---|
| JSON หลายไฟล์ | แก้ปัญหา schema แต่ยังค้นหาไม่ได้ และเขียนพร้อมกันสองโปรเซสไม่ปลอดภัย |
| ฝัง `sqlite3.c` เอง | ได้ควบคุมเต็มที่ แต่เพิ่มโค้ดที่ต้องดูแล ทั้งที่ Qt ให้มาแล้ว |
| เซิร์ฟเวอร์ DB (Postgres) | เกินความจำเป็นมากสำหรับแอปเดสก์ท็อป และเพิ่มของที่ต้องติดตั้ง |

### ⚠️ ข้อจำกัดที่ต้องออกแบบตั้งแต่ต้น: สองโปรเซสเขียนพร้อมกัน

**E1 จงใจไม่ใส่ single-instance lock ให้ `--run-task`** เพื่อให้สั่งงานจาก cron ได้ขณะเปิดหน้าต่างอยู่
แปลว่า **หน้าต่างกับ CLI จะเขียนไฟล์ DB เดียวกันพร้อมกัน** ซึ่งเป็นสิ่งที่ต้องออกแบบรับ ไม่ใช่ไปเจอทีหลัง:

- เปิด **WAL mode** (`PRAGMA journal_mode=WAL`) ให้อ่านและเขียนพร้อมกันได้
- ตั้ง **busy timeout** ไม่ใช่ปล่อยให้ล้มทันทีเมื่อชนกัน
- transaction ต้องสั้น — อย่าเปิดคาไว้ตลอดอายุงานที่รันเป็นชั่วโมง
- แต่ละ `QThread` ต้องมี connection ของตัวเอง (ข้อบังคับของ QtSql)

### Log: เก็บไฟล์ไว้เหมือนเดิม แต่ทำดัชนีใน DB

**ไม่ย้ายเนื้อ log เข้า DB** เหตุผล:

- งานที่กำลังวิ่งเขียน log ต่อเนื่อง — เขียนลงไฟล์แล้ว tail ง่ายกว่าและถูกกว่า append เข้า row
- API จะต้องส่ง log แบบ follow (SSE) ซึ่งอ่านจากไฟล์ตรงๆ ได้เลย
- log ของงานใหญ่เป็นหลายสิบ MB ยัดลง DB ทำให้ไฟล์ DB บวมและ backup ยาก

สิ่งที่ DB เก็บคือ **ตัวชี้**: `log_path` `log_bytes` ผูกกับ `job_run` — ซึ่งแก้ปัญหาจริง
(หาไม่เจอว่าไฟล์ไหนของงานไหน) โดยไม่ย้ายข้อมูลที่ไม่ควรย้าย
**นี่คือคำตอบของ `REVISIT` เรื่องชื่อไฟล์ log ด้วย** — เมื่อมีดัชนีแล้ว ชื่อไฟล์ไม่ต้องอธิบายตัวเอง

### เค้าโครงตาราง (ร่าง)

```sql
CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT);   -- schema_version

-- Task: เก็บ 46 ฟิลด์เป็น JSON หนึ่งก้อน แล้วดึงเฉพาะที่ต้องค้นออกมาเป็นคอลัมน์
-- 46 คอลัมน์จะพังทุกครั้งที่เพิ่มฟิลด์ ซึ่งเป็นปัญหาเดียวกับ tasks.bin
CREATE TABLE task (
  id          TEXT PRIMARY KEY,      -- uniqueId
  name        TEXT NOT NULL,         -- description
  operation   TEXT NOT NULL,
  source      TEXT, dest TEXT,
  options     TEXT NOT NULL,         -- JSON ของ JobOptions ทั้งก้อน
  created_at  INTEGER, updated_at INTEGER
);

-- ประวัติการรัน -- ของที่ตอนนี้หายทุกครั้งที่ปิดโปรแกรม
CREATE TABLE job_run (
  request_id   TEXT PRIMARY KEY,
  task_id      TEXT REFERENCES task(id) ON DELETE SET NULL,
  task_name    TEXT,                 -- ชื่อ ณ ตอนรัน task อาจถูกลบไปแล้ว
  kind         TEXT,                 -- transfer | mount | stream
  transfer_mode TEXT,                -- task | queue | scheduler | autostart
  started_at   INTEGER NOT NULL,
  finished_at  INTEGER,
  state        TEXT,                 -- running | finished | error | stopped
  exit_code    INTEGER,
  bytes        INTEGER, total_bytes INTEGER,
  transfers    INTEGER, errors INTEGER,
  log_path     TEXT, log_bytes INTEGER
);
CREATE INDEX job_run_by_task ON job_run(task_id, started_at DESC);
CREATE INDEX job_run_by_time ON job_run(started_at DESC);

CREATE TABLE queue_entry (
  request_id TEXT PRIMARY KEY,
  task_id    TEXT NOT NULL,
  position   INTEGER NOT NULL,
  dry_run    INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE schedule (
  id TEXT PRIMARY KEY, name TEXT, task_id TEXT,
  active INTEGER, rule TEXT,          -- JSON: daily/cron
  last_run INTEGER, last_finished INTEGER, last_status TEXT
);
```

### การย้ายข้อมูลเดิม

ต้องอ่านของเก่าได้ครบก่อนถือว่าเสร็จ · **ห้ามลบไฟล์เดิม** ให้เปลี่ยนชื่อเป็น `.migrated`
เพื่อให้ย้อนกลับได้ถ้าเจอปัญหา

```
tasks.bin      → task
queue.conf     → queue_entry
scheduler.conf → schedule
logs/*.log     → job_run (เท่าที่เดาได้จากชื่อไฟล์ -- ของเก่าจะขาดข้อมูลบางส่วน)
```

`tests/test_task_store.cpp` มี golden file v8 อยู่แล้ว ใช้เป็นฐานของ test การย้ายได้ทันที

### การตัดข้อมูลเก่า

ประวัติโตไม่มีที่สิ้นสุด ต้องมีนโยบายตั้งแต่แรก ไม่ใช่รอจนไฟล์บวม:
เก็บ N วัน หรือ N รายการล่าสุด (ตั้งค่าได้) · ลบแถวแล้ว**ลบไฟล์ log ที่ผูกอยู่ด้วย**
มิฉะนั้นโฟลเดอร์ `logs/` จะกลายเป็นขยะที่ไม่มีดัชนีอีกครั้ง

### ได้อะไรกลับมา

- **ประวัติไม่หาย** — เปิดโปรแกรมมาเห็นว่าเมื่อคืนงานไหนสำเร็จ งานไหนล้ม
- แท็บ/หน้า **History** ที่ค้นและกรองได้
- log เปิดจากประวัติได้ตรงๆ ไม่ต้องเดาจากชื่อไฟล์
- `/api/v1/jobs?history=true` เขียนได้โดยไม่ต้องมีกลไกเก็บของตัวเอง
- schema version เดียวแทนรูปแบบไฟล์ทำมือสามแบบ

### เสร็จเมื่อ

1. ปิดแล้วเปิดโปรแกรมใหม่ → ประวัติงานที่รันไปแล้วยังอยู่ครบ พร้อมลิงก์ไปไฟล์ log
2. `tasks.bin` / `queue.conf` / `scheduler.conf` เดิมถูกย้ายเข้า DB ครบ **โดยไม่สูญหาย**
3. รัน `--run-task` จาก CLI ขณะเปิดหน้าต่างอยู่ → **ทั้งสองเขียนประวัติได้ ไม่ชนกัน**
4. test ที่ลิงก์แค่ `rbcore` ครอบ: สร้าง/อ่าน/ย้ายข้อมูล/เขียนพร้อมกันสองการเชื่อมต่อ

---

## 6.5 ลำดับที่แนะนำสำหรับเฟส 2

```
✅ ก่อนอื่น  §6.0 RedactArgs()                        blocker ของ 6.1 และ 6.3
✅          §6.1 log เป็นไฟล์
✅          §6.2 ย้าย Dockerfile เข้า repo
✅          §6.7 การแสดงผลการ์ด job
✅          §6.4 autocomplete (เหลือ #3 ชื่อ remote)
✅          §6.3 E1 headless --run-task              (S1 ใน API.md §12)
⬜ ถัดไป    §6.3 E2 rbcore + JobEngine     ~1.5-2 สัปดาห์  = S2 JobRegistry
⬜          §6.3 E3 HTTP API + security    ~1.5-2 สัปดาห์
⬜          §6.3 E4 Web UI + Docker nox    ~2-3 สัปดาห์  → ปลด noVNC ทิ้ง
```

> **ที่แผนบอกว่าต้องทำ §6.1 เป็น JSONL แล้วไม่ได้ทำ:** log ที่เขียนลงไฟล์เป็นข้อความดิบตามที่
> rclone พิมพ์ออกมา เพราะ §3.5 ย้ายตัวเลขไป `core/stats` หมดแล้ว ช่อง output จึงเหลือแต่ log จริง
> ที่คนอ่าน ไม่ใช่ stat ที่ต้อง parse — การบังคับให้เป็น JSON จะทำให้อ่านด้วยตาไม่ได้โดยไม่ได้อะไรกลับมา

**สิ่งที่ควรทำก่อนแตะข้อไหนก็ตามในเฟส 2:** QTest ต้องมีแล้ว (§4 ของ Dev-Setup)
✅ **มีแล้ว 8 ชุด** และ `rbcore` ก็มีจริงแล้วโดยไม่ต้องรอ E2 — ลิงก์แค่ `Qt6::Core`/`Qt6::Network`
ทำให้ทดสอบ core ได้เต็มที่โดยไม่ต้องเปิดหน้าต่างตั้งแต่ตอนนี้

### เกณฑ์วัดความคืบหน้าที่ชัดเจน (ใช้แทนการเดา)
```
E1 ผ่านเมื่อ:  rclone-browser --run-task "X" ทำงานจบและคืน exit code ถูกต้องโดยไม่เปิดหน้าต่าง
E2 ผ่านเมื่อ:  cmake -DNO_GUI=ON คอมไพล์ผ่าน และ rbcore ไม่ลิงก์ Qt6::Widgets เลย
E3 ผ่านเมื่อ:  curl -H "Authorization: Bearer ..." /api/v1/jobs คืน progress สดของงานที่รันอยู่
E4 ผ่านเมื่อ:  docker compose up แล้วเปิดเบราว์เซอร์สั่ง task ได้ โดย image ไม่มี X11/noVNC
```
