#pragma once

// Core (L0): what has run, kept across restarts.
//
// The job card was the only place this ever existed, so closing the window --
// or the application -- destroyed the record of every transfer that had run.
// See docs/PLAN.md 6.8 (S13).
//
// The log files themselves stay on disk; only a pointer to them is stored, so
// a running job can keep appending to a file rather than to a row, and a
// fifty-megabyte log does not end up inside the database. What this fixes is
// that nothing said which log file belonged to which run.

#include <QList>
#include <QString>

struct JobRunRecord {
  QString requestId;
  QString taskId;   // empty for a job that was not started from a task
  QString taskName; // copied in: the task may be gone by the time this is read
  QString kind;         // "transfer" | "mount" | "stream"
  QString transferMode; // "task" | "queue" | "scheduler" | ...
  QString info;         // the one-line description shown on the card
  QString source;
  QString dest;

  // Milliseconds since the epoch, the same units QDateTime uses.
  qint64 startedAt = 0;
  qint64 finishedAt = 0; // 0 while running

  // "running" until it ends, then the words the rest of the application
  // already uses: "finished", "error", "stopped", "unmounted".
  QString state;
  int exitCode = -1;

  qint64 bytes = 0;
  qint64 totalBytes = 0;
  qint64 transfers = 0;
  qint64 errors = 0;

  QString logPath;
  qint64 logBytes = 0;

  // A row still marked "running" that no live job claims is one whose process
  // ended without saying so -- a crash, or a machine that was switched off.
  // Recorded as it stands rather than rewritten on startup, because a second
  // process may legitimately be running that job right now.
  bool isRunning() const { return state == QStringLiteral("running"); }
};

// TEST: (V-21) รัน transfer -> ปิดโปรแกรม -> เปิดใหม่: ประวัติต้องยังอยู่พร้อม
// ลิงก์ไปไฟล์ log · งานที่กด stop ต้องเป็น stopped ไม่ใช่ error · รัน --run-task
// จากเทอร์มินัลขณะเปิดหน้าต่างอยู่ ต้องได้แถวทั้งคู่โดยไม่ชนกัน
namespace RunHistory {

// Writes the row for a job that has just started. Returns false when there is
// no usable database; the caller carries on, because losing the history of a
// transfer is not a reason to refuse to run it.
bool recordStarted(const JobRunRecord &record);

// Fills in how it ended. Inserts the row if the start was never recorded, so
// an ending is never lost to a database that only became available later.
bool recordFinished(const JobRunRecord &record);

// Newest first.
QList<JobRunRecord> recent(int limit = 200);
QList<JobRunRecord> forTask(const QString &taskId, int limit = 50);

JobRunRecord find(const QString &requestId);
int count();

// How long history is kept. 0 for "keep everything", which is also what an
// unreadable setting falls back to being refused -- see the implementation.
int retentionDays();
int retentionRows();

// Drops rows older than keepDays and, beyond that, everything past the most
// recent keepRows. Either can be 0 to mean no limit. Returns how many rows
// went.
//
// The log file of every dropped row is deleted with it. Leaving them would
// recreate the exact problem this table was added to solve: a logs directory
// nothing has an index of.
int purge(int keepDays, int keepRows);

// Deletes every run that has ended, and its log file. A run that has not
// ended is left: it may be happening in another process right now, and its
// log is open. Returns how many rows went.
int clear();

} // namespace RunHistory
