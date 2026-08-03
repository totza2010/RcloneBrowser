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

> ⚠️ `list_of_job_options` ยังใช้ `QDataStream` แบบ binary (`tasks.bin`) — ถ้า task เดิม
> โหลดไม่ขึ้นหลังอัปเดต ให้ระบุว่า `FAIL` ทันที นี่เป็นความเสี่ยงหลักของรอบนี้

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
| Layer boundaries | | | |
| Build (Linux) | | | |
| Build (Linux, no Multimedia) | | | |
| Build (Windows) + ตรวจ hardening | | | |
| build.yml ไม่ถูก trigger | | | |

> ⚠️ ยังไม่เคยรันจริงบน GitHub — job Linux ทั้งสองตัวและ path ของ `dumpbin`
> บน runner ตรวจได้จากเครื่องนี้ไม่ได้ ถ้า fail รอบแรกให้บันทึก error ไว้ตรงนี้

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

## วิธีเพิ่มรายการใหม่

เมื่อแก้อะไรที่ต้องมีคนทดสอบ ให้ทำสองอย่างคู่กัน:

1. ทิ้ง marker ไว้ตรงโค้ด พร้อมรหัสรายการ:
   ```cpp
   // TEST: (V-07) <ทดสอบอย่างไร และเกณฑ์ผ่านคืออะไร>
   ```
2. เพิ่มหัวข้อ `### V-07 · ...` ในไฟล์นี้ พร้อมตารางให้ผู้ทดสอบกรอก

รหัสต้องไม่ซ้ำและไม่นำกลับมาใช้ใหม่ แม้รายการเก่าจะเลิกใช้แล้ว
