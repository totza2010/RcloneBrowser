#include "app_settings.h"
#include "job_log.h"
#include "run_history.h"
#include "utils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

// Every one of these settings used to be read where it was needed, with its
// default written out beside it. The point of the facade is that the default
// and the rule for what counts as a usable value exist once -- so these tests
// are mostly about the values nobody thinks about: missing, and nonsense.
// See docs/API.md S6.
class TestAppSettings : public QObject {
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

  static void forget(const QString &key) {
    GetSettings()->remove(QStringLiteral("Settings/") + key);
  }

  static void set(const QString &key, const QVariant &value) {
    GetSettings()->setValue(QStringLiteral("Settings/") + key, value);
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
    QVERIFY2(IsPortableMode(),
             "portable mode did not take effect; this would rewrite the real "
             "user settings");
  }

  void cleanupTestCase() { QFile::remove(iniPath()); }

  void aSettingThatWasNeverWrittenHasADefault() {
    forget("logToFile");
    forget("logRetentionDays");
    forget("historyRetentionDays");
    forget("historyMaxRuns");

    QCOMPARE(AppSettings::logToFile(), AppSettings::Default::kLogToFile);
    QCOMPARE(AppSettings::logRetentionDays(),
             AppSettings::Default::kLogRetentionDays);
    QCOMPARE(AppSettings::historyRetentionDays(),
             AppSettings::Default::kHistoryRetentionDays);
    QCOMPARE(AppSettings::historyMaxRuns(),
             AppSettings::Default::kHistoryMaxRuns);
  }

  void zeroMeansNoLimitAndIsKept() {
    // Zero is a real answer -- "keep everything" -- and must not be mistaken
    // for "unset" and replaced by the default.
    set("logRetentionDays", 0);
    set("historyRetentionDays", 0);
    set("historyMaxRuns", 0);

    QCOMPARE(AppSettings::logRetentionDays(), 0);
    QCOMPARE(AppSettings::historyRetentionDays(), 0);
    QCOMPARE(AppSettings::historyMaxRuns(), 0);
  }

  void anImpossibleValueFallsBackRatherThanDeletingEverything() {
    // A negative retention compares as "older than any date" in one place and
    // "newer than any" in another. Refusing it here means neither has to
    // wonder.
    set("logRetentionDays", -1);
    set("historyRetentionDays", -30);
    set("historyMaxRuns", -5);

    QCOMPARE(AppSettings::logRetentionDays(),
             AppSettings::Default::kLogRetentionDays);
    QCOMPARE(AppSettings::historyRetentionDays(),
             AppSettings::Default::kHistoryRetentionDays);
    QCOMPARE(AppSettings::historyMaxRuns(),
             AppSettings::Default::kHistoryMaxRuns);
  }

  // The readers that had their own copy of these rules now go through the
  // facade. If one drifts back to reading the key itself, this fails.
  void theOldReadersGiveTheSameAnswer() {
    set("logToFile", false);
    set("logRetentionDays", 3);
    set("historyRetentionDays", 11);
    set("historyMaxRuns", 42);

    QCOMPARE(JobLogWriter::isEnabled(), false);
    QCOMPARE(JobLogWriter::retentionDays(), 3);
    QCOMPARE(RunHistory::retentionDays(), 11);
    QCOMPARE(RunHistory::retentionRows(), 42);

    set("logRetentionDays", -1);
    QCOMPARE(JobLogWriter::retentionDays(),
             AppSettings::Default::kLogRetentionDays);
  }

  void theQueueStateIsWrittenAsTheWordsTheFileHasAlwaysHeld() {
    AppSettings::setQueueIsRunning(true);
    QCOMPARE(GetSettings()->value("Settings/queueStatus").toString(),
             QStringLiteral("true"));
    QVERIFY(AppSettings::queueIsRunning());

    AppSettings::setQueueIsRunning(false);
    QCOMPARE(GetSettings()->value("Settings/queueStatus").toString(),
             QStringLiteral("false"));
    QVERIFY(!AppSettings::queueIsRunning());

    // Never set at all: a queue that has never been started is not running.
    forget("queueStatus");
    QVERIFY(!AppSettings::queueIsRunning());
  }

  void aScriptThatIsSwitchedOffIsNoScript() {
    set("queueScript", QStringLiteral("/usr/local/bin/done.sh"));
    set("queueScriptRun", false);

    // One thing for the caller to check instead of two, so there is no way to
    // check the path and forget the switch.
    QVERIFY(AppSettings::queueFinishedScript().isEmpty());

    set("queueScriptRun", true);
    QCOMPARE(AppSettings::queueFinishedScript(),
             QStringLiteral("/usr/local/bin/done.sh"));
  }

  void therclonePathRoundTrips() {
    AppSettings::setRclonePath(QStringLiteral("C:/tools/rclone.exe"));
    AppSettings::setRcloneConfPath(QStringLiteral("C:/tools/rclone.conf"));

    QCOMPARE(AppSettings::rclonePath(), QStringLiteral("C:/tools/rclone.exe"));
    QCOMPARE(AppSettings::rcloneConfPath(),
             QStringLiteral("C:/tools/rclone.conf"));

    // Not chosen yet is a state the application starts in, and is empty
    // rather than a guess at where rclone might be.
    forget("rclone");
    QVERIFY(AppSettings::rclonePath().isEmpty());
  }
};

QTEST_MAIN(TestAppSettings)
#include "test_app_settings.moc"
