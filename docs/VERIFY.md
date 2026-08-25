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

## รอบที่ 6 — headless (2026-08-06)

### V-17 · `--run-task` ทำงานโดยไม่เปิดหน้าต่าง

marker: `task_runner.cpp` · อัตโนมัติแล้ว: `tests/test_task_runner.cpp` (7 เคส)

นี่คือ **E1** และเป็นของส่งมอบชิ้นแรกของ S1 ([`API.md` §12](API.md))

**ชื่อไฟล์ต่างกันตามแพลตฟอร์ม** — Windows คือ `RcloneBrowser.exe` ส่วน Linux/macOS คือ `rclone-browser`
(ดู `src/CMakeLists.txt`) และยังไม่ได้อยู่บน PATH จึงต้องเรียกด้วย path เต็ม

```bash
.uilduild\Release\RcloneBrowser.exe --list-tasks
.uilduild\Release\RcloneBrowser.exe --run-task "ชื่องาน" --dry-run
```

```bash
./rclone-browser --list-tasks
```

> ⚠️ **บน Windows ไบนารีเป็น GUI subsystem** (`add_executable(... WIN32 ...)`)
> เชลล์จึง**ไม่รอ**ให้จบก่อนคืน prompt และข้อความอาจโผล่หลัง prompt
> ถ้าจะอ่าน exit code ต้องบังคับให้รอ เช่น `... --run-task "X" | Out-String`
> หรือ `Start-Process -Wait` — ดูข้อ 2/3 ข้างล่าง

| exit | ความหมาย |
|---|---|
| 0–9 | ของ rclone เอง ส่งผ่านตรงๆ |
| 64 | ใช้คำสั่งผิด / เป็น mount task |
| 65 | ไม่มี task ชื่อนั้น |
| 66 | ชื่อซ้ำหลาย task |
| 69 | รัน rclone ไม่ได้เลย |
| 70 | rclone ตายผิดปกติ |

**ที่ test อัตโนมัติครอบแล้ว** (รันจริงกับ rclone จริง): copy สำเร็จ + exit 0 ·
`--dry-run` ไม่คัดลอกจริง · ไม่เจอ task → 65 · ชื่อซ้ำ → 66 พร้อมรายการ id ·
mount → 64 · `--list-tasks` มี id ครบ

**ที่ผู้ช่วยรันเองแล้ว** (2026-08-07, Windows 11, rclone v1.72.1 tgdrive build):

| คำสั่ง | ผลที่ได้ | exit |
|---|---|---|
| `--help` | ข้อความ usage ครบ | 0 |
| `--list-tasks` | 3 task พร้อม id และชนิด | 0 |
| `--run-task "no such task"` | `no task called "no such task"` + ชี้ไป `--list-tasks` | **65** |
| `--run-task "ef"` (mount task) | `"ef" is a mount task, which runs until it is stopped` | **64** |
| `--run-task` (ไม่ใส่ชื่อ) | บอกว่าต้องใส่ชื่อ + usage | **64** |
| `--run-task "_tmp_05Feb2026_193021_main_01"` | รัน rclone จริง → `CRITICAL: Failed to create file system for "main_01:"` (remote ถูกลบไปแล้ว) | **1** (ของ rclone) |

ยืนยันเพิ่ม: หลังคำสั่งจบ **ไม่มี process `RcloneBrowser` ค้าง** (`Get-Process` = 0)
และบรรทัด `rclone:` ที่พิมพ์ออกมาเป็นคำสั่งที่ผ่าน `RedactArgs()` แล้ว

> ⚠️ ทั้งหมดนี้รันแบบ **pipe** เพราะสภาพแวดล้อมนี้จับ stdout เสมอ
> **ข้อ 1, 2 จึงยังต้องมีคนดูด้วยตาจริงๆ** ว่าไม่มีหน้าต่างเปิด และข้อความขึ้นโดยไม่ต้อง pipe

> ✅ **ทดสอบเบื้องต้น: ใช้งานได้** — totza2010 / 2026-08-07
> ยังไม่ได้ไล่ทีละข้อ · ข้อ 4–6, 8 (รันขณะเปิดโปรแกรม, cron/Task Scheduler, ในคอนเทนเนอร์,
> ปิดโปรแกรมระหว่างงานวิ่ง) ยังไม่มีใครลอง

**ที่ต้องมีคนลอง:**

| # | เกณฑ์ | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|---|
| 1 | รันในเทอร์มินัล → **ไม่มีหน้าต่างเปิดเลย** | | | |
| 2 | เห็นข้อความบนหน้าจอ **โดยไม่ต้อง pipe** (Windows: แอปเป็น GUI subsystem จึงต้อง attach console) | | | |
| 3 | `--list-tasks > out.txt` ได้ไฟล์ครบ (การ attach console ต้องไม่ทับ redirect) | PASS | claude / 2026-08-07 | ยืนยันด้วยการ pipe — ได้ 3 task ครบพร้อม id |
| 4 | **รันขณะที่เปิดโปรแกรมค้างอยู่** → ทำงานได้ ไม่ติด "already running" | | | |
| 5 | ตั้งใน Task Scheduler (Windows) หรือ cron (Linux) แล้วเด้งทำงานตามเวลา | | | |
| 6 | ในคอนเทนเนอร์: `docker exec rclonebrowser rclone-browser --run-task "X"` | | | |
| 7 | task ที่มี `--rc-pass` หรือ token → บรรทัด `rclone:` ที่พิมพ์ออกมาต้องเป็น `***` | PASS | claude / 2026-08-07 | อัตโนมัติใน `test_job_registry` (`--drive-token=SECRET123` → `***`) |
| 8 | ปิดโปรแกรมระหว่าง `--run-task` วิ่ง → งาน headless ไม่ได้รับผลกระทบ | | | |

**regression ของ GUI (เพราะ `runItem()` เปลี่ยน signature):**

