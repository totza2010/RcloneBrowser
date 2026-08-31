# โครงร่าง API — สำรวจจากโค้ดจริง

> เอกสารนี้เป็น **ขั้นเตรียมของ E3** ([`PLAN.md` §6.3](PLAN.md)) — ยังไม่มีโค้ด API อยู่ในรีโป
> ทุกตารางข้างล่างมาจากการอ่านซอร์สจริง ไม่ใช่การออกแบบจากศูนย์: จุดประสงค์คือรู้ให้ชัดว่า
> **ตอนนี้ระบบมีอะไรบ้าง เก็บอะไรไว้ที่ไหน สั่งอะไรได้ เพิ่มลบอย่างไร** ก่อนจะไปตัดสินใจว่า API หน้าตาแบบไหน
>
> อ่านคู่กับ [`ARCHITECTURE.md`](ARCHITECTURE.md) ซึ่งบอกว่าโค้ดควรแบ่งชั้นอย่างไร
> เอกสารนี้บอกว่า *ของจริงวางอยู่ตรงไหน* ซึ่งยังไม่ตรงกัน

---

## สรุปก่อน: ข้อค้นพบที่กำหนดรูปร่างของ API

**สถานะเกือบทั้งหมดของแอปเก็บอยู่ใน widget ไม่ใช่ใน model** นี่คือข้อเท็จจริงข้อเดียวที่มีผลต่อ
งาน E3 มากที่สุด และเป็นเหตุผลว่าทำไม E1/E2 ต้องมาก่อน

| ระบบ | มี model จริงไหม | เก็บที่ไหน | จำนวนที่โค้ดอ้างถึง widget โดยตรง |
|---|---|---|---|
| Tasks | 🟡 มีครึ่งเดียว | `ListOfJobOptions` (singleton) **แต่** การรันอ่านจาก `ui.tasksListWidget` | 86 |
| Queue | ❌ ไม่มี | `ui.queueListWidget` ล้วนๆ | 129 |
| Jobs ที่กำลังวิ่ง | ❌ ไม่มี | `ui.jobs` (QLayout ของ `JobWidget`) | 88 |
| Schedulers | ❌ ไม่มี | `ui.schedulers` (QLayout ของ `SchedulerWidget`) | 42 |
| Remotes | ❌ ไม่มี | `ui.remotes` (QListWidget) | 31 |

ฟังก์ชันบันทึกไฟล์ก็วนอ่านจาก widget ตรงๆ — [`saveQueueFile()`](../src/main_window.cpp) วน
`ui.queueListWidget->item(i)` และ [`saveSchedulerFile()`](../src/main_window.cpp) วน
`ui.schedulers->itemAt(i)->widget()` แล้ว `qobject_cast<SchedulerWidget*>`

**แปลว่า:** ถ้าเขียน API วันนี้ มันจะต้องอ่านสถานะผ่าน `QWidget` ซึ่งบังคับให้ต้องมี GUI
และขัดกับเป้าหมาย `-DNO_GUI=ON` ทั้งหมด — API จึงต้องรอ E2 หรืออย่างน้อยต้องย้าย
queue/jobs/schedulers ออกมาเป็น model ก่อน

---

## 1. Remotes

**คือ** รายชื่อ remote ที่ตั้งไว้ใน `rclone.conf`

| | |
|---|---|
| แหล่งข้อมูล | `rclone listremotes --long` → `ชื่อ: ชนิด` |
| เก็บที่ | ไม่เก็บ — ยิงใหม่ทุกครั้งที่ refresh |
| โค้ด | `MainWindow::rcloneListRemotes()` |

**ข้อมูลต่อรายการ**

| ฟิลด์ | ที่มา |
|---|---|
| `name` | ก่อน `:` |
| `type` | หลัง `:` (`teldrive`, `drive`, `onedrive`, ...) |
| `capabilities` | `rclone backend features <name>:` → [`rclone_capabilities.h`](../src/rclone_capabilities.h) |

`RcloneCapabilities` ให้: `about` `cleanUp` `command` `copy` `duplicateFiles` `listR` `move`
`publicLink` `purge` `putStream` · `hashes[]` · `precisionNs` · `known`

**สั่งอะไรได้:** list · browse (`rclone lsjson`) · ดู capability
**เพิ่ม/ลบ:** ❌ ทำไม่ได้ในแอป — ปุ่ม Config เปิด `rclone config` แบบ interactive ใน terminal เท่านั้น

> **ช่องว่างที่ API ต้องตัดสินใจ:** จะให้เพิ่ม/ลบ remote ผ่าน API ไหม
> ถ้าเอา ต้องเรียก `rclone config create/delete/update` ซึ่งแอปไม่เคยแตะเลย
> และเป็นทางที่ credential ของ backend จะไหลผ่าน API — ต้องคิดเรื่องความปลอดภัยแยกต่างหาก

### การเรียกดูไฟล์ในremote

| | |
|---|---|
| แหล่งข้อมูล | `rclone lsjson` |
| parser | [`lsjson_parser.h`](../src/lsjson_parser.h) (L0, streaming) |
| ข้อมูลต่อรายการ | `name` `path` `size` (-1 = ไม่รู้) `isDir` `modified` |

---

## 2. Tasks (งานที่บันทึกไว้)

**คือ** ชุดตัวเลือกของงานหนึ่งงานที่เซฟไว้ใช้ซ้ำ — copy/move/sync/mount

| | |
|---|---|
| model | `ListOfJobOptions` (singleton) → `QList<JobOptions*>` |
| เก็บที่ | `<config>/tasks.bin` — **QDataStream binary** ไม่ใช่ text |
| เวอร์ชัน schema | `JobOptions::classVersion` (ปัจจุบัน v8) |
| โค้ด | [`list_of_job_options.h`](../src/list_of_job_options.h) · [`job_options.h`](../src/job_options.h) |

**ข้อมูลต่อ task: 46 ฟิลด์** แบ่งได้เป็น

