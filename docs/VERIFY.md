# รายการทดสอบ (Verification Checklist)

> ผู้ทดสอบกรอกช่อง **ผล** และ **ผู้ทดสอบ/วันที่** เอง
> ค่าที่ใส่ได้: `PASS` · `FAIL` · `N/A` (ทดสอบไม่ได้บนแพลตฟอร์มนี้)
> ถ้า `FAIL` ให้เขียนอาการจริงต่อท้ายในคอลัมน์หมายเหตุ **อย่าลบบรรทัดทิ้ง**
>
> รายการนี้คู่กับ `// TEST:` marker ในโค้ด — ดูตำแหน่งจริงด้วย:
> ```bash
> python scripts/check_layers.py --markers
> ```

## สภาพแวดล้อมที่ใช้ทดสอบ

| | |
|---|---|
| OS | |
| Qt | |
| rclone | |
| remote ที่ใช้ | |
| commit | |

---

## รอบที่ 1 — P0 + quick wins (2026-08-03)

### V-01 · Build hardening ลงในไบนารีจริง

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.9.3/msvc2022_64"
cmake --build build --config Release --parallel
```
แล้วตรวจ characteristics ของ exe:
```bash
dumpbin /headers build/build/Release/RcloneBrowser.exe
```
**เกณฑ์ผ่าน:** ต้องเห็นครบทั้ง 5 บรรทัด — `Dynamic base`, `NX compatible`, `Control Flow Guard`, `CET compatible`, `High Entropy Virtual Addresses`

| ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|
| | | |

---

### V-02 · Multimedia เป็น optional จริง

ตอน configure ต้องเห็นบรรทัดสถานะ:
```
-- Qt6 Multimedia found - transfer notification sound enabled
```
จากนั้นทดสอบเคสไม่มี Multimedia (บน Linux ที่ไม่ได้ลง `qt6-multimedia-dev` หรือชี้ `CMAKE_PREFIX_PATH` ไป Qt kit ที่ไม่มี):
```
-- Qt6 Multimedia not found - building without notification sound
```
**เกณฑ์ผ่าน:** ทั้งสองกรณี build ผ่าน · กรณีมี Multimedia แล้วเปิด "notify finished transfers" ต้องได้ยินเสียงตอนงานจบ

| ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|
| | | |

---

### V-03 · `RedactArgs()` ปิดบัง secret ใน argument

marker: `utils.cpp` (ฟังก์ชัน `RedactArgs`)

> ปรับหลัง V-08 — RC credential ไม่อยู่ใน args แล้ว จึงต้องทดสอบด้วย token ที่ผู้ใช้ใส่เอง

1. Transfer dialog → ช่อง rclone options ใส่ `--drive-token=SECRET123`
2. เริ่มงาน → hover ปุ่มลูกศรแสดง output ของการ์ด
3. กดปุ่ม **copy** แล้ววางใน Notepad
4. ทำซ้ำกับการ์ด **mount** และ **stream** (ใส่ option เดียวกันในหน้าที่เกี่ยวข้อง)

**เกณฑ์ผ่าน:** ทั้ง tooltip และ clipboard ต้องแสดง `--drive-token=***`
ห้ามเห็น `SECRET123` · argument อื่น (`--config`, path, `--transfers`) ต้องยังเห็นครบตามเดิม

| จุด | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|
| transfer — tooltip | | | |
| transfer — clipboard | | | |
| mount — tooltip | | | |
| mount — clipboard | | | |
| stream — tooltip | | | |
| stream — clipboard | | | |

---

### V-04 · log ไม่หายทั้งก้อนอีกแล้ว

marker: `job_widget.cpp` (ใกล้ `setMaximumBlockCount`)

1. copy โฟลเดอร์ที่มีไฟล์ **เกิน 10,000 ไฟล์** (หรือใส่ `-v` / `-vv` ให้ output เยอะพอ)
2. กางช่อง output ค้างไว้ระหว่างงานวิ่ง
3. สังเกตตอนที่บรรทัดสะสมเกิน 10,000

**เกณฑ์ผ่าน:** ต้อง**ไม่มี**จังหวะที่ช่อง output ว่างเปล่าทั้งหมด (พฤติกรรมเดิมคือล้างเกลี้ยง)
บรรทัดเก่าสุดค่อยๆ หายไปทีละบรรทัดจากด้านบนแทน · เลื่อน scroll ขึ้นต้องยังเห็นประวัติย้อนหลังได้

| ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|
| | | |

---

### V-05 · การแยกชั้นไม่ทำให้ฟังก์ชันเดิมพัง

การแยก `job_options` / `utils` เป็นการย้ายโค้ด ไม่ควรเปลี่ยนพฤติกรรมใดๆ
ทดสอบ regression ของเส้นทางที่แตะไฟล์เหล่านั้น:

| กรณี | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|
| เปิดแอป → รายการ remote ขึ้นครบ | | | |
| Preferences → แก้ค่า → OK → ปิดเปิดแอป → ค่ายังอยู่ | | | |
| Transfer dialog → ตั้งค่า → Save task | | | |
| Tasks → รัน task ที่ save ไว้ | | | |
| Tasks → แก้ task เดิม → ค่าที่เคยตั้งยังอยู่ครบ | | | |
| Mount dialog → ค่าที่เคยตั้งยังอยู่ | | | |
| Export dialog → export list สำเร็จ | | | |
| Scheduler → ตั้งเวลา → เด้งทำงานตามกำหนด | | | |
| โหมด portable (มีไฟล์ `.ini` ข้าง exe) → ยังอ่านค่าจาก ini | | | |

> ✅ **ข้อ `tasks.bin` อัตโนมัติแล้ว** — `tests/test_task_store.cpp` โหลด golden file v8
> แล้วตรวจ 30 ฟิลด์ทีละตัว รวมถึง persist/forget แล้วอ่านไฟล์ซ้ำ
> รันด้วย `ctest --test-dir build -C Release` · ที่เหลือในตารางยังต้องทดสอบด้วยมือ

---

### V-06 · ขอบเขตชั้นยังไม่รั่ว

```bash
python scripts/check_layers.py --check
```
**เกณฑ์ผ่าน:** ลงท้ายด้วย `No boundary violations.` และ exit code = 0

| ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|
| | | |

---

### V-07 · CI ทำงานบน push/PR

`.github/workflows/ci.yml` เพิ่มใหม่ เพราะเดิม `build.yml` fire เฉพาะ tag `v*`
ทำให้ push ธรรมดาไม่มีอะไรตรวจเลย

1. push branch ใดก็ได้ (ที่ไม่ใช่ tag) ขึ้น GitHub
2. เปิดแท็บ **Actions**

**เกณฑ์ผ่าน:** workflow **CI** ต้องขึ้นมาและผ่านครบ 4 job —
`Layer boundaries`, `Build (Linux)`, `Build (Linux, no Multimedia)`, `Build (Windows)`
และ **`Build Rclone Browser` (build.yml) ต้องไม่ถูก trigger** (สงวนไว้ให้ tag `v*` เท่านั้น)

| job | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|
| Layer boundaries | PASS | totza2010 / 2026-08-04 | |
| Build (Linux) + ctest | PASS | totza2010 / 2026-08-04 | รอบแรก **FAIL** — `test_task_store` ไม่เข้า portable mode บน Linux |
| Build (Linux, no Multimedia) | PASS | totza2010 / 2026-08-04 | ผ่านตั้งแต่รอบแรก |
| Build (Windows) + ctest + ตรวจ hardening | PASS | totza2010 / 2026-08-04 | รอบแรก **FAIL** — runner ไม่มี VS 2022 แล้ว |
| build.yml ไม่ถูก trigger | PASS | totza2010 / 2026-08-04 | มีแต่ workflow `CI` ขึ้นมา |

**รอบที่ 2 ผ่านครบทั้ง 4 job** — run `30930106541` บน branch `modernize/rc-progress-and-hardening`

---

### V-08 · RC credential ย้ายไป environment แล้ว

marker: `utils.cpp` (ฟังก์ชัน `GenerateRcCredential`)

**นี่คือรายการที่เสี่ยงที่สุดของรอบนี้** — ถ้าพลาด mount จะ unmount ไม่ได้

1. Mount remote โดย**ระบุ RC port** (เช่น `5572`) ในหน้า mount
2. ระหว่าง mount ทำงาน เปิด Task Manager → Details → คลิกขวาหัวตาราง → เลือกคอลัมน์ **Command line**
   หา process `rclone.exe`
   (Linux/macOS ใช้ `ps aux | grep rclone`)
3. กด **unmount** ที่การ์ด
4. ถ้าตั้ง mount script ไว้ ให้ตรวจว่า script ได้รับ user/password ถูกต้อง

**เกณฑ์ผ่าน:**

| # | เกณฑ์ | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|---|
| 1 | command line ของ `rclone` **ไม่มี** `--rc-user` และ `--rc-pass` แล้ว (ต้องยังมี `--rc` และ `--rc-addr`) | PASS | totza2010 / 2026-08-03 | `rclone.exe mount tgdrive_main_02: R: --rc --rc-addr localhost:5555` — ไม่มี credential |
| 1b | RC endpoint **ยังบังคับ auth** (ไม่ใช่ credential หายไปเฉยๆ) | PASS | totza2010 / 2026-08-03 | `POST http://127.0.0.1:5555/core/version` ไม่ส่ง credential → HTTP 401 |
| 2 | mount สำเร็จ เข้าถึงไฟล์ผ่าน drive/โฟลเดอร์ที่ mount ได้ | PASS | totza2010 / 2026-08-03 | `tgdrive_main_02:` → `R:` · RC เสิร์ฟที่ `127.0.0.1:5555` |
| 3 | **unmount สำเร็จ** ไม่ค้าง ไม่ขึ้น error | PASS | totza2010 / 2026-08-03 | process `rclone.exe` หายไปเรียบร้อยหลัง unmount |
| 4 | mount script (ถ้าใช้) ได้รับ user/password และทำงานได้ | ⏳ รอ | | ยังไม่ได้ทดสอบด้วย mount script |
| 5 | mount โดย**ไม่**ระบุ RC port → ยังทำงานปกติ | ⏳ รอ | | |
| 6 | mount task ที่ save ไว้**ก่อน**อัปเดต → รันแล้วยัง mount/unmount ได้ | ⏳ รอ | | |

