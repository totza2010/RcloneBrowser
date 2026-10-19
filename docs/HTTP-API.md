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

> **รายการที่ถูกต้องที่สุดคือ `GET /api/v1/openapi.json`** (หรือแท็บ API บนหน้าเว็บ) — สร้างจากรายการเดียวกับที่
> server ใช้ตรวจทุกคำขอ ตารางในไฟล์นี้เขียนด้วยมือและอาจตามไม่ทัน


| | |
|---|---|
| `GET /api/v1/ping` | **ข้อเดียวที่ไม่ต้องใช้ token** |
| `GET /api/v1/system` | เวอร์ชัน rclone · จำนวนงาน · คิวเดินอยู่ไหม |
| `GET /api/v1/remotes` | ชื่อ + ชนิด |
| `GET /api/v1/tasks` · `/tasks/{id}` | task ที่บันทึกไว้ — เต็มทุกช่อง เป็นคำ (`operation: "sync"`) ดู "task" ข้างล่าง |
| `GET /api/v1/tasks/defaults?type=upload\|download` | ค่าที่ task ใหม่เริ่มต้น — เท่ากับกล่องของหน้าต่าง |
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

## API key สำหรับระบบอื่น

ให้ระบบอื่นใช้ key ของมันเองแทน token: ทำได้เท่าระดับของมัน และเพิกถอนได้โดยไม่กระทบใคร

| ระดับ | ทำอะไรได้ |
|---|---|
| `read` | ดูทุกอย่าง · events · log |
| `control` | + รัน · เข้าคิว · หยุด · เปิดปิดคิวและตารางเวลา · stream · mount |
| `write` | + สร้าง/แก้/ลบ task และตารางเวลา · เปลี่ยนไฟล์บนรีโหมด |
| `admin` | ทุกอย่าง — token และ session ของ admin คือระดับนี้ |

```bash
curl -X POST -H "Authorization: Bearer $TOKEN" -d '{"name":"home assistant","scope":"control"}'      http://127.0.0.1:19999/api/v1/keys
```

ใช้แบบเดียวกับ token: `Authorization: Bearer rbk_…` · ต่ำกว่าระดับที่ต้องการ → `403` `insufficient_scope` พร้อม `needs`
ระดับที่แต่ละเส้นทางต้องการอยู่ใน openapi.json (`x-scope`) · key แสดงครั้งเดียวตอนสร้าง เก็บไว้เป็น hash

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

### ตารางเวลา — สร้าง แก้ ลบ เปิดปิด

| | |
|---|---|
| `POST /api/v1/schedules` | สร้าง → `201` · ต้องมี `taskId` ของ task ที่มีอยู่ (ไม่ใช่ mount) |
| `PUT /api/v1/schedules/{id}` | แก้ — ส่งเฉพาะช่องที่เปลี่ยน |
| `DELETE /api/v1/schedules/{id}` | ลบ → `200` · รอบที่เริ่มไปแล้วทำงานต่อ |
| `POST /api/v1/schedules/{id}/enable` · `/disable` | สวิตช์ของตารางเวลานั้น |
| `POST /api/v1/scheduler/start` · `/pause` | สวิตช์ของทั้งระบบ — หยุดแล้วเก็บตารางเวลาไว้แต่ไม่เริ่มอะไร |

```json
{ "taskId": "{…}", "name": "nightly", "mode": "daily",
  "days": ["mon","tue","wed","thu","fri"], "time": "06:30",
  "runAs": "queue", "active": true }
```

- `mode`: `daily` (ใช้ `days` + `time`) หรือ `cron` (ใช้ `cron` ห้าช่อง เช่น `30 6,18 * * MON-FRI`)
- `runAs`: `now` เริ่มทันที · `queue` ต่อท้ายคิว
- เวลาเป็นนาฬิกาของเครื่องที่โปรแกรมรัน · คำตอบมี `nextRun`
- `400` `bad_field` เมื่อเป็นสิ่งที่ไม่มีวันถึงกำหนด (ไม่มีวัน · เวลาไม่มีจริง · cron อ่านไม่ออก) · `400` `no_such_task`
- ลบ task ที่มีตารางเวลา → `409` `scheduled`

### stream — `POST /api/v1/remotes/{name}/stream`

`{"path": "films/a.mkv"}` หรือ `{"path": "films", "isDir": true}` → `202` `{requestId, path, isDir}`
ที่อยู่ที่โปรแกรมเล่นเปิดคือ **ที่อยู่ของ API นี้เอง** ต่อด้วย `path` (`/stream/<สุ่ม>/<ชื่อ>`) — ไม่มีพอร์ตเพิ่ม
`GET`/`HEAD` ที่ path นั้น**ไม่ต้องมี token**: ส่วนสุ่มคือสิ่งเดียวที่กันมันไว้ ใครมีที่อยู่ก็เล่นได้จนกว่าจะหยุด
รองรับ `Range` · หยุดด้วย `POST /jobs/{requestId}/stop` แล้ว path เดิมได้ `404` · `502` คือ rclone ยังไม่ขึ้น (ลองใหม่ในอึดใจ)
งานที่เป็น stream มี `stream: {path}` ใน `GET /jobs`