| กลุ่ม | ฟิลด์ |
|---|---|
| ระบุตัว | `uniqueId` (UUID) · `description` |
| ประเภท | `jobType` (Upload/Download) · `operation` (Copy/Move/Sync/Mount/Check/CryptCheck) |
| ที่มา/ปลายทาง | `source` · `dest` · `isFolder` · `remoteMode` · `remoteType` |
| พฤติกรรม sync | `sync` · `syncTiming` (During/After/Before) · `skipNewer` · `skipExisting` · `deleteExcluded` · `createEmptySrcDirs` · `deleteEmptySrcDirs` · `noTraverse` |
| การเทียบไฟล์ | `compare` · `compareOption` (SizeAndModTime/Checksum/IgnoreSize/SizeOnly/ChecksumIgnoreSize) |
| ประสิทธิภาพ | `transfers` · `checkers` · `bandwidth` · `connectTimeout` · `idleTimeout` · `retries` · `lowLevelRetries` |
| ตัวกรอง | `minSize` · `minAge` · `maxAge` · `maxDepth` · `included` · `excluded` · `filtered` |
| mount | `mountReadOnly` · `mountCacheLevel` (Off/Minimal/Writes/Full) · `mountVolume` · `mountAutoStart` · `mountRcPort` · `mountScript` · `mountWinDriveMode` |
| อื่นๆ | `verbose` · `sameFilesystem` · `dontUpdateModified` · `DriveSharedWithMe` · `extra` · `dryRun` (ไม่ persist) |

**สั่งอะไรได้**

| คำสั่ง | โค้ด |
|---|---|
| รันทันที | `MainWindow::runItem(item, "task", requestId, dryRun)` |
| รันแบบ dry-run | `runItem(..., dryrun=true)` |
| ใส่คิว | `addSavedTransfer(uniqueId, dryRun, addToQueue=true)` |
| แก้ไข | `editSelectedTask()` → เปิด transfer/mount dialog |

**เพิ่ม:** `ListOfJobOptions::getInstance()->Persist(jobOptions)` — เรียกจาก
[`transfer_dialog.cpp`](../src/transfer_dialog.cpp) และ [`mount_dialog.cpp`](../src/mount_dialog.cpp)
**ลบ:** `ListOfJobOptions::getInstance()->Forget(jobOptions)`
ทั้งคู่เขียนไฟล์ทันทีและ emit `tasksListUpdated()`

**args ที่ส่งให้ rclone:** `JobOptions::getOptions()` ประกอบเอง (44 จุดที่ `list <<`)
รวม default options จาก Preferences เข้ามาด้วย — **นี่คือที่เดียวที่ควรประกอบ args** และ API ต้องใช้ทางนี้

> ⚠️ `tasks.bin` เป็น binary ที่ผูกกับลำดับฟิลด์ ถ้า API จะรับ/ส่ง task เป็น JSON
> ต้องมีตัวแปลงแยก และต้องตัดสินใจว่า `tasks.json` จะมาแทนไหม (อยู่ในรายการ P2 ของแผน)

---

## 3. Queue (คิวงาน)

**คือ** ลำดับ task ที่รอรันทีละงาน

| | |
|---|---|
| model | ❌ **ไม่มี** — `ui.queueListWidget` คือแหล่งความจริงเดียว |
| เก็บที่ | `<config>/queue.conf` — บรรทัดละ `uniqueId,requestId` |
| โค้ด | `MainWindow::saveQueueFile()` · `addTasksToQueue()` |

**ข้อมูลต่อรายการ:** `uniqueId` ของ task + `requestId` (UUID ของการรันครั้งนั้น)

**สั่งอะไรได้**

| คำสั่ง | สถานะที่เกี่ยวข้อง |
|---|---|
| start / pause คิว | `mQueueStatus` (bool) |
| เลื่อนขึ้น/ลง | ลำดับใน list widget |
| ลบออกจากคิว | |
| ล้างคิว | |

ตัวนับที่มีอยู่: `mQueueCount` · `mQueueTaskRunning`

**เพิ่ม:** `addSavedTransfer(uniqueId, dryRun, addToQueue=true)`
**ลบ:** ลบ item ออกจาก widget แล้ว `saveQueueFile()`

> **จุดที่ต้องแก้ก่อนทำ API:** ลำดับคิวคือลำดับของ `QListWidgetItem` ล้วนๆ
> ไม่มีที่ไหนเก็บคิวเป็นข้อมูล — ต้องมี `JobQueue` ใน L1 ก่อน

---

## 4. Jobs (งานที่กำลังวิ่ง)

**คือ** process ของ rclone ที่กำลังทำงานอยู่จริง หนึ่ง job = หนึ่ง `JobWidget`

| | |
|---|---|
| model | ❌ ไม่มี — `ui.jobs` (QLayout) |
| เก็บที่ | ไม่ persist (หายเมื่อปิดแอป) แต่ log ลงไฟล์ |
| โค้ด | [`job_widget.h`](../src/job_widget.h) |

**ข้อมูลต่อ job**

| กลุ่ม | ฟิลด์ | ที่มา |
|---|---|---|
| ระบุตัว | `uniqueID` (ของ task) · `requestId` (ของการรันครั้งนี้) · `transferMode` | ตัวสร้าง |
| คำสั่ง | `info` · `source` · `dest` · `args` (ต้องผ่าน `RedactArgs()`) | ตัวสร้าง |
| เวลา | `startDateTime` · ETA ที่คำนวณเป็นเวลาเสร็จ | `updateStartInfo()` / `updateFinishInfo()` |
| สถานะ | `isRunning` · `status` · `jobFinalStatus` | |
| ความคืบหน้า | ทั้งหมดจาก `core/stats` | [`job_stats.h`](../src/job_stats.h) |

`JobStats` ให้: `bytes` `totalBytes` `checks` `totalChecks` `transfers` `totalTransfers`
`deletes` `renames` `errors` `listed` `speed` `etaSeconds` `elapsedSeconds` `fatalError` `retryError`
· `transferring[]` (ต่อไฟล์: `name` `bytes` `size` `percentage` `speed` `etaSeconds`)
· `phase()` → Starting / Scanning / Transferring / Finishing

**ค่าของ `status`** (ใช้เรียงในหน้าจอด้วย จึงมีเลขนำหน้า)

```
0_transfer_running   1_transfer_finished   2_transfer_error   2_transfer_stopped
0_zmount_mounted     1_zmount_finished     1_zmount_erro
0_stream_streaming   1_stream_finished     2_stream_error
```

**ค่าของ `transferMode`:** `task` · `queue` · `scheduler` · `autostart`
**ค่าของ `jobFinalStatus`:** `finished` · `error` · `stopped`

**สั่งอะไรได้:** stop (`JobWidget::cancel()` → kill process) · stop ทั้งหมด · ปิดการ์ดที่จบแล้ว
**เพิ่ม:** ไม่ได้สร้างโดยตรง — เกิดจากการรัน task/queue/scheduler → `MainWindow::addTransfer(...)`
**ลบ:** ปิดการ์ด (`closed` signal)

> ❗ **ทุกอย่างที่ออกทาง API ต้องผ่าน `RedactArgs()`** — `args` มี `--rc-pass` และ token ที่ผู้ใช้ใส่
> ([`ARCHITECTURE.md` §5](ARCHITECTURE.md)) ยิ่ง API เปิดออกเน็ตเวิร์ก ยิ่งพลาดไม่ได้

