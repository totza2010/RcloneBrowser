#pragma once

// Core (L0): the numbers rclone reports for a running job.
//
// These used to be scraped out of the human-readable output with ten regular
// expressions, one per rclone release that changed the wording. rclone's
// remote control returns the same figures as JSON through core/stats, which
// is a documented API rather than a display format, so it does not drift.
// See docs/ARCHITECTURE.md.

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

// One file currently being transferred, from the "transferring" array.
struct JobTransferItem {
  QString name;
  qint64 bytes = 0;
  qint64 size = -1; // -1 when the backend cannot report a size up front
  int percentage = 0;
  double speed = 0.0; // bytes per second
  qint64 etaSeconds = -1;

  QString speedText() const;
  QString etaText() const;

  // The figures describing this file, most important first.
  //
  // Backends that cannot size a file up front get no percentage and no ETA,
  // because both would be invented. They get the byte count instead, which is
  // the only thing actually known.
  QStringList progressParts() const;
  QString progressText() const;
};

// What a job is doing right now, as far as core/stats can tell.
//
// rclone reports no phase of its own, so this is inferred. The distinction
// that matters on screen is whether a percentage means anything yet: until
// rclone has finished counting, totalBytes is zero and every percentage is a
// fiction. That is what left the card reading 0% for a while and then jumping.
enum class JobPhase {
  Starting,     // nothing counted yet
  Scanning,     // listing and checking; no total to measure against
  Transferring, // a total is known and bytes are moving
  Finishing,    // everything counted has moved; rclone is closing up
};

// TEST: (V-11) รัน copy งานใหญ่ -> แถบ progress, speed, ETA, checks, transfers
// ต้องขยับตามจริงและ % ต้องไม่เกิน 100 · ปิด RC (rclone เก่า/port ไม่ว่าง) ต้อง
// fallback ไป regex เดิมได้โดยยังแสดงผลครบ
struct JobStats {
  qint64 bytes = 0;
  qint64 totalBytes = 0;
  qint64 checks = 0;
  qint64 totalChecks = 0;
  qint64 transfers = 0;
  qint64 totalTransfers = 0;
  qint64 deletes = 0;
  qint64 renames = 0;
  qint64 errors = 0;
  qint64 listed = 0;
  double speed = 0.0;         // bytes per second
  qint64 etaSeconds = -1;     // -1 when rclone reports null
  double elapsedSeconds = 0.0;
  bool fatalError = false;
  bool retryError = false;

  QList<JobTransferItem> transferring;

  bool valid = false;

  // Bounded to 0..100. rclone can briefly report more bytes than it first
  // estimated, which is how the old parser ended up showing over 100%.
  int percent() const;

  QString sizeText() const;      // "1.2 GiB / 4.5 GiB"
  QString speedText() const;     // "3.4 MiB/s"
  QString etaText() const;       // "1m23s" or "-"
  QString checksText() const;    // "12 / 40"
  QString transfersText() const; // "3 / 9"
  QString elapsedText() const;

  JobPhase phase() const;

  // What to say about the phase in words. Empty while transferring, where the
  // bar already says it better than a label can.
  QString phaseText() const;

  // The figures for the overall bar, most important first:
  // "42%", "1.2 GiB / 4.5 GiB", "3.4 MiB/s", "2m 3s left"
  //
  // Parts rclone cannot report yet are left out rather than shown as "-".
  // Kept as a list so a client with a narrow bar can drop from the end
  // instead of eliding through the middle of a figure.
  QStringList progressParts() const;
  QString progressText() const;

  static JobStats fromCoreStats(const QByteArray &json);
};

// Reads the port out of rclone's "Serving remote control on http://ADDR/"
// line. Returns 0 when the line is not that announcement.
//
// Letting rclone pick the port with --rc-addr localhost:0 and reading it back
// avoids having to reserve one up front, which would race with anything else
// on the machine between the check and the bind.
quint16 ParseRcServingPort(const QString &line);

// True for the log lines rclone emits about our own core/stats polling.
//
// At -vv and above rclone logs every remote-control request and its complete
// reply. Since the job card polls once a second, that is two long lines per
// second of our own making, which buries the log the user actually asked for.
// The reply is already on screen as the numbers on the card.
bool IsRcPollingNoise(const QString &line);

// Shared formatting so the widget and any future client agree.
QString FormatBytes(qint64 bytes);

// Exact, for a stopwatch: "1h5m3s". The seconds have to keep ticking or an
// elapsed counter looks stopped.
QString FormatSeconds(qint64 seconds);

// Rounded, for an estimate: "45s", "5m 3s", "2h 5m", "3d 4h". An ETA of two
// hours is not known to the second, and printing it that way only makes the
// figure jitter. Long jobs used to read "447h42m7s".
QString FormatEta(qint64 seconds);

// Joins the parts of a progress line with the separator both bars use.
QString JoinProgressParts(const QStringList &parts);
