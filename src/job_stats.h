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
QString FormatSeconds(qint64 seconds);
