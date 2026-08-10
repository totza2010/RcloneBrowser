#include "database.h"
#include "schedule.h"
#include "scheduler_store.h"
#include "utils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

// A schedule was 22 fields inside a widget and a comma-separated line on
// disk. The line is what is still stored (S13 moved it without reinterpreting
// it), and this is the reading of it -- so the round trip has to be exact,
// including the base64 the file has always used. See docs/API.md S4.
class TestSchedulerStore : public QObject {
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

  // The real thing, taken from the user's database on 2026-08-10: a daily
  // schedule saved by the widget, values and all.
  static QStringList realArgs() {
    return {"mSchedulerId",
            "{4d335bf6-6c4b-405d-ab44-7931cbc3f32f}",
            "mSchedulerName",
            "U2NoZWR1bGVkIHRhc2s6IF90bXBfMTBBdWcyMDI2XzE1MjI0M19PbmVEcml2ZQ==",
            "mTaskId",
            "{ddf56734-dc27-48a9-b6ac-cde42bcd4bed}",
            "mTaskName",
            "X3RtcF8xMEF1ZzIwMjZfMTUyMjQzX09uZURyaXZl",
            "mLastRun",
            "bmV2ZXI=",
            "mRequestId",
            "",
            "mLastRunFinished",
            "",
            "mLastRunStatus",
            "",
            "mSchedulerStatus",
            "paused",
            "mDailyState",
            "true",
            "mDailyMon",
            "true",
            "mDailyTue",
            "true",
            "mDailyWed",
            "true",
            "mDailyThu",
            "true",
            "mDailyFri",
            "true",
            "mDailySat",
            "true",
            "mDailySun",
            "true",
            "mDailyHour",
            "00",
            "mDailyMinute",
            "00",
            "mCronState",
            "false",
            "mCron",
            "MzAgNiwxOCAqICogTU9OLUZSSQ==",
            "mExecutionMode",
            "0"};
  }