---

## 5. Mounts

**คือ** `rclone mount` ที่กำลังทำงาน — หนึ่ง mount = หนึ่ง `MountWidget`

| | |
|---|---|
| model | ❌ ไม่มี — อยู่ใน `ui.jobs` ปนกับ transfer |
| เก็บที่ | ตัว mount ไม่ persist · การตั้งค่าอยู่ใน task (`operation = Mount`) |
| โค้ด | [`mount_widget.h`](../src/mount_widget.h) |

**ข้อมูลต่อ mount:** `remote` · `folder` (drive letter หรือ mount point) · `uniqueID` · `info`
· `status` · `rcPort` · `unmountingError` · script ที่จะรันหลัง mount

**สั่งอะไรได้:** mount · unmount (ผ่าน RC `core/quit` ไม่ใช่ kill) · autostart ตอนเปิดแอป
**เพิ่ม:** `MainWindow::addNewMount(remote, folder, remoteType, args, script, uniqueId, info)`
**ลบ:** `MountWidget::cancel()`

> unmount ใช้ RC endpoint ที่ต้อง auth — credential สุ่มต่อ mount และส่งผ่าน environment
> ไม่ใช่ command line ([V-08](VERIFY.md)) API ต้องไม่ทำให้ค่านี้หลุดออกมา

---

## 6. Streams

**คือ** `rclone cat` ส่งต่อเข้า media player

| | |
|---|---|
| model | ❌ ไม่มี |
| ข้อมูล | `remote` · `stream` (คำสั่ง player) · `status` |
| โค้ด | [`stream_widget.h`](../src/stream_widget.h) |

**เพิ่ม:** `MainWindow::addStream(remote, stream, remoteType)` · **ลบ:** `cancel()`

> **ข้อเสนอ:** ตัด stream ออกจาก API รอบแรก — มันสั่ง player บนเครื่องที่รันแอป
> ซึ่งไม่มีความหมายเมื่อสั่งจากเบราว์เซอร์เครื่องอื่น

---

## 7. Schedulers

**คือ** ตารางเวลาที่ผูกกับ task หนึ่งตัว

| | |
|---|---|
| model | ❌ ไม่มี — `ui.schedulers` (QLayout) |
| เก็บที่ | `<config>/scheduler.conf` — บรรทัดละหนึ่ง scheduler เป็น **key,value คั่นด้วย comma** ค่าข้อความเข้ารหัส base64 |
| engine | `qcron.cpp` / `qcronfield.cpp` / `qcronnode.cpp` (L1, สะอาดอยู่แล้ว) |
| โค้ด | [`scheduler_widget.h`](../src/scheduler_widget.h) |

**ข้อมูลต่อ scheduler**

| ฟิลด์ | หมายเหตุ |
|---|---|
| `mSchedulerId` · `mSchedulerName` | ชื่อเก็บเป็น base64 |
| `mTaskId` · `mTaskName` | ผูกกับ task |
| `mSchedulerStatus` | `activated` / `paused` |
| `mDailyState` + `mDailyMon..Sun` + `mDailyHour` + `mDailyMinute` | โหมดรายวัน |
| `mCronState` + `mCron` | โหมด cron (`30 6,18 * * MON-FRI`) |
| `mExecutionMode` | `0` / `1` |
| `mLastRun` · `mLastRunFinished` · `mLastRunStatus` · `mRequestId` | ประวัติการรันครั้งล่าสุด |

**สั่งอะไรได้:** start · stop · run เดี๋ยวนี้ · แก้ task ที่ผูก · เปิด/ปิดทั้งระบบ (`Settings/schedulerStatus`)
**เพิ่ม:** `MainWindow::addScheduler(taskId, taskName, args)` · **ลบ:** ปิดการ์ด แล้ว `saveSchedulerFile()`

> รูปแบบไฟล์ comma+base64 นี้เป็นของทำมือ ไม่มี schema version — ถ้า API จะแก้ scheduler ได้
> ควรเปลี่ยนเป็น JSON พร้อมกัน มิฉะนั้นจะมีสองทางเขียนไฟล์เดียวกันคนละแบบ

---

## 8. Settings

| | |
|---|---|
| เก็บที่ | `QSettings` — registry บน Windows · ไฟล์ ini บน Unix · ไฟล์ข้าง exe ในโหมด portable |
| โค้ด | `GetSettings()` ใน [`utils.h`](../src/utils.h) |

**กลุ่มค่า:** rclone path · rclone.conf path · stream/mount command · โฟลเดอร์ default
· default options 3 ชุด · proxy · การแจ้งเตือน · หน้าตา (ฟอนต์/ไอคอน/ธีม) · script 3 จุด
· preemptive loading · `schedulerStatus`

**สั่งอะไรได้:** อ่าน/เขียนทีละคีย์
**เพิ่ม/ลบ:** ไม่มีแนวคิดเพิ่มลบ — เป็น key/value คงที่

> ❗ ค่าที่ **ห้าม** ให้แก้ผ่าน API: path ของ rclone และของ script ทั้งสามตัว
> เพราะแก้ได้เท่ากับสั่งรัน binary อะไรก็ได้บนเครื่อง = remote code execution

---

## 9. Logs

| | |
|---|---|
| เก็บที่ | `<config>/logs/` — ไฟล์ละ job |
| ชื่อไฟล์ | `<timestamp>-<operation>-<short job id>.log` (⚠️ มี `REVISIT` ค้างอยู่) |
| โค้ด | [`job_log.h`](../src/job_log.h) (L0) |

**สั่งอะไรได้:** list · อ่าน · ตามแบบ tail (ยังไม่มี)
**เพิ่ม:** เขียนอัตโนมัติเมื่อ job เริ่ม · **ลบ:** ยังไม่มีระบบล้าง

---

## 10. Capabilities / Flags (ข้อมูลของตัว rclone เอง)

| ระบบ | ให้อะไร | โค้ด |
|---|---|---|
| Capability registry | backend ทำอะไรได้ ต่อ remote | [`rclone_capabilities.h`](../src/rclone_capabilities.h) |
| Flag registry | flag ทั้งหมดที่ rclone ตัวนี้รับ (1,078 ตัว) + repo ที่มาจาก | [`rclone_flags.h`](../src/rclone_flags.h) |

ทั้งคู่เป็น L0 ที่พร้อมใช้กับ API อยู่แล้ว ไม่ต้องแก้อะไร

---

## 11. โครงร่าง API ที่เสนอ

### หลักการ