ยืนยันข้อ 1 แบบไม่ต้องอ่านจากหน้าต่างที่ตัดข้อความ (รันตอน mount กำลังทำงาน):

```bash
powershell -c "(Get-CimInstance Win32_Process -Filter \"Name='rclone.exe'\").CommandLine"
```

**ข้อสังเกตจากรอบทดสอบ:** `NOTICE: Serving remote control on http://127.0.0.1:5555/`
ยืนยันว่า RC เปิดจริงและ auth มาจาก env — ถ้า rclone ไม่ได้รับ credential จาก
`RCLONE_RC_USER`/`RCLONE_RC_PASS` การ unmount ผ่าน `core/quit` จะได้ HTTP 401 และ
mount จะค้าง ซึ่ง**ไม่เกิดขึ้น** จึงเป็นหลักฐานทางอ้อมว่าเส้นทาง env ทำงานถูกต้อง

> ข้อ 6: ตรวจแล้วว่า `tasks.bin` เก็บเป็น field ของ `JobOptions` (รวมถึง `mountRcPort`)
> แล้ว args ถูกประกอบใหม่ทุกครั้งที่รันใน `runItem()` — credential เดิมก็ถูกสุ่มใหม่ทุกรอบ
> จึง**ไม่มี** credential เก่าค้างในไฟล์ task ไม่ต้องเขียน migration
> ยกเว้นกรณีที่ผู้ใช้พิมพ์ `--rc-user=` ลงช่อง extra options เอง ซึ่งอยู่นอกขอบเขตรายการนี้

