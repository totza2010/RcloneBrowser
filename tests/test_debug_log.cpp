#include "app_settings.h"
#include "debug_log.h"
#include "utils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

// The debug log has to survive being read after the fact by somebody who was
// not there. That means two things it did not used to do: the file for a
// subsystem always has the same name, and it cannot grow without limit.
//
// Rotation is worth pinning because when it goes wrong it goes wrong
// silently -- the log is still being written, just not the part anybody
// wanted. See docs/ARCHITECTURE.md and debug_log.h.
class TestDebugLog : public QObject {
  Q_OBJECT

private:
  static QString appDir() {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return QCoreApplication::applicationDirPath();
#else
    return QDir(QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME")))
        .filePath("rclone-browser");
#endif
  }

  static QString iniPath() {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return QDir(appDir()).filePath(
        QFileInfo(QCoreApplication::applicationFilePath()).baseName() + ".ini");
#else
    return QDir(appDir()).filePath("rclone-browser.ini");
#endif
  }

  static QString logDir() { return DebugLog::logDir(); }

  // logs/<subject>/<subject>[.<generation>].txt
  static QString pathOf(const QString &subject, int generation = -1) {
    const QString name =
        generation < 0
            ? QStringLiteral("%1.txt").arg(subject)
            : QStringLiteral("%1.%2.txt").arg(subject).arg(generation);
    return QDir(QDir(logDir()).filePath(subject)).filePath(name);
  }

  static qint64 sizeOf(const QString &subject) {
    return QFileInfo(pathOf(subject)).size();
  }

  static bool exists(const QString &subject, int generation = -1) {
    return QFile::exists(pathOf(subject, generation));
  }

  static QString textOf(const QString &subject) {
    QFile f(pathOf(subject));
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll())
                                       : QString();
  }

  // Enough text to pass a 64 KB file several times over.
  static void saySomething(int lines) {
    const QString filler(400, QLatin1Char('x'));
    for (int i = 0; i < lines; ++i) {
      qCDebug(rbQueue) << "line" << i << filler;
    }
  }