1. **API พูดภาษาของเรา ไม่ใช่ proxy ของ rclone rc** — คิวงาน ตารางเวลา และ task ที่บันทึกไว้
   คือของที่ L1 ของเราถือ ซึ่ง `rclone rcd` ไม่มี ([`ARCHITECTURE.md` §7](ARCHITECTURE.md))
2. **อ่านได้ก่อน เขียนทีหลัง** — แดชบอร์ดแบบ qBittorrent ต้องการ read + สั่ง start/stop เป็นหลัก
   ส่วนการสร้าง task ใหม่ผ่านเว็บทำทีหลังได้
3. **ทุก response ผ่าน `RedactArgs()`**
4. **บังคับ auth ตั้งแต่ endpoint แรก** ไม่ใช่เพิ่มทีหลัง

### ทรัพยากรและคำสั่ง

```
GET    /api/v1/remotes                    ชื่อ + ชนิด
GET    /api/v1/remotes/{name}/capabilities
GET    /api/v1/remotes/{name}/files?path= lsjson

GET    /api/v1/tasks                      รายการ task ที่บันทึกไว้
POST   /api/v1/tasks                      สร้าง                     (เฟส 2 ของ API)
GET    /api/v1/tasks/{id}
PUT    /api/v1/tasks/{id}                 แก้                       (เฟส 2)
DELETE /api/v1/tasks/{id}                 = Forget()
POST   /api/v1/tasks/{id}/run             body: {"dryRun": bool}
POST   /api/v1/tasks/{id}/enqueue

GET    /api/v1/queue                      ลำดับ + สถานะ
POST   /api/v1/queue/start
POST   /api/v1/queue/pause
DELETE /api/v1/queue/{requestId}
PUT    /api/v1/queue/order                จัดลำดับใหม่

GET    /api/v1/jobs                       งานที่วิ่งอยู่ + ที่จบแล้วในรอบนี้
GET    /api/v1/jobs/{requestId}           รวม stats ต่อไฟล์
DELETE /api/v1/jobs/{requestId}           = stop
GET    /api/v1/jobs/{requestId}/log       ?follow=true → SSE

GET    /api/v1/mounts
POST   /api/v1/mounts                     body: {"taskId": ...}
DELETE /api/v1/mounts/{id}                = unmount

GET    /api/v1/schedulers
POST   /api/v1/schedulers
PUT    /api/v1/schedulers/{id}
DELETE /api/v1/schedulers/{id}
POST   /api/v1/schedulers/{id}/start
POST   /api/v1/schedulers/{id}/stop

GET    /api/v1/settings                   เฉพาะคีย์ที่ปลอดภัย
PUT    /api/v1/settings

GET    /api/v1/system                     เวอร์ชัน rclone, repo ที่ตรวจได้, จำนวนงาน
GET    /api/v1/events                     SSE — progress สดของทุก job
```

### `/api/v1/events` คือหัวใจของแดชบอร์ด

การ poll `/jobs` ทุกวินาทีจากหลายเบราว์เซอร์จะกลายเป็นภาระ ในเมื่อ `RcClient` poll
`core/stats` อยู่แล้ววินาทีละครั้ง ทางที่ถูกคือ **ส่งต่อ** ผลนั้นออกไปทาง SSE
ไม่ใช่ให้แต่ละ client เปิด poll ของตัวเอง

---

## 12. ทะเบียนระบบ — ลำดับการย้ายเข้า L1

> ตารางนี้เป็น **เอกสารมีชีวิต** เหมือน `ARCHITECTURE.md` — อัปเดตช่องสถานะทุกครั้งที่ปิดระบบหนึ่ง
> แต่ละระบบเป็นงานที่จบในตัวเอง commit ได้แยก และทดสอบได้แยก

### ภาพรวม

| ID | ระบบ | ตอนนี้อยู่ที่ | เป้าหมาย | ขึ้นกับ | ขนาด | สถานะ |
|---|---|---|---|---|---|---|
| **S1** | Task store | `ListOfJobOptions` + `ui.tasksListWidget` | `TaskStore` (L1) | — | M | ✅ **เสร็จ** — ส่งมอบ E1 |
| **S2** | Job registry | `ui.jobs` + `JobWidget` ถือ `QProcess` | `JobRegistry` + `RunningJob` (L1) | S1 | **L** | 🟡 **transfer เสร็จ** · mount/stream = S10 |
| **S3** | Queue | `ui.queueListWidget` | `JobQueue` (L1) | S1, S2 | M | 🟡 เครื่องยนต์เสร็จ · หน้าต่างยังไม่เรียก |
| **S4** | Scheduler store | `ui.schedulers` + `scheduler.conf` (comma+b64) | `SchedulerStore` (L1) | S1, S2 | M | 🟡 เครื่องยนต์เสร็จ · หน้าต่างยังไม่เรียก |
| **S5** | Remote registry | `ui.remotes` | `RemoteRegistry` (L1) | — | S | 🟡 เครื่องยนต์เสร็จ · หน้าต่างยังไม่เรียก |
| **S6** | Settings | `QSettings` เรียกตรงทุกที่ | `AppSettings` (L1) | — | S | 🟡 ส่วนที่ core ใช้เสร็จ · ส่วนที่เป็นหน้าตายังอยู่ที่เดิม |
| **S7** | Job logs | `job_log.*` (L0) | — | — | — | ✅ **มีแล้ว** |
| **S8** | Capabilities | `rclone_capabilities.*` (L0) | — | — | — | ✅ **มีแล้ว** |
| **S9** | Flags / repo | `rclone_flags.*` (L0) | — | — | — | ✅ **มีแล้ว** |
| **S10** | Mounts / Streams | `mount_widget` / `stream_widget` | ชนิดหนึ่งของ `RunningJob` | S2 | M | ✅ **mount เสร็จ** · stream ไม่ย้าย (มีเหตุผล) |
| **S14** | **Extension เฉพาะ backend** | ยังไม่มี — ความรู้เรื่อง teldrive ไม่มีที่อยู่ | `rbext_teldrive` (โมดูลตอนคอมไพล์) | S13 | M | ⬜ ดู [`PLAN.md` §6.9](PLAN.md) |
| **S13** | **Store + ประวัติ** | SQLite ไฟล์เดียว (schema v2) | `database.*` · `run_history.*` · `config_store.*` (L1) | S2 | **L** | ✅ เสร็จ — ดู [`PLAN.md` §6.8](PLAN.md) |
| **S11** | HTTP API | — | `api_server` (L2) | S1–S6, S10 | L | 🟨 อ่านได้แล้ว |
| **S12** | Web UI | — | static (L3) | S11 | L | ⬜ |