| # | เกณฑ์ | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|---|
| 9 | ดับเบิลคลิก task ในแท็บ Tasks → รันได้ | | | |
| 10 | ใส่คิว → คิวเดินจนหมด | | | |
| 11 | scheduler เด้งตามเวลา → รันได้ | | | |
| 12 | mount autostart ตอนเปิดโปรแกรม → ยัง mount ให้ | | | |

> **บั๊กที่เจอตอนทำข้อนี้:** `JobOptions::getOptions()` ใส่ `--transfers` `--checkers`
> `--contimeout` `--timeout` `--retries` `--low-level-retries` **โดยไม่เช็คว่าค่าว่างหรือไม่**
> ทั้งที่ option อื่นๆ รอบๆ เช็คหมด · task ที่ค่าพวกนี้ว่างจะสร้างคำสั่งที่ rclone ไม่ยอมเริ่ม
> (`invalid argument "" for "--transfers" flag`) และ `--contimeout` กลายเป็น `"s"` เฉยๆ
> หน้าต่างกรอกค่าให้เสมอจึงไม่เคยเจอ — แต่ task ที่สร้างจากทางอื่น (headless หรือ API ในอนาคต) เจอแน่
> แก้แล้วพร้อม test

---

### V-18 · การ์ด job แยกจากตัวงานแล้ว (S2)

marker: `job_widget.h` · อัตโนมัติแล้ว: `tests/test_job_registry.cpp` (7 เคส)

`JobWidget` เคยเป็นเจ้าของ `QProcess` + `RcClient` + `JobLogWriter` แปลว่า
**งานที่กำลังวิ่ง = widget** ตอนนี้เป็นแค่หน้าจอที่ดู `RunningJob` (L1) อยู่
การ์ดลดจาก 462 → 382 บรรทัด และ `--rc --rc-addr=localhost:0` ย้ายเข้า `RunningJob`

**ทั้งหมดนี้ต้องทำงานเหมือนเดิมทุกอย่าง** — ไม่ได้ตั้งใจเปลี่ยนพฤติกรรมอะไรเลย

**ที่ผู้ช่วยรันเองแล้ว** (2026-08-07) — `tests/test_job_registry.cpp` รัน rclone จริง
โดยไม่มี widget ที่ไหนเลย ผ่านครบ 7 เคส:

| เคส | ยืนยันอะไร | เกี่ยวกับข้อ |
|---|---|---|
| `runsAJobAndReportsItFinished` | สั่งงาน → `Running` → `Finished` · ไฟล์ถึงปลายทางจริง · `runningCount()` ถูก | 1, 6 |
| `reportsProgressFromTheRemoteControl` | `statsUpdated` มาจาก `core/stats` และค่าสอดคล้อง | 2 |
| `aStoppedJobIsNotAnError` | หยุดกลางคัน → `Stopped` **ไม่ใช่** `Error` | **5** |
| `aJobThatCannotStartStillEnds` | rclone ไม่มีอยู่ → จบเป็น `Error` ไม่ค้าง `Running` | **15** |
| `theCommandItReportsIsRedacted` | `--drive-token=SECRET123` → `***` | 4 |
| `refusesToForgetARunningJob` | ลบงานที่ยังวิ่งไม่ได้ | 7 |
| `listsEverySavedTask` (ใน task runner) | รายการมี id ครบ | — |

> ข้อ 5 และ 15 เป็นสองข้อที่เสี่ยงที่สุดของ S2 และ**ทดสอบอัตโนมัติได้แล้ว**
> ที่เหลือในตารางข้างล่างเป็นเรื่องของหน้าจอล้วนๆ ซึ่งต้องมีคนดู

> ✅ **ทดสอบเบื้องต้น: ใช้งานได้** — totza2010 / 2026-08-07
> ยังไม่ได้ไล่ทีละข้อ · ข้อที่ยังอยากให้ดูเพิ่มคือ **ข้อ 5** (Cancel → `Stopped` ไม่ใช่ `Error`),
> **ข้อ 15** (rclone รันไม่ขึ้น → ไม่ค้าง `Running`) และข้อ 9 (ไฟล์ log ยังครบ)

| # | เกณฑ์ | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|---|
| 1 | รัน transfer → การ์ดขึ้น ตัวเลขเดินครบทุกช่อง | | | |
| 2 | แถบ progress + `Scanning` / `Finishing` ยังทำงาน (V-13 ซ้ำ) | | | |
| 3 | ช่อง output ไหลตามปกติ · `-vv` ยังเห็น DEBUG | | | |
| 4 | ปุ่ม copy คำสั่ง → ได้คำสั่งที่ redact แล้ว | | | |
| 5 | **กด Cancel → ขึ้น `Stopped` ไม่ใช่ `Error`** | PASS (core) | claude / 2026-08-07 | `aStoppedJobIsNotAnError` · เหลือยืนยันว่าการ์ดแสดงถูก |
| 6 | งานจบเอง exit 0 → `Finished` · exit ไม่ใช่ 0 → `Error` | | | |
| 7 | ปิดการ์ดที่จบแล้ว → หายไป และตัวนับแท็บ Jobs ถูก | | | |
| 8 | เรียงตามเวลา / ตามสถานะ ยังถูกต้อง | | | |
| 9 | ไฟล์ log ในโฟลเดอร์ `logs/` ยังถูกเขียนครบ | | | |
| 10 | คิวเดินต่อเองเมื่องานก่อนหน้าจบ | | | |
| 11 | scheduler เด้ง → รันแล้วการ์ดขึ้นถูก | | | |
| 12 | Stop all jobs ทำงาน | | | |
| 13 | **mount / stream ยังทำงานเหมือนเดิมทุกอย่าง** (ยังไม่ถูกย้าย = S10) | | | |
| 14 | ปิดโปรแกรมขณะมีงานวิ่ง → ยังถามยืนยันและปิดได้ตามปกติ | | | |

**เคสที่เพิ่งแก้ (ของเดิมพังมาตลอด):**

