# สภาพแวดล้อมพัฒนา RcloneBrowser

ตรวจสอบและติดตั้งเมื่อ 2026-08-03 · ปรับปรุง 2026-08-06 · Windows 11 Pro 25H2

> เอกสารคู่กัน: [`PLAN.md`](PLAN.md) · [`ARCHITECTURE.md`](ARCHITECTURE.md) · [`VERIFY.md`](VERIFY.md)

## สถานะเครื่อง

| ส่วนประกอบ | สถานะ | รายละเอียด |
|---|---|---|
| Visual Studio 2022 Community | ✅ มีอยู่แล้ว | 17.14.36915, MSVC toolset **14.44.35207**, C++ workload ครบ |
| Windows SDK | ✅ | 10.0.26100.0 |
| CMake | ✅ | 4.2.3 (`C:\Program Files\CMake\bin`) |
| **Qt 6.9.3 msvc2022_64** | ✅ **ติดตั้งใหม่** | `C:\Qt\6.9.3\msvc2022_64` + `qtmultimedia` (2.5 GB) |
| Python | ✅ | 3.14.6 |
| rclone | ✅ | **v1.72.1 พร้อม backend `teldrive`** — `C:\Users\T14_think\.installer\bin\rclone.exe` |
| | | ⚠️ ตัวนี้เป็น build ของ **tgdrive** ทั้งที่รายงานตัวเองว่า `github.com/rclone/rclone` — ดู V-16 |
| WinFsp | ✅ มีอยู่แล้ว | 2.0.23075 — ทดสอบ mount ได้ |
| Inno Setup 6 | ✅ | ใช้ตอน package installer |
| 7-Zip | ✅ | `C:\ProgramData\chocolatey\bin\7z.exe` |
| remotes สำหรับทดสอบ | ✅ | `tgdrive_main_01/02/03`, `tgdrive_adult_01` (teldrive) + `gdrive`, `OneDrive` |
| git remotes | ✅ | `origin`, `ng`, `alkl58`, `ubikore` — fetch ครบ |

**ยืนยันแล้วว่า build ผ่านและ deploy ได้:** `build\build\Release\RcloneBrowser.exe` (3.9 MB, รวม Qt runtime 52.9 MB)

---

## วิธีติดตั้ง Qt (ถ้าต้องทำซ้ำบนเครื่องอื่น)

ใช้ `aqtinstall` — ไม่ต้องสมัคร Qt account และได้ path ตรงกับที่ CI ใช้เป๊ะ

```bash
python -m pip install aqtinstall
python -m aqt install-qt windows desktop 6.9.3 win64_msvc2022_64 -m qtmultimedia -O C:\Qt
```

> **ทำไมต้อง 6.9.3 เป๊ะ:** ถูก hardcode ไว้ 2 ที่ — [`.github/workflows/build.yml`](../.github/workflows/build.yml) (`version: '6.9.3'`) และ [`scripts/release_windows.cmd:6`](../scripts/release_windows.cmd) (`set QT=C:\Qt\6.9.3\msvc2022_64`) ถ้าใช้เวอร์ชันอื่น local จะไม่ตรงกับ CI

---

## รอบการพัฒนาประจำวัน

### Configure (ทำครั้งเดียวต่อ build dir)
```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.9.3/msvc2022_64"
```

### Build
```bash
cmake --build build --config Release --parallel
```
> Debug build ใช้ `--config Debug` (ต้องรัน `windeployqt --debug` ตามด้วย)

### รันแอป
```bash
.\build\build\Release\RcloneBrowser.exe
```

### รัน test และตรวจขอบเขตชั้น (ทำก่อน commit ทุกครั้ง)
```bash
ctest --test-dir build -C Release --output-on-failure
```
```bash
python scripts/check_layers.py --check
```
> ทั้งสองอย่างนี้ CI รันให้อยู่แล้ว แต่รันเองก่อนเร็วกว่ารอ CI สามนาที
> `python scripts/check_layers.py --markers` แสดง `CORE:` / `LAYER:` / `SECURITY:` / `TEST:` ทั้งหมด

### Deploy Qt DLLs (ทำครั้งเดียว หรือหลังอัป Qt)
```bash
C:\Qt\6.9.3\msvc2022_64\bin\windeployqt.exe --release --no-translations --no-opengl-sw .\build\build\Release\RcloneBrowser.exe
```

### Build + package แบบ CI (สร้าง portable zip + installer)
```bash
.\scripts\release_windows.cmd 3.1.1-dev
```
⚠️ สคริปต์นี้ `rd /s /q build` ทุกครั้ง = build ใหม่หมด ใช้ตอนจะออก release เท่านั้น ไม่ใช่ตอน dev