  std::unique_ptr<QTemporaryDir> mScratch;
  int mCounter = 0;

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
    Database::closeForThread();
    QFile::remove(iniPath());
  }

  void init() {
    ++mCounter;
    Database::closeForThread();
    Database::setPath(
        QDir(appDir()).filePath(QStringLiteral("sched%1.db").arg(mCounter)));
    QFile::remove(Database::path());
    QVERIFY2(Database::connection().isOpen(),
             qPrintable(Database::lastError()));
  }

  void cleanup() {
    const QString path = Database::path();
    Database::closeForThread();
    QFile::remove(path);
  }

  void readsWhatTheWidgetWrote() {
    const Schedule schedule = Schedule::fromArgs(realArgs());

    QCOMPARE(schedule.id,
             QStringLiteral("{4d335bf6-6c4b-405d-ab44-7931cbc3f32f}"));
    QCOMPARE(schedule.taskId,
             QStringLiteral("{ddf56734-dc27-48a9-b6ac-cde42bcd4bed}"));
    QCOMPARE(schedule.name, QStringLiteral(
                                "Scheduled task: _tmp_10Aug2026_152243_OneDrive"));
    QCOMPARE(schedule.taskName,
             QStringLiteral("_tmp_10Aug2026_152243_OneDrive"));
    QCOMPARE(schedule.active, false); // "paused"
    QCOMPARE(schedule.dailyMode, true);
    QCOMPARE(schedule.hour, 0);
    QCOMPARE(schedule.minute, 0);
    QCOMPARE(schedule.cronMode, false);

    // Kept even though it is not in use -- it is what the user typed.
    QCOMPARE(schedule.cron, QStringLiteral("30 6,18 * * MON-FRI"));
    QCOMPARE(schedule.executionMode, 0);
    QCOMPARE(schedule.lastRun, QStringLiteral("never"));
  }

  // Anything this does not preserve is a setting that silently reverts.
  void writingItBackProducesTheSameThing() {
    const QStringList original = realArgs();
    const QStringList round = Schedule::fromArgs(original).toArgs();

    // Written in a fixed order, so compare by key rather than by position.
    for (int i = 0; i < original.size(); i += 2) {
      const QString key = original[i];
      const int at = round.indexOf(key);
      QVERIFY2(at != -1, qPrintable(QStringLiteral("lost key %1").arg(key)));
      QCOMPARE(round.at(at + 1), original[i + 1]);
    }
    QCOMPARE(round.size(), original.size());
  }

  void aNameWithACommaSurvives() {
    Schedule schedule;
    schedule.id = QStringLiteral("s1");
    schedule.taskId = QStringLiteral("t1");
    schedule.name = QStringLiteral("nightly, then weekly");
    schedule.taskName = QStringLiteral("ผ่าพิภพไททัน");

    const Schedule round = Schedule::fromArgs(schedule.toArgs());
    QCOMPARE(round.name, schedule.name);
    QCOMPARE(round.taskName, schedule.taskName);
  }

  void aDailyScheduleIsDueOnlyOnItsDaysAndMinute() {
    Schedule schedule;
    schedule.dailyMode = true;
    schedule.hour = 7;
    schedule.minute = 30;
    schedule.saturday = false;
    schedule.sunday = false;

    // Monday 2026-08-10.
    const QDate monday(2026, 8, 10);
    QVERIFY(schedule.isDue(QDateTime(monday, QTime(7, 30))));
    QVERIFY(schedule.isDue(QDateTime(monday, QTime(7, 30, 45)))); // seconds
    QVERIFY(!schedule.isDue(QDateTime(monday, QTime(7, 31))));
    QVERIFY(!schedule.isDue(QDateTime(monday, QTime(8, 30))));

    // Saturday is not ticked.
    QVERIFY(!schedule.isDue(QDateTime(QDate(2026, 8, 15), QTime(7, 30))));
  }

  void nextRunSkipsTheDaysThatAreNotTicked() {
    Schedule schedule;
    schedule.hour = 7;
    schedule.minute = 0;
    schedule.saturday = false;
    schedule.sunday = false;

    // Friday 09:00 -> the next one is Monday, not Saturday.
    const QDateTime friday(QDate(2026, 8, 14), QTime(9, 0));
    QCOMPARE(schedule.nextRun(friday),
             QDateTime(QDate(2026, 8, 17), QTime(7, 0)));

    // Earlier the same day still counts as today.
    const QDateTime fridayEarly(QDate(2026, 8, 14), QTime(6, 0));
    QCOMPARE(schedule.nextRun(fridayEarly),
             QDateTime(QDate(2026, 8, 14), QTime(7, 0)));
  }

  void aScheduleWithNoDayNeverComesDue() {
    Schedule schedule;
    schedule.monday = schedule.tuesday = schedule.wednesday = false;
    schedule.thursday = schedule.friday = false;
    schedule.saturday = schedule.sunday = false;

    QVERIFY(!schedule.nextRun(QDateTime::currentDateTime()).isValid());
    QVERIFY(!schedule.isDue(QDateTime(QDate(2026, 8, 10), QTime(0, 0))));
  }

  void aCronScheduleUsesTheExpression() {
    Schedule schedule;
    schedule.dailyMode = false;
    schedule.cronMode = true;
    schedule.cron = QStringLiteral("30 6 * * *");

    QVERIFY(schedule.isDue(QDateTime(QDate(2026, 8, 10), QTime(6, 30))));
    QVERIFY(!schedule.isDue(QDateTime(QDate(2026, 8, 10), QTime(6, 31))));
  }

  void anUnreadableCronNeverFires() {
    Schedule schedule;
    schedule.dailyMode = false;
    schedule.cronMode = true;
    schedule.cron = QStringLiteral("not a cron expression");

    QVERIFY(!schedule.isDue(QDateTime::currentDateTime()));
    QVERIFY(!schedule.nextRun(QDateTime::currentDateTime()).isValid());
  }

  void onlyActiveSchedulesComeDue() {
    SchedulerStore &store = SchedulerStore::instance();
    store.load();
    for (const Schedule &existing : store.schedules()) {
      store.remove(existing.id);
    }

    Schedule paused;
    paused.id = QStringLiteral("paused");
    paused.taskId = QStringLiteral("t1");
    paused.hour = 7;
    paused.active = false;

    Schedule armed = paused;
    armed.id = QStringLiteral("armed");
    armed.active = true;

    store.add(paused);
    store.add(armed);

    const QDateTime at(QDate(2026, 8, 10), QTime(7, 0));
    const QList<Schedule> ready = store.due(at);
    QCOMPARE(ready.size(), 1);
    QCOMPARE(ready.first().id, QStringLiteral("armed"));

    // Asked again in the same minute, it must not fire twice -- whoever is
    // watching may well be asking every second.
    QVERIFY(store.due(at.addSecs(30)).isEmpty());

    // The next day it is due again.
    QCOMPARE(store.due(at.addDays(1)).size(), 1);
  }

  void schedulesSurviveARestart() {
    SchedulerStore &store = SchedulerStore::instance();
    store.load();
    for (const Schedule &existing : store.schedules()) {
      store.remove(existing.id);
    }

    store.add(Schedule::fromArgs(realArgs()));
    QCOMPARE(store.count(), 1);

    store.load(); // what a restart amounts to

    QCOMPARE(store.count(), 1);
    const Schedule &back = store.schedules().first();
    QCOMPARE(back.id, QStringLiteral("{4d335bf6-6c4b-405d-ab44-7931cbc3f32f}"));
    QCOMPARE(back.cron, QStringLiteral("30 6,18 * * MON-FRI"));
    QCOMPARE(back.name,
             QStringLiteral("Scheduled task: _tmp_10Aug2026_152243_OneDrive"));
  }
};

QTEST_MAIN(TestSchedulerStore)
#include "test_scheduler_store.moc"
