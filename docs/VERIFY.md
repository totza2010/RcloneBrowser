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

### V-24 · แยกชั้น UI กับ backend ให้ขาด

marker: `docs/LAYER-SPLIT.md` · หมวด log ใหม่: `rb.script` `rb.remote` `rb.task`

**เกณฑ์รับงานของหัวข้อนี้ไม่ใช่ "โปรแกรมทำงานถูก"** เพราะโปรแกรมทำงานถูกอยู่แล้วตอนที่
ตรรกะยังอยู่ในหน้าต่าง เกณฑ์คือ **อ่าน log แล้วเห็นว่า core เป็นคนตัดสินใจ ไม่ใช่หน้าต่าง**

ทุกแถวจึงมีคอลัมน์ *"บรรทัดที่ต้องเห็น"* — **ถ้าไม่มีบรรทัดนั้น แปลว่ายังไม่ได้ย้าย
ต่อให้กดแล้วได้ผลถูกก็ตาม** นี่คือบทเรียนจากการย้ายคิว: หลังบ้านถูกไม่ได้แปลว่าย้ายเสร็จ
ตราบใดที่หน้าต่างยังมีทางเดินที่สอง

เปิด `RB_DEBUG=1` หรือติ๊ก Preferences → Misc. → Diagnostics ก่อนทดสอบทุกข้อ

#### บล็อก 1 · รัน script

| # | ทำอะไร | บรรทัดที่ต้องเห็น | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1.1 | ตั้ง script ตอนคิวว่าง แล้วปล่อยคิวจนหมด | `rb.script: running reason= "queue-empty"` | ✅ **PASS** | totza2010 / 2026-08-26 · คิว 3 งาน ยิงตอนงานสุดท้ายจบ |
| 1.2 | ตั้ง script ตอนงานเริ่ม แล้วเริ่มงาน | `rb.script: running reason= "transfer-started"` | ✅ **PASS** | totza2010 / 2026-08-26 · 9 ครั้ง |
| 1.3 | ตั้ง script ตอนงานจบ | `rb.script: running reason= "last-transfer-finished"` | ✅ **PASS** | totza2010 / 2026-08-26 · 3 ครั้ง |
| 1.4 | ชี้ไปยังไฟล์ที่ไม่มีอยู่ | `rb.script: refused` พร้อมเหตุผล **ไม่ใช่เงียบ** | ✅ **PASS** | totza2010 / 2026-08-26 · พิมพ์ `test-hooxk.cmd` → `refused ... because there is no such file` |
| 1.5 | script จบด้วย exit code ไม่ใช่ 0 | `rb.script: ended exit=` | ✅ **PASS** | totza2010 / 2026-08-26 · `test-hook.cmd fail-case 3` → `ended reason= "queue-empty" exit= 3` |
| 1.8 | ตั้ง "after every one" แล้วรันคิว 3 งาน | `reason= "transfer-finished"` **3 ครั้ง** | ⬜ | |
| 1.9 | ตั้ง "after the last one" แล้วรันคิว 3 งาน | `reason= "last-transfer-finished"` **ครั้งเดียว** | ✅ **PASS** | totza2010 / 2026-08-26 · คิว 3 งาน 11:13 |
| 1.6 | ปิดสวิตช์ script แต่ยังมี path ค้างอยู่ | **ไม่มีบรรทัด `running`** | ✅ **PASS** | totza2010 / 2026-08-26 · ปิด `jobStartScriptRun` โดย `transferOnScript` ยังมี path → งาน 11:08:37 กับ 11:08:53 ไม่มีบรรทัด `transfer-started` |
| 1.7 | `--run-task` จาก command line + ตั้ง script ตอนงานเริ่ม/จบ | `rb.script: running` **ทั้งที่ไม่มีหน้าต่างเลย** | ✅ **PASS** | AI / 2026-08-26 |

**บล็อก 1 ผ่านครบ 7/7 · 2026-08-26**

ตอนคิวหมด ยิงสองอันคนละเหตุผลห่างกัน 30 มิลลิวินาที และแยกจากกันได้ในบรรทัดเดียว:

```
11:13:31.043  rb.script: running reason= "queue-empty"             args= 2
11:13:31.073  rb.script: running reason= "last-transfer-finished"  args= 1
11:13:31.485  rb.script: ended   reason= "queue-empty"             exit= 3
11:13:31.514  rb.script: ended   reason= "last-transfer-finished"  exit= 0
```

คนละการตั้งค่า คนละ script คนละ exit code — **ถ้าไม่มี `reason=` ในบรรทัด
สองอันนี้จะแยกจากกันไม่ออกเลย** ซึ่งเป็นเหตุผลที่ใส่ไว้ตั้งแต่แรก

> ข้อ 1.7 คือข้อที่พิสูจน์บล็อกนี้จริงๆ ข้ออื่นพิสูจน์แค่ว่ายังไม่พัง
>
> **และรอบแรกมันไม่ผ่าน** — งานรันจบ exit 0 แต่ไม่มีบรรทัด `rb.script` เลย
> สาเหตุคือ `ScriptRunner::install()` ถูกเรียกจาก `MainWindow` เท่านั้น และ
> `TaskRunner` ไม่ได้เดินผ่าน `JobRegistry` (มันรัน rclone เอง) สัญญาณ
> `jobStarted`/`jobFinished` จึงไม่เคยเกิดในโหมดไม่มีหน้าต่าง
>
> **ย้ายความสามารถเข้า core แล้ว แต่การต่อสายยังอยู่ในหน้าต่าง** — ซึ่งเป็นอาการ
> "แยกครึ่งเดียว" ที่เอกสาร LAYER-SPLIT เตือนไว้เอง แก้โดยให้ `TaskRunner`
> เรียก `ScriptRunner::run(..., waitForIt=true)` ตรงๆ ที่จุดเริ่มและจุดจบ
>
> `waitForIt` จำเป็นเพราะ process กำลังจะจบ — **script ที่ไม่รอ คือ script ที่ถูกฆ่า
> หนึ่งวินาทีหลังเริ่ม**

**หลักฐานรอบที่ผ่าน:**

```
11:01:01 rb.script: running reason= "transfer-started"        exit= 0
11:01:07 rb.script: running reason= "last-transfer-finished"  exit= 0
```

และ `hook-runs.txt` ที่ script เขียนเองยืนยันว่ามันทำงานจริง ไม่ใช่แค่ log บอกว่าสั่งไป

> **`transferOffScript` ยิงตอนไหน — ตัดสินแล้ว 2026-08-26: ให้ผู้ใช้เลือก**
>
> **อาการ:** รัน task เดี่ยวๆ script ยิงทุกครั้ง แต่พอเอาเข้าคิว 3 งาน ยิงครั้งเดียว
> ตอนงานสุดท้าย เพราะ `JobQueue` เริ่มงานถัดไปในเสี้ยววินาทีเดียวกับที่งานก่อนจบ
> จึงไม่มีจังหวะไหนเลยที่ "ไม่เหลืองานวิ่งอยู่" จนกว่าคิวจะหมด
>
> **ของเดิมไม่เหมือนกัน** อ่านจาก `git show master:src/main_window.cpp` — ใน handler
> เดียวกันเรียงว่า `--mTransferJobCount` → เช็ค `== 0` → ยิง script → **แล้วค่อย**
> ไล่คิวไปงานถัดไป คิวเดินทีละงาน ตัวนับจึงแตะ 0 ทุกครั้ง = **ยิงทุกงาน**
>
> **แต่ป้ายในหน้า Preferences เขียนว่า `Last transfer job ends:`** — ซึ่งบรรยาย
> พฤติกรรมใหม่ ไม่ใช่ของเดิม กล่าวคือ **ของเดิมไม่ตรงกับสิ่งที่ตัวเองสัญญาไว้กับผู้ใช้**
> และมี `Finished queue script:` แยกไว้อยู่แล้วสำหรับ "ทั้งคิวจบ"
> ถ้ายิงทุกงาน สองการตั้งค่านี้จะทำเรื่องเดียวกันเกือบทั้งหมด
>
> **ทางออก: ไม่เลือกแทนผู้ใช้** เพิ่มตัวเลือกใน Preferences ใต้ช่องเดิม
> ป้ายเปลี่ยนเป็น `Transfer job ends:` แล้วมีสองปุ่ม
>
> | ตัวเลือก | คิว 3 งาน | log |
> |---|---|---|
> | **after the last one** (ค่าเริ่มต้น) | ยิง 1 ครั้ง | `reason= "last-transfer-finished"` |
> | after every one | ยิง 3 ครั้ง | `reason= "transfer-finished"` |
>
> ค่าเริ่มต้นเป็น "last" เพราะเป็นสิ่งที่ป้ายสัญญาไว้มาตลอด ส่วนพฤติกรรมของเดิม
> ยังเลือกได้ **ไม่มีใครเสียอะไรไป**
>
> **สองเหตุผลนี้แยกกันใน log โดยตั้งใจ** — ถ้าใช้ชื่อเดียวกัน คำถาม "ทำไมมันรัน
> สามครั้ง" จะต้องไปไล่ดูการตั้งค่าเอา แทนที่จะอ่านได้จากบรรทัดนั้นเลย
>
> **แล้วมันซ้ำกับ "Finished queue script" ไหม** — ซ้อนกันบางกรณี แต่ไม่ใช่อันเดียวกัน
>
> | สถานการณ์ | queue-empty | transfer ends (last) |
> |---|---|---|
> | รันงานเดี่ยว ไม่ผ่านคิว | ❌ | ✅ |
> | คิวหมด แต่มีงานที่สั่งเองยังวิ่งอยู่ | ✅ | ❌ |
> | คิวหมด และไม่เหลืออะไรวิ่ง | ✅ | ✅ (ยิงทั้งคู่) |
>
> อันหนึ่งเรื่อง **คิว** อีกอันเรื่อง **งาน** — บังเอิญตรงกันบ่อยเพราะคนส่วนใหญ่ใช้คิว
>
> และเหตุผลที่แท้จริงของตัวเลือก "after every one": ของเดิมมี hook **ตอนงานเริ่ม
> ทุกงาน** แต่ไม่มี hook ตอนงานจบทุกงาน — **มีครึ่งเดียวของคู่** ตัวเลือกนี้เติมอีกครึ่ง
> ไม่ได้ไปซ้ำกับเรื่องคิว

**สคริปต์สำหรับทดสอบ:** `scripts/test-hook.cmd` (Windows) · `scripts/test-hook.sh` (Linux/macOS)
รับ `<label> [exit-code] [seconds]` แล้วต่อท้าย `logs/hook-runs.txt` หนึ่งบรรทัดต่อการรัน
ตั้งใน Preferences เช่น

```
C:\...\RcloneBrowser\scripts\test-hook.cmd queue-empty
C:\...\RcloneBrowser\scripts\test-hook.cmd fail-case 3
```

#### บล็อก 2 · ค้นหา remote และเวอร์ชัน — **ผ่านครบ 7/7 · 2026-08-26**

| # | ทำอะไร | บรรทัดที่ต้องเห็น | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 2.1 | เปิดโปรแกรม | `rb.remote: listing remotes with <path>` แล้ว `found n=` | ✅ **PASS** | AI / 2026-08-26 · `found n= 6` |
| 2.2 | เพิ่ม remote ใน rclone config แล้วกด refresh | `found n=` เพิ่มขึ้นหนึ่ง | ✅ **PASS** | totza2010 / 2026-08-26 · เพิ่ม `ggg` ผ่าน `rclone config` แล้วเปิดใช้งานได้ทันที |
| 2.3 | ตั้ง path rclone ผิด | `rb.remote: refused` พร้อมเหตุผล + กล่องข้อความ | ✅ **PASS** (อัตโนมัติ) | AI / 2026-08-26 · `anRcloneThatIsNotThereReportsAFailure` · ส่วนกล่องข้อความยังต้องกดดู |
| 2.4 | remote ที่ **type** มี `:` | ไม่หายไป (ของเดิมหาย — ดูหมายเหตุ) | ✅ **PASS** (อัตโนมัติ) | AI / 2026-08-26 · `aTypeWithAColonIsStillARemote` |
| 2.5 | เปิดโปรแกรม | `rb.remote: rclone version "x" at "<path>"` | ✅ **PASS** | AI / 2026-08-26 · `1.73.1` |
| 2.6 | config ที่ใส่รหัสผ่านไว้ | แยก "ต้องใส่รหัส" ออกจาก "ล้มเหลว" | ✅ **PASS** (อัตโนมัติ) | AI / 2026-08-26 · `anEncryptedConfigIsAQuestionNotAFailure` · การถามรหัสจริงยังต้องมี config ที่เข้ารหัส |
| 2.7 | rclone เก่ามากจนไม่มี `listremotes` | ขึ้นข้อความให้อัปเกรด ไม่ใช่ "verify path" | ✅ **PASS** (อัตโนมัติ) | AI / 2026-08-26 · `anRcloneWithoutListremotesIsToldApart` |

**สามข้อที่เคยทดสอบด้วยมือไม่ได้ ตอนนี้ทดสอบอัตโนมัติแล้ว** — 2.3, 2.6, 2.7 ต้องใช้
rclone ที่หายไป · config ที่เข้ารหัส · binary อายุหลายปี ซึ่งแปลว่า **ไม่เคยถูกทดสอบเลย**

แก้โดยแยกการตัดสินว่า "ล้มเหลวเพราะอะไร" ออกมาเป็น `ClassifyListRemotesFailure()`
รับข้อความ stderr คืนหนึ่งในสามคำตอบ — กฎกลายเป็นสิ่งที่ตรวจได้โดยไม่ต้องมีของจริง
ส่วนที่เหลือให้ทดสอบด้วยมือคือ**กล่องข้อความบนจอ** ไม่ใช่ตัวกฎ

> จับได้ระหว่างเขียน test: rclone ที่ไม่มีอยู่ **รายงานความล้มเหลวทันทีแบบ synchronous**
> ตั้งแต่ตอนเรียก `refresh()` ยังไม่ทันกลับมาที่ event loop — `QSignalSpy::wait()`
> จึงรอสัญญาณตัวที่สองที่ไม่มีวันมา

**ที่แก้ไปโดยไม่ได้ตั้งใจจะแก้:** โค้ดเดิมในหน้าต่างแยก `name: type` ด้วย
`line.split(':')` แล้วยืนยันว่าต้องได้ **สองชิ้นพอดี** — remote ที่ type มี `:`
อยู่ด้วยจะถูกข้ามไปเงียบๆ `ParseListRemotes()` แยกที่ `:` **ตัวแรกตัวเดียว**
ซึ่งถูกต้องกว่า เพราะชื่อ remote ห้ามมี `:` อยู่แล้ว (rclone บังคับ) แต่ type
เป็นอะไรก็ได้ที่ rclone พิมพ์ออกมา

> **ข้อสังเกตที่ยังไม่ได้ไล่:** ตอนเปิดโปรแกรมมี `listing remotes` **สองครั้ง**
> ห่างกัน ~2.4 วินาที (12:54:12.683 และ 12:54:15.078) ครั้งแรกมาจาก
> `rcloneGetVersion()` ที่สำเร็จ ครั้งที่สองยังไม่รู้ว่าใครเรียก
>
> เป็นพฤติกรรมเดิมที่เพิ่งมองเห็นได้ ไม่ใช่ของใหม่ — รูปแบบเดียวกับตอนที่พบว่า
> คิวถูกอ่านสองรอบตอนเปิดโปรแกรม **ไม่ได้แก้ในรอบนี้เพราะยังไม่รู้สาเหตุ
> และการเดาแล้วแก้คือวิธีสร้างบั๊กใหม่**

#### บล็อก 3 · สร้างและแก้ task — **ผ่านครบ 8/8 · 2026-08-26**

| # | ทำอะไร | บรรทัดที่ต้องเห็น | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 3.1 | สร้าง task ใหม่จาก Transfer dialog | `rb.task: created id= name=` | ✅ **PASS** | totza2010 / 2026-08-26 · 13:47:08 |
| 3.2 | แก้ task เดิมแล้วบันทึก | `rb.task: updated id= name=` | ✅ **PASS** | totza2010 / 2026-08-26 · 13:47:25 · id เดิม |
| 3.3 | ลบ task | `rb.task: deleted id= name=` | ✅ **PASS** | totza2010 / 2026-08-26 · 13:49:42 `ihulhul` |
| 3.4 | สร้างโดยไม่ตั้งชื่อ (ติ๊ก auto name) | ได้ชื่อรูปแบบ `_tmp_<วันที่>_<เวลา>_<remote>` | ✅ **PASS** | totza2010 / 2026-08-26 · `_tmp_26Aug2026_134708_tgdrive_main_02` ตรงทั้งเวลาและชื่อ remote |
| 3.5 | บันทึกโดยไม่ใส่ปลายทาง | กล่องข้อความ + **บันทึกไม่สำเร็จ** | ✅ **PASS** | totza2010 / 2026-08-26 · ข้อความมาจาก `TaskBuilder::Explain()` |
| 3.6 | เปลี่ยนชื่อ task ที่มีตารางเวลาชี้อยู่ | ชื่อในหน้า scheduler เปลี่ยนตาม และ `(*Sch)` ยังอยู่ | ✅ **PASS** | totza2010 / 2026-08-26 · 13:50:06 เปลี่ยนเป็น `...OneDrivwcdvs` · ตารางยังชี้ id เดิม และ `mTaskName` ที่เก็บไว้ตามไปด้วย |
| 3.7 | **สร้าง task โดยไม่มี dialog เลย** | `TaskBuilder::Create()` คืน id และ task อยู่ในลิสต์ | ✅ **PASS** | AI / 2026-08-26 · `test_task_builder` |
| 3.8 | **task ที่ไม่ครบต้องไม่ถูกบันทึก** | คืนค่าว่าง + ไม่เข้าลิสต์ | ✅ **PASS** | AI / 2026-08-26 |