### cat — `GET /api/v1/remotes/{name}/cat?path=…&limit=…`

ไบต์ของไฟล์ (หรือส่วนต้น) สำหรับดูในหน้าเว็บ — **`Content-Type: application/octet-stream` เสมอ** browser จึงไม่แสดงเอง
- `X-Rb-View`: `image` · `pdf` · `text` · `media` · ว่าง (แสดงไม่ได้) · `X-Rb-Type`: ชนิดที่ใช้แสดง · `X-Rb-Truncated`: `true` เมื่อไฟล์ยาวกว่าที่อ่าน
- **HTML เป็น text เสมอ** — แสดงเป็นหน้าในหน้าของโปรแกรม มันจะกลายเป็นหน้าของโปรแกรม (อ่าน token ได้) · SVG เป็น image ใช้ใน `<img>` เท่านั้น
- `limit` ไม่บอก: ภาพ/PDF อ่านทั้งไฟล์ · อื่นๆ 2 MiB · สูงสุด 64 MiB · rclone หยุดอ่านตรงนั้น (`cat --count`)
- `400` ไม่ได้ระบุไฟล์ · `404` ไม่มีรีโหมด · `502` rclone อ่านไม่ได้ · ไม่มีประวัติ (เป็นคำถาม ไม่ใช่งาน)

### webhooks — `GET/POST /api/v1/webhooks` · `PUT/DELETE /webhooks/{id}` · `POST /webhooks/{id}/test` (ระดับ admin ทั้งหมด)

ที่อยู่ที่ถูก POST ไปหาเมื่องานจบ ไม่ว่าใครสั่ง (หน้าต่าง · หน้าเว็บ · คิว · ตารางเวลา)
`{"name","url","format":"json|text|slack|discord","failuresOnly":false,"everyJob":false,"secret":""}`
- `json`: `{"event":"job.finished|job.failed|job.stopped|test","at","summary","job":{…เหมือน /jobs…}}` · `text`: บรรทัด `summary` อย่างเดียว (ntfy, Gotify) ·
  `slack` `{"text"}` · `discord` `{"content"}`
- header: `X-Rb-Event` · มี secret → `X-Rb-Signature: sha256=<HMAC-SHA256 ของ body>` · timeout 15 วินาที · ไม่ลองซ้ำ
- `failuresOnly`: เฉพาะ job.failed · `everyJob`: ไม่ใช่เฉพาะ transfer · GET ไม่คืน secret (`hasSecret`) · `lastResult`/`lastAt` ของการส่งล่าสุด (ไม่เก็บข้ามการเปิดโปรแกรม)
- PUT: secret ว่าง = คงของเดิม · `clearSecret:true` = ลบ · test ตอบเมื่อปลายทางตอบ: `200 {status}` · `502 {status, error}`

### serve — `POST /api/v1/remotes/{name}/serve` (ระดับ admin)

`{"path":"films","protocol":"webdav","port":8081,"everyone":false,"readOnly":true,"user":"rb","password":"…"}` →
`202` `{requestId, protocol, port, everyone, readOnly}`
- `protocol`: `webdav` · `http` · `ftp` · `sftp` — รายชื่อพร้อมพอร์ตปกติอยู่ใน `GET /system` → `serveProtocols`
- **พอร์ตของตัวเอง** (ไม่ผ่านพอร์ตของ API แบบ stream — FTP/SFTP ไม่ใช่ HTTP) · `everyone:false` = ฟังที่ 127.0.0.1 เท่านั้น
- `everyone:true` **ต้องมี user และ password** · รหัสผ่านส่งให้ rclone ทาง environment (`RCLONE_PASS`) — ไม่อยู่ในคำสั่ง · log · ประวัติ · `/jobs`
- `readOnly` เป็น true ถ้าไม่บอก · `400` เมื่อ protocol ไม่รู้จัก · พอร์ตถูกใช้ (รวมถึงโดย serve อื่นที่รันอยู่) · มี user ไม่มีรหัส · ไม่มีรหัสแต่ everyone
- งานที่เป็น serve มี `serve: {protocol, port, everyone, readOnly, user}` ใน `GET /jobs` · หยุดด้วย `POST /jobs/{requestId}/stop`

### คำสั่งที่ถูกปฏิเสธ ไม่ใช่ที่ยังทำไม่เสร็จ

| | |
|---|---|
| `409` งานนั้นกำลังรันอยู่ | สอง rclone เขียนปลายทางเดียวกัน — **กฎเดียวกับที่คิวใช้** |
| `409` เอา entry ที่กำลังรันออกจากคิว | จะเหลือ rclone ที่ไม่มีอะไรถือไว้ — ให้ stop แทน |
| `409` ตารางเวลาที่รอบก่อนยังไม่จบ | เหตุผลมาจาก `AppCore::runNow()` ตรงๆ ไม่ได้ยุบเหลือว่า "รันไม่ได้" |
| `409` ลบ task ที่กำลังรัน | ให้ stop ก่อน |
| `403` `outside_roots` | ปลายของ task ที่อยู่บนเครื่องอยู่นอก `RB_LOCAL_ROOTS` |

