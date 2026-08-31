# HTTP API — สถานะและวิธีใช้

> เขียน 2026-08-31 · แผนเต็มอยู่ที่ [`API.md` §11 และ §13](API.md)
> **เฟส 1: อ่านได้อย่างเดียว** — คำสั่ง (POST/DELETE) ยังไม่มี

## เปิดใช้

```bash
RcloneBrowser.exe --daemon
```

```
rclone-browser running without a window. Ctrl+C to stop.
API on http://127.0.0.1:19999/api/v1/
token: 0be7e386-a839-426d-a7d7-c6370d15983c
```

**token สร้างครั้งแรกที่ใช้แล้วเก็บใน settings** ไม่ได้ปล่อยว่าง — เพราะ "ยังไม่ได้ตั้ง token"
ต้องไม่กลายเป็น "ไม่ต้องใช้รหัสผ่าน" ซึ่งเป็นวิธีที่ API เปิดโล่งบนเครื่องที่คนอื่นเข้าถึงได้

ตั้งค่าได้สองคีย์: `Settings/apiAddress` (ค่าเริ่มต้น `127.0.0.1`) และ `Settings/apiPort`
(ค่าเริ่มต้น `19999`)

## endpoint ที่มีแล้ว

| | |
|---|---|
| `GET /api/v1/ping` | **ข้อเดียวที่ไม่ต้องใช้ token** |
| `GET /api/v1/system` | เวอร์ชัน rclone · จำนวนงาน · คิวเดินอยู่ไหม |
| `GET /api/v1/remotes` | ชื่อ + ชนิด |
| `GET /api/v1/tasks` · `/tasks/{id}` | task ที่บันทึกไว้ |
| `GET /api/v1/queue` | ลำดับคิว + ตัวที่กำลังรัน |
| `GET /api/v1/jobs` | งานที่วิ่งอยู่ + stats |
| `GET /api/v1/schedules` | ตารางเวลา + ครั้งถัดไป |
| `GET /api/v1/history?limit=` | ประวัติการรัน |

```bash
curl -H "Authorization: Bearer $TOKEN" http://127.0.0.1:19999/api/v1/system
```

## ทำไม `/ping` ถึงไม่ต้องใช้ token

เพื่อให้ client แยกได้ว่า **"เซิร์ฟเวอร์อยู่แต่ token ผิด"** ต่างจาก **"ไม่มีเซิร์ฟเวอร์"**
ซึ่งเป็นคนละปัญหาและแก้คนละแบบ — มันตอบแค่ `{"ok":true}` ไม่บอกอย่างอื่นเลย

## ที่ตัดสินใจไว้ และเหตุผล

**เขียน HTTP เอง ไม่ใช้ framework** — Qt ที่ติดตั้งอยู่ไม่มีโมดูล `HttpServer`
และการลาก web framework เข้ามาเพื่อ API ขนาดนี้คือ dependency ที่คนแพ็กเกจทุกคนต้องหามาให้ได้
รวมถึง Alpine image ที่ daemon จะไปอยู่ · `QTcpServer` + `http_message.cpp` พอแล้ว

**แยกการอ่าน HTTP ออกจากเซิร์ฟเวอร์** — `http_message.cpp` ไม่แตะ socket เลย
ส่วนที่ยุ่งที่สุด (header จบตรงไหน · อะไรคือ request ที่ครบ · อะไรคือขยะ) จึงทดสอบได้
โดยไม่ต้องเปิด socket · 13 เคส

**ตรวจ token ก่อนดู path** — ไม่ได้ตรวจทีละ endpoint เพราะ endpoint ที่ตอบโดยไม่ต้องมี
token คือ endpoint ที่คนเขียนลืม และวิธีที่ไม่ลืมคือไม่ให้มันเป็นเรื่องของแต่ละ endpoint

**คำสั่งที่ส่งออกไปถูก redact** — `RunningJob::displayCommand()` ไม่ใช่ `mArgs` ดิบ
เพราะ args มี token ของ backend และรหัสผ่าน remote control

**อ่านก่อน เขียนทีหลัง** — method ที่ไม่ใช่ GET ตอบ `405` พร้อมบอกว่ายังไม่รองรับ
ดีกว่าแกล้งทำเป็นว่า POST ทำอะไรสำเร็จ

## ยังไม่มี

- **คำสั่ง** — run · enqueue · stop · start/pause คิว · สร้าง/แก้ task
- **SSE `/events`** — ตอนนี้ต้อง poll `/jobs` เอา · `RcClient` poll `core/stats`
  อยู่แล้ววินาทีละครั้ง ทางที่ถูกคือส่งต่อผลนั้นออกไป ไม่ใช่ให้ client แต่ละตัว poll เอง
- **`/jobs/{id}/log`**
- **Web UI (S12)**
- **วิธีสั่งให้ daemon หยุดอย่างสุภาพ** — ตอนนี้ Ctrl+C อย่างเดียว
