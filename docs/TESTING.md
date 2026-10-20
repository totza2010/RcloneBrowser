# การทดสอบ: ส่วนที่สคริปต์ทำ และส่วนที่ต้องทำเอง

รายการทดสอบทั้งหมดอยู่ใน [VERIFY.md](VERIFY.md) (รหัส V-xx) — ไฟล์นี้แบ่งว่า
**อะไรสคริปต์ทำได้ (คนแค่อ่านผล)** และ **อะไรต้องมีคนทำเอง** แล้วบอกวิธีทำทั้งสองแบบ

มีสี่ชั้น:

| ชั้น | อะไร | ใครทำ | ใช้เวลา |
|---|---|---|---|
| 1 | test อัตโนมัติ (`ctest`) — fake rclone ไม่แตะรีโหมดจริง | สคริปต์ · ทุก commit · CI | ~2 นาที |
| 2 | `scripts/verify_live.py` — API ของโปรแกรมที่รันอยู่จริง + รีโหมดจริง | สคริปต์ · คนอ่านผล | ~2 นาที |
| 2b | `tests/ui/check_ui.py` — **หน้าเว็บ** กดใน Chrome จริงเหมือนคน | สคริปต์ · คนอ่านผล | ~1 นาที |
| 3 | สิ่งที่ต้องดูด้วยตา หรือใช้โปรแกรมอื่น | คน | ตามรายการ |

ชั้น 2 ผ่านหมดไม่ได้แปลว่าหน้าเว็บใช้ได้: มันเรียก API ตรงๆ ไม่ได้กดอะไรบนหน้า — บั๊ก
"ล้างคิวแล้วขึ้น no such endpoint" (2026-10-20) ผ่านชั้น 1 และ 2 มาได้ทั้งคู่ ชั้น 2b มีไว้สำหรับสิ่งนั้น

---

## ชั้น 1 — test อัตโนมัติ

```bash
cmake --build build --config Debug
```

```bash
PATH="/c/Users/T14_think/.installer/bin:$PATH" ctest --test-dir build -C Debug --output-on-failure
```

เหมือน CI ของ Linux (Ubuntu · Qt 6.4) บนเครื่องนี้ ผลอยู่ใน `ci-linux-out/`:

```bash
bash scripts/ci_linux_local.sh
```

ทุกบรรทัดใน VERIFY.md ที่ผลเป็น `✅ PASS · AI / test_xxx` มาจากชั้นนี้

### หน้าต่าง: `test_window` (กดปุ่มจริงใน Qt)

ชั้นนี้มี test ของหน้าต่างด้วย — เปิด `MainWindow` จริงกับ fake rclone (ไม่ต้องมีจอ: `QT_QPA_PLATFORM=offscreen`)
แล้วกดปุ่มแบบคน และตอบกล่อง "แน่ใจไหม" ทั้งสองทาง · รันใน CI ทุกครั้ง (Linux Qt 6.4 + Windows)
· โค้ดของหน้าต่างอยู่ใน library `rbgui` ที่ตัวโปรแกรมกับ test ลิงก์ตัวเดียวกัน — ไม่ใช่สำเนา

```bash
QT_QPA_PLATFORM=offscreen build/build/Debug/test_window.exe
```

| กดอะไร | V-xx |
|---|---|
| หน้าต่างเปิด · รายชื่อรีโหมดจาก rclone | V-05 |
| คิว: Clean → No ไม่หาย · Yes ว่างทั้งรายการและในคิว · Remove ทีละอันตอนคิวหยุด | V-42 |
| ลบ task: ถามพร้อมชื่อ · No / Yes | V-38 |
| เปิดรีโหมด → แท็บของมันพร้อมรายการไฟล์ | V-30 |
| New folder (ถามชื่อ · Cancel ไม่ทำอะไร) · Rename · Delete (No / Yes) — ดูจากคำสั่งที่ rclone ได้รับจริง (`FAKE_RCLONE_LOG`) | V-30 · V-31 |
| Size · Tree · Info: ถาม rclone และแสดงผลในกล่อง | V-32 |
| API key: สร้างด้วยชื่อและระดับ · แสดงครั้งเดียว · Take back (No / Yes) | V-71 |
| Webhook: เพิ่มผ่านฟอร์ม · Remove (No / Yes) | V-54 |
| Backup: เป็น task + ตารางเวลาที่ชี้ไปที่ task นั้น | V-56 |
| ฟอร์ม Transfer: copy · exclude · bwlimit · no-traverse → คำสั่ง rclone ตามนั้น | V-38 · V-76 |
| ฟอร์ม Mount: read-only · cache writes · ชื่อ volume · สคริปต์ → คำสั่งและสคริปต์ตามนั้น | V-36 · V-79 |