---

## รอบที่ 2 — Capability registry (2026-08-03)

### V-09 · ปุ่มถูก gate ตามความสามารถจริงของ backend

marker: `rclone_capabilities.cpp` · ข้อมูลอ้างอิง: [`tests/fixtures/teldrive_features.json`](../tests/fixtures/teldrive_features.json)

เปิดแท็บ remote แล้วรอ 1-2 วินาที (query ทำงานแบบ async) จากนั้นดูปุ่มบนแถบเครื่องมือ

**เกณฑ์ผ่าน — teldrive** (`DuplicateFiles: false`, `CleanUp: false`, `About: true`, `PublicLink: true`, `Hashes: []`):

| # | เกณฑ์ | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|---|
| 1 | ปุ่ม **Dedupe** ถูกซ่อน/ปิด | | | |
| 2 | ปุ่ม **Cleanup** ถูกปิด และ status tip บอกว่า backend ไม่รองรับ | | | |
| 3 | ปุ่ม **Link** ยังใช้ได้ (กดแล้วได้ลิงก์จริง) | | | |
| 4 | ปุ่ม **Info** ยังใช้ได้ (แสดง quota) | | | |
| 5 | status tip ของ **Check** เตือนว่า backend ไม่มี hash | | | |

**เกณฑ์ผ่าน — Google Drive** (ต่างจาก teldrive):

| # | เกณฑ์ | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|---|
| 6 | ปุ่ม **Dedupe** ใช้ได้ตามเดิม | | | |
| 7 | ปุ่ม **Cleanup** ใช้ได้ตามเดิม | | | |

**เกณฑ์ผ่าน — กรณีผิดปกติ (สำคัญ):**

| # | เกณฑ์ | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|---|
| 8 | remote ที่เชื่อมต่อไม่ได้ (ปิดเน็ต/token หมดอายุ) → ปุ่ม**ไม่ถูกปิด** ยังกดได้ตามปกติ | | | |
| 9 | เปิดแท็บแล้วปิดทันทีก่อน query จบ → ไม่ crash | | | |

> ⚠️ ข้อ 8 คือหลักการที่ยึด: **query ล้มเหลวต้องไม่ปิดปุ่ม** ถ้า probe พังแล้วปุ่มหายหมด
> จะแย่กว่าไม่มีฟีเจอร์นี้เลย

---

### V-10 · เขียน mount script ในหน้าต่างได้เลย

marker: `script_editor_dialog.cpp`

เดิมต้องไปสร้างไฟล์เอง ตั้ง exec bit เอง แล้วค่อยกลับมากด browse