### task — สร้าง แก้ ลบ

| | |
|---|---|
| `POST /api/v1/tasks` | สร้าง → `201` + task เต็ม · ไม่ใส่ `name` ได้ชื่อ `_tmp_…` เหมือนหน้าต่าง |
| `PUT /api/v1/tasks/{id}` | แก้ — **ส่งเฉพาะช่องที่เปลี่ยน** ช่องที่ไม่เอ่ยถึงคงเดิม |
| `DELETE /api/v1/tasks/{id}` | ลบ → `200` `{"deleted": id}` |
| `POST /api/v1/tasks/preview` | คำสั่งที่ task แบบนี้จะรัน (`{"command": …}`) ไม่บันทึกอะไร · `dryRun: true` ได้ |

```json
{ "type": "upload", "name": "nightly", "source": "/media/films", "dest": "gdrive:films",
  "operation": "sync", "syncTiming": "after", "compare": "checksum",
  "transfers": 4, "bandwidth": "2M", "excluded": "*.tmp\n*.part", "extra": "--fast-list" }
```

- `type`: `upload` · `download` — `operation`: `copy` · `move` · `sync` — `syncTiming`: `during` · `after` · `before`
- `compare`: `none` · `sizeAndModTime` · `checksum` · `ignoreSize` · `sizeOnly` · `checksumIgnoreSize`
- ช่องข้อความรับตัวเลขได้ (`"transfers": 4` กับ `"4"` คือสิ่งเดียวกัน) · `maxDepth` เป็นตัวเลข 0 = ไม่จำกัด
- `400` `bad_field` บอกชื่อช่องที่ผิด · `400` `incomplete` ใช้ประโยคเดียวกับกล่องของหน้าต่าง
- คำขอที่ผิด**ไม่เปลี่ยนอะไรเลย** — ไม่มี task ที่ถูกแก้ไปครึ่งเดียว
- task ชนิด mount: อ่านได้ ลบได้ **แก้ผ่าน API ไม่ได้** (`mountScript` คือการรันโปรแกรม)

**`extra` เขียนได้ ภายใต้สองกฎ** (`src/extra_flags.*`) — task มี flag ของ rclone ของมันเอง
การเขียน task จึงเป็นการเขียน command line:

1. flag ต้องเป็นตัวที่ rclone บนเครื่องนี้มี (`GET /api/v1/rclone/flags`) — ไม่ใช่ → `400` `bad_flag` บอกชื่อ
2. flag ที่รันโปรแกรมหรือย้ายไฟล์ของ rclone เองถูกปฏิเสธเสมอ: `--config` · `--password-command` · `--rc*` ·
   `*-command` · `--log-file` · `--dump*` ฯลฯ และเมื่อตั้ง `RB_LOCAL_ROOTS` ก็รวม flag ที่อ่านที่อื่นบนเครื่อง
   (`--copy-links` · `--files-from` · `--backup-dir` ฯลฯ)

รายชื่อนี้เป็นการตัดสิน ไม่ใช่ข้อพิสูจน์ — **อย่าเปิด API ออกนอกเครื่องโดยไม่มีรหัสผ่านและ TLS**

ปลายของ task ถูกตรวจด้วย: ฝั่งรีโหมดต้องขึ้นต้นด้วยรีโหมดที่มี (`400` `no_such_remote`)
และสิ่งที่ upload ต้องมีอยู่ (`400` `no_such_path`) ช่องตัวเลขต้องเป็นรูปที่ rclone รับ (`400` `bad_field`)

| | |
|---|---|
| `GET /api/v1/rclone/flags` | `{loaded, flags: [{name, short, type, takesValue, group, description}]}` — `loaded: false` คือยังถาม rclone ไม่ได้ ไม่ใช่ "ไม่มี flag" |
| `GET /api/v1/local/stat?path=` | `{path, exists, isDir}` หรือ `403` `outside_roots` |

**คำสั่งที่ส่งออกไปถูก redact** — `RunningJob::displayCommand()` ไม่ใช่ `mArgs` ดิบ
เพราะ args มี token ของ backend และรหัสผ่าน remote control

**method ที่ไม่รู้จักตอบ `405`** พร้อมบอกว่าไม่รองรับ ดีกว่าแกล้งทำเป็นว่าทำอะไรสำเร็จ

## ยังไม่มี

- **แก้ task ชนิด mount ผ่าน API** — ตั้งใจไม่ทำ ดูเหตุผลข้างบน
- **Web UI (S12)**
  ใน Docker ที่ถูกคือรับ SIGTERM ไม่ใช่ endpoint