| # | เกณฑ์ | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|---|
| 15 | ตั้ง path rclone เป็นค่าที่ใช้ไม่ได้ แล้วสั่งรัน task → **การ์ดต้องจบเป็น `Error` ไม่ค้าง `Running`** | PASS (core) | claude / 2026-08-07 | `aJobThatCannotStartStillEnds` · เหลือยืนยันบนการ์ดจริง |

> ข้อ 15 คือบั๊กเก่า: `QProcess` ส่ง `errorOccurred` แล้ว**ไม่ส่ง** `finished`
> การ์ดจึงค้างที่ Running ตลอดชีวิตโปรแกรม · มองไม่เห็นเลยจนกระทั่งเขียน test ได้
> ตอนแก้ยังเจอ segfault ต่ออีกชั้น เพราะบน Windows `FailedToStart` ยิงจาก**ใน** `start()`
> ทำให้ตัวชี้ process เป็น null ก่อนที่ `start()` จะ return

---

### V-19 · args ของ mount ย้ายออกจากหน้าต่างแล้ว (S10)

marker: `job_options.h` (`getMountOptions`) · อัตโนมัติแล้ว: `tests/test_task_store.cpp` (6 เคส)

73 บรรทัดที่ประกอบ args ของ mount อยู่ใน `MainWindow::runItem` ย้ายมาเป็น
`JobOptions::getMountOptions()` (L1) — **VIO-1 ปิดสนิทแล้ว** ไม่มี `LAYER:` marker เหลือในโค้ด

**เป็นการย้ายโค้ด ไม่ได้ตั้งใจเปลี่ยน args แม้แต่ตัวเดียว** — สิ่งที่ต้องยืนยันคือคำสั่งที่ออกมาเหมือนเดิม

> ✅ **ทดสอบเบื้องต้น: ใช้งานได้** — totza2010 / 2026-08-07
> ยังไม่ได้ไล่ทีละข้อ · ข้อที่ยังอยากให้ดูเพิ่มคือ **ข้อ 2** (เทียบคำสั่งกับก่อนอัปเดต)
> และข้อ 5–8 ซึ่งเป็นเส้นทางที่การใช้งานปกติไม่ผ่าน

**ที่ test อัตโนมัติครอบแล้ว** (ไม่ต้องใช้ rclone เพราะเป็นตรรกะล้วน):
`--rc-addr localhost:<port>` · `--volname` · `--vfs-cache-mode` ทั้ง 3 ระดับ ·
drive shared/trash รวมกรณี shared แล้วบังคับ `--read-only` · extra options ที่มีเครื่องหมายคำพูด ·
**และยืนยันว่า `--rc-user` / `--rc-pass` ไม่เคยอยู่ใน args**

| # | เกณฑ์ | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|---|
| 1 | mount task เดิมที่เคยใช้ได้ → ยัง mount ได้เหมือนเดิม | | | |
| 2 | **hover ปุ่ม output เทียบคำสั่งกับก่อนอัปเดต → เหมือนกันทุกตัว** | | | |
| 3 | mount ที่ตั้ง RC port → unmount ได้ตามปกติ | | | |
| 4 | mount ที่**ไม่**ตั้ง RC port → ยังทำงาน (Windows จะ unmount ไม่ได้ ซึ่งเป็นพฤติกรรมเดิม) | | | |
| 5 | mount script ยังถูกเรียกหลัง mount สำเร็จ | | | |
| 6 | `--vfs-cache-mode` ที่ตั้งไว้ในหน้า mount มีผลจริง | | | |
| 7 | extra options ที่มี path มีเว้นวรรค (ในเครื่องหมายคำพูด) ยังส่งถึง rclone ถูกตัว | | | |
| 8 | gdrive โหมด shared → ยังได้ `--drive-shared-with-me` และ `--read-only` | | | |
| 9 | mount autostart ตอนเปิดโปรแกรม | | | |

> ข้อ 2 เป็นข้อที่ตัดสินทั้งหมด — ถ้าคำสั่งเหมือนเดิมทุกตัว ที่เหลือตามมาเอง

**ยังไม่ทำในรอบนี้:** ตัว process ของ mount ยังเป็นของ `MountWidget` อยู่ (ยังไม่เป็น `RunningJob`)
เพราะ**ทดสอบอัตโนมัติในเครื่องนี้ไม่ได้เลย** — mount ต้องมี WinFsp + remote จริง + drive letter
ต่างจาก transfer ที่รัน rclone จริงใน test ได้ · ควรให้ข้อ 1–9 ข้างบนผ่านก่อน

---

### V-20 · mount เดินทาง `RunningJob` แล้ว (S10 ครึ่งหลัง)

marker: `mount_widget.h` · อัตโนมัติแล้ว: `tests/test_job_registry.cpp` (2 เคสของ mount)

`MountWidget` เลิกเป็นเจ้าของ `QProcess` + log + credential + mount script — ทั้งหมดอยู่ที่
`RunningJob` (L1) แล้ว การ์ดลดจาก 428 → 310 บรรทัด และ `addNewMount` ลดไป 107 บรรทัด

> ⚠️ **นี่คือรายการที่ทดสอบอัตโนมัติได้น้อยที่สุดในโครงการนี้**
> mount ต้องมี WinFsp + remote จริง + drive letter ว่าง ซึ่งเครื่อง CI ไม่มี
> test ที่เขียนได้ครอบแค่ *รูปร่าง* ของ job — ไม่ได้พิสูจน์ว่า filesystem ขึ้นจริง
> **ทุกข้อข้างล่างต้องมีคนลอง**

**ที่ test อัตโนมัติครอบแล้ว:** mount ไม่ได้ `--rc-addr` เพิ่มเป็นตัวที่สอง ·
mount ที่ล้มจบเป็น `Error` ไม่ค้าง · คำใน log เป็น `unmounted` ไม่ใช่ `finished`