1. Mount dialog → กดปุ่ม **Edit...** ข้างช่อง "Script to run after mount"
2. ดู template ที่เติมมาให้ → แก้เนื้อหา → ตั้งชื่อ → **Save**
3. ดูช่อง path ในหน้า mount
4. กด **Edit...** อีกครั้ง
5. Mount จริงโดยเลือก script นี้

| # | เกณฑ์ | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|---|
| 1 | template ที่ขึ้นมาอธิบาย `$1..$5` ครบ | | | |
| 2 | ไฟล์ถูกเขียนที่ `<config dir>/mount-scripts/<ชื่อ>.cmd` (Windows) หรือ `.sh` (unix) | | | |
| 3 | ช่อง path ในหน้า mount ถูกเติมให้อัตโนมัติ | | | |
| 4 | RC port ถูกตั้งให้เองถ้ายังว่าง (unix) | | | |
| 5 | **unix เท่านั้น:** ไฟล์มี exec bit (`ls -l`) และปุ่ม browse ยอมรับไฟล์นี้ | | | |
| 6 | กด **Edit...** ซ้ำ → โหลดเนื้อหาเดิมขึ้นมาแก้ได้ ไม่ใช่ template เปล่า | | | |
| 7 | mount จริงแล้ว script ทำงาน (ดูช่อง script output ในการ์ด mount) | | | |
| 8 | ปุ่ม **browse** เดิมยังใช้เลือกไฟล์จากที่อื่นได้ตามปกติ | | | |
| 9 | ใส่ชื่อที่มี `/` `\` `:` `*` `?` `"` `<` `>` `\|` `.` → ถูกปฏิเสธพร้อมข้อความ | | | |

> ⚠️ ข้อ 9 เป็น path traversal guard — ถ้าชื่อหลุด `..` ไปได้จะเขียนไฟล์นอกโฟลเดอร์ที่ตั้งใจ

---

## รอบที่ 3 — Progress จาก RC API (2026-08-03)

### V-11 · progress อ่านจาก `core/stats` แทน regex

marker: `job_stats.h` · fixture: [`tests/fixtures/core_stats_active.json`](../tests/fixtures/core_stats_active.json)

transfer job จะเปิด `--rc --rc-addr=localhost:0` แล้ว `JobWidget` อ่านตัวเลขจาก RC
ส่วน regex เดิมยังอยู่เป็น fallback

1. Copy โฟลเดอร์ใหญ่ (หลาย GB, หลายไฟล์) ไป/จาก remote
2. กางการ์ด job ดูค่าระหว่างวิ่ง
3. กางช่อง output ดูบรรทัด `Serving remote control on http://127.0.0.1:PORT/`

| # | เกณฑ์ | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|---|
| 1 | Size / Total size ขยับตามจริง | | | |
| 2 | Speed ขยับตามจริง | | | |
| 3 | ETA แสดงเวลา ไม่ใช่ `-` ค้าง (หลัง rclone ประเมินได้) | | | |
| 4 | Checks / Transferred ขยับ | | | |
| 5 | Elapsed เดินตามเวลาจริง | | | |
| 6 | **% ไม่เกิน 100 ตลอดงาน** (บั๊กเดิมที่เคยแก้ซ้ำๆ) | | | |
| 7 | แถบ progress รายไฟล์ขึ้นครบตามจำนวนไฟล์ที่วิ่งพร้อมกัน (`--transfers`) | | | |
| 8 | ไฟล์ที่ทรานเฟอร์เสร็จ แถบหายไปเอง ไม่ค้าง | | | |
| 9 | กด Cancel แล้วงานหยุด ไม่มี process ค้าง | | | |
| 10 | teldrive: ค่าที่รายงานสมเหตุสมผล (backend ไม่มี hash และอาจไม่รู้ขนาดล่วงหน้า) | | | |

**ช่อง output ต้องเป็น log จริง ไม่ใช่ stat:**

| # | เกณฑ์ | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|---|
| 11 | ช่อง output **ไม่มี** บล็อก `Transferred: ... / Elapsed time: ... / Transferring:` โผล่ทุกวินาทีอีกแล้ว | PASS | totza2010 / 2026-08-04 | |
| 12 | ใส่ `-vv` หรือ `-vvv` ในช่อง rclone options → เห็น DEBUG log จริงไหลมา ไม่ถูก stat กลบ | PASS | totza2010 / 2026-08-04 | เห็น `Uploading segment`, `quickxor ... OK`, `Copied (new)` ครบ |
| 12b | log ของ polling ตัวเอง (`rc: "core/stats"`) ถูกกรองออก ไม่ท่วม `-vvv` | PASS | totza2010 / 2026-08-04 | รอบแรก **FAIL** — poll ทุก 0.5 วิ ทำให้ท่วม แก้ด้วย `IsRcPollingNoise()` + poll 1 วิ · ยืนยันซ้ำกับงาน 4 ไฟล์พร้อมกันไป teldrive |