  std::unique_ptr<QTemporaryDir> mScratch;

private slots:
  void initTestCase() {
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    mScratch.reset(new QTemporaryDir);
    QVERIFY(mScratch->isValid());
    qputenv("XDG_CONFIG_HOME", mScratch->path().toLocal8Bit());
    QVERIFY(QDir().mkpath(appDir()));
#endif
    QFile ini(iniPath());
    QVERIFY(ini.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ini.close();
    QVERIFY2(IsPortableMode(), "portable mode did not take effect");
  }

  void cleanupTestCase() {
    DebugLog::setEnabled(false);
    QFile::remove(iniPath());
  }

  void init() {
    DebugLog::setEnabled(false);
    QDir(logDir()).removeRecursively();
    QVERIFY(QDir().mkpath(logDir()));

    // 64 KB: small enough that a handful of lines rolls the file over, which
    // is the same reason the setting goes this low in the interface.
    GetSettings()->setValue("Settings/logMaxFileKb", 64);
    GetSettings()->setValue("Settings/logKeepFiles", 3);
    GetSettings()->sync();
  }

  void cleanup() { DebugLog::setEnabled(false); }

  // Off is off: nothing is written and no files appear, because most runs
  // have nothing to explain and should cost nothing.
  void nothingIsWrittenWhileItIsOff() {
    QVERIFY(!DebugLog::isEnabled());
    saySomething(50);
    QVERIFY(!exists("queue"));
    QVERIFY(!exists("all"));
    QVERIFY(DebugLog::filePath().isEmpty());
  }

  // A line goes to the file for its own subsystem and to the combined file.
  // Both, because following one run across subsystems needs them in one
  // order, and reading the queue on its own needs them apart.
  void everyLineIsWrittenTwiceOnPurpose() {
    DebugLog::setEnabled(true);
    QVERIFY(DebugLog::isEnabled());

    qCDebug(rbQueue) << "a queue line";
    qCDebug(rbSched) << "a scheduler line";
    qCDebug(rbJob) << "a job line";
    qCDebug(rbDb) << "a database line";
    qCDebug(rbApp) << "an application line";
    DebugLog::setEnabled(false); // flushes

    for (const char *subject :
         {"queue", "scheduler", "jobs", "database", "app"}) {
      QVERIFY2(exists(subject), subject);
    }

    const QString text = textOf(QStringLiteral("all"));
    QVERIFY(text.contains(QStringLiteral("a queue line")));
    QVERIFY(text.contains(QStringLiteral("a scheduler line")));
    QVERIFY(text.contains(QStringLiteral("an application line")));

    const QString queueText = textOf(QStringLiteral("queue"));
    QVERIFY(queueText.contains(QStringLiteral("a queue line")));
    QVERIFY2(!queueText.contains(QStringLiteral("a scheduler line")),
             "the queue file picked up another subsystem's line");
  }

  // The name of the current file never changes; the older ones queue up
  // behind it. Somebody looking for "the queue log" must not have to work
  // out which of several names is today's.
  void aFullFileIsRotatedAndTheNameStaysPut() {
    DebugLog::setEnabled(true);
    saySomething(400); // comfortably past 64 KB
    DebugLog::setEnabled(false);

    QVERIFY2(exists("queue"), "the current file went missing");
    QVERIFY2(exists("queue", 0), "nothing was rotated");

    // Tight on purpose. A bound of "about twice the limit" would pass while
    // the limit was being read wrongly -- which is exactly the kind of thing
    // that is then explained away by hand when a file turns up too big.
    // One line of slack, because the check happens before the line is added.
    const qint64 limit = 64 * 1024;
    const qint64 slack = 1024;
    QVERIFY2(sizeOf("queue") <= limit + slack,
             qPrintable(QStringLiteral("current file is %1 bytes, limit %2")
                            .arg(sizeOf("queue"))
                            .arg(limit)));
    QVERIFY2(QFileInfo(pathOf("queue", 0)).size() <= limit + slack,
             qPrintable(QStringLiteral("rotated file is %1 bytes, limit %2")
                            .arg(QFileInfo(pathOf("queue", 0)).size())
                            .arg(limit)));

    // And it is in its own folder, not loose beside the other subjects.
    QVERIFY2(!QFile::exists(QDir(logDir()).filePath("queue.txt")),
             "the queue log was written straight into logs/");
  }

  // Older than the number asked for is deleted rather than kept for ever.
  void onlyAsManyOldFilesAsAskedForAreKept() {
    DebugLog::setEnabled(true);
    saySomething(2000); // enough to rotate several times over
    DebugLog::setEnabled(false);

    QVERIFY(exists("queue", 0));
    QVERIFY(exists("queue", 1));
    QVERIFY(exists("queue", 2));
    QVERIFY2(!exists("queue", 3), "kept more files than logKeepFiles allows");
  }

  // Restarting twice in a minute must not throw away what was said the first
  // time. This is why the file is appended to rather than truncated.
  void switchingItOnAgainKeepsWhatWasAlreadyThere() {
    DebugLog::setEnabled(true);
    qCDebug(rbQueue) << "from the first run";
    DebugLog::setEnabled(false);

    DebugLog::setEnabled(true);
    qCDebug(rbQueue) << "from the second run";
    DebugLog::setEnabled(false);

    const QString text = textOf(QStringLiteral("queue"));
    QVERIFY2(text.contains(QStringLiteral("from the first run")),
             "the earlier run was overwritten");
    QVERIFY(text.contains(QStringLiteral("from the second run")));
  }

  // SECURITY: this file is the kind of thing people attach to a bug report.
  void credentialsDoNotReachTheFile() {
    DebugLog::setEnabled(true);
    qCDebug(rbJob) << "running rclone --drive-token=SECRET123 --verbose";
    qCDebug(rbJob) << "config: password: hunter2";
    DebugLog::setEnabled(false);

    const QString text = textOf(QStringLiteral("jobs"));
    QVERIFY2(!text.contains(QStringLiteral("SECRET123")), "a token got out");
    QVERIFY2(!text.contains(QStringLiteral("hunter2")), "a password got out");
    QVERIFY2(text.contains(QStringLiteral("--verbose")),
             "scrubbing ate the rest of the line");
  }

  // Below the floor the floor is used. A file smaller than its own header
  // would rotate on every line and keep nothing at all.
  void aSizeBelowTheFloorIsRaisedToIt() {
    GetSettings()->setValue("Settings/logMaxFileKb", 1);
    GetSettings()->sync();
    QCOMPARE(AppSettings::logMaxFileKb(), AppSettings::Default::kLogMinFileKb);

    DebugLog::setEnabled(true);
    saySomething(60);
    DebugLog::setEnabled(false);

    const QString text = textOf(QStringLiteral("queue"));
    QVERIFY2(!text.isEmpty(), "the current file kept nothing");
  }

  // Purging is by age and only of the rotated ones. Deleting the file a
  // subsystem is writing would take away the log somebody is watching.
  void purgingLeavesTheFileThatIsBeingWritten() {
    DebugLog::setEnabled(true);
    saySomething(400);
    DebugLog::setEnabled(false);
    QVERIFY(exists("queue", 0));

    // Make the rotated one look old.
    GetSettings()->setValue("Settings/logRetentionDays", 1);
    GetSettings()->sync();
    {
      QFile f(pathOf(QStringLiteral("queue"), 0));
      QVERIFY(f.open(QIODevice::ReadWrite));
      QVERIFY(f.setFileTime(QDateTime::currentDateTime().addDays(-5),
                            QFileDevice::FileModificationTime));
    }

    // Purging has to reach into the subject folders, not just logs/ itself.
    QVERIFY(DebugLog::purgeOldLogs() >= 1);
    QVERIFY2(!exists("queue", 0), "the old rotated file was kept");
    QVERIFY2(exists("queue"), "purging took the current file");
  }
};

QTEST_MAIN(TestDebugLog)
#include "test_debug_log.moc"