**หมายเหตุจากการทดสอบ 3.6:** ชื่อของ *ตารางเวลา* เอง (`Scheduled task: ..._OneDrive2w`)
**ไม่ได้เปลี่ยนตาม** ซึ่งถูกต้องและเป็นไปตามที่ตัดสินไว้ก่อนหน้า — ชื่อตารางเป็นของตารางเอง
ส่วนชื่อ task ที่แสดงในการ์ดอ่านจาก id ทุกครั้ง

> **ข้อ 3.8 คือเหตุผลที่ `Create()` มีอยู่** ถ้า API บันทึก task ที่ไม่ผ่านการตรวจ
> มันจะนั่งอยู่ในลิสต์ดูปกติทุกอย่าง **จนถึงคืนที่มันควรจะรัน**
>
> **บั๊กที่ test จับได้ระหว่างเขียน:** `ListOfJobOptions::Persist()` คืนค่าว่า
> "อันนี้ใหม่ไหม" **ไม่ใช่ "บันทึกสำเร็จไหม"** — ตอนแรกผมอ่านผิดเป็นค่าความสำเร็จ
> ซึ่งจะทำให้**การแก้ task ที่มีอยู่แล้วทุกครั้งถูกรายงานว่าล้มเหลว**

> ข้อ 3.7 เป็นข้อเดียวกันกับที่การันตี S4 สำเร็จ — **ถ้า round trip ไม่ตรง ผู้ใช้เสียการตั้งค่าเงียบๆ**

#### บล็อก 4 · เดินดูไฟล์ใน remote — **ผ่าน 5/5 · 2026-08-26**

| # | ทำอะไร | บรรทัดที่ต้องเห็น | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 4.1 | เปิดแท็บ remote | `rb.remote: reading path=` | ✅ **PASS** | totza2010 / 2026-08-26 |
| 4.2 | เข้าโฟลเดอร์ย่อย | `rb.remote: reading path=` ที่ลึกลงไป | ✅ **PASS** | totza2010 / 2026-08-26 · เห็นการโหลดล่วงหน้าหนึ่งชั้นด้วย |
| 4.3 | โฟลเดอร์ที่เข้าไม่ได้ | `rb.remote: failed path= reason=` | ✅ **PASS** | totza2010 / 2026-08-26 · แก้ url ของ remote ให้พัง · **ไม่หมุนค้างแล้ว** แต่บนจอเงียบ — ดูแผนข้างล่าง |
| 4.4 | ชื่อไฟล์ภาษาไทย / อักขระพิเศษ | ชื่อไม่เพี้ยนใน log และบนจอ | ✅ **PASS** | AI / 2026-08-26 · 238 path ที่มีอักขระนอก ASCII อ่านได้ครบ เช่น `饕top` `陆十九` `木脑阔先森` และชื่อที่มี `&` |
| 4.5 | เปิดโฟลเดอร์ใหญ่ | ไม่ค้าง และ log ไม่ท่วม | ✅ **PASS** | AI / 2026-08-26 · `tgdrive_adult_01:XX2` **7,103 รายการ** · หนึ่งบรรทัดต่อหนึ่งโฟลเดอร์ ไม่ใช่ต่อหนึ่งไฟล์ |

> **ช่องว่างที่พบจากการทดสอบ 4.3 — บนจอยังเงียบ**
>
> `DirectoryListing` รู้เหตุผลและเขียนลง log แล้ว แต่บนจอได้แค่**รายการว่าง**
> ซึ่งอ่านได้ว่า "โฟลเดอร์นี้ไม่มีอะไร" ทั้งที่ความจริงคือ "อ่านไม่ได้"
>
> จาก log ของการทดสอบครั้งนี้ เหตุผลจริงมีสองแบบและต่างกันมาก:
>
> ```
> CRITICAL: Failed to load config file ...:
>           The process cannot access the file because it is being used by another process
> CRITICAL: Failed to create file system for "tgdrive_main_01:...":
>           didn't find section in config file ("tgdrive_main_01")
> ```
>
> อันแรก**ชั่วคราว** (ไฟล์ config ถูกล็อกอยู่ กดใหม่อีกทีก็ได้)
> อันหลัง**ถาวร** (remote นั้นไม่มีอยู่แล้ว) — คนละคำแนะนำกันสิ้นเชิง
> แต่บนจอทั้งสองแบบหน้าตาเหมือนกันเป๊ะ **แผนอยู่ใน `LAYER-SPLIT.md`**

#### บล็อก 5 · เครื่องมือในแท็บ remote — **ผ่าน · 2026-08-26**

| # | ทำอะไร | บรรทัดที่ต้องเห็น | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 5.1 | สั่ง check | `kind= "check"` + แถวในประวัติ | ✅ **PASS** | totza2010 / 2026-08-26 · 2 ครั้ง (`state=error` ตรงกับ ERROR ที่เห็นในหน้าต่าง) |
| 5.2 | สั่ง dedupe | `kind= "dedupe"` | ✅ **PASS** | totza2010 / 2026-08-26 |
| 5.3 | สั่ง export | `kind= "export"` + **ไฟล์ที่เขียนออกมาถูกต้อง** | ✅ **PASS** | totza2010 / 2026-08-26 · 2 ครั้ง · output เดินทางจาก `RunningJob::outputLine` แทน `readAll()` แล้วยังเขียนไฟล์ได้เหมือนเดิม |
| 5.4 | ต้องมีแถวใน `job_run` | นับตาม kind ได้ | ✅ **PASS** | AI / 2026-08-26 · `export 2 · check 2 · tree 1 · link 1 · dedupe 1 · about 1` แยกประเภทได้ทั้งหมด |
| 5.5 | teldrive → ปุ่ม dedupe/cleanup ต้องปิดอยู่ (V-09) | กดไม่ได้ **หลังคลิกในทรีแล้วด้วย** | ✅ **PASS** | totza2010 / 2026-08-26 · เจอบั๊กจากข้อนี้ ดูข้างล่าง |
| 5.6 | DirTree · Size · Link · Info · Cleanup | มีแถวในประวัติเหมือนกัน | ✅ **PASS** | totza2010 / 2026-08-26 · `cleanup` ไม่ได้ลอง (teldrive ปิดอยู่ ถูกแล้ว) |
| 5.7 | Copy / Move ในแท็บ remote | `kind= "transfer"` + การ์ดในแท็บ Jobs | ✅ **PASS** | totza2010 / 2026-08-26 |

> **บั๊กที่ข้อ 5.5 จับได้ — capability ถูกเขียนทับทุกครั้งที่คลิก**
>
> teldrive ตอบว่า `"CleanUp": false` และ `applyCapabilities()` ปิดปุ่มถูกต้องตอนโหลด
> **แต่ `processSelection()` มี `ui.cleanup->setDisabled(false)` อยู่ 3 ที่** ซึ่งทำงาน
> ทุกครั้งที่เลือกอะไรในทรี — คลิกแรกล้างผลทิ้ง
>
> เบาะแสอยู่ในภาพหน้าจอเอง: แถบสถานะเขียนว่า *"This backend has nothing to
> clean up"* ซึ่งเป็นข้อความที่ตั้งเฉพาะตอน `caps.cleanUp == false`
> **อ่าน capability ถูก แต่ผลถูกลบทีหลัง**
>
> กินอีกสองปุ่มด้วย: `link` (ทับ `caps.publicLink`) และ `actionDedupe`
> (ทับ `caps.duplicateFiles`) — ยังไม่มีใครเจอเพราะยังไม่มีใครลองกับ backend
> ที่ทำ public link ไม่ได้
>
> **แก้โดยแยกคำถามสองข้อ** — "สิ่งที่เลือกอยู่ทำได้ไหม" เป็นของกฎการเลือก
> ส่วน "backend นี้ทำเป็นไหม" เป็นของ capability และมีสิทธิ์วีโต้ท้ายสุดเสมอ
> (`applyCapabilityLimits()`) พร้อมเก็บ `mCaps` ไว้ถามซ้ำได้ —
> **การอ่านครั้งเดียวแล้วลงมือครั้งเดียวคือตัวบั๊กเอง**

#### ทั้งระบบ · ข้อสอบจริง

| # | ทำอะไร | เกณฑ์ | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 6.1 | ไล่ `logs/all/all.txt` หนึ่งรอบงานเต็ม | ไล่ requestId เดียวได้ตั้งแต่ `rb.sched` → `rb.queue` → `rb.job` → `rb.script` | ⬜ | |
| 6.2 | `grep` หาบรรทัดที่ตัดสินใจแล้วมาจากหน้าต่าง | **ต้องไม่เจอ** — ทุกการตัดสินใจมาจากหมวด core | ⬜ | |
| 6.3 | `python scripts/check_layers.py` | ไฟล์ปลอด GUI ต้องมากกว่า 62/105 อย่างมีนัย | ✅ **PASS** | AI / 2026-08-26 · **72/115** · 0 violation |
| 6.4 | **รันโดยไม่มีหน้าต่างเลย** (`--daemon`) | ตารางเวลา คิว งาน ประวัติ script ทำงานครบ | ✅ **PASS** | totza2010 / 2026-08-26 · เห็น `rb.app: starting` → `rb.script: listening` → `rb.app: loaded schedules= 2` → นาฬิกาเดินทุกนาที **โดยไม่มี `MainWindow` เกิดขึ้นเลย** |
| 6.5 | โหมดหน้าต่าง: คิวที่ค้างอยู่ต้องทำงานต่อตอนเปิดใหม่ | งานเดินต่อ ไม่ใช่หายไป | ✅ **PASS** | totza2010 / 2026-08-26 · **เจอบั๊กจากข้อนี้ ดูข้างล่าง** |
| 6.6 | โหมดหน้าต่าง: กด Run บนการ์ดตารางเวลา | เดินทาง `AppCore::runNow()` เส้นเดียวกับนาฬิกา | ✅ **PASS** | totza2010 / 2026-08-26 |

> **6.4 คือข้อสอบจริง** ทุกอย่างข้างบนคือการเตรียมตัวสอบ ถ้าข้อนี้ผ่าน แปลว่าแยกขาดจริง
> และ daemon (S11/S12) เป็นแค่การใส่ HTTP ครอบของที่ทำงานอยู่แล้ว

> **บั๊กที่ข้อ 6.5 จับได้ — ลำดับกลับหัวหลังแยก `AppCore`**
>
> `AppCore::start()` อ่านคิวที่ค้างไว้แล้วเริ่มงานแรกทันที **แต่ `SetRclone()` อยู่ใน
> constructor ของ `MainWindow` ซึ่งทำงานทีหลัง** — rclone จึงถูกเรียกด้วย path ว่าง
> ล้มใน 16 มิลลิวินาที และคิวกินรายการนั้นทิ้ง อาการที่ผู้ใช้เห็นคือ **"คิวไม่กลับมา"**
>
> ```
> rb.job: running "" with 25 arguments        ← log บอกตรงๆ
> rb.job: job ended status= "error" exit= 1
> ```
>
> **ของเดิมไม่พังเพราะหน้าต่างเป็นทั้งคนตั้ง path และคนอ่านคิว** ลำดับจึงถูกโดยบังเอิญ
> พอแยกออกจากกัน ความบังเอิญนั้นหายไป — เป็นราคาที่ต้องจ่ายของการแยกชั้น
> และเป็นเหตุผลที่ต้องทดสอบด้วยการรันจริง ไม่ใช่แค่คอมไพล์ผ่าน
>
> แก้โดยให้ `AppCore::start()` อ่าน path ของ rclone **เป็นอย่างแรก** — core ที่รันงานได้
> ต้องรู้ก่อนว่า rclone อยู่ไหน ไม่ว่าโหมดไหน · `tests/test_app_core.cpp` เคสแรกคุมไว้แล้ว

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

### V-25 · สั่งงานผ่าน API และตามเหตุการณ์สด

marker: `docs/HTTP-API.md` · หมวด log: `rb.app`

เปิดด้วย `RcloneBrowser.exe --daemon` แล้วจดค่า token ที่มันพิมพ์ออกมา

```bash
TOKEN=<ที่ daemon พิมพ์ออกมา>
H="Authorization: Bearer $TOKEN"
API=http://127.0.0.1:19999/api/v1
```

**เกณฑ์ของหัวข้อนี้ไม่ใช่ "สั่งแล้วได้ผล"** — เกณฑ์คือ **สิ่งที่ API สั่ง ต้องไปโผล่ในที่เดียวกับ
ที่หน้าต่างสั่ง** คือคิวเดียวกัน ประวัติเดียวกัน requestId เดียวกัน ถ้ามันทำงานถูกแต่มีเส้นทาง
ของตัวเอง แปลว่ายังไม่เสร็จ — บทเรียนเดิมจากการย้ายคิว

#### บล็อก 1 · คำสั่ง

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1.1 | `curl -X POST -H "$H" $API/tasks/{id}/enqueue` | `202` + `requestId` และ **งานโผล่ในแท็บคิวของหน้าต่างที่เปิดอยู่** | ⬜ | |
| 1.2 | `curl -X POST -H "$H" $API/queue/start` | คิวเดิน · `rb.app: API set the queue running to true` | ⬜ | |
| 1.3 | `curl -X POST -H "$H" $API/tasks/{id}/run` ตอนงานนั้นวิ่งอยู่แล้ว | `409` **ไม่ใช่ 202** พร้อมข้อความว่ากำลังรันอยู่ | ⬜ | |
| 1.4 | `curl -X POST -H "$H" $API/jobs/{requestId}/stop` | `202` แล้วงานหยุดจริง · ประวัติขึ้นว่า `stopped` **ไม่ใช่ `error`** | ⬜ | |
| 1.5 | `curl -X DELETE -H "$H" $API/queue/{requestId}` ของตัวที่กำลังรัน | `409` บอกให้ไป stop แทน | ⬜ | |
| 1.6 | `curl -X POST -H "$H" $API/schedules/{id}/run` | `202` + งานเริ่ม · ถ้ารอบก่อนยังไม่จบต้องได้ `409` **พร้อมเหตุผลของมันเอง** | ⬜ | |
| 1.7 | `curl -X POST $API/queue/start` **ไม่ใส่ token** | `401` และ **คิวต้องไม่เดิน** | ⬜ | |
| 1.8 | `curl -X POST -H "$H" $API/tasks` (สร้าง task) | `405` บอกว่าอ่านได้ สั่งไม่ได้ | ⬜ | |
| 1.9 | สั่งจาก API แล้วดูประวัติ | แถวใน History มี `startedBy` = `api` — **แยกออกจาก `task` / `queue` / `scheduler` ได้** | ⬜ | |

#### บล็อก 2 · เหตุการณ์สด

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 2.1 | `curl -N -H "$H" $API/events` | `event: hello` **ทันที** ไม่ต้องรอให้มีอะไรเกิดขึ้น | ✅ **PASS** | totza2010 / 2026-08-31 · `{"queueRunning":true,"queued":0,"runningJobs":0}` แล้วสายค้างรออยู่ |
| 2.2 | ระหว่างที่ค้างสายไว้ ไปกดอะไรก็ได้ในหน้าต่าง | `event: queue` / `event: job` ไหลออกมา — **สิ่งที่หน้าต่างทำ ไม่ใช่แค่สิ่งที่ API สั่ง** | ⬜ | |
| 2.3 | ปล่อยทิ้งไว้เกิน 20 วินาทีโดยไม่ทำอะไร | บรรทัด `: still here` | ⬜ | |
| 2.4 | รัน transfer แล้วดูสาย | `event: job` มีบล็อก `stats` อัปเดตเรื่อยๆ ประมาณวินาทีละครั้ง | ⬜ | |
| 2.5 | ปิด curl แล้วเปิดใหม่หลายรอบ | daemon ไม่พัง · `rb.app: an event stream opened; listeners=` นับถูก | ⬜ | |

#### บล็อก 3 · log ของงาน

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 3.1 | `curl -H "$H" $API/jobs/{requestId}/log` ของงานที่**จบไปแล้ว** | ได้เนื้อ log จริง — ไม่ใช่ 404 | ⬜ | |
| 3.2 | `...?tail=5` | ได้ 5 บรรทัดสุดท้าย | ⬜ | |
| 3.3 | ขอ log ของ requestId มั่วๆ | `404` ว่า `no such job` — **คนละข้อความกับงานที่มีจริงแต่ไม่ได้เขียน log** | ⬜ | |

#### บล็อก 4 · สองอย่างที่เจอระหว่างทาง (ทดสอบชุด test แล้ว)

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 4.1 | ตั้งตารางเวลาแบบ cron แล้วปล่อยให้ถึงเวลา | ทำงานตามเดิม — ตัวอ่าน cron เคยอ่านเลย**ท้ายสตริงไปหนึ่งตัว** | ✅ **PASS** | AI / 2026-08-31 · `test_scheduler_store` ใน Debug build |
| 4.2 | build แบบ Debug แล้วสั่ง `ctest -C Debug` | ผ่าน 24/24 — เมื่อก่อนพัง 5 ชุดด้วย heap corruption | ✅ **PASS** | AI / 2026-08-31 |

> **4.2 คือเหตุผลที่ 4.1 ถูกเจอ** — Debug build ใช้ CRT คนละตัวกับ Qt debug libraries
> อยู่นานแล้ว (`/MD` ทุก config) ผลคือ `std::string` ที่ส่งข้ามไฟล์กลายเป็นขยะ และ test
> ตายด้วย heap corruption **มันลิงก์ผ่านและรันได้ ซึ่งเป็นวิธีผิดที่แย่ที่สุด** พอแก้แล้ว
> Qt ก็ชี้ให้เห็นทันทีว่า `_parseInt()` ในตัวอ่าน cron อ่านเลยท้ายสตริง — ของเดิมรอดมาได้
> เพราะ QString บังเอิญมี null ต่อท้าย

