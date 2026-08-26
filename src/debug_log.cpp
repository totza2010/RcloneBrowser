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
Q_LOGGING_CATEGORY(rbSched, "rb.sched")
Q_LOGGING_CATEGORY(rbScript, "rb.script")
Q_LOGGING_CATEGORY(rbRemote, "rb.remote")
Q_LOGGING_CATEGORY(rbTask, "rb.task")
Q_LOGGING_CATEGORY(rbJob, "rb.job")
Q_LOGGING_CATEGORY(rbDb, "rb.db")
Q_LOGGING_CATEGORY(rbApp, "rb.app")

namespace {

QMutex gMutex; // qDebug can be called from any thread
QtMessageHandler gPrevious = nullptr;
bool gEnabled = false;

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

// One file, kept open, that makes room for itself when it gets too big.
//
// Rotating rather than starting a new file per run is what makes the name
// stable: "the queue log" is always queue.txt, and the older ones are behind
// it in order. A file named after the moment the program happened to start
// is a file you have to go looking for.
class Sink {
public:
  explicit Sink(const QString &base) : mBase(base) { open(); }

  ~Sink() {
    mStream.flush();
    mFile.close();
  }

  void write(const QString &line) {
    if (!mFile.isOpen()) {
      return;
    }
    const qint64 size = line.toUtf8().size() + 1;
    if (mBytes + size > maxBytes()) {
      rotate();
    }
    mStream << line << Qt::endl;
    mBytes += size;
  }

  QString path() const { return mFile.fileName(); }

private:
  static qint64 maxBytes() {
    return static_cast<qint64>(AppSettings::logMaxFileKb()) * 1024;
  }

  // Each subsystem gets a folder of its own, because with ten rotations
  // apiece a single directory becomes seventy files that have to be read
  // before the interesting one can be picked out. One folder, one subject.
  QString dirPath() const {
    return QDir(DebugLog::logDir()).filePath(mBase);
  }

  QString pathFor(int generation) const {
    QDir dir(dirPath());
    return generation < 0
               ? dir.filePath(QStringLiteral("%1.txt").arg(mBase))
               : dir.filePath(QStringLiteral("%1.%2.txt").arg(mBase).arg(
                     generation));
  }

  void open() {
    QDir().mkpath(dirPath());
    mFile.setFileName(pathFor(-1));
    // Appended, not truncated: a program that is restarted twice in a minute
    // must not throw away what it said the first time.
    if (!mFile.open(QIODevice::WriteOnly | QIODevice::Text |
                    QIODevice::Append)) {
      return;
    }
    mStream.setDevice(&mFile);
    mBytes = mFile.size();

    mStream << "# rclone-browser " << mBase << " log, opened "
            << QDateTime::currentDateTime().toString(Qt::ISODate) << Qt::endl
            << "# passwords and tokens are removed; paths and remote names"
               " are not"
            << Qt::endl;
    mStream.flush();
    mBytes = mFile.size();
  }

  void rotate() {
    mStream.flush();
    mFile.close();

    const int keep = AppSettings::logKeepFiles();
    if (keep <= 0) {
      // Nothing is kept, so there is nothing to shift along.
      QFile::remove(pathFor(-1));
    } else {
      QFile::remove(pathFor(keep - 1));
      for (int i = keep - 2; i >= 0; --i) {
        if (QFile::exists(pathFor(i))) {
          QFile::rename(pathFor(i), pathFor(i + 1));
        }
      }
      QFile::rename(pathFor(-1), pathFor(0));
    }

    mBytes = 0;
    open();
  }