**S7–S9 เสร็จไปแล้วโดยไม่ได้ตั้งใจ** — ทั้งสามเกิดจากงาน P1/เฟส 2 ที่เขียนเป็น L0 ตั้งแต่แรก
ซึ่งเป็นหลักฐานว่าวิธี "เจอ core ก็แยกเลยถ้าต้นทุนไม่เกินครึ่งชั่วโมง" ได้ผลจริง

### ลำดับที่จะทำ และเหตุผล

```
S1 TaskStore ✅ ──> S2 JobRegistry 🟡 ──> S10 Mount/Stream ──> S13 Store+History ──┬──> S3 JobQueue ──┐
                                                                                   └──> S4 Scheduler ─┼──> S11 API ──> S12 Web UI
S5 RemoteRegistry ─────────────────────────────────────────────────────────────────────────────────────┤
S6 SettingsFacade ─────────────────────────────────────────────────────────────────────────────────────┘
```

> **ลำดับเปลี่ยนหลังเพิ่ม S13 (2026-08-07):** เดิมวาง S3/S4 ไว้ก่อน แต่ทั้งคู่ต้องเขียนข้อมูลลงที่เก็บ
> ถ้าทำก่อน S13 จะได้เขียนโค้ดอ่าน/เขียน `.conf` ขึ้นมาใหม่แล้วรื้อทิ้งทันทีที่ย้ายเข้า DB
> ทำ S13 ก่อนแปลว่า `JobQueue` กับ `SchedulerStore` เกิดมาพร้อมที่เก็บที่ถูกต้องตั้งแต่แรก

**S1 ก่อน** เพราะทุกอย่างอ้างถึง task และเพราะมันปลดล็อก **E1 (`--run-task`)** ได้ทันที
ซึ่งเป็นของส่งมอบที่ใช้งานได้จริงชิ้นแรกของสายนี้ — ไม่ต้องรอ S2–S4 เลย
**✅ ทำแล้ว 2026-08-06** ดู V-17

**S2 คือก้อนใหญ่ที่สุดและเสี่ยงที่สุด** ทำเป็นอันดับสองเพราะ S3/S4/S10 ต้องใช้
ถ้าทำ S3 ก่อน จะได้คิวที่ยังต้องคุยกับ widget อยู่ดี

**S5 และ S6 แทรกได้ทุกเมื่อ** ไม่ขึ้นกับใคร ใช้เป็นงานคั่นเวลาที่ติดขัดได้

**S13 มาหลัง S10** เพราะ mount/stream ต้องเดินทาง `RunningJob` เสียก่อน ประวัติจึงจะบันทึก
งานทุกชนิดด้วยโค้ดชุดเดียว ไม่ต้องมีเส้นทางพิเศษต่อชนิด

---

### S1 · Task store

| | |
|---|---|
| ตอนนี้ | `ListOfJobOptions` มี `QList<JobOptions*>` อยู่แล้ว **แต่** การรันงานรับ `JobOptionsListWidgetItem*` เข้ามา (`MainWindow::runItem`) และการหา task ทำโดยวนหา `ui.tasksListWidget` |
| ปัญหา | หา task โดยไม่มีหน้าต่างไม่ได้ ทั้งที่ข้อมูลอยู่ในหน่วยความจำแล้ว |

**เป้าหมาย**

```cpp
// L1 -- ยังเป็น ListOfJobOptions เดิม เพิ่มสิ่งที่ headless ต้องใช้
class TaskStore : public QObject {
public:
  static TaskStore &instance();

  const QList<JobOptions *> &tasks() const;
  JobOptions *find(const QUuid &id) const;        // ← ยังไม่มี
  JobOptions *findByName(const QString &name) const; // ← ยังไม่มี (--run-task รับชื่อ)

  bool add(JobOptions *task);                     // = Persist()
  bool remove(const QUuid &id);                   // = Forget()

signals:
  void changed();
};
```

**ที่ต้องแก้**
- เพิ่ม `find()` / `findByName()` — ตอนนี้ตรรกะนี้กระจายอยู่ใน `main_window.cpp` หลายที่
- `runItem()` ต้องรับ `JobOptions*` ไม่ใช่ `JobOptionsListWidgetItem*`
- GUI เหลือหน้าที่แค่ "แสดงรายการที่ store บอก" และฟัง `changed()`

**ของส่งมอบ: E1** — `rclone-browser --run-task "ชื่องาน"` ทำงานจนจบและคืน exit code ถูก โดยไม่เปิดหน้าต่าง

**ทดสอบ:** `tests/test_task_store.cpp` มีอยู่แล้ว (golden file v8, 30 ฟิลด์) เพิ่มเคส `find`/`findByName`
· เคส headless ต้องเป็น integration test เพราะต้อง spawn rclone จริง

**เสร็จเมื่อ:** `--run-task` ผ่านบนเครื่องที่ไม่มีจอ (`QT_QPA_PLATFORM=offscreen`) และ V-05 ยังผ่านครบ

#### ความคืบหน้า

| ขั้น | สถานะ |
|---|---|
| `find(QUuid)` / `find(QString)` / `findByName()` / `countByName()` | ✅ + test 5 เคส |
| `runItem()` รับ `JobOptions*` แทน `JobOptionsListWidgetItem*` | ✅ 12 จุดเรียก |
| `addSavedTransfer()` หา task จาก store ไม่ใช่จาก widget | ✅ |
| `restoreSchedulersFromFile()` ใช้ `find()` | ✅ |
| ยังเหลือ: อีก ~13 จุดที่วน `ui.tasksListWidget` เพื่อ**อัปเดตหน้าจอ** | ⬜ ปล่อยไว้ได้ — เป็น L3 จริงๆ |
| แยก `main()` ให้ไม่สร้าง `QApplication` เมื่อรัน headless | ✅ |
| `--run-task` / `--list-tasks` / `--dry-run` + exit code | ✅ `task_runner.*` (L1) · test 7 เคส |
| Windows: attach console (แอปเป็น GUI subsystem) | ✅ |

> จุดที่วน `ui.tasksListWidget` **ไม่ได้ผิดทั้งหมด** — ที่วนเพื่อระบายสีแถวหรือแก้ข้อความ
> เป็นงานของ L3 โดยชอบธรรม ที่ต้องย้ายคือจุดที่วนเพื่อ *หา task* ซึ่งตอนนี้หมดแล้ว

---

### S2 · Job registry

| | |
|---|---|
| ตอนนี้ | `JobWidget` เป็นเจ้าของ `QProcess` + `RcClient` + `JobLogWriter` — งานที่วิ่งอยู่ **คือ** widget |
| ปัญหา | ไม่มี widget = ไม่มีงาน · API อ่านสถานะงานไม่ได้ · ปิดหน้าต่าง = ฆ่างาน |