---

### V-26 · Web UI

marker: `docs/WEB-UI.md` · เปิด `RcloneBrowser.exe --daemon` แล้วเข้า **http://127.0.0.1:19999/**

**เกณฑ์ของหัวข้อนี้คือ "หน้าเว็บบอกความจริงของ daemon"** ไม่ใช่ "หน้าเว็บสวย" —
ทุกตัวเลขที่มันแสดงต้องตรงกับที่หน้าต่างแสดง เพราะมันอ่านจากของชิ้นเดียวกัน

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | เปิดหน้าแรกโดยยังไม่มี token | หน้าให้ใส่ token — **ไม่ใช่ 401 เปล่าๆ** | ⬜ | |
| 2 | ใส่ token ผิด | บอกว่าไม่ผ่าน และยังอยู่หน้าเดิม | ⬜ | |
| 3 | ใส่ token ถูก | เห็น task · คิว · ตารางเวลา · ประวัติ **ตรงกับในหน้าต่าง** | ⬜ | |
| 4 | ปิด browser แล้วเปิดใหม่ | ไม่ต้องใส่ token ซ้ำ (จำไว้ที่ browser) | ⬜ | |
| 5 | กด **Sign out** | กลับไปหน้าใส่ token | ⬜ | |
| 6 | กด **Run** ที่ task หนึ่ง | งานขึ้นในแท็บ Jobs **ทันที** พร้อมหลอดความคืบหน้าที่ขยับ | ⬜ | |
| 7 | กด **Run** ซ้ำตอนงานนั้นยังวิ่ง | ข้อความว่า *that task is already running* **ไม่ใช่เงียบ** | ⬜ | |
| 8 | กด **Queue** แล้วดูแท็บ Queue | เข้าคิว · ปุ่ม Remove ใช้ได้ · ตัวที่กำลังรันไม่มีปุ่ม Remove | ⬜ | |
| 9 | กด **Stop** ตอนงานวิ่ง | งานหยุด · ประวัติขึ้น `stopped` **ไม่ใช่ `error`** | ⬜ | |
| 10 | กด **Log** ที่งานที่จบไปแล้ว | เห็นเนื้อ log จริง | ⬜ | |
| 11 | กด **Run now** ที่ตารางเวลา | งานเริ่ม · ถ้ารอบก่อนยังไม่จบต้องเห็นเหตุผลของมัน | ⬜ | |
| 12 | เปิดหน้าเว็บค้างไว้ แล้วไปกดอะไรก็ได้**ในหน้าต่างโปรแกรม** | หน้าเว็บขยับตาม **โดยไม่ต้อง refresh** | ⬜ | |
| 13 | ดูจุดกลมข้างชื่อ | เขียว = สายต่ออยู่ · ปิด daemon แล้วต้องเป็นแดง | ⬜ | |
| 14 | ปิด daemon แล้วเปิดใหม่ โดยไม่แตะหน้าเว็บ | สายกลับมาเอง จุดกลับเป็นเขียว **ไม่ต้อง refresh** | ⬜ | |
| 15 | เปิด `http://127.0.0.1:19999/../src/api_server.cpp` | `404` — ไม่ใช่ไฟล์โค้ด | ✅ **PASS** | AI / 2026-08-31 · `test_api_server` |
| 16 | แท็บ **Remotes** | เห็นรีโหมดครบทุกตัว **ตรงกับในหน้าต่าง** | ⬜ | |
| 17 | คลิกรีโหมดหนึ่ง | เห็นไฟล์และโฟลเดอร์ · โฟลเดอร์อยู่บน · ขนาดกับเวลาถูกต้อง | ⬜ | |
| 17b | เปิดโฟลเดอร์ที่มีไฟล์เป็นพัน | **ต้องขึ้นครบ ไม่ค้างที่ reading…** | ✅ **PASS** | AI / 2026-08-31 · `tgdrive_adult_01:XX2` 7,103 รายการ 1.7 วิ |
| 18 | คลิกเข้าไปในโฟลเดอร์ แล้วกด breadcrumb กลับ | เดินเข้า-ออกได้ · **breadcrumb ต้องเก็บชั้นก่อนหน้าไว้ครบ** | ✅ **PASS** | totza2010 / 2026-08-31 · เดินเข้า-ออกหลายชั้นบนหน้าเว็บได้ หลังแก้ path ให้เป็นเส้นทางเต็ม |
| 19 | เปิดโฟลเดอร์ที่ไม่มีอยู่ / รีโหมดที่พัง | **เห็นเหตุผลว่าทำไมเปิดไม่ได้** ไม่ใช่รายการว่าง | ✅ **PASS** | totza2010 / 2026-08-31 · ขึ้น `directory not found` ของ rclone ตรงๆ |
| 20 | กดปุ่ม **Light** | หน้าเป็นสีสว่างทั้งหน้า · refresh แล้วยังสว่างอยู่ | ⬜ | |
| 21 | ล้าง localStorage แล้วเปิดใหม่ | ธีมตามเครื่อง (ตั้งเครื่องเป็น light แล้วต้องได้ light) | ⬜ | |
| 22 | กด **Set one** ตั้งรหัสผ่าน | ตั้งได้ · แถบเตือนหาย · ยังอยู่ในหน้าเดิมไม่โดนเตะออก | ⬜ | |
| 23 | เปิด browser อีกตัว (หรือ incognito) | หน้าให้ใส่ **user + password** ไม่ใช่ token | ⬜ | |
| 24 | ใส่รหัสผ่านผิด 5 ครั้ง | ครั้งที่ 5 ขึ้นว่า **ต้องรอกี่วินาที** ไม่ใช่แค่ "ผิด" | ⬜ | |
| 25 | เปลี่ยนรหัสผ่านโดยใส่รหัสเดิมผิด | ไม่ให้เปลี่ยน | ⬜ | |
| 26 | เปลี่ยนรหัสผ่านสำเร็จ แล้วดู browser อีกตัวที่เปิดค้างไว้ | **ตัวนั้นหลุด** ต้อง sign in ใหม่ | ⬜ | |
| 27 | เปิดโปรแกรมตามปกติ (ไม่ใช่ `--daemon`) แล้วเข้า `http://127.0.0.1:19999/` | หน้าเว็บขึ้น และ API ตอบ | ✅ **PASS** | AI / 2026-08-31 · `/` = 200 · `/system` ตอบ `coreStarted:true` |
| 28 | เปิดหน้าต่างอยู่ แล้วสั่ง `--daemon` อีกตัว | **ไม่ยอมเปิด** บอกว่ามีตัวรันอยู่ · exit 75 | ✅ **PASS** | AI / 2026-08-31 · เหลือ process เดียว |
| 29 | เปิด daemon อยู่ แล้วเปิดโปรแกรมตามปกติ | กล่องข้อความบอกว่ารันอยู่เบื้องหลัง ไม่เปิดหน้าต่างซ้อน | ✅ **PASS** | totza2010 / 2026-10-12 |
| 30 | เมนู **Help → Open web interface…** | บอกที่อยู่ · เปิด browser ได้ · คัดลอก token ได้ | ✅ **PASS** | totza2010 / 2026-10-12 |
| 31 | กด **Run** ในหน้าเว็บ ขณะหน้าต่างเปิดอยู่ | **การ์ดงานขึ้นในหน้าต่าง**ทันที | ✅ **PASS** | totza2010 / 2026-10-12 |
| 32 | กดหยุดคิวในหน้าต่าง | ปุ่มในหน้าเว็บเปลี่ยนเป็น *Start queue* โดยไม่ต้อง refresh | ✅ **PASS** | totza2010 / 2026-10-12 |
| 33 | กด Run ในแท็บ Tasks ของหน้าต่าง แล้วหยุดด้วยมือ ทำสองรอบ | ประวัติมี**สองแถว** วันเวลาเป็นของวันนี้ ไม่ทับกัน | ⬜ | |
| 34 | แท็บ History ในหน้าเว็บ | มีคอลัมน์ Started และ Took · ใหม่สุดอยู่บน | ⬜ | |

> ข้อ 12 คือข้อที่บอกว่าแยกชั้นสำเร็จจริง — **หน้าเว็บไม่ได้รู้เรื่องเพราะมันสั่งเอง
> แต่รู้เพราะ core เป็นคนบอก** ซึ่งแปลว่าหน้าต่างกับหน้าเว็บกำลังดูของชิ้นเดียวกันอยู่

---

### V-27 · หยุดอย่างสุภาพ

marker: `docs/DOCKER-MODES.md` ขั้น 1 · `AppCore::shutdown()` · `ShutdownSignal`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | daemon บน Windows · `POST /api/v1/shutdown` | ตอบ `202` **ก่อน**หยุด · process ออกด้วยรหัส 0 · พิมพ์ `stopped` | ✅ **PASS** | AI / 2026-10-12 |
| 2 | daemon ใน Alpine 3.24 · `docker stop` | `stopping (SIGTERM)... stopped` · ออกรหัส 0 ไม่ถึง 10 วิ | ✅ **PASS** | AI / 2026-10-12 · 0.6 วิ |
| 3 | หยุดขณะคิวมีงานค้าง | คิว**ยังมีรายการครบ** และ**ไม่ถูก pause** หลังเปิดใหม่ | ✅ **PASS** | AI / 2026-10-12 · `test_shutdown` + ของจริง `queued:1` |
| 4 | เปิดหน้าต่างปกติ · `POST /api/v1/shutdown` | `409` หน้าต่างไม่ปิด | ⬜ | |
| 5 | daemon บน Windows ใน terminal · กด **Ctrl+C** | พิมพ์ `stopping (Ctrl+C)... stopped` | ⬜ | |
| 6 | สั่ง Run ให้ transfer จริงวิ่ง แล้วหยุด daemon | งานขึ้นประวัติเป็น `stopped` **ไม่ใช่ `running` ค้าง** · rclone ไม่เหลือใน process list | ⬜ | |
| 7 | คิวกำลังเดินและมีงานรออีก · หยุด daemon · เปิดใหม่ | งานที่ถูกหยุด**เริ่มใหม่เอง** และงานที่รออยู่ยังอยู่ | ⬜ | |

> ข้อ 7 คือเหตุผลที่คิวถูก "ถือ" แทนที่จะ pause — pause ถูกจำข้ามการรีสตาร์ต
> daemon ที่รีสตาร์ตมาเจอคิวหยุดอยู่ คือ daemon ที่เลิกทำงานไปเงียบๆ

---

### V-28 · Docker สามโหมด

marker: `docs/DOCKER-MODES.md` · `docker/README.md`

```bash
docker compose -f docker/compose.yaml build
docker compose -f docker/compose.yaml up -d      # RB_MODE ใน docker/.env
```

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | `RB_MODE=web` | หน้าเว็บที่ `:19999` ขึ้น · ในคอนเทนเนอร์มีแต่ `rclone-browser --daemon` | ✅ **PASS** | AI / 2026-10-12 |
| 2 | `RB_MODE=both` | หน้าเว็บ `:19999` **และ** หน้าต่างที่ `https://…:3001` | 🟨 ตอบ 200 ทั้งคู่ | AI / 2026-10-12 · ยังไม่มีใครดูภาพ |
| 3 | `RB_MODE=desktop` | `:3001` ขึ้น · `:19999` **ไม่ตอบ** | ✅ **PASS** | AI / 2026-10-12 |
| 4 | `docker stop` ทุกโหมด | ออกรหัส 0 ภายใน 20 วิ · log มี `stopping … stopped` | ✅ **PASS** | AI / 2026-10-12 · web 3.9 วิ · both 5.6 วิ |
| 5 | `RB_MODE=อะไรก็ไม่รู้` | คอนเทนเนอร์ไม่ยอมเริ่ม บอกว่าค่าที่ใช้ได้มีอะไร | ⬜ | |
| 6 | เปิด `https://localhost:3001` ในโหมด `both` | **เห็นหน้าต่างโปรแกรม** คลิกได้ พิมพ์ไทยได้ ชื่อรีโหมดไทยไม่เป็นกล่อง | ✅ **PASS** | totza2010 / 2026-10-12 · ภาพขึ้น ภาษาไทยปกติ (หลังแก้ลำดับ chown) |
| 7 | โหมด `both` · กด Run ในหน้าเว็บ `:19999` | การ์ดงานขึ้นในหน้าต่างที่สตรีม | ⬜ | |
| 8 | ใช้ volume จากอิมเมจ noVNC ตัวเดิม | task · ตารางเวลา · ประวัติ · รีโหมด **อยู่ครบ** | ⬜ | |
| 9 | ตั้ง `RB_STREAM_USER` / `RB_STREAM_PASSWORD` | `:3001` ถามรหัสก่อนให้เข้า | ⬜ | |
| 10 | `docker logs rclonebrowser` ในโหมด `web` | เห็น token ของหน้าเว็บ | ✅ **PASS** | AI / 2026-10-12 |
| 12 | `compose.dev.yaml` · volume ใหม่ · โหมด `web` | API เห็นรีโหมดจาก `rclone.conf` ของเครื่อง **ครบ** และบอกเวอร์ชัน rclone | ✅ **PASS** | AI / 2026-10-12 · 7 ตัว · 1.75.3 |
| 13 | `RB_DEV_RCLONE_DIR` ชี้ไปโฟลเดอร์ที่มี rclone ของ Linux | log ขึ้น `using the mounted rclone` · `/usr/bin/rclone` ชี้ไป `/opt/rclone/rclone` | ✅ **PASS** | AI / 2026-10-12 |
| 21 | โหมด `both` กับ `PUID=1000` (ค่าของ compose) | ตัวสตรีมขึ้น · `/api/websockets` ตอบ 101 · ไม่ใช่จอดำ | ✅ **PASS** | AI + totza2010 / 2026-10-12 · handshake 101 และผู้ใช้เห็นภาพ |
| 16 | `RB_RCLONE_REPO=rclone/rclone` `RB_RCLONE_VERSION=1.75.0` แล้ว restart | log ขึ้น `fetching` แล้ว `using rclone rclone/rclone v1.75.0` · หน้าเว็บบอกเวอร์ชัน 1.75.0 | ✅ **PASS** | AI / 2026-10-12 |
| 17 | restart อีกรอบด้วยค่าเดิม | **ไม่มี** `fetching` — ใช้ตัวที่เก็บไว้ | ✅ **PASS** | AI / 2026-10-12 |
| 18 | ตั้งเวอร์ชันที่ไม่มีจริง | เตือน แล้วใช้ตัวในอิมเมจ · คอนเทนเนอร์ยังเริ่มได้ | ✅ **PASS** | AI / 2026-10-12 |
| 19 | เอาตัวแปรออกแล้ว restart | กลับไปใช้ตัวในอิมเมจ | ✅ **PASS** | AI / 2026-10-12 |
| 20 | ตัดเน็ตคอนเทนเนอร์ แล้วเริ่มด้วย `RB_RCLONE_VERSION=latest` | ใช้ตัวใหม่สุดที่เก็บไว้ · ไม่ค้าง | ⬜ | |
| 14 | `RB_DEV_RCLONE_DIR` ชี้ไปโฟลเดอร์ที่มี `rclone.exe` ของ Windows เปลี่ยนชื่อเป็น `rclone` | log เตือนว่ารันไม่ได้ และใช้ตัวในอิมเมจต่อ | ⬜ | |
| 15 | เปิด dev พร้อมกับตัวจริงและโปรแกรมบนเครื่อง | ทั้งสามรันได้ ไม่แย่งพอร์ต ไม่เห็นข้อมูลของกัน | ⬜ | |
| 11 | `PIXELFLUX_WAYLAND=true` | หน้าต่างยังขึ้น | 🟨 process อยู่และโหลด plugin Wayland | AI / 2026-10-12 · ยังไม่มีใครดูภาพ |

---

### V-29 · รีโหมดบอกว่าอัปโหลดครั้งล่าสุดเมื่อไร