  QString mBase;
  QFile mFile;
  QTextStream mStream;
  qint64 mBytes = 0;
};

// The combined file first, then one per subsystem.
//
// Both, on purpose. Splitting alone would have made the queue and scheduler
// bugs harder to find rather than easier: what settled them was following
// one request id from rb.sched to rb.queue to rb.job, and that story only
// exists where the lines are in one order. The split files are for reading a
// subsystem on its own; the combined one is for reading what happened.
Sink *gAll = nullptr;
QHash<QString, Sink *> gBySubsystem;

// Which file a category belongs in. Anything unrecognised -- Qt's own
// categories included -- goes to the application file rather than being
// dropped, because a warning from Qt is often the answer.
QString subsystemFor(const char *category) {
  const QLatin1String name(category != nullptr ? category : "default");
  if (name == QLatin1String("rb.queue")) {
    return QStringLiteral("queue");
  }
  if (name == QLatin1String("rb.sched")) {
    return QStringLiteral("scheduler");
  }
  if (name == QLatin1String("rb.script")) {
    return QStringLiteral("scripts");
  }
  if (name == QLatin1String("rb.remote")) {
    return QStringLiteral("remotes");
  }
  if (name == QLatin1String("rb.task")) {
    return QStringLiteral("tasks");
  }
  if (name == QLatin1String("rb.job")) {
    return QStringLiteral("jobs");
  }
  if (name == QLatin1String("rb.db")) {
    return QStringLiteral("database");
  }
  return QStringLiteral("app");
}

void closeAll() {
  delete gAll;
  gAll = nullptr;
  qDeleteAll(gBySubsystem);
  gBySubsystem.clear();
}

void handler(QtMsgType type, const QMessageLogContext &context,
             const QString &message) {
  {
    QMutexLocker lock(&gMutex);
    if (gAll != nullptr) {
      const QString category =
          QString::fromLatin1(context.category ? context.category : "default");
      const QString line =
          QStringLiteral("%1 %2 %3: %4")
              .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                   QString::fromLatin1(levelName(type)), category,
                   scrub(message));

      gAll->write(line);

      const QString subsystem = subsystemFor(context.category);
      Sink *sink = gBySubsystem.value(subsystem);
      if (sink == nullptr) {
        sink = new Sink(subsystem);
        gBySubsystem.insert(subsystem, sink);
      }
      sink->write(line);
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
  return gAll != nullptr ? gAll->path() : QString();
}

void DebugLog::install() {
  // The environment wins over the setting, so a run can be traced without
  // changing anything the user would have to remember to change back. The
  // setting is the normal way in: tick the box, and every run from then on
  // writes a log with no special command involved.
  const QByteArray fromEnvironment = qgetenv("RB_DEBUG");
  const bool wanted =
      fromEnvironment == "1" || fromEnvironment.toLower() == "true"
          ? true
          : AppSettings::debugLog();
  setEnabled(wanted);
}

void DebugLog::setEnabled(bool on) {
  QMutexLocker lock(&gMutex);
  if (on == gEnabled) {
    return;
  }

  if (!on) {
    // Put the previous handler back before closing anything, or a message
    // arriving in between would be written to a file that has just gone.
    if (gPrevious != nullptr) {
      qInstallMessageHandler(gPrevious);
      gPrevious = nullptr;
    }
    closeAll();
    gEnabled = false;
    return;
  }

  QDir dir(logDir());
  if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
    return;
  }

  gAll = new Sink(QStringLiteral("all"));

  // Everything the application has to say about itself. Qt's own categories
  // are left alone: turning those on buries the interesting lines.
  QLoggingCategory::setFilterRules(QStringLiteral("rb.*=true"));

  gPrevious = qInstallMessageHandler(handler);
  gEnabled = true;
}

void DebugLog::setEnabledForNextRun(bool on) {
  AppSettings::setDebugLog(on);

  // And take effect now, so that ticking the box and reproducing the problem
  // is one step rather than two. Turning it off closes the files, which is
  // what somebody who has just unticked it expects to be able to delete.
  setEnabled(on);
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

  // Only the rotated ones, and only by age. A subsystem's current file is
  // never a candidate however old it looks -- deleting that would take away
  // the log somebody is watching.
  const QStringList patterns{QStringLiteral("*.[0-9].txt"),
                             QStringLiteral("*.[0-9][0-9].txt")};

  // One folder per subsystem now, so look in each of them; and in logs/
  // itself for the files the older scheme left behind.
  QStringList places{dir.absolutePath()};
  for (const QString &sub :
       dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    places.append(dir.filePath(sub));
  }

  for (const QString &where : places) {
    QDir sub(where);
    QStringList wanted = patterns;
    if (where == dir.absolutePath()) {
      // The one-file-per-run scheme this replaced.
      wanted.append(QStringLiteral("debug-*.log"));
    }
    for (const QFileInfo &info : sub.entryInfoList(wanted, QDir::Files)) {
      if (info.lastModified() < cutoff &&
          QFile::remove(info.absoluteFilePath())) {
        ++removed;
      }
    }
  }
  return removed;
}