| # | เกณฑ์ | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|---|
| 1 | mount task ที่เคยใช้ได้ → ยัง mount ขึ้นเหมือนเดิม | | | |
| 2 | เข้าถึงไฟล์ผ่าน drive/โฟลเดอร์ที่ mount ได้ | | | |
| 3 | **กด unmount → unmount สำเร็จ ไม่ค้าง** | | | |
| 4 | **unmount ขณะที่มีไฟล์เปิดค้างอยู่ → ขึ้น "Mounted" สีแดง + tooltip บอกเหตุผล และ mount ยังอยู่** | | | |
| 5 | ช่อง output ของ mount ยังไหลตามปกติ | | | |
| 6 | mount script ยังถูกเรียก และช่อง script output แสดงผล | | | |
| 7 | script จบแล้วป้ายเปลี่ยนเป็น Finished / Error ตาม exit code | | | |
| 8 | ปุ่ม copy คำสั่ง → ได้คำสั่งที่ redact แล้ว | | | |
| 9 | ไฟล์ log ของ mount ยังถูกเขียน รวมบรรทัด `[script]` | | | |
| 10 | mount ที่**ไม่**ตั้ง RC port → ยัง mount ได้ (unmount บน Windows ไม่ได้ ซึ่งเป็นพฤติกรรมเดิม) | | | |
| 11 | mount autostart ตอนเปิดโปรแกรม | | | |
| 12 | ปิดโปรแกรมขณะ mount อยู่ → ยังถามยืนยันตามเดิม | | | |
| 13 | เรียงในแท็บ Jobs — mount ยังอยู่ท้าย transfer (สถานะขึ้นต้นด้วย `z`) | | | |
| 14 | **stream ยังทำงานเหมือนเดิมทุกอย่าง** (ตั้งใจไม่ย้าย) | | | |

> **ข้อ 4 คือข้อที่สำคัญที่สุด** — เป็นความต่างจริงระหว่าง mount กับ transfer:
> การ unmount เป็น *คำขอ* ที่ถูกปฏิเสธได้ ส่วนการ kill ไม่มีทางล้มเหลว
> ถ้าข้อนี้พัง การ์ดจะค้างที่ "Unmounting" ตลอดไปทั้งที่ mount ยังอยู่

> ✅ **ทดสอบเบื้องต้น: ใช้งานได้** — totza2010 / 2026-08-07
> ผ่านหลังแก้อาการปิดโปรแกรมค้าง (ดูข้างล่าง) · ยังไม่ได้ไล่ทีละข้อ
> ข้อที่ยังอยากให้ดูเพิ่มคือ **ข้อ 4** (unmount ขณะมีไฟล์เปิดค้าง), ข้อ 6–7 (mount script), ข้อ 14 (stream)

**ปิดโปรแกรมขณะ mount อยู่แล้วค้าง — บั๊กเดิม ไม่ใช่ของ S10**

อาการ: กล่อง "Terminating all processes" ค้างถาวร · ไดรฟ์ยังอยู่ใน Explorer ·
**แต่ไม่มี `rclone.exe` เหลือ** ซึ่งเป็นลายเซ็นของ mount ที่ถูกฆ่าไม่ใช่ถูก unmount
(WinFsp ทิ้งไดรฟ์ค้างไว้) · ผู้ทดสอบยืนยันว่าเป็นมาตั้งแต่ก่อนรอบนี้

แก้ไป 4 อย่าง:

| # | แก้อะไร |
|---|---|
| 1 | mount ไม่เคยถูก `forget()` ออกจาก registry ตอนปิดการ์ด (transfer มี mount ไม่มี) |
| 2 | `quitApp()` เลิกอ่าน `isRunning` จากการ์ดแต่ละใบ → ถาม `JobRegistry` ซึ่งเป็นเจ้าของ process จริง (stream ยังนับแบบเดิมเพราะยังไม่เข้า registry) |
| 3 | `unmount()` รายงานผลลง output + log — `[unmount] exit code N` และเหตุผลเมื่อ client รันไม่ขึ้น · **เดิมถ้าล้มเงียบจะไม่มีร่องรอยเลย** |
| 4 | mount ที่ไม่มี RC port บน Windows: บอกตรงๆ ว่าขอ quit ไม่ได้ ต้อง unmount จาก Windows เอง (เดิมยิง `--rc-addr localhost:` ว่างแล้วเงียบ) |

> ⚠️ **ยังไม่รู้ว่าข้อไหนคือสาเหตุจริง** — ทดสอบแล้วไม่เกิดซ้ำ แต่ไม่ได้พิสูจน์ว่าอันไหนแก้
> ข้อ 3 จึงมีค่าที่สุดในระยะยาว: ถ้ากลับมาเป็นอีก บรรทัด `[unmount]` จะบอกเองว่าติดตรงไหน
> และกล่องตอนปิดโปรแกรมจะระบุชื่องานที่รออยู่แทนข้อความกลางๆ

**เจอระหว่างทาง (แก้แล้ว):**

| อาการ | สาเหตุ |
|---|---|
| `mount_widget.cpp` มี regex อ่าน RC port **ของตัวเอง** | เศษของ VIO-2 ที่ตกค้าง ทั้งที่ `ParseRcServingPort()` อยู่ใน L0 พร้อม test มาตั้งแต่ V-11 |
| `waitForStarted()` หมุน event loop | บรรทัดแรกของ output อาจถูกส่งก่อนตัวแสดงผลจะต่อ signal ทัน — เอาออก ใช้ `errorOccurred` แทน |

---

## รอบที่ 7 — ประวัติการรันไม่หายตอนปิดโปรแกรม (2026-08-09)

### V-21 · ประวัติงานอยู่ในฐานข้อมูล (S13 ส่วนแรก)

marker: `run_history.h` · อัตโนมัติแล้ว: `tests/test_run_history.cpp` (16 เคส)

