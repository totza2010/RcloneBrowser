# HTTP API — สถานะและวิธีใช้

> เขียน 2026-08-31 · แผนเต็มอยู่ที่ [`API.md` §11 และ §13](API.md)
> **อ่าน · สั่ง · ตามเหตุการณ์สด** — สิ่งที่ยังไม่มีอยู่ท้ายหน้า

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
| `GET /api/v1/remotes/{name}/ls?path=` | **ไฟล์ในโฟลเดอร์** — คำตอบมาช้าเพราะต้องรัน rclone |
| `GET /api/v1/auth` | **ไม่ต้องใช้ token** — บอกแค่ว่าตั้งรหัสผ่านไว้หรือยัง |
| `POST /api/v1/login` | **ไม่ต้องใช้ token** — `{user,password}` → `{session}` |
| `POST /api/v1/logout` · `POST /api/v1/password` | ออกจากระบบ · ตั้ง/เปลี่ยนรหัสผ่าน |
| `POST /api/v1/shutdown` | **daemon เท่านั้น** — หยุดงาน เก็บคิวไว้ แล้วออก · หน้าต่างตอบ `409` · ดู [`DOCKER-MODES.md`](DOCKER-MODES.md) |

```bash
curl -H "Authorization: Bearer $TOKEN" http://127.0.0.1:19999/api/v1/system
```

## รหัสผ่านแบบ admin

token เหมาะกับสคริปต์ **แต่ไม่เหมาะกับคน** — จำไม่ได้ เลือกเองไม่ได้ และเปลี่ยนไม่ได้
โดยไม่ต้องรีสตาร์ท daemon แล้วไปบอกทุก client ใหม่

```bash
curl -X POST -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  -d '{"current":"","new":"a good long one"}' $API/password
```

จากนั้น `POST /login` ด้วย `{user,password}` จะได้ **session** ที่ใช้แทน token ได้ทุกที่
· **token ยังใช้ได้เหมือนเดิม** สำหรับ curl และงานที่ไม่มีคนนั่งเฝ้า

- เก็บเป็น **PBKDF2-SHA256 120,000 รอบ พร้อม salt** ไม่ใช่รหัสผ่านตรงๆ
- **เทียบแบบไม่หยุดที่ไบต์แรกที่ต่าง** — การหยุดก่อนบอกคนเดาว่าเดาถูกไปกี่ตัว
- **เดาผิด 5 ครั้งติดกันแล้วล็อก** 1 นาที เพิ่มเป็นเท่าตัว สูงสุด 15 นาที
- **session อยู่ในหน่วยความจำเท่านั้น** — daemon รีสตาร์ทแล้วทุกคนหลุด
  ซึ่งเป็นสิ่งที่ต้องการ: session ที่อยู่ได้นานกว่า process คือ session ที่เพิกถอนไม่ได้
- **เปลี่ยนรหัสผ่าน = เตะทุก session ออก** ไม่งั้นการเปลี่ยนก็ไม่ได้กันใครเลย

## ทำไม `/ping` ถึงไม่ต้องใช้ token

เพื่อให้ client แยกได้ว่า **"เซิร์ฟเวอร์อยู่แต่ token ผิด"** ต่างจาก **"ไม่มีเซิร์ฟเวอร์"**
ซึ่งเป็นคนละปัญหาและแก้คนละแบบ — มันตอบแค่ `{"ok":true}` ไม่บอกอย่างอื่นเลย

## คำสั่ง

| | |
|---|---|
| `POST /api/v1/tasks/{id}/run` | เริ่มเดี๋ยวนี้ → `202` + `requestId` |
| `POST /api/v1/tasks/{id}/enqueue` | ต่อท้ายคิว → `202` + `requestId` |
| `POST /api/v1/schedules/{id}/run` | สั่งตารางเวลาให้ทำเดี๋ยวนี้ |
| `POST /api/v1/jobs/{requestId}/stop` | หยุดงาน → `202` (ขอให้หยุด ไม่ใช่หยุดแล้ว) |
| `POST /api/v1/queue/start` · `/pause` | เดิน/หยุดคิว |
| `DELETE /api/v1/queue/{requestId}` | เอาออกจากคิว → `204` |

```bash
curl -X POST -H "Authorization: Bearer $TOKEN" \
     "http://127.0.0.1:19999/api/v1/tasks/{id}/enqueue?dryRun=1"
```

