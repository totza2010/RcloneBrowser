#include "config_store.h"
#include "database.h"
#include "utils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

#include <memory>

// The queue and the schedules moved out of two hand-written .conf files and
// into the database (docs/PLAN.md 6.8). Nothing about their contents changed,
// which is what these tests are for: a migration that quietly reinterprets
// what it moves is worse than no migration at all.
//
// Same portable-mode redirection as test_task_store, so the real user
// configuration is never read or written.
class TestConfigStore : public QObject {
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

  static QString conf(const QString &name) {
    return QDir(appDir()).filePath(name);
  }

  static void writeConf(const QString &name, const QStringList &lines) {
    QFile file(conf(name));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QTextStream out(&file);
    for (const QString &line : lines) {
      out << line << Qt::endl;
    }
    out.flush();
    file.close();
  }

  static void removeAll(const QString &name) {
    QFile::remove(conf(name));
    QFile::remove(conf(name + QStringLiteral(".migrated")));
  }

  static QStringList schedule(const QString &id, const QString &taskId) {
    return {QStringLiteral("mSchedulerId"),
            id,
            QStringLiteral("mSchedulerName"),
            QStringLiteral("bmlnaHRseQ=="), // base64, exactly as saved
            QStringLiteral("mTaskId"),
            taskId,
            QStringLiteral("mCron"),
            QStringLiteral("KiAqICogKiAq"),
            QStringLiteral("mExecutionMode"),
            QStringLiteral("1")};
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
    removeAll(QStringLiteral("queue.conf"));
    removeAll(QStringLiteral("scheduler.conf"));
  }

  void init() {
    // A database per test: the import only runs against an empty table, so
    // sharing one would make every test after the first test nothing.
    ++mCounter;
    removeAll(QStringLiteral("queue.conf"));
    removeAll(QStringLiteral("scheduler.conf"));
    Database::setPath(
        QDir(appDir()).filePath(QStringLiteral("cfg%1.db").arg(mCounter)));
    QFile::remove(Database::path());
    Database::closeForThread();
    QVERIFY2(Database::connection().isOpen(), qPrintable(Database::lastError()));
  }

  void cleanup() {
    const QString path = Database::path();
    Database::closeForThread();
    QFile::remove(path);
  }

  void importsQueueConfInOrderAndKeepsTheFile() {
    writeConf(QStringLiteral("queue.conf"),
              {QStringLiteral("{11111111-1111-1111-1111-111111111111},req-1"),
               QStringLiteral("{22222222-2222-2222-2222-222222222222},req-2")});

    const QList<QueueEntry> entries = QueueStore::load();
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries[0].taskId,
             QStringLiteral("{11111111-1111-1111-1111-111111111111}"));
    QCOMPARE(entries[0].requestId, QStringLiteral("req-1"));
    QCOMPARE(entries[1].requestId, QStringLiteral("req-2"));

    // Renamed rather than deleted, so a rollback still has the original.
    QVERIFY(!QFile::exists(conf(QStringLiteral("queue.conf"))));
    QVERIFY(QFile::exists(conf(QStringLiteral("queue.conf.migrated"))));