**กรณี RC เปิดไม่ได้ (ไม่มี fallback แล้ว):**

| # | เกณฑ์ | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|---|
| 13 | ทำให้ RC ต่อไม่ติด → การ์ดขึ้น **"(no progress info)"** สีส้ม พร้อม tooltip อธิบาย | | | |
| 14 | ในกรณีนั้น **งานยังทำงานต่อจนจบตามปกติ** (ตรวจไฟล์ปลายทาง) | | | |

> ⚠️ regex ถูกลบทั้งหมดแล้ว ถ้า RC ล้ม จะไม่มีตัวเลข progress — แต่งานต้องไม่พัง
> `RcClient` ยอมแพ้หลังต่อไม่ติด 6 ครั้งติดแล้วส่ง `unavailable`

---

### V-12 · credential ไม่โผล่ในช่อง output ตอน `-vv`

marker: `utils.cpp` (ฟังก์ชัน `RedactOutputLine`)

**พบตอนทดสอบ `--stats 0` กับ `-vv`** — rclone พิมพ์ค่าที่อ่านจาก environment ออกมาตรงๆ:

```
DEBUG : Setting --rc-pass "sw8Kd2" from environment variable RCLONE_RC_PASS="sw8Kd2"
```

1. รัน transfer โดยใส่ `-vv` ในช่อง rclone options
2. กางช่อง output หาบรรทัด `RCLONE_RC_PASS` / `RCLONE_RC_USER`

| # | เกณฑ์ | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|---|
| 1 | บรรทัดนั้นแสดง `***` แทนค่าจริง **ทั้งสองตำแหน่งในบรรทัด** | PASS | totza2010 / 2026-08-04 | `Setting --rc-pass "***" from environment variable RCLONE_RC_PASS="***"` |
| 2 | ส่วนที่เหลือของบรรทัดยังอ่านได้ (`DEBUG`, `RCLONE_RC_PASS`) | PASS | totza2010 / 2026-08-04 | |
| 3 | บรรทัด log อื่นไม่ถูกแก้ | PASS | totza2010 / 2026-08-04 | |

---

## รอบที่ 4 — การแสดงผลการ์ด job (2026-08-05)

### V-13 · การ์ดบอกสถานะจริง ไม่ใช่ตัวเลขที่เดาเอา

marker: `job_widget.cpp` (`applyStats`) · L0: `JobStats::phase()` / `progressText()`

เดิมการ์ดมีแค่ป้าย `(42%)` ตัวเล็กๆ และ % จะค้าง 0 อยู่พักหนึ่งแล้วกระโดด
เพราะช่วงแรก rclone ยังนับไม่เสร็จ (`totalBytes = 0`) — เลขนั้นไม่มีอะไรรองรับ

ตอนนี้แบ่งเป็น 4 ระยะจาก `core/stats` โดยตรง:

| ระยะ | เงื่อนไข | การ์ดแสดง |
|---|---|---|
| Starting | ยังไม่มีอะไรถูกนับ | แถบวิ่ง (busy) + `Starting` |
| Scanning | `totalBytes = 0` แต่เริ่ม list/check แล้ว | แถบวิ่ง + `Scanning — N listed` |
| Transferring | มี `totalBytes` แล้ว | แถบ % จริง + `42%  ·  1.2 GiB / 4.5 GiB  ·  3.4 MiB/s  ·  2m 3s left` |
| Finishing | ย้ายครบแล้วแต่ process ยังอยู่ | แถบเต็ม + `Finishing` |

1. Copy โฟลเดอร์ใหญ่ (หลายพันไฟล์ ยิ่งช้ายิ่งดี) ไป remote — ดูตั้งแต่วินาทีแรก
2. ย่อ/ขยายหน้าต่างระหว่างงานวิ่ง
3. ทำงานเดียวกันกับ **teldrive** ซึ่งมักไม่รู้ขนาดไฟล์ล่วงหน้า

| # | เกณฑ์ | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|---|
| 1 | เห็นแถบ progress บนหัวการ์ด **โดยไม่ต้องกางรายละเอียด** | | | |
| 2 | ช่วงแรกเป็นแถบวิ่ง + `Scanning` **ไม่ใช่แถบ 0% ค้าง** | | | |
| 3 | พอ rclone นับเสร็จ แถบเปลี่ยนเป็น % จริง ไม่กระโดดถอยหลังแรงๆ | | | |
| 4 | ข้อความบนแถบอ่านครบ: %, ขนาด, ความเร็ว, เวลาที่เหลือ | | | |
| 5 | ETA อ่านง่าย (`2h 5m` ไม่ใช่ `2h5m12s` ที่กระพริบทุกวินาที) | | | |
| 6 | ช่วงท้ายขึ้น `Finishing` ไม่ใช่ 100% ค้างเฉยๆ | | | |
| 7 | จบงานแล้วแถบหายไป เหลือ `Finished` / `Error` ตามเดิม | | | |
| 8 | กด Cancel → แถบหาย ขึ้น `Stopped` | | | |
| 9 | **ข้อความบนแถบไม่ตกบรรทัดจนโดนตัดครึ่ง** แม้ตอน ETA ยาวๆ (`18d 15h left`) | | | |
| 10 | ถ้าข้อความยาวเกินแถบ → ตัด**ทั้งก้อน**จากท้าย (ETA หายก่อน) ไม่ใช่ตัดกลางตัวเลข · hover เห็นครบ | | | |
| 11 | ช่อง `Remaining:` อ่านง่าย (`18d 15h`) ไม่ใช่ `447h42m7s` | | | |