ก่อนหน้านี้ **ปิดโปรแกรมแล้วไม่เหลือร่องรอยเลย**ว่าเมื่อคืนงานไหนรัน สำเร็จหรือล้ม
โอนไปเท่าไหร่ ไฟล์ log ไม่ได้หาย แต่ไม่มีอะไรบอกว่าไฟล์ไหนของงานไหน

ตอนนี้ทุกงานที่ `JobRegistry` เริ่ม และทุก `--run-task` เขียนแถวลง
`rclone-browser.db` (SQLite ผ่าน `Qt6::Sql`) — **เนื้อ log ยังเป็นไฟล์เหมือนเดิม
DB เก็บแค่ตัวชี้** ตามเหตุผลใน [`PLAN.md` §6.8](PLAN.md)

**ที่ test อัตโนมัติครอบแล้ว:** สร้าง schema · เขียนตอนเริ่ม/ตอนจบ · เวลาเริ่มไม่ถูกทับ ·
งานที่จบโดยไม่เคยบันทึกตอนเริ่มก็ยังได้แถว · เรียงใหม่ก่อน · กรองตาม task ·
ตัดตามอายุและตามจำนวน (ลบไฟล์ log ที่ผูกอยู่ด้วย) · **งานที่ยังวิ่งอยู่ไม่ถูกตัดทิ้ง** ·
**สองการเชื่อมต่อเขียนพร้อมกัน 100 แถวไม่หาย** · DB ที่เปิดไม่ได้ต้องไม่ทำให้งานล้ม

**ที่ยืนยันด้วยการรันจริงแล้ว (2026-08-09):** `--run-task --dry-run` กับ task ที่ remote
หายไปแล้ว → ได้แถว `state=error` `exit_code=1` พร้อม source/dest และเวลาเริ่ม/จบ ครบ 3 ครั้ง

| # | เกณฑ์ | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|---|
| 1 | รัน transfer จากหน้าต่าง → ปิดโปรแกรม → เปิดใหม่ → แถวยังอยู่ | ✅ **PASS** | totza2010 + AI / 2026-08-09 | copy 388.6 GiB 9h51m17s ยังอยู่หลังเปิดใหม่ |
| 2 | แถวชี้ไปไฟล์ log ที่เปิดอ่านได้จริง | ✅ **PASS** | AI / 2026-08-09 | `20260809-111022-copy-4324eb58.log` 1.6 MB อ่านได้ |
| 3 | งานที่กด stop เอง → `state=stopped` ไม่ใช่ `error` | | | |
| 4 | mount → `state=unmounted` ตอน unmount สำเร็จ | ⚠️ ได้ `stopped` | AI / 2026-08-09 | พฤติกรรมเดิมของ `finalStatus()` — unmount ที่ผู้ใช้สั่งนับเป็น stopped |
| 5 | ตัวเลข bytes / transfers ตรงกับที่การ์ดแสดงตอนจบ | ✅ **PASS** | AI / 2026-08-09 | 417,295,790,949 ตรงกับผลรวมจาก log **เป๊ะ** |
| 6 | **รัน `--run-task` จากเทอร์มินัลขณะเปิดหน้าต่างอยู่ → ทั้งสองได้แถว ไม่มีใครล้ม** | | | |
| 7 | ปิดโปรแกรมทั้งที่งานยังวิ่ง (kill) → แถวค้างที่ `running` ไม่ใช่รายงานผิดว่าสำเร็จ | | | |
| 8 | Docker: ประวัติยังถูกเขียน (ต้องมี `qt6-qtbase-sqlite` ในอิมเมจ) | | | |
| 9 | **หน้า Jobs แยกเป็น Running / History** — Running ยังทำงานเหมือนเดิมทุกอย่าง (ปุ่ม Stop All / Clean / Sort, การ์ดงาน) | | | |
| 9b | แท็บ History แสดงงานเก่าครบ เรียงใหม่ก่อน | | | |
| 10 | เลือกแถว → ปุ่ม "Open log" เปิดไฟล์ log ของงานนั้นได้จริง (ดับเบิลคลิกก็ได้) | | | |
| 11 | ช่องกรอง พิมพ์ชื่อ task / remote / `error` แล้วเหลือเฉพาะที่ตรง | | | |
| 12 | "Clear history..." ถามยืนยันก่อน และ**ลบไฟล์ log ที่ผูกอยู่ด้วย** | | | |
| 13 | งานที่จบระหว่างเปิดแท็บอื่นอยู่ → กลับมาแท็บ History แล้วเห็นทันที | | | |
| 14 | **task ทั้งหมดยังอยู่ครบหลังย้ายเข้า DB** — แก้ / เพิ่ม / ลบ task แล้วปิดเปิดใหม่ยังถูก | ✅ ผ่านเบื้องต้น | AI / 2026-08-09 | 6 task ครบ เรียงเหมือนเดิม |
| 15 | scheduler ยังอยู่ครบ เวลา next run ถูก | ✅ ผ่านเบื้องต้น | AI / 2026-08-09 | 1 scheduler พร้อม paused state |
| 16 | queue ยังอยู่ครบ ลำดับไม่สลับ (ใส่ของในคิวก่อนอัปเดต แล้วเปิดใหม่) | | | |
| 17 | `tasks.bin` / `queue.conf` / `scheduler.conf` ถูกเปลี่ยนชื่อเป็น `.migrated` **ไม่ถูกลบ** | ✅ ผ่านเบื้องต้น | AI / 2026-08-09 | |
| 18 | ลบไฟล์ `qsqlite.dll` ออกชั่วคราว → โปรแกรมยังเปิดได้และ task ยังอยู่ (อ่านจากไฟล์เดิม) | | | |

> **ข้อ 6 คือข้อที่ออกแบบมารับตั้งแต่ต้น** — E1 จงใจไม่มี single-instance lock
> DB จึงเปิด WAL + busy timeout 10 วินาที ถ้าข้อนี้พังจะเห็นเป็น "database is locked"
>
> **ข้อ 8 มีทางพังเงียบ** — ถ้าไดรเวอร์ SQLite ไม่มี ประวัติจะไม่ถูกเขียนโดยไม่มีข้อความใดๆ
> ฝั่ง `--run-task` เตือนออก stderr แล้ว แต่ฝั่งหน้าต่างยังเงียบอยู่