**เป้าหมาย** — แยกเป็นสองชั้นชัดๆ

```cpp
// L1 -- งานหนึ่งงานที่กำลังวิ่ง ไม่รู้จัก widget เลย
class RunningJob : public QObject {
public:
  QUuid requestId() const;
  QUuid taskId() const;
  JobKind kind() const;          // Transfer | Mount | Stream
  QString transferMode() const;  // task | queue | scheduler | autostart
  QStringList redactedArgs() const;
  QDateTime startedAt() const;
  JobStatus status() const;
  JobStats stats() const;

  void stop();

signals:
  void statsUpdated(const JobStats &);
  void outputLine(const QString &redacted);
  void finished(JobStatus);
};

class JobRegistry : public QObject {
public:
  static JobRegistry &instance();
  QList<RunningJob *> jobs() const;
  RunningJob *find(const QUuid &requestId) const;
  RunningJob *start(const JobOptions &task, const QString &transferMode,
                    const QUuid &requestId, bool dryRun);
signals:
  void jobStarted(RunningJob *);
  void jobFinished(RunningJob *);
};
```

**ที่ต้องแก้**
- ย้าย `QProcess` + `RcClient` + `JobLogWriter` ออกจาก `JobWidget` ไป `RunningJob`
- `JobWidget` เหลือหน้าที่ **แสดงผลอย่างเดียว** — รับ `RunningJob*` แล้วต่อ signal
- `mStatus` ที่เป็นสตริงเรียงลำดับ (`0_transfer_running`) แยกเป็น enum + ฟังก์ชันจัดลำดับใน L3

**นี่คือจุดที่ VIO-1 หายไปด้วย** — `main_window.cpp` เลิกประกอบ args เอง เพราะ `start()` รับ `JobOptions`

**ทดสอบ:** `RunningJob` ทดสอบได้ด้วย rclone จริงที่รันงานสั้นๆ · `JobRegistry` ทดสอบ lifecycle ได้ล้วนๆ
**เสร็จเมื่อ:** สั่งงานและอ่าน progress ได้จาก test ที่ไม่ลิงก์ `Qt6::Widgets`

#### ความคืบหน้า (2026-08-07)

| ขั้น | สถานะ |
|---|---|
| `RunningJob` (L1) ถือ `QProcess` + `RcClient` + `JobLogWriter` | ✅ |
| `JobRegistry` (L1) — `jobs()` `find()` `runningCount()` `start()` `forget()` | ✅ |
| `JobWidget` เหลือแค่แสดงผล รับ `RunningJob*` ตัวเดียว | ✅ ลดจาก 462 → 382 บรรทัด (−158/+78) |
| `addTransfer()` สั่งผ่าน registry | ✅ |
| `--rc --rc-addr=localhost:0` ย้ายเข้า `RunningJob` | ✅ ไม่ต้องให้ผู้เรียกจำ |
| test ที่ไม่มี widget เลย | ✅ `test_job_registry.cpp` 7 เคส รัน rclone จริง |
| **mount / stream** | ⬜ = S10 · ยังถือ `QProcess` เอง |
| VIO-1 ฝั่ง transfer | ✅ หายแล้ว · ฝั่ง mount ยังเหลือ ~30 บรรทัด |

**บั๊กที่เจอเพราะเขียน test ได้:**

| อาการ | สาเหตุ |
|---|---|
| rclone รันไม่ขึ้น → การ์ดค้าง "Running" ตลอดชีวิตโปรแกรม | `QProcess` ส่ง `errorOccurred` แล้ว**ไม่ส่ง** `finished` — ของเดิมก็เป็นแบบนี้ ไม่ใช่ของใหม่ |
| segfault ตอน rclone ไม่มีอยู่จริง | บน Windows `FailedToStart` ยิงจาก**ใน** `start()` ทำให้ `mProcess` เป็น null ก่อน `start()` จะ return |

ทั้งสองอย่างมองไม่เห็นเลยตอนที่งานที่วิ่งอยู่ยังเป็น widget

---

### S3 · Queue

| | |
|---|---|
| ตอนนี้ | ลำดับคิว = ลำดับของ `QListWidgetItem` · `queue.conf` เขียนโดยวน widget |
| ปัญหา | ไม่มีที่ไหนเก็บคิวเป็นข้อมูลเลย |

```cpp
// L1
class JobQueue : public QObject {
public:
  struct Entry { QUuid taskId; QUuid requestId; bool dryRun; };

  QList<Entry> entries() const;
  void enqueue(const QUuid &taskId, bool dryRun);
  void remove(const QUuid &requestId);
  void move(int from, int to);
  void clear();

  void start();
  void pause();
  bool isRunning() const;

  bool load();   // <config>/queue.conf
  bool save();

signals:
  void changed();
};
```

**ที่ต้องแก้:** `mQueueStatus` `mQueueCount` `mQueueTaskRunning` ย้ายเข้า `JobQueue`
· `saveQueueFile()` เลิกวน widget
· ตัวคิวเป็นคนสั่ง `JobRegistry::start()` เมื่องานก่อนหน้าจบ

**เสร็จเมื่อ:** คิวเดินต่อได้เองจนหมดโดยไม่มี widget เลย

---

### S4 · Scheduler store

| | |
|---|---|
| ตอนนี้ | `SchedulerWidget` ถือ state 23 ฟิลด์ · `scheduler.conf` เป็น `key,value` คั่นด้วย comma ค่าเป็น base64 · ไม่มี schema version |
| ปัญหา | รูปแบบไฟล์ทำมือ แก้ยาก และผูกกับ widget |

```cpp
// L1
struct Schedule {
  QUuid id;
  QString name;
  QUuid taskId;
  bool active = false;

  bool dailyMode = true;
  QSet<Qt::DayOfWeek> days;
  int hour = 0, minute = 0;

  bool cronMode = false;
  QString cron;

  int executionMode = 0;

  QDateTime lastRun, lastFinished;
  QString lastStatus;
};

class SchedulerStore : public QObject {
public:
  QList<Schedule> schedules() const;
  void add(const Schedule &);
  void update(const Schedule &);
  void remove(const QUuid &id);
  QDateTime nextRun(const QUuid &id) const;   // ใช้ qcron ที่มีอยู่แล้ว
  bool load();  bool save();
signals:
  void changed();
  void due(const QUuid &scheduleId);
};
```

