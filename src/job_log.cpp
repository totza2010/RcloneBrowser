#include "job_log.h"
#include "app_settings.h"
#include "utils.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace {
// A single job should not be able to fill the disk. rclone at -vv on a large
// tree produces a line per file; this is generous for reading back what
// happened without being unbounded.
constexpr qint64 kMaxBytes = 50LL * 1024 * 1024;
} // namespace

QString JobLogWriter::logDir() {
  // A folder of its own. These used to sit loose in logs/ alongside the
  // application's own files, so a directory listing mixed "what rclone said
  // about one transfer" with "what the program was thinking" -- two things
  // that are read for completely different reasons.
  //
  // Runs recorded before this hold their full path in the history (S13), so
  // they still open from where they are; only new ones land here.
  return QDir(GetConfigDir().filePath(QStringLiteral("logs")))
      .filePath(QStringLiteral("transfers"));
}

QString JobLogWriter::legacyLogDir() {
  return GetConfigDir().filePath(QStringLiteral("logs"));
}

bool JobLogWriter::isEnabled() { return AppSettings::logToFile(); }

int JobLogWriter::retentionDays() {
  return AppSettings::logRetentionDays();
}

QString JobLogWriter::sanitizeForFileName(const QString &text) {
  QString out;
  out.reserve(text.size());
  for (const QChar &c : text) {
    if (c.isLetterOrNumber() && c.unicode() < 128) {
      out += c;
    } else if (c == QLatin1Char('-') || c == QLatin1Char('_')) {
      out += c;
    }
  }
  return out;
}

JobLogWriter::JobLogWriter() = default;

JobLogWriter::~JobLogWriter() {
  if (mFile.isOpen()) {
    mStream.flush();
    mFile.close();
  }
}

void JobLogWriter::begin(const QString &operation, const QString &jobId,
                         const QStringList &redactedArgs) {
  mOperation = sanitizeForFileName(operation);
  if (mOperation.isEmpty()) {
    mOperation = QStringLiteral("job");
  }
  mJobId = sanitizeForFileName(jobId).left(8);
  mHeaderArgs = redactedArgs;
  mBegun = true;
}

// REVISIT: the file name is "<timestamp>-<operation>-<short job id>.log",
// which sorts by time and says what ran, but nothing about which remote or
// path. Finding "last night's upload to tgdrive" still means opening files.
// Worth another look once the log has been lived with -- possibly a sidecar
// index, or the remote name in the name itself. Raised 2026-08-05.
void JobLogWriter::openFile() {
  if (mFile.isOpen() || !mBegun) {
    return;
  }

  QDir dir(logDir());
  if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
    return;
  }

  const QString stamp =
      QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
  QString name = QStringLiteral("%1-%2").arg(stamp, mOperation);
  if (!mJobId.isEmpty()) {
    name += QLatin1Char('-') + mJobId;
  }

  // Two jobs can start in the same second.
  QString path = dir.filePath(name + QStringLiteral(".log"));
  for (int suffix = 2; QFileInfo::exists(path) && suffix < 100; ++suffix) {
    path = dir.filePath(
        QStringLiteral("%1-%2.log").arg(name).arg(suffix));
  }

  mFile.setFileName(path);
  if (!mFile.open(QIODevice::WriteOnly | QIODevice::Text |
                  QIODevice::Truncate)) {
    return;
  }
  mStream.setDevice(&mFile);

  mStream << "# rclone-browser job log\n"
          << "# started: "
          << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n"
          // Already redacted by the caller; written here so the log says what
          // was actually run.
          << "# command: " << mHeaderArgs.join(QLatin1Char(' ')) << "\n\n";
  mStream.flush();
  mBytes = mFile.size();
}

void JobLogWriter::appendLine(const QString &line) {
  if (!mBegun || mTruncated) {
    return;
  }
  if (!mFile.isOpen()) {
    openFile();
    if (!mFile.isOpen()) {
      return;
    }
  }

  if (mBytes >= kMaxBytes) {
    mTruncated = true;
    mStream << "\n# log truncated at " << kMaxBytes
            << " bytes; the job carries on and its result is recorded below\n";
    mStream.flush();
    return;
  }

  mStream << line << '\n';

  // Bytes, not characters. QString::size() counts UTF-16 units, so a log full
  // of Thai or CJK file names counted about a third of what it was really
  // writing -- which made the size limit above roughly three times larger than
  // it says, and made the size recorded with the run wrong by the same amount.
  mBytes += line.toUtf8().size() + 1;
}

void JobLogWriter::finish(const QString &status) {
  if (!mFile.isOpen()) {
    return;
  }
  // Written even after truncation: how the job ended is the one line most
  // worth keeping.
  mStream << "\n# finished: "
          << QDateTime::currentDateTime().toString(Qt::ISODate) << " ("
          << status << ")\n";
  mStream.flush();

  // The running count is an estimate -- it cannot know about the newline
  // translation this file does on Windows. Once the last line is on disk the
  // real size is there for the asking, and that is the number recorded with
  // the run.
  mBytes = mFile.size();
  mFile.close();
}

int JobLogWriter::purgeOldLogs() {
  const int days = retentionDays();
  if (days == 0) {
    return 0; // keep everything
  }

  const QDateTime cutoff = QDateTime::currentDateTime().addDays(-days);
  int removed = 0;

  // Both places: the folder these are written to now, and the one they used
  // to sit in. A job log that was old enough to go yesterday is still old
  // enough today, and leaving them behind would mean the retention setting
  // quietly stopped applying to everything already on disk.
  for (const QString &where : {logDir(), legacyLogDir()}) {
    QDir dir(where);
    if (!dir.exists()) {
      continue;
    }
    const QFileInfoList entries =
        dir.entryInfoList(QStringList() << QStringLiteral("*.log"), QDir::Files);
    for (const QFileInfo &info : entries) {
      if (info.lastModified() < cutoff &&
          QFile::remove(info.absoluteFilePath())) {
        ++removed;
      }
    }
  }
  return removed;
}