---

## ชั้น 2 — `scripts/verify_live.py` (สคริปต์ทำ คนอ่านผล)

ใช้แค่ Python มาตรฐาน ไม่ต้องติดตั้งอะไร ยิงไปที่โปรแกรมที่รันอยู่จริงผ่าน API

```bash
python scripts/verify_live.py --container rclonebrowser-dev --remote gdrive
```

ตัวที่รันบน Windows (หน้าต่าง) — token อ่านจาก registry ให้เอง:

```bash
python scripts/verify_live.py --base http://127.0.0.1:19999 --remote gdrive
```

- `--remote` รีโหมดที่ยอมให้สร้างโฟลเดอร์ทดสอบ `zz-rb-verify-<เวลา>` (ลบให้ตอนจบ)
- `--local <โฟลเดอร์>` ที่ดาวน์โหลดลงบนเครื่องที่โปรแกรมรัน — ไม่ใส่: โฟลเดอร์แรกที่ API ใช้ได้
  (Docker: `/media`) แต่ถ้านั่นคือทั้งไดรฟ์ (ตัวบน Windows ใช้ได้ทุกไดรฟ์) ใช้โฟลเดอร์ temp ของเครื่องนี้แทน
- `--skip bandwidth,serve,webhook,backup` ข้ามข้อที่ช้าหรือไม่อยากทำ
- `-v` แสดงทุกคำขอ

**อ่านผลอย่างไร** — ทุกข้อพิมพ์หนึ่งในสี่แบบ พร้อมค่าที่เห็นจริง และเขียนซ้ำลง `verify-report.md`:

| | ความหมาย | ทำอะไรต่อ |
|---|---|---|
| `PASS` | ได้ตามที่ต้องได้ | ใส่ PASS ในบรรทัดของ VERIFY.md |
| `FAIL` | ไม่ได้ — บรรทัดถัดไปบอกว่าเห็นอะไร | ใส่ FAIL + อาการ |
| `LOOK` | ได้คำตอบ แต่ต้องมีคนตัดสิน (เช่น ขนาดโควตาตรงกับเว็บของผู้ให้บริการไหม) | เทียบเอง |
| `SKIP` | ทำไม่ได้ในสภาพนี้ (บอกเหตุผล) | ทำเองในชั้น 3 ถ้าจำเป็น |

**ที่สคริปต์ทิ้งไว้**: โฟลเดอร์ทดสอบบนเครื่องที่โปรแกรมรัน (ใน Docker คือ `/media/zz-rb-verify-<เวลา>`)
— API ไม่มีทางลบโฟลเดอร์ของเครื่อง สคริปต์บอก path ตอนจบ ลบเองด้วย
`docker exec rclonebrowser-dev rm -rf /media/zz-rb-verify-*`

### สิ่งที่สคริปต์ตรวจ → รหัสใน VERIFY.md

| สคริปต์ตรวจ | V-xx |
|---|---|
| `/system` ตอบ · มี serve 4 แบบ | V-25 · V-52 |
| รีโหมดอยู่ · ความสามารถของ backend · โควตา (LOOK) | V-61 · V-68 |
| อัปโหลดจากเครื่องนี้ 3 ไฟล์ (receive) · ไฟล์อยู่บนรีโหมด | V-50 |
| ดูไฟล์ข้อความ (`X-Rb-View: text`) | V-53 |
| Size ของโฟลเดอร์ · คำถามไม่มีแถวในประวัติ | V-32 · V-70 |
| Copy พร้อม filter `- *.nfo` → .nfo ไม่ไป | V-34 · V-76 |
| ค้นประวัติด้วยคำ · ดูหนึ่งรัน · เวลามี offset | V-59 · V-60 |
| key ระดับ control ดูได้ แต่แก้ค่าโปรแกรมไม่ได้ | V-44 · V-73 |
| ดาวน์โหลดลงเครื่องที่โปรแกรมรัน · ไฟล์อยู่จริง | V-37 |
| Check กับโฟลเดอร์ของเครื่องนี้ (LOOK) | V-77 |
| สร้าง backup = task + schedule · รัน backup | V-56 |
| เฝ้าโฟลเดอร์ · ของใหม่ในโฟลเดอร์ → งานเข้าคิวโดย "folder watch" | V-57 · V-62 |
| webhook: ทดสอบส่ง → ตัวรับในสคริปต์ได้ `X-Rb-Event: test` | V-54 |
| serve WebDAV: ไม่ login → 401 · รหัสผ่านไม่โผล่ใน `/jobs` | V-52 |
| จำกัดความเร็วงานที่วิ่งอยู่เป็น 300k · วัดความเร็วจริง · ปลดแล้วจบ | V-51 |