**ตรวจ transfer จริงที่สำเร็จแล้ว (2026-08-09, copy 388.6 GiB ไป teldrive):**

| ตรวจอะไร | จาก log | ที่บันทึกไว้ | ผล |
|---|---|---|---|
| จำนวนไฟล์ที่คัดลอกใหม่ | 131 | `transfers` = 130 | ต่างกัน 1 — poll สุดท้ายของ `core/stats` ตามไฟล์สุดท้ายไม่ทัน |
| ไบต์ที่โอน | 417,295,790,949 | 417,295,790,949 | **ตรงเป๊ะ** |
| error | 0 | `errors` = 0 | ตรง |
| ผลลัพธ์ | `# finished: ... (finished)` | `state` = finished, exit 0 | ตรง |
| credential ใน log | `rc_pass="***"` ทุกบรรทัด | — | ไม่รั่ว |

**ความครบของ chunk:** 131 ไฟล์ multi-thread **ทุก chunk ที่ประกาศไว้จบครบทุกไฟล์**
และทุกไฟล์ผ่าน `size = N OK` — ไม่มีอาการของหายเงียบแบบที่เจอเมื่อ 2026-08-08

> ไฟล์ 3 ไฟล์มี `Done sending chunk` น้อยกว่าจำนวน part ซึ่ง**ไม่ใช่ของขาด** — อ่านบรรทัดจริงแล้ว
> เป็นการอัปโหลดต่อจากของเดิม: chunk ที่เซิร์ฟเวอร์มีอยู่แล้วขึ้น `finished` ทันทีโดยไม่ต้องส่งใหม่
> **นี่คือเหตุผลที่ต้องนับจาก `chunk N/M ... finished` ไม่ใช่จาก `Done sending chunk`**

> 🐛 **เจอบั๊กจากการตรวจนี้ (แก้แล้ว):** `log_bytes` บันทึก 1,298,410 แต่ไฟล์จริง 1,615,532
> `JobLogWriter` นับ `QString::size()` ซึ่งเป็นจำนวนอักขระ UTF-16 ไม่ใช่ไบต์ ชื่อไฟล์ภาษาไทย
> จึงถูกนับเป็น 1 ทั้งที่เขียนจริง 3 ไบต์ — **ทำให้เพดาน 50 MB ที่กันไม่ให้ log ถมดิสก์
> กลายเป็นราว 150 MB ด้วย** แก้ให้นับ UTF-8 และให้ `finish()` อ่านขนาดจริงจากไฟล์

**การย้ายข้อมูลเดิม (ทดสอบกับข้อมูลจริงของผู้ใช้แล้ว 2026-08-09):**

| ไฟล์เดิม | ผล |
|---|---|
| `tasks.bin` (1,946 ไบต์ · 6 task) | เข้า `task` ครบ เรียงเหมือนเดิม → `tasks.bin.migrated` |
| `scheduler.conf` (535 ไบต์ · 1 รายการ) | เข้า `schedule` ครบ ผูก task ถูก → `scheduler.conf.migrated` |
| `queue.conf` (0 ไบต์) | ว่างอยู่แล้ว ไม่มีอะไรให้ย้าย ไฟล์คงเดิม |

> **นำเข้าครั้งเดียว** เงื่อนไขคือตารางว่าง แล้วเปลี่ยนชื่อไฟล์ ซึ่งเป็นตัวกันไม่ให้ทำซ้ำ
> **ห้ามลบไฟล์เดิม** — ถ้าต้องถอย ให้เปลี่ยนชื่อกลับแล้วลบ `rclone-browser.db`
>
> ⚠️ **ข้อ 18 คือทางถอยที่ต้องมีคนลอง** — ถ้าไม่มีไดรเวอร์ SQLite โปรแกรมจะกลับไปอ่าน/เขียน
> ไฟล์เดิมทั้งหมด เสียแค่ประวัติ ไม่ใช่ task (แต่ถ้าย้ายไปแล้วจะเห็น `.migrated` ซึ่งอ่านไม่เจอ
> จนกว่าจะเปลี่ยนชื่อกลับ)

---

### V-23 · เปิด debug log จาก Preferences และแยกไฟล์ตามระบบ

marker: `debug_log.h` · อัตโนมัติแล้ว: `tests/test_debug_log.cpp` (7 เคส)

เมื่อก่อนต้องตั้ง `RB_DEBUG=1` ในสภาพแวดล้อมก่อนเปิดโปรแกรม **ซึ่งแปลว่าคนที่เจอปัญหา
ต้องรู้วิธีตั้ง environment variable ก่อนถึงจะรายงานปัญหาได้** ตอนนี้ติ๊กช่องใน
Preferences → Misc. → Diagnostics แล้วเปิดโปรแกรมตามปกติก็ได้ log เลย
(`RB_DEBUG=1` ยังใช้ได้ สำหรับรันครั้งเดียวโดยไม่แตะค่าที่ตั้งไว้)

**หนึ่งโฟลเดอร์ต่อหนึ่งเรื่อง** ใต้ `logs/`:

| ที่อยู่ | เนื้อหา |
|---|---|
| `logs/all/all.txt` | ทุกอย่าง เรียงตามเวลาจริง |
| `logs/queue/queue.txt` | `rb.queue` |
| `logs/scheduler/scheduler.txt` | `rb.sched` |
| `logs/jobs/jobs.txt` | `rb.job` — สิ่งที่**โปรแกรม**ทำกับงาน |
| `logs/database/database.txt` | `rb.db` |
| `logs/app/app.txt` | `rb.app` และอย่างอื่นรวมถึงของ Qt เอง |
| `logs/transfers/` | สิ่งที่ **rclone** พูด ไฟล์ละหนึ่งรอบงาน |