marker: `RunHistory::lastUploadTo` · `MainWindow::refreshRemoteActivity` · `docs/WEB-API-PLAN.md`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | แท็บ Remotes ในหน้าต่าง | ใต้ชื่อรีโหมดที่เคยอัปโหลดมีบรรทัด `↑ วันที่ เวลา · ใช้เวลา` · ที่ไม่เคยไม่มีบรรทัด | ✅ **PASS** | totza2010 / 2026-10-12 · ภาพหน้าจอทั้งสองฝั่ง |
| 2 | ชี้เมาส์ค้างที่รีโหมด | tooltip บอก **เริ่ม · จบ · ใช้เวลา · ผล** ของอัปโหลดและดาวน์โหลดครั้งล่าสุด | ⬜ | |
| 3 | รีโหมดที่ครั้งล่าสุดล้ม (OneDrive) | บรรทัดมีคำว่า `error` ต่อท้าย · ที่สำเร็จไม่มีคำต่อท้าย | ⬜ | |
| 4 | สั่งอัปโหลดไปรีโหมดหนึ่ง | บรรทัดเปลี่ยนเป็น `running` ทันที และเป็นเวลาที่ใช้เมื่อจบ **โดยรีโหมดที่เลือกอยู่ไม่หลุด** | ⬜ | |
| 5 | ดับเบิลคลิกรีโหมดที่มีบรรทัดที่สอง | เปิดแท็บรีโหมดได้ตามปกติ ชื่อแท็บเป็นชื่อรีโหมดล้วน | ⬜ | |
| 6 | แท็บ Remotes ในหน้าเว็บ | บรรทัดเดียวกันใต้ชื่อ · เลือกรีโหมดแล้วเห็นการ์ด Last upload / Last download | ✅ **PASS** | totza2010 / 2026-10-12 · ภาพหน้าจอทั้งสองฝั่ง |
| 7 | เทียบหน้าต่างกับหน้าเว็บ | **วันเวลาและระยะเวลาตรงกันทุกรีโหมด** | ✅ **PASS** | totza2010 / 2026-10-12 · ภาพหน้าจอทั้งสองฝั่ง |
| 8 | `GET /api/v1/remotes` | `lastUpload` มี `startedAt` `finishedAt` (มี offset) `durationSeconds` `state` · ไม่เคย = `null` | ✅ **PASS** | AI / 2026-10-12 · ของจริง 7 รีโหมด |
| 9 | รีโหมดชื่อคล้ายกัน (`tgdrive_main_01` กับ `_02`) | ไม่ปนกัน | ✅ **PASS** | AI / 2026-10-12 · `test_run_history` |
| 10 | อัปโหลดไฟล์จริงไปรีโหมดหนึ่ง (ไม่ใช่ dry run) | บรรทัดมี**ขนาด**ต่อท้าย `· 297.9 KiB` ทั้งสองหน้าจอ · tooltip บอก `sent … in N file(s)` | ✅ **PASS** | totza2010 / 2026-10-12 · ขนาด 176.4 KB ขึ้นทั้งบรรทัดและการ์ด |
| 11 | อัปโหลดไฟล์เล็กที่จบในไม่ถึงวินาที | ขนาด**ไม่เป็น 0** | ✅ **PASS** | AI / 2026-10-12 · `test_job_registry` ด้วย rclone จริง |
| 12 | สั่ง dry run ไปรีโหมดที่เคยอัปโหลดจริง (ทั้งจากปุ่ม Dry run และจาก task ที่มี `--dry-run` ใน flag) | บรรทัด**ยังเป็นของครั้งที่อัปโหลดจริง** · History ขึ้น `transfer (dry run)` ทั้งสองหน้าจอ | 🟨 ฐานข้อมูลจริงถูกจัดประเภทแล้ว | AI / 2026-10-12 · 110 จาก 145 แถวเป็น dry run · รอผู้ใช้ดูหน้าจอ |
| 14 | เปิดโปรแกรมครั้งแรกหลังอัปเกรด | ประวัติอยู่ครบ 145 แถว · รีโหมดที่เคยมีแต่ dry run กลับเป็น **ไม่เคยอัปโหลด** | ✅ **PASS** | AI / 2026-10-12 · สำรองไว้ที่ `backup-before-schema3-…` |
| 13 | เปิด log ของงานที่เพิ่งจบ | ท้าย log มีบล็อก `Transferred: …` ของ rclone | ⬜ | |

---

### V-30 · หน้า Remote: สร้างโฟลเดอร์

marker: `src/remote_ops.*` · `docs/WEB-API-PLAN.md` หัวข้อ "หน้า Remote"

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | หน้าต่าง · แท็บรีโหมด · Mkdir | สร้างได้เหมือนเดิม · รายการ refresh เอง | ⬜ | |
| 2 | หลังข้อ 1 ดูแท็บ History | **มีแถวชนิด `mkdir`** (เมื่อก่อนไม่มี) | ⬜ | |
| 3 | หน้าเว็บ · Remotes · เลือกรีโหมด · **New folder** | ถามชื่อ · สร้าง · รายการ refresh เองและมีโฟลเดอร์ใหม่ | ⬜ | |
| 4 | หน้าเว็บ · สร้างโฟลเดอร์ชื่อไทย / มีช่องว่าง | สร้างได้ ชื่อถูก | ⬜ | |
| 5 | หน้าเว็บ · ตั้งชื่อ `a/b` หรือ `..` | ข้อความว่าใช้ไม่ได้ **ไม่มีอะไรถูกสร้าง** | ✅ **PASS** | AI / 2026-10-12 · `test_api_server` + ของจริงตอบ 400 |
| 6 | สร้างโฟลเดอร์ในรีโหมดที่ต่อไม่ได้ (OneDrive ตอนนี้) | ข้อความ**ของ rclone เอง**ว่าทำไมไม่ได้ | ⬜ | |
| 7 | หน้าเว็บ · **Refresh** | อ่านรายการใหม่ | ⬜ | |
| 8 | `POST /api/v1/remotes/{name}/mkdir` | ตอบ**เมื่อเสร็จ** · 200 + `path` + `requestId` · ล้ม = 502 + ข้อความ rclone + `code` | ✅ **PASS** | AI / 2026-10-12 · rclone ปลอม |

> ผมไม่ได้สร้างโฟลเดอร์จริงบนรีโหมดของผู้ใช้ ข้อ 1–4 และ 6 จึงรอผู้ใช้

---

### V-31 · หน้า Remote: เปลี่ยนชื่อและลบ

marker: `RemoteOps::Rename` · `RemoteOps::Delete` · `docs/WEB-API-PLAN.md`

> **ลองกับโฟลเดอร์ทดสอบที่สร้างขึ้นเอง** การลบไม่มี undo นอกจากถังขยะของ backend
> ผมไม่ได้เปลี่ยนชื่อหรือลบอะไรบนรีโหมดจริงของผู้ใช้ ทุกข้อที่ต้องใช้รีโหมดจริงจึงรอผู้ใช้

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | หน้าต่าง · Rename ไฟล์/โฟลเดอร์ | เปลี่ยนชื่อได้เหมือนเดิม · History มีแถว `rename` | ⬜ | |
| 2 | หน้าต่าง · Rename แล้วพิมพ์ชื่อเดิม | ไม่เกิดอะไร ไม่มีคำเตือน | ⬜ | |
| 3 | หน้าต่าง · เลือกหลายรายการ · Delete | ลบครบ · กล่องนับถูก · History มีแถว `delete` ต่อรายการ | ⬜ | |
| 4 | หน้าเว็บ · คลิกแถว | แถวถูกเลือก (แถบสีด้านซ้าย) · ปุ่ม Rename/Delete ใช้ได้ · คลิกซ้ำยกเลิก | ⬜ | |
| 5 | หน้าเว็บ · คลิก**ชื่อ**โฟลเดอร์ | ยังเปิดโฟลเดอร์ตามเดิม ไม่ใช่เลือก | ⬜ | |
| 6 | หน้าเว็บ · Rename | ถามชื่อโดยใส่ชื่อเดิมให้ · เปลี่ยน · รายการ refresh | ⬜ | |
| 7 | หน้าเว็บ · Delete ไฟล์ | กล่องยืนยัน**บอกชื่อเต็ม** · ลบ · รายการ refresh | ⬜ | |
| 8 | หน้าเว็บ · Delete โฟลเดอร์ที่มีของ | กล่องยืนยันบอกว่า**ของข้างในไปด้วย** · ลบทั้งโฟลเดอร์ | ⬜ | |
| 9 | กด Cancel ในกล่องยืนยัน | ไม่มีอะไรถูกลบ | ⬜ | |
| 10 | `POST …/delete` ด้วย path ว่าง | 400 **ไม่มีอะไรรัน** | ✅ **PASS** | AI / 2026-10-12 · `test_api_server` 5 แบบ |
| 11 | `POST …/delete` กับโฟลเดอร์โดยไม่บอก `isDir` | rclone ปฏิเสธ (502) **โฟลเดอร์ไม่ถูกกวาด** | ✅ **PASS** | AI / 2026-10-12 · rclone จริงกับโฟลเดอร์ในเครื่อง: `deletefile` ล้ม ไฟล์อยู่ครบ 2 · `delete` แบบเดิมลบหมด |
| 12 | `POST …/rename` ชื่อเดิม / ชื่อมี `/` / รากของรีโหมด | 400 ทั้งหมด | ✅ **PASS** | AI / 2026-10-12 |

---

### V-32 · หน้า Remote: Size · Info · Tree

marker: `RemoteOps::Size` `About` `Tree` · `docs/WEB-API-PLAN.md`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | หน้าต่าง · Size · Info · DirTree | ทำงานเหมือนเดิม | ⬜ | |
| 2 | หน้าต่าง · DirTree ของโฟลเดอร์ที่มีหลายชั้น | **กิ่งสุดท้ายของแต่ละชั้นเยื้องถูก** (เมื่อก่อนชิดซ้ายหมด) | ⬜ | |
| 3 | หน้าเว็บ · **Info** ที่ gdrive | Total · Used · Free · In the trash · Other | 🟨 API ตอบครบ | AI / 2026-10-12 · total 16.1 GB · รอดูหน้าจอ |
| 4 | หน้าเว็บ · **Info** ที่รีโหมด teldrive | มีแค่ Used ไม่มีแถวที่เป็น 0 ปลอม | 🟨 API ตอบแค่ `used` | AI / 2026-10-12 · 53 TB |
| 5 | หน้าเว็บ · เลือกโฟลเดอร์ · **Size** | จำนวนไฟล์และขนาดรวม ตรงกับหน้าต่าง | 🟨 API ตอบ | AI / 2026-10-12 · `gdrive:test` |
| 6 | หน้าเว็บ · ไม่เลือกอะไร · **Size** | เป็นขนาดของโฟลเดอร์ที่กำลังดู | ⬜ | |
| 7 | หน้าเว็บ · **Tree** | ต้นไม้ที่เยื้องถูก ชื่อไทยอ่านได้ | 🟨 API ตอบ รูปถูก | AI / 2026-10-12 · `tgdrive_main_03:tg3` |
| 8 | Info/Size ที่รีโหมดที่ต่อไม่ได้ (OneDrive) | กล่องผลลัพธ์แสดง**ข้อความของ rclone** | ⬜ | |
| 9 | กด Size ที่โฟลเดอร์ใหญ่ แล้วปิดแท็บ | rclone หยุด ไม่วิ่งต่อ | ⬜ | |

---

### V-33 · หน้า Remote: Link · Cleanup · Dedupe

marker: `RemoteOps::Link` `Cleanup` `Dedupe` · `docs/WEB-API-PLAN.md`

> **Link เปลี่ยนการแชร์จริง · Cleanup กับ Dedupe (ที่ไม่ใช่ dry run) ลบข้อมูลจริง**
> ผมลองแค่ dry run ของ Dedupe กับโฟลเดอร์ทดสอบ และการปฏิเสธก่อนเริ่ม ที่เหลือรอผู้ใช้

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | หน้าต่าง · Link · Cleanup · Dedupe | ทำงานเหมือนเดิม | ⬜ | |
| 2 | หน้าเว็บ · เลือกไฟล์ · **Link** | ถามยืนยันก่อน · ได้ที่อยู่ในช่องที่คัดลอกได้ | ⬜ | |
| 3 | หน้าเว็บ · Link ที่ backend ที่แชร์ไม่ได้ (teldrive) | ข้อความของ rclone ว่าทำไม่ได้ | ⬜ | |
| 4 | หน้าเว็บ · **Dedupe** · Dry run | บอกว่าจะทำอะไร **ไม่มีอะไรถูกลบ** · History ขึ้น `dedupe (dry run)` | 🟨 API ผ่าน | AI / 2026-10-12 · `gdrive:test` โหมด skip |
| 5 | หน้าเว็บ · Dedupe · Run for real | ถามยืนยัน · ทำ · รายการ refresh | ⬜ | |
| 6 | หน้าเว็บ · **Cleanup** | กล่องยืนยันอธิบายว่าทำอะไร · ทำ | ⬜ | |
| 7 | `POST …/link` ด้วย path ว่าง | 400 ไม่มีอะไรรัน | ✅ **PASS** | AI / 2026-10-12 · ของจริงตอบ `refused` |
| 8 | `POST …/dedupe` ด้วยโหมดที่ไม่รู้จัก หรือโหมดที่แอบพก flag | 400 `bad_mode` | ✅ **PASS** | AI / 2026-10-12 · `test_api_server` |

---

### V-34 · หน้า Remote: Copy · Move และตัวเลือกโฟลเดอร์

marker: `RemoteOps::CopyInto` · `pickFolder` ใน `app.js` · `docs/WEB-API-PLAN.md`

> ลองกับโฟลเดอร์ทดสอบ และเริ่มด้วย **Dry run** · Move ย้ายของจริง

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | หน้าต่าง · Copy / Move (รายการเดียว และหลายรายการ) | ทำงานเหมือนเดิม | ⬜ | |
| 2 | หน้าเว็บ · เลือกแถว · **Copy** | กล่องเลือกโฟลเดอร์ขึ้น เริ่มที่โฟลเดอร์ปัจจุบัน | ✅ **PASS** | totza2010 / 2026-10-12 · ใช้ได้จริง |
| 3 | ในกล่อง · คลิกโฟลเดอร์ / breadcrumb | เดินเข้า-ออกได้ | ⬜ | |
| 4 | ในกล่อง · เปลี่ยนรีโหมด | เห็นโฟลเดอร์ของรีโหมดนั้น · รีโหมดที่ต่อไม่ได้บอกเหตุผล | ⬜ | |
| 5 | ติ๊ก Dry run · Copy here | ข้อความว่าเริ่มแล้ว · งานขึ้นในแท็บ Jobs มีป้าย dry run · **ไม่มีอะไรถูกคัดลอก** | 🟨 API ผ่าน | AI / 2026-10-12 · `gdrive:test` → `gdrive:Books/test` |
| 6 | Copy โฟลเดอร์จริง | ไปอยู่ใต้**ชื่อของตัวเอง**ในปลายทาง ไม่ใช่ของข้างในหกลงไป | ⬜ | |
| 7 | Copy ไฟล์จริง | ไฟล์ไปอยู่ในโฟลเดอร์ปลายทาง | ✅ **PASS** | totza2010 / 2026-10-12 · `gdrive:…jpeg` → `gdrive:test` · 176.4 KB ใน 1 ไฟล์ |
| 8 | Move | ของหายจากต้นทาง ไปอยู่ปลายทาง · รายการต้นทาง refresh เองเมื่องานจบ | ⬜ | |
| 9 | Copy ข้ามรีโหมด | ได้ | ⬜ | |
| 10 | เลือกปลายทางเป็นที่เดิม / โฟลเดอร์เข้าไปในตัวเอง | ข้อความปฏิเสธ ไม่มีงานเริ่ม | ✅ **PASS** | AI / 2026-10-12 · `test_api_server` `test_remote_ops` |
| 11 | dry run ไม่นับเป็น "อัปโหลดครั้งล่าสุด" ของรีโหมดปลายทาง | บรรทัดใต้รีโหมดไม่เปลี่ยน | ✅ **PASS** | AI / 2026-10-12 · gdrive ยังเป็น never |
| 12 | Copy/Move ภายในรีโหมด หรือข้ามรีโหมด | **ไม่ขึ้นเป็น Last upload หรือ Last download** ของรีโหมดไหนเลย | ✅ **PASS** | ผู้ใช้เจอว่าขึ้นทั้งคู่ · แก้แล้ว · AI / 2026-10-12 gdrive กลับเป็น never |

---

### V-35 · หน้า Remote: Check · Export

marker: `RemoteOps::Check` `ListForExport` · `docs/WEB-API-PLAN.md` · ทั้งสองไม่เปลี่ยนอะไรบนรีโหมด

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | หน้าต่าง · Export | ไฟล์ที่ได้เหมือนเดิม (ชื่อ, เวลา, ขนาด) | ⬜ | |
| 2 | หน้าต่าง · Export โฟลเดอร์ที่มีไฟล์ชื่อมี `"` | แถวไม่พัง (`""` แทน `"`) | ⬜ | |
| 3 | หน้าเว็บ · **Export** · CSV | ไฟล์ถูกดาวน์โหลด · เปิดใน Excel ได้ · ชื่อไทยอ่านได้ | 🟨 API ผ่าน | AI / 2026-10-12 · `gdrive:test` ได้ 1 แถว |
| 4 | หน้าเว็บ · Export · names only | ได้ไฟล์ `.txt` มีแต่ชื่อ | ⬜ | |
| 5 | หน้าเว็บ · **Check** · เลือกโฟลเดอร์ที่เหมือนกัน | ป้าย `the same` | ⬜ | |
| 6 | หน้าเว็บ · Check · เลือกโฟลเดอร์ที่ต่างกัน | ป้าย `N difference(s)` + รายการที่ต่าง **ไม่ใช่ข้อความ error** | 🟨 API ผ่าน | AI / 2026-10-12 · `gdrive:test` กับ `gdrive:win10` → 1 difference |
| 7 | Check กับรีโหมดที่ต่อไม่ได้ | ข้อความของ rclone | ⬜ | |
| 8 | Check โฟลเดอร์กับตัวเอง | ปฏิเสธ | ✅ **PASS** | AI / 2026-10-12 · `test_api_server` |
| 9 | ไฟล์ export ของหน้าต่างกับหน้าเว็บ จากโฟลเดอร์เดียวกัน | **เหมือนกันทุกไบต์** | ⬜ | |

---

### V-36 · หน้า Remote: Mount

marker: `RemoteOps::Mount` · `MainWindow::addMountCard` · `docs/WEB-API-PLAN.md`