**ตัดสินใจไปพร้อมกัน:** เปลี่ยน `scheduler.conf` เป็น JSON พร้อม schema version
เพราะถ้าไม่เปลี่ยนตอนนี้ จะมีสองทางเขียนไฟล์เดียวกันคนละแบบ · ต้องอ่านของเดิมได้ (migration)

**เสร็จเมื่อ:** ตารางเวลาเด้งทำงานได้โดยไม่มีหน้าต่าง และไฟล์เก่าอ่านขึ้นครบ

---

### S5 · Remote registry

```cpp
// L1
class RemoteRegistry : public QObject {
public:
  struct Remote { QString name, type; };
  QList<Remote> remotes() const;
  void refresh();                       // rclone listremotes --long
signals:
  void changed();
};
```

เล็กและไม่ขึ้นกับใคร · รวมกับ `RcloneCapabilityRegistry` ที่มีอยู่แล้วได้ทันที
**เสร็จเมื่อ:** `ui.remotes` เป็นแค่ตัวแสดงผลของ registry

---

### S6 · Settings

ปัญหาไม่ใช่ที่ `QSettings` แต่คือ **ทุกไฟล์เรียก `GetSettings()` เองแล้วอ่านคีย์ดิบๆ**
API จะต้องรู้ว่าคีย์ไหนเปิดให้แก้ได้ ซึ่งตอนนี้ไม่มีที่ไหนบอก

```cpp
// L1
class SettingsFacade {
public:
  // คีย์ที่ API แก้ได้ -- นอกรายการนี้อ่านได้อย่างเดียว
  static bool isWritableFromApi(const QString &key);
  // ห้ามเด็ดขาด: Settings/rclone, queueScript, transferOnScript, transferOffScript
};
```

**เสร็จเมื่อ:** มีรายการคีย์ที่แก้ได้อยู่ที่เดียว พร้อม test ที่ยืนยันว่า path ของ rclone และ script ไม่อยู่ในนั้น

---

### S10 · Mounts / Streams

mount และ stream คือ `RunningJob` คนละชนิด ไม่ใช่ระบบแยก — ทำหลัง S2 แล้วจะเหลือแค่:
- `MountWidget` / `StreamWidget` เลิกถือ `QProcess`
- unmount ผ่าน RC `core/quit` ย้ายเข้า `RunningJob::stop()` ตามชนิด
- **stream ไม่เปิดทาง API** (สั่ง player บนเครื่องที่รันแอป ไม่มีความหมายจากเบราว์เซอร์)

---

#### ความคืบหน้า (2026-08-07)

| ขั้น | สถานะ |
|---|---|
| `JobOptions::getMountOptions()` (L1) — ย้าย 73 บรรทัดออกจาก `MainWindow::runItem` | ✅ |
| `SplitExtraOptions()` ใช้ร่วมกันระหว่าง transfer กับ mount | ✅ เดิมเป็น regex คนละตัวที่ต่างกันแค่ capture group |
| **VIO-1 ปิดสนิท** — ไม่มี `LAYER:` marker เหลือในโค้ดแล้ว | ✅ |
| test ของ args ของ mount (6 เคส ไม่ต้องใช้ rclone) | ✅ |
| mount เดินทาง `RunningJob` | ✅ `mount_widget.cpp` 428 → 310 บรรทัด · `addNewMount` −107 |
| `stop()` ของ mount = unmount ไม่ใช่ kill · `stopFailed` เมื่อ unmount ไม่สำเร็จ | ✅ |
| mount script ย้ายเข้า `RunningJob` (headless ก็ต้องรัน) | ✅ |
| regex อ่าน RC port ของ mount ที่เขียนซ้ำ → ใช้ `ParseRcServingPort` (L0) ตัวเดียว | ✅ |
| stream เดินทาง `RunningJob` | ❌ **ไม่ทำ** ดูเหตุผลข้างล่าง |

**mount ย้ายเสร็จแล้ว (2026-08-07)** — สิ่งที่เจอระหว่างทาง:

| เจอ | รายละเอียด |
|---|---|
| `mount_widget.cpp` มี regex อ่าน RC port **ของตัวเอง** | `^.+Serving\sremote\scontrol\son\s\S+$` ทั้งที่ `ParseRcServingPort()` อยู่ใน L0 และมี test อยู่แล้ว — เศษของ VIO-2 ที่ตกค้าง |
| `waitForStarted()` หมุน event loop | บรรทัดแรกๆ ของ output อาจถูก emit ก่อนที่ตัวแสดงผลจะต่อ signal ทัน · เอาออกแล้ว ใช้ `errorOccurred` แทนซึ่งไม่ต้องรอ |
| mount ห้ามได้ `--rc-addr=localhost:0` | port ของ mount อยู่ใน task เพราะการ unmount บน Windows ต้องยิงไปที่ port เดิมทุกครั้ง — ไม่มีใครอ่านประกาศย้อนหลังได้หลังรีสตาร์ท · ใส่เพิ่มจะทำให้ rclone ไม่ยอมเริ่ม |

**ทำไม stream ไม่ย้าย** — การย้ายนี้**ทดสอบอัตโนมัติไม่ได้เลยในเครื่องนี้**
mount ต้องมี WinFsp + remote จริง + drive letter ว่าง ต่างจาก transfer ที่
`test_job_registry` รัน rclone จริงได้ · ความเสี่ยงจึงลดด้วย test ไม่ได้ ต้องให้คนลองเท่านั้น
`RunningJob` แตกพฤติกรรมตาม kind แล้ว:

```
Transfer : + --rc --rc-addr=localhost:0 · poll stats · capture output · log
Mount    : args พก --rc-addr ของตัวเองมา (หรือไม่มีเลย) · ไม่ poll stats
           · capture output · log · รัน script เมื่อ RC ขึ้น
           · stop() = unmount ไม่ใช่ kill → ล้มเหลวได้ → stopFailed
```

`rclone rc core/quit` (Windows) · `umount` (macOS/FreeBSD) · `fusermount -u` (อื่นๆ)

⚠️ **ทดสอบอัตโนมัติในเครื่องนี้ไม่ได้** — mount ต้องมี WinFsp + remote จริง + drive letter
test ที่เขียนได้ครอบแค่ *รูปร่าง* ของ job (ไม่มี `--rc-addr` ซ้ำ · จบเป็น error ได้ · คำว่า
`unmounted` ต่างจาก `finished`) ไม่ใช่ว่า filesystem ขึ้นจริง — **V-20 ต้องมีคนลอง**