ที่ต้องเป็นโฟลเดอร์ไม่ใช่กองรวมกัน เพราะแต่ละเรื่องเก็บไฟล์ที่หมุนแล้วไว้ข้างตัว —
6 เรื่อง × 10 รุ่น = 70 ไฟล์ที่ต้องอ่านผ่านก่อนจะเจออันที่ต้องการ

`logs/transfers/` เพิ่งแยกออกมา เมื่อก่อน log ของ rclone กองรวมอยู่ใน `logs/` เลย
**ประวัติเก่าเก็บ path เต็มไว้ (S13) ของเดิมจึงยังเปิดได้จากที่เดิม** ของใหม่ลงโฟลเดอร์ใหม่

**เขียนสองที่ทั้งไฟล์รวมและไฟล์แยก โดยตั้งใจ** — ถ้าแยกอย่างเดียวจะทำให้หาบั๊กยากขึ้น
ไม่ใช่ง่ายขึ้น เพราะสิ่งที่ปิดคดีคิวกับ scheduler คือการไล่ requestId เดียวจาก
`rb.sched` → `rb.queue` → `rb.job` ซึ่งมีอยู่เฉพาะตอนที่บรรทัดเรียงอยู่ด้วยกัน

ไฟล์โตเกินขนาดที่ตั้งไว้จะถูกหมุน: `queue.txt` → `queue.0.txt` → `queue.1.txt` ...
**ชื่อไฟล์ปัจจุบันไม่เปลี่ยน** ซึ่งเป็นประเด็นทั้งหมด — "log ของคิว" คือ `queue.txt` เสมอ
ไม่ใช่ไฟล์ที่ตั้งชื่อตามเวลาที่โปรแกรมบังเอิญเปิด

ขนาดไฟล์ตั้งเป็น **KB ต่ำสุด 16** ไม่ใช่ MB — เพราะเมกะไบต์เป็นหน่วยที่ผิดที่ปลายล่างของช่วง:
แค่จะตรวจว่าการหมุนไฟล์ทำงานไหมต้องเขียน log ให้ครบหนึ่งเมกก่อน ซึ่งนานจนไม่มีใครทำ
**ถ้าตรวจสอบไม่ได้ ก็เท่ากับไม่รู้ว่ามันทำงาน**

**ที่ test อัตโนมัติครอบแล้ว:** ปิดอยู่ต้องไม่สร้างไฟล์ · บรรทัดลงทั้งไฟล์รวมและไฟล์ของระบบตัวเอง
และไม่ปนไปไฟล์อื่น · ไฟล์เต็มแล้วหมุนและชื่อเดิมยังอยู่ · เก็บไม่เกินจำนวนที่ตั้ง ·
**เปิดโปรแกรมใหม่ต้องต่อท้าย ไม่ทับของเดิม** · รหัสผ่านและ token ไม่หลุด แต่ส่วนที่เหลือของบรรทัดยังอยู่ ·
**purge ลบเฉพาะไฟล์ที่หมุนแล้ว ไม่แตะไฟล์ที่กำลังเขียน** · ตั้งขนาดต่ำกว่าพื้นต้องถูกยกขึ้นมาที่พื้น

> **หมายเหตุ 1:** ขณะโปรแกรมเปิดอยู่ Explorer อาจแสดงขนาดไฟล์เป็น **0** เพราะ Windows
> ยังไม่อัปเดต directory entry ของไฟล์ที่ยังเปิดค้าง — เนื้อในมีอยู่จริง (ตรวจแล้ว
> `app.txt` แสดง 0 แต่จริงๆ 1140 ไบต์) และการหมุนไฟล์นับจากตัวเลขในหน่วยความจำ
> ไม่ได้นับจากตัวเลขที่ Explorer แสดง
>
> **หมายเหตุ 2:** เปลี่ยนขนาดจาก registry/ini ตรงๆ ขณะโปรแกรมเปิดอยู่**จะไม่มีผล**
> เพราะ `QSettings` เก็บค่าที่อ่านไว้ในหน่วยความจำ ต้องเปลี่ยนผ่าน Preferences
> (หรือปิด-เปิดโปรแกรม) — ถ้าเห็นไฟล์หมุนที่ขนาดไม่ตรงกับที่ตั้งไว้ ให้สงสัยข้อนี้ก่อน

| # | เกณฑ์ | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|---|
| 1 | ติ๊กช่องใน Preferences แล้วเปิดโปรแกรมปกติ (ไม่มี `RB_DEBUG`) → ได้โฟลเดอร์ครบ | ✅ **PASS** | AI / 2026-08-25 | ได้ `all/` `app/` `database/` `queue/` `scheduler/` (`jobs/` กับ `transfers/` สร้างเมื่อมีงานรันจริง) |
| 2 | ติ๊กแล้วกด OK → ได้ log ทันทีโดยไม่ต้องปิดเปิดโปรแกรม | ✅ **PASS** | totza2010 / 2026-08-25 | |
| 3 | เอาติ๊กออก → ไฟล์หยุดโต และลบไฟล์ได้ (ไม่ถูกล็อก) | ✅ **PASS** | totza2010 / 2026-08-25 | |
| 4 | ปุ่ม "Open log folder" เปิดโฟลเดอร์ถูกที่ | ✅ **PASS** | totza2010 / 2026-08-25 | |
| 5 | ตั้งขนาดเล็กแล้วเขียน log → เห็น `.0.txt` `.1.txt` ในโฟลเดอร์เดียวกัน | ✅ **PASS** | totza2010 / 2026-08-25 | ได้ `all/all.0.txt` 20 KB คู่กับ `all/all.txt` ที่เริ่มใหม่ · หน่วยเปลี่ยนเป็น **KB ต่ำสุด 16** แล้ว |
| 6 | รันงาน → มี `logs/transfers/` และประวัติเก่ายังกดเปิด log เดิมได้ | ✅ **PASS** | totza2010 / 2026-08-25 | |