> ผมไม่ได้ mount จริงบนเครื่องผู้ใช้ (ต้องมี WinFsp และจะเกิดไดรฟ์ขึ้นจริง) ตรวจด้วย rclone ปลอมเท่านั้น

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | หน้าต่าง · Mount จากแท็บรีโหมด | ทำงานเหมือนเดิม · การ์ด mount ขึ้น **ใบเดียว** ไม่ซ้ำ | ⬜ | |
| 2 | หน้าต่าง · รัน task แบบ mount จากแท็บ Tasks | การ์ดขึ้นใบเดียว | ⬜ | |
| 3 | หน้าเว็บ · **Mount** · ใส่อักษรไดรฟ์ | ไดรฟ์ขึ้นบนเครื่อง · งานขึ้นในแท็บ Jobs | ⬜ | |
| 4 | หลังข้อ 3 ดูที่**หน้าต่าง** | **มีการ์ด mount ขึ้นด้วย** (เมื่อก่อนไม่มี) | ⬜ | |
| 5 | หน้าเว็บ · กด **Unmount** ที่การ์ด | ไดรฟ์หาย · การ์ดในหน้าต่างเปลี่ยนสถานะตาม | ⬜ | |
| 6 | หน้าต่าง · กด unmount ของ mount ที่เริ่มจากหน้าเว็บ | ไดรฟ์หาย | ⬜ | |
| 7 | Mount ไปที่ที่ใช้ไม่ได้ (อักษรไดรฟ์ที่มีอยู่แล้ว) | งานจบเป็น error พร้อมข้อความของ rclone ใน Log | ⬜ | |
| 8 | รัน task แบบ mount จากหน้าเว็บ (แท็บ Tasks) | การ์ดขึ้นในหน้าต่างด้วย | ⬜ | |
| 9 | `POST …/mount` โดยไม่บอกที่ mount | 400 | ✅ **PASS** | AI / 2026-10-12 · ของจริงตอบ `refused` |
| 10 | คำสั่งของ mount มี `--rc-addr` แต่**ไม่มีรหัสเข้า** | ใช่ | ✅ **PASS** | AI / 2026-10-12 · `test_api_server` |

---

### V-37 · หน้า Remote: Upload · Download และโฟลเดอร์ของเครื่อง

marker: `src/local_files.*` · `RemoteOps::Upload` `Download` · `docs/WEB-API-PLAN.md`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | หน้าต่าง · Upload / Download | ทำงานเหมือนเดิม (ไม่ได้แตะ) | ⬜ | |
| 2 | หน้าเว็บ · **Upload** (รันบนเครื่อง ไม่ใช่ Docker) | ตัวเลือกเริ่มที่รายการไดรฟ์ · เดินเข้าได้ · ปุ่ม ↑ up ถอยได้ | 🟨 API ตอบ `C:/ Z:/` | AI / 2026-10-12 |
| 3 | เลือก**ไฟล์** ในตัวเลือก | ไฟล์ถูกทำเครื่องหมาย · ปุ่มหมายถึงไฟล์นั้น | ⬜ | |
| 4 | ติ๊ก Dry run · Upload | งานขึ้นใน Jobs มีป้าย dry run · ไม่มีอะไรถูกส่ง | 🟨 API ผ่าน | AI / 2026-10-12 · ไฟล์ทดสอบ → `gdrive:test` |
| 5 | Upload ไฟล์จริง | ไฟล์ไปอยู่ในโฟลเดอร์ที่กำลังดู · รายการ refresh เองเมื่อจบ · **บรรทัด "อัปโหลดครั้งล่าสุด" ของรีโหมดเปลี่ยน** พร้อมขนาด | ⬜ | |
| 6 | Upload โฟลเดอร์จริง | ไปอยู่ใต้ชื่อของตัวเอง | ⬜ | |
| 7 | เลือกแถว · **Download** · เลือกโฟลเดอร์ | ของมาอยู่ในโฟลเดอร์นั้น · Last download ของรีโหมดเปลี่ยน | ⬜ | |
| 8 | ใน Docker · เปิดตัวเลือก | **เห็นแค่ `/media`** | ✅ **PASS** | AI / 2026-10-12 · คอนเทนเนอร์ dev |
| 9 | ใน Docker · ขอดู `/etc` · `/config` · `/media/../etc` · ลิงก์ใน `/media` ที่ชี้ออกไป | 403 ทั้งหมด | ✅ **PASS** | AI / 2026-10-12 |
| 10 | Upload จาก path นอกที่อนุญาต ผ่าน API | 403 `outside_roots` ไม่มีงานเริ่ม | ✅ **PASS** | AI / 2026-10-12 · `test_api_server` |
| 11 | ใน Docker · mount โฟลเดอร์ของโฮสต์เข้า `/media` แล้ว Upload | ไฟล์ของโฮสต์ขึ้นรีโหมดได้ | ⬜ | |

---

### V-38 · ฟอร์ม Transfer บนหน้าเว็บ และการสร้าง/แก้/ลบ task

marker: `src/task_json.*` · `POST/PUT/DELETE /api/v1/tasks` · `#taskbox` ใน `src/webui/index.html`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | หน้าเว็บ · Remotes · **Upload** | ฟอร์มเปิด · To = โฟลเดอร์ที่กำลังดู · 7 แท็บ · ค่าเริ่มต้น transfers 4 / checkers 8 | ⬜ | |
| 2 | Browse ที่ From · เลือก**โฟลเดอร์** | From = path · To ต่อท้ายด้วยชื่อโฟลเดอร์นั้น | ⬜ | |
| 3 | Browse ที่ From · เลือก**ไฟล์** | To กลับเป็นโฟลเดอร์ที่กำลังดู | ⬜ | |
| 4 | เลือก Sync | Delete during/after/before และ Delete excluded กดได้ · เลือก Copy แล้วเป็นสีเทา | ⬜ | |
| 5 | แท็บ **rclone cmd** | เห็นคำสั่งเต็ม ตรงกับที่ตั้งในแท็บอื่น รวมตัวเลือกเริ่มต้นจาก Preferences | 🟨 API ตอบ `rclone sync … --bwlimit 2M --exclude *.tmp --dry-run` | AI / 2026-10-12 |
| 6 | **Save and Dry run** | task ถูกบันทึก · งานขึ้นใน Jobs มีป้าย dry run | ⬜ | |
| 7 | **Save and Run** | task ถูกบันทึก · ไฟล์ขึ้นจริง · รายการ refresh เมื่อจบ | ⬜ | |
| 8 | ติ๊ก Add to the queue · Save and Run | task ไปอยู่ใน Queue ไม่ได้เริ่มทันที | ⬜ | |
| 9 | **Save task** โดยไม่ใส่ชื่อ | ได้ชื่อ `_tmp_วัน_เวลา_รีโหมด` · **โผล่ในแท็บ Tasks ของหน้าต่างทันที** | 🟨 API สร้างได้ ชื่อ/isFolder ถูก | AI / 2026-10-12 |
| 10 | เลือกแถว · **Download** | From = `remote:path` ของแถวนั้น · Browse ที่ To · โฟลเดอร์ลงใต้ชื่อของตัวเอง | ⬜ | |
| 11 | แท็บ Tasks · **Edit** | ฟอร์มเปิดพร้อมค่าของ task ทุกแท็บ · Save task แล้วค่าเปลี่ยน ไม่มี task ใหม่ | 🟨 API `PUT` ผ่าน | AI / 2026-10-12 |
| 12 | แก้ task ในหน้าต่าง ขณะหน้าเว็บเปิดอยู่ | หน้าเว็บเปลี่ยนตามเอง (เหตุการณ์ `tasks`) | ⬜ | |
| 13 | แท็บ Tasks · **Delete** | ถามยืนยัน · task หาย ทั้งหน้าเว็บและหน้าต่าง | 🟨 API `DELETE` ผ่าน | AI / 2026-10-12 |
| 14 | แท็บ Tasks · New upload task / New download task | ฟอร์มว่าง · Browse ได้ทั้งสองช่อง | ⬜ | |
| 15 | task ชนิด mount ในแท็บ Tasks | ไม่มีปุ่ม Edit | ⬜ | |
| 16 | ค่าที่ไม่ใช่ตัวเลือก (`compare: "vibes"`) ผ่าน API | 400 บอกชื่อช่อง · task เดิมไม่เปลี่ยน | ✅ **PASS** | AI / 2026-10-12 · `test_task_json` + โปรแกรมจริง |
| 17 | สร้าง/ย้าย task ไป path นอก `RB_LOCAL_ROOTS` ผ่าน API | 403 `outside_roots` | ✅ **PASS** | AI / 2026-10-12 · `test_api_server` |
| 18 | หน้าต่าง · Upload / Download / แก้ task | ทำงานเหมือนเดิม (ไม่ได้แตะ) | ⬜ | |

---

### V-39 · ฟอร์ม Transfer: ใส่ได้เฉพาะสิ่งที่มีอยู่และที่รองรับ

marker: `src/extra_flags.*` · `endsProblem` ใน `src/api_server.cpp` · `GET /rclone/flags` · `GET /local/stat`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | หน้าเว็บ · Upload · ดูช่อง To | เป็น**ตัวเลือกรีโหมด** + ช่อง path — พิมพ์ชื่อรีโหมดเองไม่ได้ | ⬜ | |
| 2 | พิมพ์ path มั่วๆ ในช่อง From | ใต้ช่องขึ้นสีแดงว่าไม่มีอยู่ · พิมพ์ path จริง ขึ้น a folder / a file | 🟨 `/local/stat` ตอบถูก | AI / 2026-10-12 |
| 3 | กด Save task ทั้งที่ From ไม่มีอยู่ | ไม่ถูกบันทึก · บอกเหตุผลในฟอร์ม | ✅ **PASS** (API) | AI / 2026-10-12 · โปรแกรมจริง |
| 4 | แท็บ Extra options · พิมพ์ `fast` | รายการ flag ที่ตรงขึ้นมา พร้อมคำอธิบาย | ⬜ | |
| 5 | เลือก `--fast-list` · Add | ขึ้นเป็นแถว · ช่อง value ถูกปิด (ไม่มีค่า) | ⬜ | |
| 6 | เลือก flag ที่มีค่า เช่น `--max-transfer` · ไม่ใส่ค่า · Add | ไม่ถูกเพิ่ม · บอกว่าต้องมีค่า | ⬜ | |
| 7 | พิมพ์ชื่อที่ไม่มี เช่น `--fast_list` · Add | ไม่ถูกเพิ่ม | ✅ **PASS** (API 400 `bad_flag`) | AI / 2026-10-12 · rclone จริง 1043 flag |
| 8 | ค้นหา `password-command` · `config` | **ไม่อยู่ในรายการ** | ✅ **PASS** (API ไม่ส่งมา และปฏิเสธเมื่อส่งตรง) | AI / 2026-10-12 |
| 9 | แท็บ rclone cmd หลังเพิ่ม flag | flag ที่เพิ่มอยู่ในคำสั่ง | ✅ **PASS** (API) | AI / 2026-10-12 |
| 10 | Edit task จากหน้าต่างที่มี flag พิมพ์เอง | แถวของ flag ที่ไม่รองรับเป็นสีแดง · Remove ได้ | ⬜ | |
| 11 | แท็บ Transfer · ช่อง Transfers | รับเฉพาะตัวเลข · Bandwidth `abc;` → บันทึกไม่ได้ บอกชื่อช่อง | 🟨 `test_task_json` ผ่าน | AI / 2026-10-12 |
| 12 | ย่อหน้าต่าง browser ให้เตี้ย | ปุ่ม Cancel / Save ยังเห็นครบ ส่วนแท็บเลื่อนได้ | ⬜ | |
| 13 | หน้าต่าง · พิมพ์ `--ver` ในช่อง extra options | completion เติม `--verbose` ไม่มี `=` ต่อท้าย | ⬜ | |
| 20 | Upload ไป teldrive · Extra options · ค้น `chunk` | เห็น `--teldrive-chunk-size` ไม่เห็น `--drive-chunk-size` · บรรทัดล่างบอกจำนวน flag ที่ใช้กับรีโหมดนี้ | 🟨 API แยกกลุ่มถูก (teldrive 13 · drive 51 · onedrive 28 · sftp 36) | AI / 2026-10-12 |
| 21 | ติ๊ก "Also offer the flags of other kinds of remote" | `--drive-chunk-size` ขึ้นมา · เพิ่มแล้วแถวมีหมายเหตุว่า rclone จะไม่สนใจ | ⬜ | |
| 22 | เปลี่ยนรีโหมดในช่อง To เป็น gdrive | รายการและหมายเหตุของแถวเปลี่ยนตาม | ⬜ | |
| 14 | แท็บ Transfer · Bandwidth / Minimum size / Minimum age / Maximum age | ช่องตัวเลข + **ตัวเลือกหน่วย** · ใส่ 10 เลือก MiB/s → rclone cmd มี `--bwlimit 10M` | ⬜ | |
| 15 | ลบตัวเลขออก | flag นั้นหายจาก rclone cmd | ⬜ | |
| 16 | Edit task ที่ตั้ง bandwidth เป็นตารางเวลาจากหน้าต่าง (`08:00,512k 18:00,off`) | ใต้ช่องบอกว่า "set to … kept as it is" · Save แล้วค่าเดิมยังอยู่ | ⬜ | |
| 17 | แท็บ Include / Exclude · พิมพ์ pattern · Add หรือ Enter | ขึ้นเป็นแถว ทีละค่า · Remove ได้ · ซ้ำไม่ได้ | ⬜ | |
| 18 | แท็บ Filter · เลือก + include / − exclude · Add | แถวขึ้นเป็น `+ *.mkv` มีเลขลำดับ · ปุ่ม ↑ ย้ายขึ้นได้ | ⬜ | |
| 19 | rclone cmd หลังเพิ่ม pattern | มี `--include` / `--exclude` / `--filter` ครบตามแถว | ⬜ | |

---

### V-40 · หน้า Remote: Stream ที่เปิดในโปรแกรมเล่นได้

marker: `RemoteOps::Stream` · `POST /api/v1/remotes/{n}/stream` · `docs/WEB-API-PLAN.md`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | หน้าต่าง · Stream | ทำงานเหมือนเดิม (ไม่ได้แตะ) | ⬜ | |
| 2 | หน้าเว็บ · เลือกไฟล์ MP4 · **Stream** | กล่องเครื่องเล่นขึ้นและเล่นเอง · งาน `Stream of …` ขึ้นใน Jobs | 🟨 API 202 · ยังไม่ได้ดูใน browser | AI / 2026-10-12 |
| 3 | เลื่อนตำแหน่งในเครื่องเล่น ไปมาหลายครั้ง | เล่นต่อจากจุดนั้น · **โปรแกรมไม่ล้ม** | ✅ **PASS** (API: อ่าน · ทิ้ง · เลื่อน · ทิ้ง · เลื่อน บนไฟล์ 604 MB) | AI / 2026-10-12 · teldrive จริง |
| 4 | เลือกไฟล์ MKV ที่ browser เล่นไม่ได้ · Stream | ขึ้นข้อความว่า browser นี้เล่นไม่ได้ และชี้ไปที่ที่อยู่ | ⬜ | |
| 5 | คัดลอกที่อยู่ไปเปิดใน VLC | เล่นได้ · **ที่อยู่เป็นพอร์ตเดียวกับหน้าเว็บ** | 🟨 ที่อยู่ถูก · `Range` ได้ 206 · ยังไม่ได้เปิดใน VLC | AI / 2026-10-12 |
| 6 | Download .m3u แล้วเปิด | โปรแกรมเล่นเริ่มเล่น | ⬜ | |
| 7 | ไม่เลือกอะไร (หรือเลือกโฟลเดอร์) · Stream | มีรายการไฟล์สื่อให้กด · จบไฟล์แล้วเล่นไฟล์ถัดไปเอง | ⬜ | |
| 8 | ปิดกล่องเครื่องเล่น | เสียงหยุด · แท็บ Jobs ปุ่ม **Play** เปิดกลับมาได้ | ⬜ | |
| 9 | **Stop the stream** ในกล่อง หรือ Stop ที่การ์ด | ที่อยู่เดิมได้ 404 · ไม่มี rclone ค้าง | ✅ **PASS** (API) | AI / 2026-10-12 |
| 10 | ที่อยู่ที่ส่วนสุ่มผิด | 404 | ✅ **PASS** | AI / 2026-10-12 |
| 11 | stream ไฟล์เดียวจากโฟลเดอร์ที่มีหลายไฟล์ | เห็นไฟล์เดียว | ✅ **PASS** | AI / 2026-10-12 · 158 รายการ เห็น 1 |
| 12 | คำสั่งบนการ์ด / ในประวัติ | `--baseurl=***` · `--addr 127.0.0.1:…` | ✅ **PASS** | AI / 2026-10-12 |
| 12a | ไฟล์ชื่อภาษาไทย มีช่องว่าง | เล่นได้ | ✅ **PASS** (API ดึงได้ 206) | AI / 2026-10-12 · NCIS … ซับไทย |
| 12b | เล่นนานๆ ดูหน่วยความจำของโปรแกรม | ไม่โตตามขนาดไฟล์ | 🟨 40 MB ผู้อ่านช้า: 133 → 146 MB | AI / 2026-10-12 |
| 12d | เปิด Stream แล้วปิดกล่องโดยไม่กด Stop · รอเกิน 30 นาที | งานในแท็บ Jobs หยุดเอง (stopped) | ✅ **PASS** (ตั้ง 1 นาที: หยุดที่ 70 วินาที) | AI / 2026-10-18 · โปรแกรมจริง |
| 12e | เล่นใน VLC แล้วกด pause ทิ้งไว้นานกว่ากำหนด · กดเล่นต่อ | เล่นต่อได้ (VLC ค้างการเชื่อมต่อไว้) | 🟨 test: ผู้เล่นที่ต่ออยู่ไม่ถูกตัด | AI / 2026-10-18 |
| 12c | Docker · Stream | เล่นได้โดย**ไม่ต้องเปิดพอร์ตเพิ่ม** | ✅ **PASS** — ผ่านพอร์ตของหน้าเว็บ 206 · key ผิด 404 · หยุดแล้ว 404 · ไม่ต้องเปิดพอร์ตเพิ่ม | AI / 2026-10-18 · คอนเทนเนอร์ dev |
| 13 | เปิดหน้าเว็บ | **แท็บ Remotes อยู่ซ้ายสุดและเปิดเป็นหน้าแรก** | ⬜ | |
| 14 | ดูรายการรีโหมด | แต่ละตัวมีไอคอนตามชนิด ตัวเดียวกับในหน้าต่าง (drive · onedrive · sftp · teldrive) | ⬜ | |
| 15 | สลับธีม สว่าง/มืด | ไอคอนเปลี่ยนเป็นชุดที่เข้ากับพื้น | ⬜ | |
| 12 | Docker · Stream | พอร์ตอยู่ใน 19990-19993 · เปิดจากโฮสต์ได้ · เปิดครบ 4 แล้วอันที่ 5 ถูกปฏิเสธพร้อมเหตุผล | ⬜ | |