ข้อสังเกตของแต่ละข้อ:

- **serve จาก Docker**: พอร์ตของ serve อยู่ในคอนเทนเนอร์ ไม่ได้เปิดออกมา สคริปต์จะ `SKIP` การเข้าถึง
  แต่ยังตรวจว่ารหัสผ่านไม่รั่ว — การเข้าจากโปรแกรมอื่นอยู่ในชั้น 3
- **webhook จาก Docker**: ส่งไปที่ `host.docker.internal` — ถ้า Windows Firewall ถามให้ยอม Python
- **folder watch**: รอหนึ่งรอบที่โปรแกรมไล่ดูโฟลเดอร์ (`pollSeconds` ใน `/watches`) ไม่เกิน 7 นาที
  ถ้าคิวหยุดอยู่ งานจะค้างในคิว — สคริปต์นับว่าผ่านเมื่อเห็นในคิวแล้ว
- **ความเร็ว**: รีโหมดบางตัวเร็วจนงาน 24 MB จบก่อนจะจำกัดทัน → `SKIP`

---

## ชั้น 2b — `tests/ui/check_ui.py` (หน้าเว็บ กดจริง)

ใช้ Playwright คุม **Chrome ที่มีในเครื่องอยู่แล้ว** (ไม่โหลด browser เพิ่ม) · Playwright เป็น dependency
ของการทดสอบเท่านั้น: อยู่ใน venv ของมันเอง (`tests/ui/.venv`, ~125 MB, ไม่เข้า git) — `tests/` ไม่เข้า
Docker image (`.dockerignore`) และ CMake ไม่แตะไฟล์ Python ตัวโปรแกรมที่ build จริงจึงไม่มีมัน

ติดตั้งครั้งเดียว:

```bash
python -m venv tests/ui/.venv
```

```bash
tests/ui/.venv/Scripts/python -m pip install -r tests/ui/requirements.txt
```

รัน:

```bash
tests/ui/.venv/Scripts/python tests/ui/check_ui.py --container rclonebrowser-dev --remote gdrive
```

- `--base http://127.0.0.1:19999` ตัวบน Windows (token จาก registry — รันจาก PowerShell)
- `--show` เปิดหน้าต่าง Chrome ให้ดูมันกด · `--slow 300` ช้าลง
- `--app-js <ไฟล์>` ใช้ app.js จากไฟล์แทนตัวที่โปรแกรมส่ง — ลองแก้หน้าเว็บได้โดยไม่ต้อง build อะไร