---

### V-22 · scheduler พูดใน debug log แล้ว (ก่อนย้าย S4 ครึ่งหลัง)

marker: `debug_log.h` (หัวข้อ scheduler) · โค้ด: `scheduler_widget.cpp`, `main_window.cpp`

ก่อนหน้านี้ scheduler มี log **บรรทัดเดียว** คือตอนถึงเวลา ซึ่งแปลว่าอาการ
"ตั้งไว้แล้วไม่รัน" อ่านจาก log ไม่ออกเลย เพราะสามสาเหตุนี้เงียบเหมือนกันหมด:

| สาเหตุ | เมื่อก่อน | ตอนนี้ |
|---|---|---|
| ยังไม่ถึงเวลา | เงียบ | `check ... in=38609 s` |
| timer ไม่เดิน | เงียบ | **ไม่มีบรรทัด `check`** ← นี่คือคำตอบ |
| ถึงเวลาแล้วแต่ไม่รัน | เงียบ | `held ... reason=...` |

**บรรทัด `check` เต้นนาทีละครั้งต่อหนึ่งตาราง** ตั้งใจให้เป็นแบบนั้น —
ความเงียบถึงจะเป็นหลักฐานได้ ต้องมีเสียงตอนปกติก่อน (~130 ไบต์/บรรทัด
สองตาราง = ~16 KB/ชม. เทียบกับเพดาน 20 MB)

บรรทัดที่คุ้มที่สุดคือ `status ignored` — ตารางฟังเฉพาะข่าวของ requestId
ที่ตัวเองขอ ซึ่งถูกต้อง **แต่ก็เป็นวิธีที่มันค้างอยู่ที่ "in the queue" ตลอดกาล**
ถ้างานจบมาด้วย id ที่มันไม่รู้จัก ตอนนี้เห็นทั้งสองฝั่ง (`request=` กับ `waitingFor=`)

| # | เกณฑ์ | ผล | ผู้ทดสอบ / วันที่ | หมายเหตุ |
|---|---|---|---|---|
| 1 | เปิดด้วย `RB_DEBUG=1` → เห็น `check` ทุกตาราง นาทีละครั้ง | ✅ **PASS** | AI / 2026-08-25 | หลังย้ายเป็นนาฬิกาเดียว: timestamp ของทุกตารางตรงกันระดับมิลลิวินาที และ tick ที่วินาที :01 ทุกนาที |
| 2 | ตารางที่ paused ถึงเวลา → เห็น `held ... reason= schedule paused` | ⬜ | | |
| 3 | ตารางที่ activated ถึงเวลา → นาฬิกาเดียวยิงเอง | ✅ **PASS** | totza2010 / 2026-08-25 | ตั้งไว้ 15:13 mode=queue · เปิดโปรแกรม 15:12:32 · `job_run` เริ่ม **15:13:01** จบ 15:13:14 exit=0 · requestId `{3613e887}` ตรงกันทั้งใน `schedule` และ `job_run` |
| 4 | requestId เดียวกันตลอดสาย scheduler → queue → job | ✅ **PASS** | totza2010 / 2026-08-25 | กด Run เอง 14:43:40 → `{f974ba54}` เดียวกันครบ `queueing` → `enqueue` → `starting` → `start task` → `status "in the queue"` → `job ended finished` → `status "finished"` |
| 5 | ลบ task ที่ตารางชี้อยู่ แล้วรอถึงเวลา → เห็น `run refused: no such task` | ⬜ | | |
| 6 | sleep เครื่องข้ามเวลาที่ตั้งไว้ → เห็น `missed ... by=` ไม่ใช่รันย้อนหลัง | ⬜ | | |
| 7 | ตารางที่ไม่ได้เป็นเจ้าของงาน ต้องเห็น `status ignored` ไม่ใช่เงียบ | ✅ **PASS** | AI / 2026-08-25 | ตอนงานจบ 14:43:58 ตารางอีกอันขึ้น `status ignored ... waitingFor= ""` ตามที่ออกแบบ |

> ข้อ 5 กับ 7 คือสองอาการที่เคยเจอมาแล้วจริง — ใส่ไว้เพื่อให้รอบหน้าตอบได้จาก log
> โดยไม่ต้องเดา

> **ข้อ 3 ผ่านแล้ว 2026-08-25** ตั้งตารางไว้ 15:13 แล้วเปิดโปรแกรม 15:12:32
> งานเริ่มจริง **15:13:01** — ตรงกับจังหวะของนาฬิกาเดียวพอดี (tick ที่วินาที `:01`
> ของทุกนาที) ซึ่งเป็นหลักฐานว่ามันมาจากนาฬิกาใหม่ ไม่ใช่ timer เก่าใน widget
>
> รอบนั้นเปิดโปรแกรมโดยไม่มี `RB_DEBUG=1` จึงไม่มี log — ยืนยันจากฐานข้อมูลแทน
> (`job_run` กับ `schedule` ถือ requestId `{3613e887}` เดียวกัน) **ซึ่งเป็นหลักฐาน
> ที่แข็งกว่า log ด้วยซ้ำ เพราะเป็นสิ่งที่โปรแกรมบันทึกไว้เอง ไม่ใช่สิ่งที่มันเล่าว่าทำ**

---

## วิธีเพิ่มรายการใหม่

เมื่อแก้อะไรที่ต้องมีคนทดสอบ ให้ทำสองอย่างคู่กัน:

1. ทิ้ง marker ไว้ตรงโค้ด พร้อมรหัสรายการ:
   ```cpp
   // TEST: (V-07) <ทดสอบอย่างไร และเกณฑ์ผ่านคืออะไร>
   ```
2. เพิ่มหัวข้อ `### V-07 · ...` ในไฟล์นี้ พร้อมตารางให้ผู้ทดสอบกรอก

รหัสต้องไม่ซ้ำและไม่นำกลับมาใช้ใหม่ แม้รายการเก่าจะเลิกใช้แล้ว