---

### V-41 · ตารางเวลาจากหน้าเว็บ และหน้าต่างที่ตามให้ทัน

marker: `src/schedule_json.*` · `MainWindow::followSchedulerStore` · `POST/PUT/DELETE /api/v1/schedules`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | หน้าเว็บ · Tasks · **Schedule** บนการ์ด | ฟอร์มขึ้น task ถูกเลือกไว้แล้ว · ทุกวัน 06:30 · Switched on | ⬜ | |
| 2 | เลือก Mon–Fri · 07:00 · Save | แถวขึ้นในแท็บ Schedules: "Mon–Fri at 07:00" · Next run ถูก · การ์ดของ task มีบรรทัด ⏱ | 🟨 API 201 · nextRun ถูก | AI / 2026-10-12 |
| 3 | **ดูแท็บ Scheduler ของหน้าต่าง** | **ตารางเวลานั้นขึ้นมาเอง** ไม่ต้องเปิดโปรแกรมใหม่ | ✅ **PASS** | ผู้ใช้ / 2026-10-18 |
| 4 | **แก้การ์ดอื่นในหน้าต่างแล้ว Save** จากนั้นดูหน้าเว็บ | **ตารางเวลาที่สร้างจากเว็บยังอยู่** (ก่อนแก้: หายเงียบๆ) | ✅ **PASS** | ผู้ใช้ / 2026-10-18 |
| 5 | หน้าเว็บ · Edit · เปลี่ยนเป็น cron `30 6,18 * * MON-FRI` | When เป็น "cron …" · Next run เป็นรอบถัดไปของ cron · หน้าต่างเปลี่ยนตาม | 🟨 API ผ่าน | AI / 2026-10-12 |
| 6 | ใส่ cron มั่วๆ · Save | ไม่บันทึก บอกเหตุผลในฟอร์ม | ✅ **PASS** (API 400) | AI / 2026-10-12 · โปรแกรมจริง |
| 7 | เอาติ๊กวันออกหมด · Save | ไม่บันทึก บอกว่าต้องมีอย่างน้อยหนึ่งวัน | ✅ **PASS** | AI / 2026-10-12 · `test_schedule_json` |
| 8 | **Switch off** / **Switch on** | State เปลี่ยน · Next run เป็น — เมื่อปิด · การ์ดในหน้าต่างเปลี่ยนตาม | 🟨 API ผ่าน | AI / 2026-10-12 · `test_api_server` |
| 9 | **Pause the scheduler** | ข้อความบอกว่าหยุด · Next run ทุกแถวเป็น — · ปุ่มของหน้าต่างสลับตาม | 🟨 API ผ่าน | AI / 2026-10-12 |
| 10 | สร้าง/แก้/ลบตารางเวลา**ในหน้าต่าง** ขณะหน้าเว็บเปิด | หน้าเว็บเปลี่ยนตามเอง | ⬜ | |
| 11 | ตั้งเวลาอีก 2 นาทีข้างหน้า แบบ into the queue | ถึงเวลาแล้ว task เข้าคิว · Last เปลี่ยน | ⬜ | |
| 12 | แท็บ Tasks · Delete task ที่มีตารางเวลา | ถูกปฏิเสธ บอกชื่อตารางเวลา | ✅ **PASS** | AI / 2026-10-12 · โปรแกรมจริง 409 |
| 13 | **Delete** ตารางเวลา | หายทั้งหน้าเว็บและหน้าต่าง · task ยังอยู่ | 🟨 API ผ่าน · task ยังอยู่ | AI / 2026-10-12 |
| 14 | task ชนิด mount | ไม่มีปุ่ม Schedule · ไม่อยู่ในตัวเลือกของฟอร์ม | 🟨 API 400 | AI / 2026-10-12 |
| 15 | หน้าต่าง · เพิ่ม/แก้/ลบตารางเวลาแบบเดิม | ทำงานเหมือนเดิม | ⬜ | |
| 16 | **Docker โหมด web (ไม่มีหน้าต่าง)** · ตารางเวลาถึงกำหนดและรันจบ | คอลัมน์ Last บนหน้าเว็บ: เวลา · finished / error | ✅ **PASS** — โหมด web ไม่มีหน้าต่าง: running → finished พร้อมเวลาจบ (task ที่มี --dry-run) | AI / 2026-10-18 · คอนเทนเนอร์ dev |
| 18 | ตารางเวลาแบบ into the queue · คิวหยุดอยู่ · Run now · **ปิดแล้วเปิดโปรแกรมใหม่** | การ์ดและหน้าเว็บยังเป็น in the queue (ไม่ใช่ unknown) | ✅ **PASS** (หน้าเว็บ) | AI / 2026-10-18 · ตารางเวลา OneDrive จริง |
| 19 | หยุด scheduler ด้วยปุ่มของหน้าต่าง ขณะมีรอบของตารางเวลารออยู่ในคิว | รอบนั้นถูกเอาออก (เหมือนเดิม) | ⬜ | |
| 17 | กด Run now ซ้ำขณะรอบก่อนยังรันอยู่ | Last: "its last run has not finished" · พอรอบเดิมจบเป็น finished | 🟨 `test_api_server` | AI / 2026-10-18 |

---

### V-42 · คิว: เรียงลำดับและล้างจากหน้าเว็บ

marker: `POST /api/v1/queue/{id}/move` · `POST /api/v1/queue/clear`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | ใส่ task 3 ตัวเข้าคิว (คิวหยุดอยู่) · แท็บ Queue | มีเลขลำดับ · ปุ่ม ⤒ ↑ ↓ ⤓ กดได้เฉพาะที่ย้ายได้ | ⬜ | |
| 2 | กด ⤒ ที่ตัวสุดท้าย | ขึ้นไปเป็นอันดับ 1 · **แท็บ Queue ของหน้าต่างเรียงตาม** | 🟨 API ผ่าน (`test_api_server`) | AI / 2026-10-18 |
| 3 | เรียงในหน้าต่าง (ปุ่มขึ้น/ลง) | หน้าเว็บเรียงตาม | ⬜ | |
| 4 | เริ่มคิว ขณะตัวแรกรัน ลองย้ายตัวอื่นขึ้นไปบนสุด | ทำไม่ได้ (ปุ่มกดไม่ได้ / API 409) | 🟨 API ผ่าน | AI / 2026-10-18 |
| 5 | **Clear the queue** | ถามยืนยัน · ตัวที่รอหายหมด · ตัวที่รันอยู่ทำงานต่อ · task ยังอยู่ | 🟨 API ผ่าน | AI / 2026-10-18 |
| 5a | ตารางเวลาแบบ into the queue · Run now · แล้วเอารอบนั้นออกจากคิว**ทางหน้าเว็บ** | การ์ดในหน้าต่างและคอลัมน์ Last บนเว็บ: "removed from the queue" | ✅ **PASS** (API · `test_api_server` · `test_job_queue`) | AI / 2026-10-18 |
| 6 | Pause the queue / Start the queue ในแท็บ Queue | สถานะเปลี่ยน ทั้งหน้าเว็บและหน้าต่าง | ⬜ | |

---

### V-43 · เอกสาร API อัตโนมัติ

marker: `src/api_routes.*` · `GET /api/v1/openapi.json` · แท็บ API

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | เปิด `http://127.0.0.1:19999/api/v1/openapi.json` ใน browser โดยไม่ sign in | ได้ JSON · version 3.1.1 · ไม่มี token | ✅ **PASS** | AI / 2026-10-18 · 61 operations |
| 2 | หน้าเว็บ · แท็บ **API** | รายการแยกหมวด · กดเปิดดูรายละเอียดได้ · ป้าย open ที่ ping/auth/login/openapi/stream | ⬜ | |
| 3 | พิมพ์ `schedule` ในช่องกรอง | เหลือเฉพาะเส้นทางของตารางเวลา · หัวข้อหมวดที่ว่างหายไป | ⬜ | |
| 4 | **Try it** ที่ GET /system | เห็นคำตอบจริง | ⬜ | |
| 5 | นำ openapi.json ไปเปิดใน Swagger Editor หรือ import ใน Postman | อ่านได้ ไม่มี error | ⬜ | |
| 6 | เอาเส้นทางหนึ่งออกจาก `api_routes.cpp` แล้วรัน `test_api_server` | ล้ม บอกเส้นทางที่ขาด | ✅ **PASS** | AI / 2026-10-18 · ลบ queue/clear ชั่วคราว |
| 7 | เปิดฟอร์ม Upload แล้วสลับแท็บด้านบน แล้วกลับมาเปิดฟอร์มอีกครั้ง | แถบแท็บในฟอร์มยังขีดเส้นใต้แท็บที่เปิดอยู่ | ⬜ | |

---

### V-44 · API key แบบจำกัดสิทธิ์

marker: `ApiAuth::Keys` · `ApiRoutes` scope · `/api/v1/keys` · กล่อง Keys ในแท็บ API

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | แท็บ API · Make a key ชื่อ "test" ระดับ read | key ขึ้นครั้งเดียวพร้อมปุ่ม Copy · แถวขึ้นในรายการ | ⬜ | |
| 2 | เปิดหน้าเว็บใน private window · sign in ด้วย key นั้นในช่อง Token | ดูได้ทุกแท็บ · กด Run / Start queue ได้ข้อความว่า key นี้ทำได้แค่ read | 🟨 API: GET 200 · POST 403 `needs: control` | AI / 2026-10-18 |
| 3 | กลับมาหน้าเดิม · refresh | Last used ของ key มีเวลา | ✅ **PASS** (API) | AI / 2026-10-18 |
| 4 | **Take back** | หน้า private ใช้ต่อไม่ได้ (ต้อง sign in ใหม่) | ✅ **PASS** (API 401 ทันที) | AI / 2026-10-18 · โปรแกรมจริง |
| 5 | key ระดับ control · รัน task | ประวัติ/การ์ดงานบอก started by `api:<ชื่อ>` | 🟨 `test_api_server` | AI / 2026-10-18 |
| 6 | key ระดับ write · ลอง shutdown / สร้าง key | 403 | ✅ **PASS** | AI / 2026-10-18 · `test_api_server` |
| 7 | ตัว key ในที่เก็บ settings | ไม่มี (มีแต่ hash) | ✅ **PASS** | AI / 2026-10-18 · registry จริง |
| 8 | ตั้งชื่อซ้ำ (ตัวพิมพ์ต่าง) | ถูกปฏิเสธพร้อมเหตุผล | ✅ **PASS** | AI / 2026-10-18 · `test_api_auth` |

---

### V-45 · ดูการตั้งค่าของรีโหมดจากหน้าเว็บ (R1 · R2)

marker: `src/rclone_config.*` · `GET /api/v1/remotes/{name}/config` · ปุ่ม Settings

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | หน้าเว็บ · เลือกรีโหมด teldrive · **Settings** | ตาราง: chunk_size · upload_concurrency ฯลฯ พร้อมคำอธิบาย · access_token กับ api_host ขึ้นว่า "set — not shown" | 🟨 API ผ่าน | AI / 2026-10-19 |
| 2 | Settings ของ drive / onedrive / sftp | token · client_secret · pass ไม่แสดงค่า | ✅ **PASS** (API) | AI / 2026-10-19 · 7 รีโหมดจริง ไม่มีค่าลับหลุด |
| 3 | เปิด DevTools → Network ดูคำตอบของ /config | ไม่มีค่าลับในคำตอบดิบ | ✅ **PASS** (API) | AI / 2026-10-19 |
| 4 | แก้ rclone.conf ข้างนอกแล้วกด Settings อีกครั้ง | เห็นค่าใหม่ (อ่านใหม่ทุกครั้ง) | ⬜ | |
| 5 | sign in ด้วย key ระดับ write แล้วกด Settings | ถูกปฏิเสธ (ต้องเป็น admin) | ✅ **PASS** | AI / 2026-10-19 · `test_api_server` |

---

### V-46 · ลบและเปลี่ยนชื่อรีโหมดจากหน้าเว็บ (R3)

marker: `RenameRemote` · `DeleteRemote` · `PUT /api/v1/remotes/{name}/config` · `DELETE /api/v1/remotes/{name}`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | สร้างรีโหมดทดสอบ · Settings · **Rename this remote…** | ชื่อใหม่ขึ้นในรายการ · หน้าต่างเห็นชื่อใหม่ · ค่าเดิมครบ | ✅ **PASS** (API · local) | AI / 2026-10-19 · config ก่อน/หลังเหมือนเดิม |
| 2 | เปลี่ยนชื่อรีโหมดที่มี task ชี้อยู่ | task เปลี่ยนต้นทาง/ปลายทางตาม · ข้อความบอกจำนวน | 🟨 `test_api_server` | AI / 2026-10-19 |
| 3 | เปลี่ยนชื่อรีโหมด drive (OAuth) | ยังใช้งานได้หลังเปลี่ยน (ไม่ต้อง authorize ใหม่) | 🟨 ลองกับสำเนา config: ค่าเหมือนเดิมทุกค่า | AI / 2026-10-19 |
| 4 | เปลี่ยนเป็นชื่อที่มีอยู่แล้ว / ชื่อมี `:` | ถูกปฏิเสธพร้อมเหตุผล | ✅ **PASS** | AI / 2026-10-19 · โปรแกรมจริง 409 |
| 5 | **Delete this remote…** ที่มี task ใช้อยู่ | ถามอีกครั้งพร้อมรายชื่อ task · ตกลงแล้วลบ · task ยังอยู่ | 🟨 API ผ่าน | AI / 2026-10-19 |
| 6 | ลบหรือเปลี่ยนชื่อขณะมีงานใช้รีโหมดนั้น | ถูกปฏิเสธ | ✅ **PASS** | AI / 2026-10-19 · `test_api_server` |
| 7 | key ระดับ write ลองลบรีโหมด | 403 | ⬜ | |

---

### V-47 · สร้างและแก้รีโหมดจากหน้าเว็บ (R4)

marker: `CreateRemote` · `UpdateRemote` · `WhyNotSettings` · การ์ด + New remote · Edit settings…

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | Remotes · **+ New remote** · ค้น `sftp` · เลือก | ฟอร์มขึ้น: host (มี *) · user · port · pass (ช่องรหัส) · ลิงก์ Show N advanced options | ⬜ | |
| 2 | กด Save โดยไม่ใส่ host | ไม่บันทึก บอกว่าต้องมี host | ✅ **PASS** (API) | AI / 2026-10-19 · โปรแกรมจริง |
| 3 | ใส่ครบ · Save | รีโหมดใหม่ขึ้นในรายการ · หน้าต่างเห็นด้วย · รหัสผ่านใน rclone.conf ถูก obscure | ✅ **PASS** (API) | AI / 2026-10-19 |
| 4 | เลือกชนิด drive | บอกว่าต้อง sign in ผ่าน browser · ปุ่ม Save กดไม่ได้ | 🟨 API 409 | AI / 2026-10-19 |
| 5 | สร้าง teldrive: access_token · api_host | ใช้งานได้ (Refresh เห็นไฟล์) | ⬜ | |
| 6 | รีโหมดเดิม · Settings · **Edit settings…** · เปลี่ยน chunk_size · Save | ค่าเปลี่ยน · access_token คงเดิม (ไม่ได้พิมพ์) | 🟨 API: แก้ port แล้วรหัสคงเดิม | AI / 2026-10-19 |
| 7 | Edit settings ของ drive · เปลี่ยน scope | บันทึกได้ · token ไม่หาย · มีข้อความว่า rclone จะถามเรื่อง refresh token | 🟨 ลองบนสำเนา config: token คงเดิม | AI / 2026-10-19 |
| 8 | Edit settings · เปลี่ยนชื่อด้วย | ค่าเปลี่ยนแล้วเปลี่ยนชื่อ · task ตาม | ⬜ | |
| 9 | ส่ง key แปลก (`--config`) ผ่าน API | 400 ไม่มีอะไรถูกเขียน | ✅ **PASS** | AI / 2026-10-19 · `test_api_server` |

---

### V-48 · รีโหมดที่ sign in ผ่าน browser (R5)

marker: `TokenFrom` · `ContinueRemote` · `POST /api/v1/remotes/{name}/answer` · กล่อง Sign in

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | + New remote · ชนิด drive | กล่อง Sign in: คำสั่ง `rclone authorize "drive"` · ปุ่ม Copy · ช่องวาง | ⬜ | |
| 2 | รันคำสั่งบนเครื่องที่มี browser · วางทั้งก้อน · Save | ฟอร์มเปลี่ยนเป็นคำถาม team drive · ตอบ No · รีโหมดขึ้นในรายการ เดินดูได้ | ✅ **PASS** (API · token ของ gdrive) | AI / 2026-10-19 · 158 รายการ |
| 3 | วางข้อความที่ไม่ใช่ token | ไม่บันทึก บอกว่าไม่ใช่ token | ✅ **PASS** | AI / 2026-10-19 · `test_api_server` |
| 4 | OneDrive · Settings · Edit settings · วาง token ใหม่จาก `rclone authorize "onedrive"` | sign in ใหม่ได้ · รีโหมดกลับมาใช้ได้ | ⬜ | |
| 5 | สร้าง onedrive ใหม่ | คำถามเลือกชนิดไดรฟ์ (onedrive / sharepoint …) แล้วเลือกไดรฟ์ | ⬜ | |
| 6 | ใน Docker โหมด web | ทำข้อ 2 ได้เหมือนกัน (ไม่ต้องมี browser ในคอนเทนเนอร์) | ⬜ | |
| 7 | กด **Leave the rest** ระหว่างคำถาม | ปิดได้ · รีโหมดถูกบันทึกเท่าที่ไปถึง | ⬜ | |