---

## เปิดใน IDE

**Visual Studio** — เปิด solution ที่ CMake generate ไว้:
```
build\rclone-browser.sln
```
ตั้ง `RcloneBrowser` เป็น Startup Project แล้ว F5 ได้เลย (debugger ทำงานเต็มรูปแบบ)

**VS Code** — ติดตั้ง `ms-vscode.cmake-tools` แล้วสร้าง `.vscode/settings.json`:
```json
{
  "cmake.configureSettings": { "CMAKE_PREFIX_PATH": "C:/Qt/6.9.3/msvc2022_64" },
  "cmake.generator": "Visual Studio 17 2022"
}
```

**Qt Designer** (แก้ไฟล์ `.ui`) — `aqt` ไม่ลง Qt Creator ให้ แต่ Designer มาด้วย:
```
C:\Qt\6.9.3\msvc2022_64\bin\designer.exe
```

---

## ดูโค้ดของ fork อื่นโดยไม่ต้อง checkout

```bash
git show ng/master:src/rclone_capabilities.cpp
git show ng/master:src/schedule_manager.cpp
git show ubikore/master:src/utils.cpp
git diff HEAD alkl58/master -- CMakeLists.txt
git log --oneline HEAD..alkl58/master
```

HEAD ของแต่ละ remote ตอน fetch (ภาพนิ่งวัน setup — ไม่ได้ตามอัปเดต):
```
origin    67af990  Remove Win32 configuration from AppVeyor...
ng        f08a1a0  docs: add AppStream release history
alkl58    8f11309  (AI) Fix parsing speed of rclone - fixes #10
ubikore   a9ed132  Support project-local hosts for rclone HTTPS requests
```

---

## ข้อควรระวังที่พบตอน setup

1. **`build/` เดิมมี cache เสีย** — `CMakeCache.txt` เก่ามี `Qt6_DIR:PATH=Qt6_DIR-NOTFOUND` ค้างจากการ configure ที่ล้มเหลว ลบทิ้งแล้ว ถ้าเจออีกให้ `Remove-Item -Recurse -Force build` (`build*` อยู่ใน `.gitignore` อยู่แล้ว ปลอดภัย)
2. **`cl.exe` ไม่อยู่บน PATH** เป็นเรื่องปกติ — CMake หาเจอเองผ่าน generator "Visual Studio 17 2022" ไม่ต้องเปิด Developer Command Prompt
3. **`Could NOT find WrapVulkanHeaders`** ตอน configure — เป็นแค่ warning ของ Qt ไม่กระทบ build
4. ~~**ยังไม่มี test framework**~~ — ✅ มีแล้ว `tests/` มี QTest 8 ชุดที่ลิงก์แค่ `rbcore`
   ถ้า test ไหนต้องใช้ `Qt6::Widgets` ถึงจะลิงก์ผ่าน แปลว่ามีของที่ควรอยู่ใน GUI หลุดเข้า core
5. ~~**workflow trigger เป็น `tags: v*` เท่านั้น**~~ — ✅ มี `.github/workflows/ci.yml` (4 job)
   และ `docker.yml` (2 flavour) แล้ว · `build.yml` ยังสงวนไว้ให้ tag `v*` อย่างเดียว

---

## ทดสอบกับ teldrive

รันคำสั่งนี้เก็บ baseline ไว้ก่อนแก้โค้ด (ใช้เป็น fixture ของ capability registry):
```bash
rclone backend features tgdrive_main_01: > tests/fixtures/teldrive_features.json
rclone lsjson tgdrive_main_01: > tests/fixtures/teldrive_lsjson.json
rclone lsjson --stat tgdrive_main_01:some/file > tests/fixtures/teldrive_stat.json
```

**ข้อจำกัดของ teldrive ที่ต้องจำ** (จาก `backend features` จริง):
- `Hashes: []` → `rclone check` ต้องใช้ `--size-only`, `cryptcheck` ใช้ไม่ได้
  ⚠️ **ไม่ได้แปลว่า teldrive ไม่มี hash** — มันคำนวณ blake3 (`--teldrive-hash-enabled`
  ค่าเริ่มต้น true) และเก็บไว้ในฟิลด์ `hash` ของ API แต่ไม่ได้ประกาศให้ rclone
  แปลว่า `rclone check` เทียบได้แค่ขนาด และจับไฟล์ที่ขนาดถูกแต่เนื้อไม่ครบไม่ได้ (ดู `PLAN.md` §6.9)
- `DuplicateFiles: false` → dedupe ใช้ไม่ได้
- `ListR: false` → listing แบบ recursive ช้ามาก
- `Precision: 1s` → sync ข้าม backend ต้องใช้ `--modify-window 1s`