    // Reading again must not import a second time or lose the order.
    const QList<QueueEntry> again = QueueStore::load();
    QCOMPARE(again.size(), 2);
    QCOMPARE(again[0].requestId, QStringLiteral("req-1"));
    QCOMPARE(again[1].requestId, QStringLiteral("req-2"));
  }

  void aQueueLineWithoutARequestIdStillLoads() {
    // Written by a version that recorded only the task. Dropping the entry
    // would silently empty the queue of anyone upgrading from it.
    writeConf(QStringLiteral("queue.conf"),
              {QStringLiteral("{33333333-3333-3333-3333-333333333333}")});

    const QList<QueueEntry> entries = QueueStore::load();
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries[0].taskId,
             QStringLiteral("{33333333-3333-3333-3333-333333333333}"));
    QVERIFY(!entries[0].requestId.isEmpty());
  }

  void savingReplacesTheWholeQueue() {
    QueueEntry first;
    first.taskId = QStringLiteral("task-a");
    first.requestId = QStringLiteral("r1");
    QueueEntry second;
    second.taskId = QStringLiteral("task-b");
    second.requestId = QStringLiteral("r2");

    QVERIFY(QueueStore::save({first, second}));
    QCOMPARE(QueueStore::load().size(), 2);

    // The queue is rewritten whole on every change, and its order is the
    // point of it.
    QVERIFY(QueueStore::save({second}));
    const QList<QueueEntry> left = QueueStore::load();
    QCOMPARE(left.size(), 1);
    QCOMPARE(left[0].requestId, QStringLiteral("r2"));

    QVERIFY(QueueStore::save({}));
    QVERIFY(QueueStore::load().isEmpty());
  }

  void importsSchedulerConfWordForWord() {
    const QStringList one = schedule(QStringLiteral("sched-1"),
                                     QStringLiteral("task-1"));
    const QStringList two = schedule(QStringLiteral("sched-2"),
                                     QStringLiteral("task-2"));
    writeConf(QStringLiteral("scheduler.conf"),
              {one.join(QLatin1Char(',')), two.join(QLatin1Char(','))});

    const QList<QStringList> schedules = ScheduleStore::load();
    QCOMPARE(schedules.size(), 2);

    // Word for word: the scheduler reads this back by position and by key,
    // and a base64 value that came through changed would restore as mojibake.
    QCOMPARE(schedules[0], one);
    QCOMPARE(schedules[1], two);

    QVERIFY(!QFile::exists(conf(QStringLiteral("scheduler.conf"))));
    QVERIFY(QFile::exists(conf(QStringLiteral("scheduler.conf.migrated"))));
  }

  void theTaskOfAScheduleIsReadFromItsArguments() {
    QCOMPARE(ScheduleStore::taskIdOf(
                 schedule(QStringLiteral("s"), QStringLiteral("the-task"))),
             QStringLiteral("the-task"));

    // A list with no task in it must answer nothing rather than read past its
    // own end -- the old code indexed mTaskId + 1 with no check at all.
    QVERIFY(ScheduleStore::taskIdOf({}).isEmpty());
    QVERIFY(ScheduleStore::taskIdOf({QStringLiteral("mTaskId")}).isEmpty());
  }

  void savingReplacesEverySchedule() {
    const QStringList one = schedule(QStringLiteral("s1"),
                                     QStringLiteral("task-1"));
    const QStringList two = schedule(QStringLiteral("s2"),
                                     QStringLiteral("task-2"));

    QVERIFY(ScheduleStore::save({one, two}));
    QCOMPARE(ScheduleStore::load().size(), 2);

    QVERIFY(ScheduleStore::save({two}));
    const QList<QStringList> left = ScheduleStore::load();
    QCOMPARE(left.size(), 1);
    QCOMPARE(left[0], two);
  }

  void aValueWithACommaInItSurvives() {
    // The reason for leaving the file behind: scheduler.conf split on commas,
    // so any value containing one was torn in half. JSON has no such problem.
    QStringList args = schedule(QStringLiteral("s1"), QStringLiteral("t1"));
    args << QStringLiteral("mNote")
         << QStringLiteral("one,two,three");

    QVERIFY(ScheduleStore::save({args}));
    const QList<QStringList> back = ScheduleStore::load();
    QCOMPARE(back.size(), 1);
    QCOMPARE(back[0], args);
  }

  void nothingIsImportedWhenThereIsNoFile() {
    QVERIFY(QueueStore::load().isEmpty());
    QVERIFY(ScheduleStore::load().isEmpty());
    QVERIFY(!QFile::exists(conf(QStringLiteral("queue.conf.migrated"))));
  }
};

QTEST_MAIN(TestConfigStore)
#include "test_config_store.moc"