---

### V-49 · ตั้งค่ารีโหมดในหน้าต่าง

marker: `src/remote_settings_dialog.*` · เมนูคลิกขวาที่รายการรีโหมด · เมนูของปุ่ม Config

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | คลิกขวาที่รีโหมด teldrive · **Settings…** | กล่องขึ้น: ค่าทั้งหมด · access_token กับ api_host เป็นช่องรหัสว่าง ("set — type to replace it") | ⬜ | |
| 2 | เปลี่ยน chunk_size · Save | ค่าเปลี่ยน · access_token คงเดิม · หน้าเว็บเห็นค่าใหม่ | ⬜ | |
| 3 | คลิกขวาที่ว่าง · **New remote…** · เลือก sftp · กรอก · Save | รีโหมดใหม่ขึ้นในรายการของหน้าต่างและหน้าเว็บ | ⬜ | |
| 4 | New remote · drive | กล่อง Sign in · คำสั่ง authorize · วาง token · Save · คำถาม team drive · Answer | ⬜ | |
| 5 | Settings · เปลี่ยนชื่อ · Save | ชื่อเปลี่ยน · บอกว่า task ไหนเปลี่ยนตาม | ⬜ | |
| 6 | Settings · **Delete this remote…** | ถามยืนยัน · ถ้ามี task ใช้อยู่ถามอีกครั้งพร้อมรายชื่อ · หายจากรายการ | ⬜ | |
| 7 | ปุ่ม Config (กดตรงๆ) | ยังเปิด rclone config ในเทอร์มินัลเหมือนเดิม | ⬜ | |
| 8 | ลูกศรของปุ่ม Config | เมนู New remote… · Settings of the chosen remote… (กดไม่ได้ถ้าไม่ได้เลือก) · rclone config in a terminal | ⬜ | |

---

### V-50 · อัปโหลดจากเครื่องที่เปิด browser

marker: `ParseHttpHead` · `ApiServer::startReceiving` · `RemoteOps::UploadReceived` · ปุ่ม Upload from this computer

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | Remotes · เปิดโฟลเดอร์ · **Upload from this computer** · เลือก 2 ไฟล์ | แถบความคืบหน้าต่อไฟล์ · "received — going up" · งานใน Jobs · รายการ refresh เมื่อจบ | 🟨 API ผ่าน (20 MB ไบต์ตรงกัน) | AI / 2026-10-19 · gdrive |
| 2 | **ลากโฟลเดอร์**มาวางบนรายการ | ไฟล์ขึ้นไปพร้อมโฟลเดอร์ย่อยแบบเดิม | ⬜ | |
| 3 | ไฟล์ใหญ่ (หลาย GB) | หน่วยความจำของโปรแกรมไม่โตตามไฟล์ | 🟨 20 MB: 127 → 128 MB | AI / 2026-10-19 |
| 4 | ปิดแท็บระหว่างส่ง | ไม่มีไฟล์พักค้างบนเครื่องที่โปรแกรมรัน | ✅ **PASS** | AI / 2026-10-19 · `test_api_server` |
| 5 | ใน Docker · ส่งจาก browser บนอีกเครื่อง | ขึ้นรีโหมดได้ | ⬜ | |
| 6 | sign in ด้วย key ระดับ read แล้วลองส่ง | ถูกปฏิเสธ | ⬜ | |

---

### V-51 · ความเร็วรวมและปรับความเร็วขณะรัน

marker: `RcClient::setBandwidth` · `POST /api/v1/jobs/{id}/bandwidth` · `POST /api/v1/bandwidth`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | เริ่ม transfer · ดูหัวหน้าเว็บและแท็บ Jobs | ⇅ ความเร็วรวม · การ์ดมีความเร็ว และเวลาที่เหลือ | 🟨 API: speed ตรงกับงาน | AI / 2026-10-19 |
| 2 | การ์ดงาน · **Speed…** · ใส่ 200k | ความเร็วลดลงเหลือราว 200 KB/s · การ์ดบอก "limited to" | ✅ **PASS** (API · วัดได้ 191 KB/s) | AI / 2026-10-19 · gdrive |
| 3 | **Limit every running transfer…** · off | ทุกงานกลับมาเต็มความเร็ว | ✅ **PASS** (API) | AI / 2026-10-19 |
| 4 | ใส่ค่าที่ไม่ใช่ความเร็ว (`fast`) | ถูกปฏิเสธพร้อมเหตุผล | ✅ **PASS** | AI / 2026-10-19 · `test_api_server` |
| 5 | หน้าต่าง · การ์ดงานที่กำลังส่ง · **Speed…** · ใส่ 200k | ปุ่มกลายเป็น `≤ 200Ki/s` · ความเร็วลดลง · หน้าเว็บบอก limited to | ⬜ | |
| 6 | ตั้งความเร็วจากหน้าเว็บขณะเปิดหน้าต่างอยู่ | การ์ดในหน้าต่างเปลี่ยนตาม | ⬜ | |
| 7 | หน้าต่าง · ใส่ `fast` | บอกว่าไม่ใช่ความเร็ว · ไม่ส่งให้ rclone | ⬜ | |
| 8 | งานจบ | ปุ่ม Speed… หายไป | ⬜ | |

### V-52 · serve โฟลเดอร์ให้โปรแกรมอื่น

marker: `RemoteOps::Serve` · `POST /api/v1/remotes/{name}/serve` · `ServeDialog`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | API: serve webdav · 127.0.0.1 · read only · user/password | ไม่ login 401 · login 207 · เขียนไม่ได้ | ✅ **PASS** (gdrive · MKCOL 405) | AI / 2026-10-19 |
| 2 | ดู command line ของ rclone และ `/jobs` | ไม่มีรหัสผ่าน | ✅ **PASS** | AI / 2026-10-19 |
| 3 | Stop | พอร์ตปิด · ไม่มี rclone ค้าง | ✅ **PASS** | AI / 2026-10-19 |
| 4 | everyone ไม่มีรหัส · protocol แปลก · พอร์ตซ้ำ · user ขึ้นต้น `-` · key ระดับ write | ปฏิเสธพร้อมเหตุผล · 403 | ✅ **PASS** | AI / 2026-10-19 · `test_api_server` |
| 5 | หน้าเว็บ · **Serve** · webdav | การ์ดใน Jobs มีที่อยู่ · **Copy address** | ⬜ | |
| 6 | เปิดที่อยู่ใน Explorer / Finder / แอปมือถือ (everyone + รหัส) | เห็นไฟล์ · ถามรหัส | ⬜ | |
| 7 | FTP และ SFTP (FileZilla) | เข้าได้ด้วย user/password | ⬜ | |
| 8 | หน้าต่าง · คลิกขวาโฟลเดอร์ · **Serve…** | กล่องบอกที่อยู่ · การ์ด "Serving" ใน Jobs | ⬜ | |
| 9 | หน้าเว็บสั่ง stream ขณะเปิดหน้าต่าง | การ์ด stream ขึ้นในหน้าต่าง · Stop ได้ | ⬜ | |
| 10 | หน้าต่าง · **Stop all transfers** ขณะมี serve | serve ยังทำงาน | ⬜ | |

### V-53 · ดูไฟล์ในตัว

marker: `FileReader` · `GET /api/v1/remotes/{name}/cat` · `FileViewDialog`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | API: `.html` · ข้อความยาวเกิน limit · png · pdf · mkv · ไม่ระบุไฟล์ · ไม่มีรีโหมด · rclone ล้ม | text (ไม่ใช่หน้า) · truncated · ชนิดถูก · 400/404/502 | ✅ **PASS** | AI / 2026-10-19 · `test_api_server` |
| 2 | หน้าเว็บ · ดับเบิลคลิกภาพ | ภาพขึ้นในกล่อง ไม่ล้นจอ | ⬜ | |
| 3 | หน้าเว็บ · View PDF | PDF ขึ้นใน frame | ⬜ | |
| 4 | หน้าเว็บ · ไฟล์ข้อความ > 2 MiB | 2 MiB แรก + บอกว่าเห็นแค่ส่วนต้น | ⬜ | |
| 5 | หน้าเว็บ · ไฟล์ `.html` ที่มี `<script>` | เห็นเป็นข้อความ · สคริปต์ไม่รัน | ⬜ | |
| 6 | หน้าเว็บ · `.mkv` · Play it | เปิดเครื่องเล่น stream | ⬜ | |
| 7 | หน้าเว็บ · ภาพ > 64 MiB | "Too large to show here" | ⬜ | |
| 8 | หน้าต่าง · ดับเบิลคลิกภาพ / ข้อความ | กล่องแสดงภาพ (ขนาด) / ข้อความ | ⬜ | |
| 9 | หน้าต่าง · View PDF | เปิดด้วยโปรแกรมอ่าน PDF ของเครื่อง | ⬜ | |
| 10 | config ที่เข้ารหัส · serve ที่มีรหัสผ่าน | rclone เปิด config ได้ · serve ขึ้น | ⬜ | (test: ✅ ทั้งสองค่าไปถึง rclone) |

### V-54 · webhook และการแจ้งเตือน

marker: `Webhooks` · `/api/v1/webhooks` · `WebhooksDialog` · `jobs-notify`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | API: เพิ่ม webhook ที่มี secret · รัน task · เฉพาะที่ล้ม · test · เปลี่ยนโดยไม่บอก secret · ที่อยู่ไม่ใช่ http · key ระดับ write | event job.finished ตรง · ลายเซ็นถูก · hook เฉพาะล้มไม่ถูกเรียก · test 204 · secret คงอยู่ · 400 · 403 | ✅ **PASS** | AI / 2026-10-19 · `test_api_server` |
| 2 | หน้าเว็บ · แท็บ API · เพิ่ม ntfy (text) · Test | มือถือได้แจ้งเตือน "Test: …" | ⬜ | |
| 3 | Discord webhook · งาน copy จบ | ข้อความในช่อง | ⬜ | |
| 4 | งานล้ม (รีโหมดผิด) กับ hook เฉพาะที่ล้ม | ได้ job.failed | ⬜ | |
| 5 | หน้าต่าง · File → Webhooks… · เพิ่ม/Change/Remove/Test | ตรงกับหน้าเว็บ · เปลี่ยนจากหน้าเว็บแล้วหน้าต่างวาดใหม่ | ⬜ | |
| 6 | หน้าเว็บ · ☐ Tell me in this browser… · transfer จบ | browser แจ้งเตือน | ⬜ | |
| 7 | งานจากตารางเวลาตอนไม่มีหน้าต่าง (daemon) | webhook ยังถูกเรียก | ⬜ | |

### V-55 · bisync

marker: `JobOptions::Bisync` · `"resync"` · `Run first time (resync)`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | API: task bisync · run resync · run ธรรมดา · resync กับ copy · PUT operation | `--resync` เฉพาะรอบที่ขอ · ไม่ติดกับ task · 400 | ✅ **PASS** | AI / 2026-10-19 · `test_api_server` |
| 2 | rclone จริง: คำสั่งของ task + resync / ไม่ resync / เปลี่ยนทั้งสองฝั่ง | exit 7 · รวมกัน · ตรงกัน | ✅ **PASS** (โฟลเดอร์ในเครื่อง) | AI / 2026-10-19 |
| 3 | หน้าเว็บ · ฟอร์ม task เลือก Bisync · บันทึก · First run (resync) | งานจบ · สองฝั่งตรงกัน | ⬜ | |
| 4 | หน้าต่าง · กล่อง transfer เลือก Bisync · คลิกขวา → Run first time (resync) | เหมือนข้อ 3 | ⬜ | |
| 5 | bisync เครื่องนี้ ↔ gdrive ตามตารางเวลา | รอบต่อไปไม่มี --resync · การเปลี่ยนทั้งสองฝั่งไปถึงกัน | ⬜ | |
| 6 | เปิด task เดิม (copy/sync/mount) หลังอัปเดต | ชนิดเดิมไม่เปลี่ยน | ⬜ | |

### V-56 · ตัวช่วยสำรองข้อมูล

marker: `Backup::Build` · `POST /api/v1/backups` · `BackupDialog` · `{date}`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | API: backup mirror + versions · keep everything · สองฝั่งในเครื่อง · สองฝั่งรีโหมด · ราก · เวลา/วันผิด · นอกโฟลเดอร์ | task sync + backup-dir · copy · 400 · 403 · `{date}` ถูกแทน | ✅ **PASS** | AI / 2026-10-19 · `test_api_server` |
| 2 | rclone จริง: sync + backup-dir ข้างๆ สองรอบ | ไฟล์เก่าและที่ลบอยู่ใน `<dest>-versions/<date>` | ✅ **PASS** (โฟลเดอร์ในเครื่อง) | AI / 2026-10-19 |
| 3 | หน้าเว็บ · Tasks → New backup… → Make | task + ตารางเวลาขึ้น · Run แล้วไฟล์ไปถึง gdrive | ⬜ | |
| 4 | หน้าต่าง · File → New backup… | เหมือนข้อ 3 · ขึ้นในหน้าเว็บด้วย | ⬜ | |
| 5 | แก้ไฟล์ในเครื่อง · รอรอบถัดไป | ไฟล์เดิมอยู่ใน `-versions/<วันเวลา>` บน gdrive | ⬜ | |

### V-57 · เฝ้าโฟลเดอร์

marker: `FolderWatches` · `/api/v1/tasks/{id}/watch` · `Watch folder for changes`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | API: เฝ้า · ของเดิมไม่นับ · เพิ่มไฟล์ · ดูสองครั้ง · เปลี่ยนอีกขณะอยู่ในคิว · เลิกเฝ้า · download | เข้าคิวครั้งเดียว · ไม่ซ้ำ · 404 · 400 | ✅ **PASS** | AI / 2026-10-19 · `test_api_server` |
| 2 | หน้าเว็บ · Watch folder · คัดลอกไฟล์ 50 ไฟล์เข้าโฟลเดอร์ | เข้าคิว 1 ครั้ง หลังเงียบ 30 วิ · ไฟล์ขึ้น gdrive | ⬜ | |
| 3 | หน้าต่าง · คลิกขวา task → Watch folder for changes | ☑ · ตรงกับหน้าเว็บ | ⬜ | |
| 4 | Docker · โฟลเดอร์ที่ mount จาก Windows host | การดูทุก 5 นาทีจับได้ (สัญญาณระบบไม่มา) | ⬜ | |
| 5 | แก้ไฟล์ขณะงานของ task นั้นรันอยู่ | รอบต่อไปส่งการเปลี่ยนนั้น | ⬜ | |

### V-58 · ระบบหลายภาษา

marker: `WindowLanguage` · `/api/v1/languages` · `t()` · `docs/TRANSLATING.md`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | API: `/languages` ไม่ sign in · `/i18n/en.json` · ภาษาที่ไม่มี · ชื่อแปลก | 200 · 200 · 404 · 404 | ✅ **PASS** | AI / 2026-10-19 · `test_api_server` |
| 2 | build มี `.ts` ลอง | `.qm` อยู่ใน `:/i18n/` | ✅ **PASS** (ลบไฟล์ลองแล้ว) | AI / 2026-10-19 |
| 3 | Preferences → แท็บ Language | มี "The language this computer is set to" และ English · บันทึกได้ | ⬜ | |
| 4 | หน้าเว็บ (มีภาษาเดียว) | ไม่มีช่องเลือกภาษา · ทุกอย่างเหมือนเดิม | ⬜ | |
| 5 | เมื่อมี th.json / rclonebrowser_th.ts จริง | เลือกได้ทั้งสองฝั่ง · คำที่ยังไม่แปลเป็นอังกฤษ | ⬜ | (รอคำแปล) |

### V-59 · ความคืบหน้ารายไฟล์ · เวลามี offset

marker: `"transferring"` ใน `stats` · `JobJson::Stamp` · `fileBars`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | API: งานที่ rclone บอกสองไฟล์ (หนึ่งไม่รู้ขนาด) | ทั้งสองไฟล์ · percent 50 และ -1 · checks · phase · startedAt มี offset | ✅ **PASS** | AI / 2026-10-19 · `test_api_server` |
| 2 | หน้าเว็บ · copy โฟลเดอร์หลายไฟล์ใหญ่ | การ์ดมีแถบของแต่ละไฟล์ ขยับตามจริง · ตรงกับหน้าต่าง | ⬜ | |
| 3 | `/history` และ `/keys` | เวลาลงท้าย `+07:00` | ⬜ | |

### V-60 · ค้นและแบ่งหน้าประวัติ

marker: `RunHistory::search` · `GET /api/v1/history/{requestId}` · `Load older`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | API: limit · before · state · kind · q (มี `_`) · before ผิด · รายละเอียด · ไม่มี · สองงานในวินาทีเดียว | ถูกทุกข้อ · ไม่ตกหล่น | ✅ **PASS** | AI / 2026-10-19 · `test_api_server` |
| 2 | หน้าเว็บ · History · พิมพ์ชื่อรีโหมด | เหลือแถวที่ตรง · ช่องค้นไม่หลุดโฟกัส | ⬜ | |
| 3 | กรอง error · Load older จนหมด | ปุ่มหายเมื่อหมด · refresh แล้วแถวเก่าไม่หาย | ⬜ | |
| 4 | คลิกแถว | รายละเอียดครบ · ปุ่ม Log เปิด log | ⬜ | |
| 5 | หน้าต่าง · History · ค้น · กรองผล/ชนิด · Load older | ตรงกับหน้าเว็บ · หางานที่เก่ากว่า 500 รายการเจอ | ⬜ | |
| 6 | หน้าต่าง · ดับเบิลคลิกแถว | กล่องรายละเอียด · Open log | ⬜ | |