**แถบรายไฟล์:**

| # | เกณฑ์ | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|---|
| 12 | ขยายหน้าต่าง → ชื่อไฟล์ยาวขึ้นตาม (เดิมตัดที่ 420px ตายตัว) | | | |
| 13 | **ไฟล์ที่ไม่รู้ขนาด (teldrive) ไม่ขึ้น `0% of unknown size` อีก** — เป็นแถบวิ่ง + จำนวนไบต์ที่ส่งไปแล้ว | | | |
| 14 | งานที่มีไฟล์หลายร้อยไฟล์: การ์ด**ไม่บวมด้วยแถวว่าง** ที่ค้างจากไฟล์ที่เสร็จไปแล้ว | | | |

> ข้อ 14 เป็นบั๊กจริงที่เจอตอนทำ: `QFormLayout::removeWidget()` เอา widget ออกแต่
> **ทิ้งแถวไว้ในเลย์เอาต์** ทุกไฟล์ที่เสร็จจึงเหลือแถวว่างสะสมไปเรื่อยๆ
> แก้เป็น `removeRow()` แล้ว — ทดสอบด้วยงานที่มีไฟล์เล็กจำนวนมาก

---

## รอบที่ 5 — autocomplete ในหน้า Preferences (2026-08-05)

### V-14 · ช่อง options เติมชื่อ flag ให้เอง จาก rclone ตัวจริง

marker: `rclone_flags.cpp` (`ParseRcloneHelpFlags`) · L3: `completers.cpp`
fixture: [`tests/fixtures/rclone_help_flags.txt`](../tests/fixtures/rclone_help_flags.txt)

รายการ flag **ไม่ได้ฝังไว้ในโค้ด** แต่ถามจาก `rclone help flags` ของ binary ที่ตั้งค่าไว้
ตอนเปิด Preferences ครั้งแรก แล้วจำไว้ทั้ง session — build ของ teldrive จึงมี
`--teldrive-*` ส่วน mainline ไม่มี โดยที่โค้ดไม่ต้องรู้จักทั้งคู่

ช่องที่ได้ flag completion: **Default rclone options · Default download options ·
Default upload options · Mount options**
ช่องที่ได้ path completion: **rclone · rclone.conf · default download dir · default upload dir**

| # | เกณฑ์ | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|---|
| 1 | พิมพ์ `--tr` ในช่อง options → ขึ้นรายการมี `--transfers` พร้อมคำอธิบาย | | | |
| 2 | **พิมพ์ `--tel` → เห็น flag ของ teldrive** (ยืนยันว่าถามจาก binary จริง) | | | |
| 3 | พิมพ์ `chunk` ต่อจาก `--` → เจอทั้ง `--teldrive-chunk-size` และ `--drive-chunk-size` (ค้นแบบ contains) | | | |
| 4 | เลือก flag แบบ **boolean** (`--dry-run`) → ได้ชื่อ flag + เว้นวรรค ไม่มี `=` | | | |
| 5 | เลือก flag ที่**ต้องมีค่า** (`--transfers`) → ได้ `--transfers=` และเคอร์เซอร์อยู่ท้าย พร้อมพิมพ์ค่าต่อ | | | |
| 6 | มี flag อยู่ก่อนแล้ว (`--transfers=8 --ch`) → เติมเฉพาะคำหลัง ตัวแรกไม่ถูกแตะ | | | |
| 7 | กำลังพิมพ์**ค่า** (`--transfers=`) → รายการไม่โผล่มากวน | | | |
| 8 | ปุ่มลูกศรขึ้น/ลง + Enter เลือกได้ · Esc ปิดรายการ | | | |
| 9 | เปลี่ยน path ของ rclone ใน Preferences เป็น binary อีกตัว → OK → เปิด Preferences ใหม่ → **รายการ flag เปลี่ยนตาม** | | | |
| 10 | ตั้ง path rclone เป็นค่าที่ใช้ไม่ได้ → **ไม่มีรายการขึ้น และไม่มี error เด้ง** (ไม่ควรเดารายการเอง) | | | |

**path completion:**