**ข้อไหน FAIL**: เมื่อสิ่งที่มันตรวจเองไม่เป็นตามนั้น **หรือ** มีอย่างใดอย่างหนึ่งต่อไปนี้ระหว่างทาง:
แถบ error (#toast) · error ในกล่องผล · คำขอ `/api/` ได้ 4xx/5xx · JavaScript error / console error —
พร้อมภาพหน้าจอใน `ui-failures/` · ผลทั้งหมดใน `ui-report.md`

| กดอะไร | V-xx |
|---|---|
| เปิดหน้าด้วย token · ทุกแท็บ · ที่อยู่ของแต่ละแท็บ | V-26 · V-66 |
| คิว: Remove · เลื่อนขึ้น · Clear แล้ว Cancel · Clear แล้ว Yes → ว่างทั้งหน้าและใน daemon | V-42 · V-80 |
| รีโหมด: เปิดโฟลเดอร์จากที่อยู่ · New folder · Rename · Size / Tree / Info · Size แล้ว Cancel | V-30 · V-31 · V-32 · V-68 |
| รีโหมด: Delete แล้ว Cancel · Delete · เมนูคลิกขวา · View ไฟล์ข้อความ | V-31 · V-53 · V-64 |
| รีโหมด: ติ๊กสองรายการ · Delete ทั้งสอง (ถามครั้งเดียว) | V-64 |
| API: สร้าง key ระดับ read · Take back (Cancel / Yes) · เพิ่ม webhook · ลบ (Cancel / Yes) | V-44 · V-54 |
| Tasks: New backup → task + ตารางเวลา · Schedules: ปิด/เปิด · ลบ (Cancel / Yes) | V-41 · V-56 |
| Tasks: ลบ task (ถามก่อน) | V-38 |
| จอกว้าง 375 px ทุกแท็บ: หน้าไม่กว้างเกินจอ | V-69 |

ตรวจแล้วว่าจับบั๊กจริงได้: ป้อน app.js ก่อนแก้ (`--app-js`) → **7 ข้อ FAIL** พร้อม
`GET /remotes//[object Object]?path=undefined -> 404` ตรงกับที่ผู้ใช้เห็น · app.js ปัจจุบัน → ผ่าน 41/41

สิ่งที่ยังไม่ได้กด (เพิ่มได้ทีละข้อ) — เว็บ: ฟอร์ม Transfer · Mount · Serve · สร้าง/แก้ตารางเวลาเอง · แก้รีโหมด ·
Copy/Move/Download หลายรายการ · กล่องเลือกโฟลเดอร์ของเครื่อง · หน้าต่าง: Preferences · Serve · Check · Dedupe ·
Copy/Move ข้ามรีโหมด · ตั้งค่ารีโหมด · การ์ดงานและปุ่มหยุด

---

## ชั้น 3 — ทำเอง

สิ่งที่สคริปต์ทำไม่ได้: ต้องดูหน้าตา ต้องใช้อีกโปรแกรม หรือต้องปิดเปิดตัวโปรแกรม
ทำตามบรรทัด ⬜ ใน VERIFY.md ตามรหัสข้างล่าง

### หน้าต่าง (Windows)

| อะไร | V-xx |
|---|---|
| ปุ่มเทาตามความสามารถของ backend · การ์ดงาน · ความคืบหน้า | V-09 · V-11 · V-13 · V-18 |
| completion ของช่อง options ในกล่อง Transfer / Mount | V-14 · V-15 |
| ตั้งค่ารีโหมด · คัดลอกข้ามรีโหมด · ค่าของโปรแกรม (API key ส่วนที่กดได้อยู่ใน `test_window` แล้ว) | V-49 · V-72 · V-73 |
| หน้าตา: ไอคอน · ขนาดตัวอักษร · ธีมมืด — `test_window` ไม่ได้ดู | |
| mount script เขียนในหน้าต่าง · mount ขึ้นเองตอนเปิดโปรแกรม | V-10 · V-75 ข้อ 3 |
| หน้าต่างตามการเปลี่ยนจากหน้าเว็บทัน (task · ตารางเวลา) | V-41 |
| Help → Try now เมื่อพอร์ต API ไม่ว่าง | V-65 |

### หน้าเว็บ (ดูด้วยตา)

| อะไร | V-xx |
|---|---|
| ฟอร์ม Transfer · More ของ Copy/Move/Mount | V-38 · V-39 · V-75 ข้อ 2 · V-76 ข้อ 2–3 |
| เลือกหลายรายการ · คลิกขวา · Size หลายรายการ · ดาวน์โหลดหลายรายการ | V-64 · V-77 ข้อ 3 · V-78 |
| กล่องถาม · ที่อยู่ของแต่ละหน้า (ปุ่ม back) · รายละเอียดงาน | V-63 · V-66 · V-67 |
| สคริปต์หลัง mount: ผลขึ้นในรายละเอียดงาน | V-79 |
| จอมือถือ (เปิดจากโทรศัพท์ หรือ DevTools 375 px) | V-69 |
| รีโหมดที่ sign in ผ่าน browser | V-48 |
| การแจ้งเตือนของ browser | V-54 |

### กับโปรแกรมอื่น

| อะไร | ใช้อะไร | V-xx |
|---|---|---|
| เปิด stream ในโปรแกรมเล่น | VLC / mpv | V-40 |
| serve WebDAV / FTP / SFTP / HTTP | Explorer (Map network drive) · FileZilla · browser | V-52 |
| webhook ไปโทรศัพท์ | ntfy | V-54 |
| mount เป็นไดรฟ์ | Explorer | V-36 · V-74 ข้อ 2 |

### ปิดเปิดโปรแกรม / Docker

| อะไร | V-xx |
|---|---|
| restart container → mount autostart ขึ้นเอง | V-75 ข้อ 4 |
| `docker stop` หยุดอย่างสุภาพ · สามโหมด · image web-only | V-27 · V-28 |
| `--run-task` ไม่เปิดหน้าต่าง | V-17 |
| ความคืบหน้าและเหตุการณ์ยังมาหลังหน้าเว็บหลุดแล้วกลับมา | V-65 |

---

## ลำดับที่แนะนำสำหรับรอบทดสอบ

1. ชั้น 1 ผ่านทั้งหมด (ทุก commit ทำอยู่แล้ว)
2. build container ใหม่ให้ตรงกับ commit ที่จะทดสอบ:
   `docker compose -f docker/compose.dev.yaml up -d --build`
3. `verify_live.py` กับ container → ไล่ `FAIL` / `LOOK` ก่อน
4. `check_ui.py` กับ container
5. ทั้งสองกับตัวบน Windows (ถ้าทดสอบหน้าต่างด้วย — build Release ใหม่ก่อน)
6. ชั้น 3 ตามตาราง — ใส่ผลใน VERIFY.md ตามบรรทัด