### V-61 · ความสามารถของรีโหมดบนหน้าเว็บ

marker: `GET /api/v1/remotes/{name}/capabilities` · `drawCapabilities`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | API: fake (แบบ teldrive) · ถามซ้ำ · ไม่มีรีโหมด | link ได้ · cleanup/dedupe ไม่ได้ · ไม่มี hash · 404 | ✅ **PASS** | AI / 2026-10-19 · `test_api_server` |
| 2 | หน้าเว็บ · เปิดรีโหมด teldrive | Dedupe · Cleanup ปิดพร้อมเหตุผล · Check บอกว่าไม่มี hash · ตรงกับหน้าต่าง | ⬜ | |
| 3 | สลับ teldrive ↔ gdrive | ปุ่มเปลี่ยนตามรีโหมดทันที | ⬜ | |

### V-62 · ใครขอให้รันจากคิว

marker: `askedBy` · schema 4

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | API: key "sonarr" ใส่คิว · คิวเริ่ม · งานจบ | `/queue` · `/jobs` · ประวัติ บอก `api:sonarr` · startedBy ยังเป็น queue | ✅ **PASS** | AI / 2026-10-19 · `test_api_server` |
| 2 | เปิด build นี้กับ database เดิมของจริง (schema 3) | ขึ้น schema 4 · คิว task ประวัติเดิมครบ | ⬜ | (สำรอง rclone-browser.db ก่อน) |
| 3 | หน้าต่าง · ใส่คิว · ตารางเวลา · เฝ้าโฟลเดอร์ | askedBy = window · schedule: ชื่อ · folder watch | ⬜ | |

### V-63 · กล่องถามของหน้าเว็บ

marker: `askbox` · `askText` · `askYes`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | สร้างโฟลเดอร์ · เปลี่ยนชื่อ · Speed… · Watch folder | กล่องของหน้าเว็บ (ไม่ใช่ของ browser) · Enter ตกลง · Escape ยกเลิก | ⬜ | |
| 2 | ลบไฟล์ · ลบ task · ลบรีโหมด · Take back key · Remove webhook | ปุ่มแดง "Yes, do it" · ยกเลิกแล้วไม่มีอะไรเกิด | ⬜ | |
| 3 | Dedupe (ไม่ใช่ dry run) จากในกล่อง Dedupe | กล่องยืนยันอยู่เหนือกล่องเดิม | ⬜ | |
| 4 | Copy address บนหน้าที่ไม่ใช่ HTTPS | กล่อง "Copy it from here" เลือกข้อความได้ · ปุ่ม Done | ⬜ | |
| 5 | จอมือถือ | กล่องพอดีจอ · อ่านได้ | ⬜ | |

### V-64 · เลือกหลายรายการและเมนูคลิกขวา

marker: `browsing.picked` · `pickRow` · `row-menu` · `deleteMany`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | คลิก · Ctrl+คลิก · Shift+คลิก · ช่องติ๊ก · ช่องหัวตาราง | เลือกตามแบบ file manager · "N chosen" | ⬜ | |
| 2 | เลือก 3 แล้ว Delete | ถามครั้งเดียวพร้อมชื่อ · ลบครบ · รายการอ่านใหม่ | ⬜ | (โฟลเดอร์ทดสอบเท่านั้น) |
| 3 | เลือก 3 แล้ว Copy ไปอีกโฟลเดอร์ | 3 งานใน Jobs · ไฟล์ถึงครบ | ⬜ | |
| 4 | เลือก 2 · Rename / Link / View / Download | ปิด · Download บอกเหตุผลใน tooltip | ⬜ | |
| 5 | คลิกขวาไฟล์ · โฟลเดอร์ · แถวที่ไม่ได้เลือก · ใกล้ขอบจอ | เมนูถูกชนิด · เลือกเดี่ยว · ไม่ล้นจอ · Escape ปิด | ⬜ | |

### V-65 · หน้าเว็บกลับมาเองเมื่อพอร์ตว่าง

marker: `apiProblem` · `Try now` · "trying again every 15 seconds"

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | ถือพอร์ต 19999 แล้วเปิดโปรแกรม · ปล่อยพอร์ต | หน้าเว็บขึ้นเองภายใน 15 วิ โดยไม่ต้องเปิดใหม่ | ✅ **PASS** | AI / 2026-10-19 · Release จริง |
| 2 | ระหว่างที่พอร์ตยังไม่ว่าง | status bar บอกเหตุ · Help → Open web interface บอกเหตุ + Try now | ⬜ | |
| 3 | `RB_API_ENABLED=0` | ไม่ลองใหม่ · ไม่มีข้อความรบกวน | ⬜ | |

### V-66 · ที่อยู่ของแต่ละหน้า

marker: `route()` · `setAddress` · `#/history/<id>`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | กดแท็บ Jobs → History → Back → Back | กลับ Jobs แล้ว Remotes · ที่อยู่ตาม | ⬜ | |
| 2 | เปิดโฟลเดอร์ลึกสองชั้น · Back | กลับโฟลเดอร์ก่อนหน้า | ⬜ | |
| 3 | คัดลอกที่อยู่ `#/remotes/gdrive/films` ไปแท็บใหม่ | เปิดโฟลเดอร์นั้นหลัง sign in | ⬜ | |
| 4 | คลิกแถวประวัติ · ที่อยู่ · Back | `#/history/<id>` · Back ปิดกล่อง | ⬜ | |
| 5 | F5 บนแท็บ Schedules | ยังอยู่ Schedules | ⬜ | |

### V-67 · รายละเอียดของ task และงาน

marker: `openTask` · `openJob` · `#/tasks/<id>` · `#/jobs/<id>`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | Tasks · คลิกชื่อ task ที่มีตารางเวลาและเคยรัน | การตั้งค่า · ตารางเวลา · 10 ครั้งล่าสุด · คำสั่ง · ปุ่ม | ⬜ | 19991 |
| 2 | คลิกแถวในรายการครั้งล่าสุด | เปิดรายละเอียดรอบนั้น | ⬜ | |
| 3 | Jobs · คลิกชื่องานที่กำลังส่ง | ตัวเลข · แถบรายไฟล์ · log ท้าย · Stop | ⬜ | |
| 4 | เปิด `#/tasks/<id>` ตรงๆ · F5 · Back | เปิดรายละเอียด · Back ปิด | ⬜ | |

### V-68 · โควตาและยกเลิกคำถาม

marker: `readQuota` · `data-ask-cancel` · `AbortController`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | เปิด gdrive | แถบ % · used of total · free ข้าง path | ⬜ | 19991 |
| 2 | เปิด teldrive (about ไม่ได้) | ไม่มีโควตา ไม่มี error | ⬜ | |
| 3 | Size ของโฟลเดอร์ใหญ่ → Cancel | กล่องปิด · ใน Jobs/ประวัติงาน size ถูกหยุด (stopped) | ⬜ | |
| 4 | เปิดหน้าใหม่ภายใน 10 นาที | ไม่มีแถว about ใหม่ในประวัติ | ⬜ | |

### V-69 · จอมือถือ และเหตุการณ์ remotes

marker: `@media (max-width: 640px)` ท้าย app.css · event `remotes`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | API: event stream · อ่านรีโหมดใหม่ | `event: remotes` พร้อมชื่อ | ✅ **PASS** | AI / 2026-10-19 · `test_api_server` |
| 2 | เปิด 19991 สองแท็บ · ลบ/เปลี่ยนชื่อรีโหมดในแท็บหนึ่ง | อีกแท็บเปลี่ยนตามทันที · ถ้าเปิดรีโหมดนั้นอยู่ บอกและกลับรายการ | ⬜ | |
| 3 | มือถือจริง (หรือ DevTools 375px) ทุกแท็บ | ไม่มีเลื่อนข้างทั้งหน้า · ปุ่มกดง่าย · ตารางเลื่อนในกรอบ | ⬜ | |
| 4 | มือถือ · กดค้างแถวไฟล์ | เมนูขึ้น · อยู่ในจอ | ⬜ | |
| 5 | มือถือ · ฟอร์ม task · รายละเอียด task · log | อยู่ในจอ เลื่อนได้ | ⬜ | |

### V-70 · คำถามไม่ขึ้นในประวัติ

marker: `JobKindIsQuestion`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | API: size · about · แล้ว mkdir | ประวัติ 0 แถว แล้ว 1 แถว | ✅ **PASS** | AI / 2026-10-19 · `test_api_server` |
| 2 | หน้าต่าง · Size · Info · Tree | ได้คำตอบ · ไม่มีแถวใหม่ใน History · ไม่มี log ใหม่ | ⬜ | |
| 3 | หน้าเว็บ · เปิดรีโหมดหลายครั้ง (โควตา) | ไม่มีแถว about ในประวัติ | ⬜ | 19991 |

### V-71 · API key ในหน้าต่าง

marker: `ApiKeysDialog` · File → API keys…

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | File → API keys… · ทำ key "test" ระดับ read | key แสดงครั้งเดียว · Copy ได้ · ขึ้นในรายการ · หน้าเว็บแท็บ API ก็เห็น | ⬜ | |
| 2 | ใช้ key นั้นเรียก `/api/v1/jobs` แล้วเปิดกล่องใหม่ | Last used เปลี่ยน | ⬜ | |
| 3 | Take back | ถามก่อน · key ใช้ไม่ได้ทันที | ⬜ | |

### V-72 · หน้าต่างคัดลอกข้ามรีโหมด

marker: "Copy or move to another remote…"

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | เลือก 2 ไฟล์ใน gdrive · คลิกขวา → Copy or move to another remote… → teldrive:test · Dry run | 2 การ์ดใน Jobs (dry run) · ไม่มีอะไรเปลี่ยน | ⬜ | |
| 2 | ทำจริง (Copy) | ไฟล์ถึง teldrive · ต้นทางยังอยู่ | ⬜ | (โฟลเดอร์ทดสอบเท่านั้น) |
| 3 | Move · โฟลเดอร์ไปที่ที่อยู่ข้างในตัวเอง | ปฏิเสธพร้อมเหตุผล | ⬜ | |

### V-73 · ค่าของโปรแกรมจากหน้าต่างและหน้าเว็บ

marker: `ProgramSettings` · `/api/v1/settings` · Preferences → Web page

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | API: เปลี่ยน idle และ roots · โฟลเดอร์ไม่มีจริง · ติดลบ · env ตั้งไว้ · key write | ใช้ทันที · 400 · 409 · 403 | ✅ **PASS** | AI / 2026-10-19 · `test_api_server` |
| 2 | หน้าเว็บ 19991 · API → Program settings | roots แสดง /media แบบแก้ไม่ได้ (RB_LOCAL_ROOTS) · idle แก้ได้ | ⬜ | |
| 3 | หน้าต่าง · Preferences → Web page · Add folder… · OK | ค่าบันทึก · หน้าเว็บเห็นตรงกัน · ตัวเลือกไฟล์ของหน้าเว็บเห็นแค่โฟลเดอร์นั้น | ⬜ | |

### V-74 · mount task ได้ตัวเลือกจาก Preferences

marker: `Settings/mount` ใน `StartTask`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | test: Settings/mount = `--vfs-cache-max-age 2h` · StartTask ของ mount task | คำสั่งมีตัวเลือกนั้น | ✅ **PASS** | AI / 2026-10-19 · `test_api_server` |
| 2 | ตั้งตัวเลือก mount ใน Preferences · รัน mount task จากหน้าเว็บ/ตารางเวลา | Log ของงานแสดงตัวเลือกนั้นในคำสั่ง | ⬜ | |

### V-75 · ตัวเลือก Mount บนหน้าเว็บ · autostart ใน core

marker: `saveAs` · `autoStart` · `AppCore::startAutoMounts`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | API: mount พร้อม volume · extra · saveAs · autoStart · startAutoMounts ตอนขึ้นอยู่และตอนลง · extra ต้องห้าม | task ถูกเก็บ · คำสั่งมีตัวเลือก · ไม่ซ้ำ · ขึ้นใหม่ด้วย autostart · 400 | ✅ **PASS** | AI / 2026-10-19 · `test_api_server` |
| 2 | หน้าเว็บ · Mount → More · เก็บเป็น task + ทุกครั้งที่เปิด | task ขึ้นในแท็บ Tasks · mount ขึ้น | ⬜ | |
| 3 | ปิดแล้วเปิดโปรแกรม (หน้าต่าง) | mount ขึ้นเองครั้งเดียว · การ์ดขึ้นในหน้าต่าง | ⬜ | |
| 4 | Docker โหมด web · restart container | mount ที่ตั้ง autostart ขึ้นเอง (เดิมไม่ขึ้น) | ⬜ | |

### V-76 · ตัวเลือก Copy/Move บนหน้าเว็บ

marker: `CopyOptions` · `pick-more`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | API: copy พร้อม emptyFolders · noTraverse · removeEmptyFolders (copy) · filter · extra · extra ต้องห้าม | คำสั่งมีครบ · ไม่มี delete-empty บน copy · 400 | ✅ **PASS** | AI / 2026-10-19 · `test_api_server` |
| 2 | หน้าเว็บ 19991 · Copy → More · filter `- *.nfo` · Dry run | log ของงานแสดงว่าไฟล์ .nfo ไม่ถูกส่ง | ⬜ | |
| 3 | Move → More | มีตัวเลือก "Remove the folders a move leaves empty" (Copy ไม่มี) | ⬜ | |

### V-77 · Check กับเครื่องนี้ · Size หลายรายการ

marker: `toLocal` · `sizeOfMany`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | API: check กับโฟลเดอร์ที่อนุญาต · นอกขอบเขต | 200 · ปลายทางเป็น path ตรงๆ · 403 | ✅ **PASS** | AI / 2026-10-19 · `test_api_server` |
| 2 | หน้าเว็บ 19991 · Check → This machine → /media/... | ผลเทียบ · the same / differences | ⬜ | |
| 3 | เลือก 3 รายการ · Size | ยอดรวม + ต่อรายการ · Cancel กลางทาง | ⬜ | |

### V-78 · ดาวน์โหลดหลายรายการบนหน้าเว็บ

marker: `downloadMany`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | หน้าเว็บ 19991 · เลือก 3 ไฟล์ · Download → /media/films | 3 งานใน Jobs · ไฟล์ถึงครบ | ⬜ | |
| 2 | เลือกหลายรายการ · คลิกขวา → Download… | เหมือนข้อ 1 | ⬜ | |
| 3 | เลือกรายการเดียว · Download | ฟอร์ม transfer เต็มเหมือนเดิม | ⬜ | |

### V-79 · สคริปต์หลัง mount: core รัน หน้าเว็บแสดงผล

marker: `MountOptions.script` · `ScriptReport` · `scriptReport`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | API: mount พร้อม script (admin) · key write · ไฟล์นอกโฟลเดอร์ที่อนุญาต | `/jobs` มี output (รหัสผ่านเป็น `***`) และ exit code 3 · 403 · 403 | ✅ **PASS** | AI / 2026-10-20 · `test_api_server` Windows + Linux |
| 2 | หน้าเว็บ 19991 · Mount → Script → เลือก `/media/after.sh` (echo แล้ว exit 0) | รายละเอียดงานบอก running → exit 0 และ output ของสคริปต์ | ⬜ | |
| 3 | สคริปต์ที่ไม่มีสิทธิ์รัน (ไม่มี chmod +x) | รายละเอียดงานบอกว่าเริ่มไม่ได้ · mount ยังอยู่ | ⬜ | |
| 4 | ติ๊กเก็บเป็น task · รัน task นั้นจากแท็บ Tasks | สคริปต์รันอีกครั้ง · ผลขึ้นเหมือนข้อ 2 | ⬜ | |

### V-80 · กล่องถามของหน้าเว็บกลับมาใช้ได้ (ชื่อ `ask` ซ้ำ)

marker: `askRclone`

| # | ทำอะไร | ต้องได้อะไร | ผล | ผู้ทดสอบ / วันที่ |
|---|---|---|---|---|
| 1 | test: app.js ไม่มี function ระดับบนสุดชื่อซ้ำ | ผ่าน (ของเดิมเจอ `ask` ซ้ำ) | ✅ **PASS** | AI / 2026-10-20 · `test_api_server` |
| 2 | หน้าเว็บ · คิวที่หยุดอยู่มีงาน · Clear the queue | ถามก่อน · Yes → คิวว่าง · ไม่มี "no such endpoint" | ❌ **FAIL** ก่อนแก้ | ผู้ใช้ / 2026-10-20 · `/remotes//[object Object]` |
| 3 | หลังแก้: ข้อ 2 อีกครั้ง · Delete ไฟล์ · New folder · Stop งาน · ลบ task | ทุกอันถามก่อนแล้วทำตาม | ⬜ | |
| 4 | Size · Tree · About ของรีโหมด | ยังทำงาน · Cancel ได้ | ⬜ | |

---

## วิธีเพิ่มรายการใหม่

เมื่อแก้อะไรที่ต้องมีคนทดสอบ ให้ทำสองอย่างคู่กัน:

1. ทิ้ง marker ไว้ตรงโค้ด พร้อมรหัสรายการ:
   ```cpp
   // TEST: (V-07) <ทดสอบอย่างไร และเกณฑ์ผ่านคืออะไร>
   ```
2. เพิ่มหัวข้อ `### V-07 · ...` ในไฟล์นี้ พร้อมตารางให้ผู้ทดสอบกรอก

รหัสต้องไม่ซ้ำและไม่นำกลับมาใช้ใหม่ แม้รายการเก่าจะเลิกใช้แล้ว