| # | เกณฑ์ | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|---|
| 11 | ช่อง download/upload dir พิมพ์ path บางส่วน → เติมโฟลเดอร์ให้ **และไม่เสนอไฟล์** | | | |
| 12 | ช่อง rclone / rclone.conf เสนอทั้งไฟล์และโฟลเดอร์ | | | |
| 13 | ปุ่ม Browse ยังทำงานเหมือนเดิมทุกช่อง | | | |

**ยังไม่ทำ:** เติม **ชื่อ remote** ในช่อง source/destination

> การ parse `rclone help flags` มี unit test 11 ชุด และตรวจกับ output จริงทั้ง 1,078 flag
> แล้วว่าอ่านได้ครบ 0 บรรทัดที่หลุด — ข้อ 1–13 ข้างบนคือส่วนที่ test แตะไม่ถึง

---

### V-15 · completion ใน Transfer dialog และ Mount dialog

marker: `completers.cpp` (`PopupCommitFilter`)

ช่อง extra options ของสองหน้านี้เป็น **`QPlainTextEdit`** ไม่ใช่ช่องบรรทัดเดียวแบบ
Preferences จึงเขียนตัวจัดการแยก และมีพฤติกรรมหนึ่งที่ต่างกันจริง:

> **Enter ในช่องหลายบรรทัดใช้ไม่ได้ถ้าไม่ทำอะไรเพิ่ม** — QCompleter ส่งปุ่มให้ตัว editor
> ก่อนเสมอ และ `QPlainTextEdit` รับ Return ไปขึ้นบรรทัดใหม่ ปุ่มจึงถูกกินหมด
> ส่วน `QLineEdit` ไม่รับ Return ช่องบรรทัดเดียวเลยทำงานได้เอง
>
> วัดจริงด้วยโปรแกรมทดลอง ไม่ได้เดา:
> ```
> QLineEdit        enter -> activated=1
> QPlainTextEdit   enter -> activated=0     <- ปุ่มหาย
> QPlainTextEdit   enter -> activated=1     <- หลังใส่ event filter
>   + filter
> ```

| # | เกณฑ์ | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|---|
| 1 | Transfer dialog → แท็บ options → พิมพ์ `--tr` → ขึ้นรายการ | | | |
| 2 | **รายการโผล่ข้างเคอร์เซอร์** ไม่ใช่ใต้กล่องทั้งกล่อง | | | |
| 3 | **กด Enter แล้วเลือกได้จริง** (ข้อที่พังถ้าไม่มี filter) | | | |
| 4 | กด Tab เลือกได้เหมือน Enter | | | |
| 5 | คลิกเมาส์เลือกได้ | | | |
| 6 | ลูกศรขึ้น/ลง เลื่อนในรายการ **ไม่ใช่เลื่อนเคอร์เซอร์ในข้อความ** | | | |
| 7 | Esc ปิดรายการ และ**ไม่**ขึ้นบรรทัดใหม่ | | | |
| 8 | เลือกแล้ว รายการไม่เด้งกลับมาเอง (ไม่วนลูป) | | | |
| 9 | flag คนละบรรทัด (`--dry-run` ขึ้นบรรทัดใหม่ `--tr`) → เติมเฉพาะบรรทัดที่เคอร์เซอร์อยู่ | | | |
| 10 | Mount dialog → ช่อง extra options → ทำงานเหมือนกันทุกข้อ | | | |

**path completion ในสองหน้านี้:**

| # | เกณฑ์ | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|---|
| 11 | Upload → ช่อง **source** เติม path ในเครื่อง · ช่อง dest (remote) ไม่ถูกแตะ | | | |
| 12 | Download → ช่อง **dest** เติม path ในเครื่อง (เฉพาะโฟลเดอร์) · ช่อง source ไม่ถูกแตะ | | | |
| 13 | Mount dialog → mount base / mount point (ไม่ใช่ Windows) เติมเฉพาะโฟลเดอร์ | | | |
| 14 | Mount dialog → ช่อง mount script เติม path ได้ และปุ่ม Edit... ยังทำงาน | | | |
| 15 | ปุ่ม Browse ทุกปุ่มในสองหน้านี้ยังทำงานเหมือนเดิม | | | |

> ข้อ 11/12 จงใจไม่ใส่ completion ให้ฝั่ง remote — filesystem completer จะเสนอโฟลเดอร์
> ในเครื่องซึ่งไม่เกี่ยวอะไรกับ remote เลย

---

### V-16 · ตรวจ repo ของ rclone เอง (เอาช่องกรอกออกแล้ว)

marker: `rclone_flags.h` (`DetectRcloneRepo`)

ช่อง **Custom Rclone Repo** พร้อมปุ่ม Check และ checkbox ถูกเอาออกทั้งหมด
แท็บ Misc เหลือแค่บรรทัดบอกว่าตรวจเจออะไร

**ข้อเท็จจริงที่วัดมาแล้ว: binary ไม่ได้บอกว่ามาจาก repo ไหน**