**stream อาจไม่ควรย้ายเลย** — `rclone cat` ต่อท่อเข้า player ด้วย
`setStandardOutputProcess()` แปลว่า **สองโปรเซส** ไม่มี output ของตัวเอง ไม่มี stats ไม่มี log
ถ้ายัดเข้า `RunningJob` จะได้ branch ที่สามใน L1 เพื่อแลกกับอะไรที่ API ใช้ไม่ได้อยู่ดี
(§6 ตัดสินไปแล้วว่า stream ไม่เปิดทาง API เพราะสั่ง player บนเครื่องที่รันแอป)
**ข้อเสนอ: ปล่อย stream ไว้ตามเดิม** แล้วบันทึกเหตุผลไว้ ไม่ใช่ทำครึ่งๆ

---

### S13 · Store + ประวัติการรัน

รายละเอียดเต็มอยู่ใน [`PLAN.md` §6.8](PLAN.md) — สรุปสิ่งที่กระทบทะเบียนนี้:

> **ทำเสร็จ 2026-08-09** · schema v1 = `meta` `job_run` · v2 = `task` `queue_entry` `schedule`
> `Database` เปิด WAL, busy timeout, และหนึ่งการเชื่อมต่อต่อเธรดตามข้อบังคับของ QtSql
>
> **task เก็บ 45 ฟิลด์เป็น JSON หนึ่งคอลัมน์** ส่วนที่ใช้ค้น (name/operation/source/dest)
> ยกออกมาเป็นคอลัมน์ · **schedule เก็บ argument list ของ scheduler ตามเดิมเป็น JSON**
> ไม่แตกเป็นคอลัมน์ เพราะรูปร่างของมันเป็นความรู้ของ scheduler ไม่ใช่ของ store
>
> **ไม่มีไดรเวอร์ SQLite = ยังทำงานได้** อ่าน/เขียน `tasks.bin` และ `.conf` แบบเดิม
> เสียแค่ประวัติ — ไม่ใช่ task

- `task` `queue_entry` `schedule` `job_run` อยู่ใน SQLite ไฟล์เดียว แทนรูปแบบไฟล์ทำมือ 3 แบบ
- **ประวัติการรันคือของใหม่ทั้งหมด** ตอนนี้ไม่มีที่ไหนเก็บเลย ปิดโปรแกรมแล้วหายหมด
- log ยังเป็นไฟล์ DB เก็บแค่ตัวชี้ — และนั่นคือคำตอบของ `REVISIT` เรื่องชื่อไฟล์ log
- **`Qt6::Sql` เข้า `rbcore` แล้ว** (ไม่ใช่ GUI) · [`ARCHITECTURE.md`](ARCHITECTURE.md) แก้ตามแล้ว
  · ไดรเวอร์เป็นแพ็กเกจแยกบน Alpine/Debian จึงเพิ่มเข้า CI และ Dockerfile ด้วย
- ⚠️ หน้าต่างกับ `--run-task` เขียน DB พร้อมกันได้ (เพราะ E1 จงใจไม่ล็อก) → ต้อง WAL + busy timeout

หลังทำเสร็จ `S3` และ `S4` จะเหลือแค่ตรรกะ ไม่ต้องเขียนโค้ดจัดการไฟล์ของตัวเอง

---

### S14 · Extension เฉพาะ backend

รายละเอียดเต็มอยู่ใน [`PLAN.md` §6.9](PLAN.md) — สรุปสิ่งที่กระทบทะเบียนนี้:

- **core ห้ามรู้จักชื่อ backend** เหมือนที่ห้ามรู้จัก widget · `RcloneCapabilities` ถามความสามารถ
  แบบทั่วไปได้อยู่แล้ว แต่ endpoint ของ teldrive เป็นความรู้เฉพาะตัว
- `RemoteExtension` เป็น interface ที่ไม่มีอะไรเกี่ยวกับ widget → CLI และ API เรียกได้เท่าหน้าต่าง
- โมดูลตอนคอมไพล์ (`-DRB_EXT_TELDRIVE`) ไม่ใช่ปลั๊กอินตอนรัน — linker บังคับขอบเขตได้เหมือน `rbcore`
  โดยไม่ต้องแบก ABI/การโหลด/ความปลอดภัยของปลั๊กอิน
- 🔴 **access token ของ teldrive เป็น credential** ต้องอยู่ใต้กฎข้อ 5 เต็มที่
  และเอามาจาก `rclone config show` ไม่ใช่แกะ `rclone.conf` เอง (ไฟล์อาจถูกเข้ารหัส)

**สิ่งที่ API (S11) ต้องเผื่อไว้:** `POST /api/v1/remotes/{name}/check` จะเป็นของ extension
ไม่ใช่ของ core — endpoint มีอยู่ก็ต่อเมื่อ build มี extension ที่รับ remote ชนิดนั้น

---

### S11 · HTTP API · S12 · Web UI

เริ่มได้เมื่อ S1–S6 และ S10 เสร็จ · รายละเอียด endpoint อยู่ใน §11
รีโปอ้างอิงของ Web UI อยู่ใน [`ARCHITECTURE.md` §7](ARCHITECTURE.md)

---

### กติกาของงานชุดนี้

1. **หนึ่งระบบ = หนึ่ง commit** ที่ build ผ่าน test ผ่าน และแอปยังใช้งานได้เหมือนเดิม
2. **ห้ามเปลี่ยนพฤติกรรมที่ผู้ใช้เห็นระหว่างย้าย** — ถ้าอยากปรับ UI ให้แยก commit
3. **ทุกระบบต้องมี test ที่ลิงก์แค่ `rbcore`** ก่อนถือว่าเสร็จ — ถ้าเขียน test ไม่ได้โดยไม่มี widget แปลว่ายังย้ายไม่เสร็จ
4. **อัปเดตช่องสถานะในตารางข้างบน** พร้อม commit นั้น
5. ระหว่างทางเจอ `CORE:` / `LAYER:` ให้ทิ้ง marker ไว้ตามเดิม อย่าแวะแก้
## 13. ความปลอดภัย — เงื่อนไขบังคับ

| เรื่อง | ทำไม |
|---|---|
| auth ตั้งแต่ endpoint แรก | ไม่ใช่ของเติมทีหลัง |
| bind `127.0.0.1` เป็นค่าเริ่มต้น | เปิดออกเน็ตต้องเป็นการตัดสินใจของผู้ใช้ |
| ทุก response ผ่าน `RedactArgs()` | args มี `--rc-pass` และ token ของ backend |
| ห้ามแก้ path ของ rclone และ script ผ่าน API | = สั่งรัน binary อะไรก็ได้ |
| ต้องมี test ที่ scan หา `mArgs.join` แบบดิบ | ตามที่ [`ARCHITECTURE.md` §5](ARCHITECTURE.md) วางไว้ |
