#include "debug_log.h"
#include "app_settings.h"
#include "utils.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QRegularExpression>
#include <QTextStream>

Q_LOGGING_CATEGORY(rbQueue, "rb.queue")
Q_LOGGING_CATEGORY(rbJob, "rb.job")
Q_LOGGING_CATEGORY(rbDb, "rb.db")
Q_LOGGING_CATEGORY(rbApp, "rb.app")

namespace {

QMutex gMutex; // qDebug can be called from any thread
QFile gFile;
QTextStream gStream;
QtMessageHandler gPrevious = nullptr;
bool gEnabled = false;

// A single run should not be able to fill the disk, the same reasoning as
// the job log.
constexpr qint64 kMaxBytes = 20LL * 1024 * 1024;
qint64 gBytes = 0;

// SECURITY: a debug log is the kind of file people attach to a bug report,
// so it must not carry a credential out of the machine. Anything that looks
// like one is replaced by name (docs/ARCHITECTURE.md section 5).
//
// Paths and remote names do stay, because a log without them says nothing --
// that is worth saying out loud in the file itself rather than assuming
// everyone knows.
QString scrub(QString text) {
  static const QRegularExpression assigned(
      QStringLiteral("((?:pass|password|token|secret|key)[\"']?\\s*[=:]\\s*)"
                     "\"?([^\\s\"',}]+)"),
      QRegularExpression::CaseInsensitiveOption);
  text.replace(assigned, QStringLiteral("\\1***"));

  static const QRegularExpression flag(
      QStringLiteral("(--[\\w-]*(?:pass|token|secret|key)[\\w-]*)"
                     "(?:[= ])(\\S+)"),
      QRegularExpression::CaseInsensitiveOption);
  text.replace(flag, QStringLiteral("\\1=***"));

  return text;
}

const char *levelName(QtMsgType type) {
  switch (type) {
  case QtDebugMsg:
    return "debug";
  case QtInfoMsg:
    return "info";
  case QtWarningMsg:
    return "warning";
  case QtCriticalMsg:
    return "critical";
  case QtFatalMsg:
    return "fatal";
  }
  return "?";
}

void handler(QtMsgType type, const QMessageLogContext &context,
             const QString &message) {
  {
    QMutexLocker lock(&gMutex);
    if (gFile.isOpen() && gBytes < kMaxBytes) {
      const QString line =
          QStringLiteral("%1 %2 %3: %4")
              .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                   QString::fromLatin1(levelName(type)),
                   QString::fromLatin1(context.category ? context.category
                                                        : "default"),
                   scrub(message));
      gStream << line << Qt::endl;
      gBytes += line.toUtf8().size() + 1;

      if (gBytes >= kMaxBytes) {
        gStream << "# debug log truncated at " << kMaxBytes << " bytes"
                << Qt::endl;
      }
    }
  }

  // Whatever was handling messages before still gets them: on Windows that
  // is what puts them on the terminal when one is attached.
  if (gPrevious != nullptr) {
    gPrevious(type, context, message);
  }
}

} // namespace

QString DebugLog::logDir() {
  return GetConfigDir().filePath(QStringLiteral("logs"));
}

bool DebugLog::isEnabled() { return gEnabled; }

QString DebugLog::filePath() {
  QMutexLocker lock(&gMutex);
  return gFile.isOpen() ? gFile.fileName() : QString();
}

void DebugLog::setEnabled(bool on) {
  GetSettings()->setValue("Settings/debugLog", on);
}

void DebugLog::install() {
  // The environment wins over the setting, so a run can be traced without
  // changing anything the user would have to remember to change back.
  const QByteArray fromEnvironment = qgetenv("RB_DEBUG");
  const bool wanted =
      fromEnvironment == "1" || fromEnvironment.toLower() == "true"
          ? true
          : GetSettings()->value("Settings/debugLog", false).toBool();
  if (!wanted) {
    return;
  }

  QDir dir(logDir());
  if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
    return;
  }

  const QString stamp =
      QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
  gFile.setFileName(dir.filePath(QStringLiteral("debug-%1.log").arg(stamp)));
  if (!gFile.open(QIODevice::WriteOnly | QIODevice::Text |
                  QIODevice::Truncate)) {
    return;
  }
  gStream.setDevice(&gFile);

  gStream << "# rclone-browser debug log" << Qt::endl
          << "# started: "
          << QDateTime::currentDateTime().toString(Qt::ISODate) << Qt::endl
          << "# passwords and tokens are removed; paths and remote names are"
             " not"
          << Qt::endl
          << Qt::endl;
  gStream.flush();
  gBytes = gFile.size();

  // Everything the application has to say about itself. Qt's own categories
  // are left alone: turning those on buries the interesting lines.
  QLoggingCategory::setFilterRules(QStringLiteral("rb.*=true"));

  gPrevious = qInstallMessageHandler(handler);
  gEnabled = true;
}

int DebugLog::purgeOldLogs() {
  const int days = AppSettings::logRetentionDays();
  if (days == 0) {
    return 0; // keep everything, the same as the job logs
  }

  QDir dir(logDir());
  if (!dir.exists()) {
    return 0;
  }

  const QDateTime cutoff = QDateTime::currentDateTime().addDays(-days);
  int removed = 0;
  const QFileInfoList entries = dir.entryInfoList(
      QStringList() << QStringLiteral("debug-*.log"), QDir::Files);
  for (const QFileInfo &info : entries) {
    if (info.lastModified() < cutoff &&
        info.absoluteFilePath() != filePath() &&
        QFile::remove(info.absoluteFilePath())) {
      ++removed;
    }
  }
  return removed;
}