| ที่ลองอ่าน | mainline v1.75.0 | tgdrive v1.73.1 | ใช้แยกได้ไหม |
|---|---|---|---|
| `rclone version` | `rclone v1.75.0` + 7 บรรทัด | `rclone v1.73.1` + 7 บรรทัดเหมือนกัน | ❌ ไม่มีชื่อ repo · เลขเวอร์ชันตามต้นน้ำ |
| Go module path ใน binary | `github.com/rclone/rclone` | `github.com/rclone/rclone` | ❌ **tgdrive ไม่ได้เปลี่ยนชื่อ module** |
| `vcs.*` build settings | ไม่มี | ไม่มี | ❌ |
| **backend ที่มีในตัว** | ไม่มี `teldrive` | **มี `teldrive`** | ✅ |

จึงดูจาก**ความสามารถ**แทน: มี flag `--teldrive-*` → `tgdrive/rclone` · ไม่มี → `rclone/rclone`
ใช้รายการ flag ชุดเดียวกับที่ดึงมาทำ completion ไม่มีการเรียก rclone เพิ่ม

**ทดสอบด้วยเครื่องแล้ว** — รัน `ParseRcloneHelpFlags` + `DetectRcloneRepo` ตัวจริงกับ binary จริง 3 ตัว:

```
.installer/bin/rclone.exe (v1.72.1)     flags=1065  teldrive=12  -> tgdrive/rclone
tg/rclone-v1.73.1/rclone.exe            flags=1092  teldrive=13  -> tgdrive/rclone
mainline/rclone-v1.75.0/rclone.exe      flags=1129  teldrive=0   -> rclone/rclone
C:/nonexistent/rclone.exe               flags=0     teldrive=0   -> (unknown)
```

> รวมถึงข้อที่น่าสนใจ: rclone ที่ลงในเครื่องอยู่ **เป็น build ของ tgdrive** ทั้งที่รายงานตัวเองว่าเป็น
> `github.com/rclone/rclone v1.72.1` — ซึ่งเป็นเหตุผลว่าทำไมอ่าน module path ไม่ได้

**นี่คือการอนุมาน ไม่ใช่การอ่านค่า** — fork ที่ไม่ได้เพิ่ม backend อะไรเลยจะถูกมองเป็น upstream
ถ้าเจอกรณีแบบนั้น ให้เพิ่มแถวใน `kForkMarkers` แทนการเอาช่องกรอกกลับมา

| # | เกณฑ์ | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|---|
| 1 | แท็บ Misc → กลุ่ม Advanced เหลือบรรทัดเดียว **ไม่มีช่องให้กรอก ไม่มีปุ่ม Check** | | | |
| 2 | บรรทัดนั้นขึ้น `tgdrive/rclone` (ลิงก์กดได้) + บอกว่าเพราะ build นี้มี teldrive backend | | | |
| 3 | เปลี่ยน path rclone ไปที่ build ของ mainline → OK → เปิด Preferences ใหม่ → ได้ `rclone/rclone` | | | |
| 4 | ตั้ง path rclone เป็นค่าที่ใช้ไม่ได้ → ขึ้นว่าอ่านไม่ได้ · **ไม่เดาเป็น `rclone/rclone`** | | | |
| 5 | การแจ้งเตือนอัปเดต rclone ชี้ไป repo ที่ถูก (ลบ `Settings/lastRcloneUpdateCheck` แล้วเปิดใหม่) | | | |
| 6 | หน้าต่าง Preferences ยังจัดวางปกติ ไม่มีที่ว่างค้างจากของที่เอาออก | | | |
| 7 | ค่าเดิมใน `Settings/queueRcloneRepo` ที่เคยกรอกไว้ ถูกเมินเฉยๆ ไม่ทำให้พัง | | | |

> ข้อ 5: ค่าที่ตรวจได้มาจากการถาม rclone ตอนเปิดโปรแกรม ถ้ายังไม่ได้คำตอบ
> **จะข้ามการเช็คอัปเดตรอบนั้นไปเลย** ไม่ใช่เดาเป็น `rclone/rclone`
> เพราะถ้าเดาผิดจะเสนอไฟล์ของ upstream ให้คนที่ใช้ fork ทุกวัน

---

## วิธีเพิ่มรายการใหม่

เมื่อแก้อะไรที่ต้องมีคนทดสอบ ให้ทำสองอย่างคู่กัน:

1. ทิ้ง marker ไว้ตรงโค้ด พร้อมรหัสรายการ:
   ```cpp
   // TEST: (V-07) <ทดสอบอย่างไร และเกณฑ์ผ่านคืออะไร>
   ```
2. เพิ่มหัวข้อ `### V-07 · ...` ในไฟล์นี้ พร้อมตารางให้ผู้ทดสอบกรอก

รหัสต้องไม่ซ้ำและไม่นำกลับมาใช้ใหม่ แม้รายการเก่าจะเลิกใช้แล้ว