`dryRun` รับทั้งใน query และใน body (`{"dryRun":true}`) — **อย่างที่คนพิมพ์เองจะใช้
กับอย่างที่โปรแกรมจะส่ง** สิ่งที่ถามได้ทางเดียวคือสิ่งที่ครึ่งหนึ่งของคนเรียกผิด

### คำสั่งที่ถูกปฏิเสธ ไม่ใช่ที่ยังทำไม่เสร็จ

| | |
|---|---|
| `409` งานนั้นกำลังรันอยู่ | สอง rclone เขียนปลายทางเดียวกัน — **กฎเดียวกับที่คิวใช้** |
| `409` เอา entry ที่กำลังรันออกจากคิว | จะเหลือ rclone ที่ไม่มีอะไรถือไว้ — ให้ stop แทน |
| `409` ตารางเวลาที่รอบก่อนยังไม่จบ | เหตุผลมาจาก `AppCore::runNow()` ตรงๆ ไม่ได้ยุบเหลือว่า "รันไม่ได้" |
| `405` สร้าง/แก้/ลบ task | **task มี rclone flags ของมันเอง — เขียน task คือเขียน command line** |

ข้อสุดท้ายคือเส้นที่ตัดไว้ตั้งแต่ต้น: token ที่หลุดออกไปต้องไม่กลายเป็น "รันโปรแกรมอะไรก็ได้
บนเครื่องนี้" การสร้าง task จึงยังอยู่กับหน้าต่าง จนกว่าจะมีวิธีบอก task ที่ไม่ใช่ command line

## ตามเหตุการณ์สด — `GET /api/v1/events`

```bash
curl -N -H "Authorization: Bearer $TOKEN" http://127.0.0.1:19999/api/v1/events
```

```
event: hello
data: {"queueRunning":true,"queued":0,"runningJobs":0}

event: job
data: {"requestId":"{9f2…}","running":true,"stats":{"bytes":1048576,…}}

event: queue
data: {"queued":2,"running":true,"runningRequestId":"{9f2…}"}

: still here
```

**ทำไมต้องมี ในเมื่อ poll ก็ได้** — dashboard ที่ poll `/jobs` ทุกวินาที
จะรู้ข่าวช้ากว่าความจริงหนึ่งวินาทีเสมอ และ**จ่ายค่านั้นทั้งคืนที่ไม่มีอะไรเกิดขึ้นเลย**
เลขจากงาน (`event: job`) มาจาก `core/stats` ของ rclone ที่ `RcClient` อ่านอยู่แล้ว
วินาทีละครั้ง — **ส่งต่อของที่มีอยู่แล้ว ไม่ใช่ให้ client แต่ละตัวไปถามเอง**

บรรทัด `: still here` ทุก 20 วินาทีเป็น comment ที่ฝั่ง client มองข้าม — มีไว้เพื่อให้
สายที่ตายไปเงียบๆ (โน้ตสลีป · proxy ที่มีความเห็นของตัวเองเรื่อง idle) **ถูกจับได้ ไม่ใช่ถูกเชื่อ**

## log ของงาน — `GET /api/v1/jobs/{requestId}/log?tail=200`

ตอบเป็น `text/plain` **ไม่ใช่ JSON** — log ที่ห่อใน JSON string คือ log ที่ขึ้นต้นบรรทัด
เป็น `\n` ทุกตัว อ่านใน browser ไม่ได้ และ client ทุกตัวต้องแกะออกเอง

หาจากงานที่กำลังรันก่อน ไม่เจอจึงดูจากประวัติ — **log ที่อ่านได้เฉพาะตอนงานยังวิ่งอยู่
ไม่ตอบคำถามที่คนถามจริงๆ คือ "เมื่อคืนเกิดอะไรขึ้น"** อ่านท้ายสุดไม่เกิน 5000 บรรทัด
และอ่านจากท้ายไฟล์ 1 MB สุดท้าย — log ของ transfer ใหญ่ๆ มีขนาดเป็นเมกะไบต์

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

- **สร้าง/แก้ task ผ่าน API** — ตั้งใจไม่ทำ ดูเหตุผลข้างบน
- **Web UI (S12)**
  ใน Docker ที่ถูกคือรับ SIGTERM ไม่ใช่ endpoint
