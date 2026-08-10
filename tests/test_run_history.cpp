#include "database.h"
#include "run_history.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <memory>

// The history is written by two processes at once by design -- see
// docs/PLAN.md 6.8 -- so the concurrency case is tested here rather than left
// to be discovered when a scheduled run happens to overlap with the window.
class TestRunHistory : public QObject {
  Q_OBJECT

private:
  static JobRunRecord sample(const QString &requestId, qint64 startedAt) {
    JobRunRecord r;
    r.requestId = requestId;
    r.taskId = QStringLiteral("task-1");
    r.taskName = QStringLiteral("nightly photos");
    r.kind = QStringLiteral("transfer");
    r.transferMode = QStringLiteral("task");
    r.info = QStringLiteral("Task: \"nightly photos\", Copy from C:/photos");
    r.source = QStringLiteral("C:/photos");
    r.dest = QStringLiteral("tgdrive:photos");
    r.startedAt = startedAt;
    return r;
  }

private slots:
  void initTestCase() {
    mScratch.reset(new QTemporaryDir);
    QVERIFY(mScratch->isValid());
    // Not a skip: the driver is a packaging decision, and an installation
    // without it loses every run silently. Better for the build to say so.
    QVERIFY2(QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")),
             "the Qt SQLite driver is missing; history cannot be tested");
  }

  void init() {
    // A file per test, so one test's rows cannot explain another's result.
    ++mCounter;
    Database::setPath(
        QDir(mScratch->path()).filePath(QStringLiteral("h%1.db").arg(mCounter)));
  }

  void cleanup() { Database::closeForThread(); }

  void createsSchemaOnFirstUse() {
    QVERIFY(Database::isAvailable());
    QCOMPARE(Database::schemaVersion(), Database::kSchemaVersion);
    QVERIFY(QFile::exists(Database::path()));
  }

  void openingTwiceKeepsTheSameSchema() {
    QVERIFY(Database::isAvailable());
    QVERIFY(RunHistory::recordStarted(sample(QStringLiteral("a"), 1000)));

    Database::closeForThread();
    QVERIFY(Database::isAvailable());
    QCOMPARE(Database::schemaVersion(), Database::kSchemaVersion);
    QCOMPARE(RunHistory::count(), 1);
  }

  void recordsAStartedJobAsRunning() {
    QVERIFY(RunHistory::recordStarted(sample(QStringLiteral("r1"), 1000)));

    const JobRunRecord row = RunHistory::find(QStringLiteral("r1"));
    QCOMPARE(row.requestId, QStringLiteral("r1"));
    QCOMPARE(row.state, QStringLiteral("running"));
    QVERIFY(row.isRunning());
    QCOMPARE(row.taskName, QStringLiteral("nightly photos"));
    QCOMPARE(row.dest, QStringLiteral("tgdrive:photos"));
    QCOMPARE(row.finishedAt, 0LL);
  }

  void endingAJobKeepsItsOriginalStartTime() {
    QVERIFY(RunHistory::recordStarted(sample(QStringLiteral("r1"), 1000)));

    JobRunRecord done = sample(QStringLiteral("r1"), 9999); // wrong on purpose
    done.state = QStringLiteral("finished");
    done.exitCode = 0;
    done.finishedAt = 5000;
    done.bytes = 4096;
    done.totalBytes = 4096;
    done.transfers = 3;
    done.logPath = QStringLiteral("/logs/x.log");
    done.logBytes = 120;
    QVERIFY(RunHistory::recordFinished(done));

    const JobRunRecord row = RunHistory::find(QStringLiteral("r1"));
    QCOMPARE(RunHistory::count(), 1); // updated, not added
    QCOMPARE(row.startedAt, 1000LL);  // the first record of it is the true one
    QCOMPARE(row.finishedAt, 5000LL);
    QCOMPARE(row.state, QStringLiteral("finished"));
    QCOMPARE(row.exitCode, 0);
    QCOMPARE(row.bytes, 4096LL);
    QCOMPARE(row.transfers, 3LL);
    QCOMPARE(row.logPath, QStringLiteral("/logs/x.log"));
    QVERIFY(!row.isRunning());
  }

  void anEndingIsKeptEvenWithoutAStart() {
    JobRunRecord done = sample(QStringLiteral("r9"), 1000);
    done.state = QStringLiteral("error");
    done.exitCode = 1;
    done.finishedAt = 2000;
    QVERIFY(RunHistory::recordFinished(done));

    QCOMPARE(RunHistory::count(), 1);
    QCOMPARE(RunHistory::find(QStringLiteral("r9")).state,
             QStringLiteral("error"));
  }

  void aJobWithNoRequestIdIsNotRecorded() {
    JobRunRecord r = sample(QString(), 1000);
    QVERIFY(!RunHistory::recordStarted(r));
    QCOMPARE(RunHistory::count(), 0);
  }

  void recentIsNewestFirstAndRespectsTheLimit() {
    for (int i = 1; i <= 5; ++i) {
      QVERIFY(RunHistory::recordStarted(
          sample(QStringLiteral("r%1").arg(i), i * 1000)));
    }

    const QList<JobRunRecord> rows = RunHistory::recent(3);
    QCOMPARE(rows.size(), 3);
    QCOMPARE(rows[0].requestId, QStringLiteral("r5"));
    QCOMPARE(rows[1].requestId, QStringLiteral("r4"));
    QCOMPARE(rows[2].requestId, QStringLiteral("r3"));
  }

  void forTaskReturnsOnlyThatTask() {
    QVERIFY(RunHistory::recordStarted(sample(QStringLiteral("r1"), 1000)));

    JobRunRecord other = sample(QStringLiteral("r2"), 2000);
    other.taskId = QStringLiteral("task-2");
    QVERIFY(RunHistory::recordStarted(other));

    const QList<JobRunRecord> rows =
        RunHistory::forTask(QStringLiteral("task-1"));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().requestId, QStringLiteral("r1"));

    // A job started outside any task has no task to be listed under.
    QVERIFY(RunHistory::forTask(QString()).isEmpty());
  }

  void purgeByAgeDeletesTheRowAndItsLog() {
    const QString oldLog =
        QDir(mScratch->path()).filePath(QStringLiteral("old.log"));
    QFile file(oldLog);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("older than the retention window\n");
    file.close();

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    JobRunRecord old = sample(QStringLiteral("old"),
                              now - 40LL * 24 * 60 * 60 * 1000);
    old.state = QStringLiteral("finished");
    old.logPath = oldLog;
    QVERIFY(RunHistory::recordFinished(old));

    JobRunRecord fresh = sample(QStringLiteral("fresh"), now);
    fresh.state = QStringLiteral("finished");
    QVERIFY(RunHistory::recordFinished(fresh));

    QCOMPARE(RunHistory::purge(30, 0), 1);
    QCOMPARE(RunHistory::count(), 1);
    QCOMPARE(RunHistory::recent().first().requestId, QStringLiteral("fresh"));

    // Leaving the file behind would rebuild the very problem the index was
    // added to solve: logs nothing points at.
    QVERIFY(!QFile::exists(oldLog));
  }

  void purgeLeavesARunningJobAlone() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // A mount that has been up for a month is old by date and still live.
    JobRunRecord mount =
        sample(QStringLiteral("mount"), now - 40LL * 24 * 60 * 60 * 1000);
    mount.kind = QStringLiteral("mount");
    QVERIFY(RunHistory::recordStarted(mount));

    QCOMPARE(RunHistory::purge(30, 0), 0);
    QCOMPARE(RunHistory::count(), 1);
  }

  void purgeByCountKeepsTheNewest() {
    for (int i = 1; i <= 5; ++i) {
      JobRunRecord r = sample(QStringLiteral("r%1").arg(i), i * 1000);
      r.state = QStringLiteral("finished");
      QVERIFY(RunHistory::recordFinished(r));
    }

    QCOMPARE(RunHistory::purge(0, 2), 3);

    const QList<JobRunRecord> rows = RunHistory::recent();
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[0].requestId, QStringLiteral("r5"));
    QCOMPARE(rows[1].requestId, QStringLiteral("r4"));
  }

  void purgeCountsEachRowOnce() {
    // A row that both rules condemn must not be counted twice.
    const qint64 old = QDateTime::currentMSecsSinceEpoch() -
                       40LL * 24 * 60 * 60 * 1000;
    JobRunRecord r = sample(QStringLiteral("both"), old);
    r.state = QStringLiteral("finished");
    QVERIFY(RunHistory::recordFinished(r));

    QCOMPARE(RunHistory::purge(30, 0 + 0), 1);
    QCOMPARE(RunHistory::count(), 0);
  }

  // The window and a "--run-task" from cron write this file at the same time:
  // E1 has no single-instance lock on purpose. Two threads with their own
  // connections is the same situation as far as SQLite is concerned.
  void twoConnectionsCanWriteAtOnce() {
    QVERIFY(Database::isAvailable());

    struct Writer : QThread {
      QString prefix;
      bool ok = true;
      void run() override {
        for (int i = 0; i < 50; ++i) {
          JobRunRecord r;
          r.requestId = prefix + QString::number(i);
          r.kind = QStringLiteral("transfer");
          r.startedAt = 1000 + i;
          r.state = QStringLiteral("finished");
          if (!RunHistory::recordFinished(r)) {
            ok = false;
          }
        }
        // Every thread that opens a connection has to close it, or QtSql
        // warns that the connection is still in use when it ends.
        Database::closeForThread();
      }
    };

    Writer other;
    other.prefix = QStringLiteral("b");
    other.start();

    bool ok = true;
    for (int i = 0; i < 50; ++i) {
      JobRunRecord r;
      r.requestId = QStringLiteral("a") + QString::number(i);
      r.kind = QStringLiteral("transfer");
      r.startedAt = 1000 + i;
      r.state = QStringLiteral("finished");
      if (!RunHistory::recordFinished(r)) {
        ok = false;
      }
    }

    QVERIFY(other.wait(30000));
    QVERIFY2(ok, "the first writer lost rows");
    QVERIFY2(other.ok, "the second writer lost rows");
    QCOMPARE(RunHistory::count(), 100);
  }

  void aDatabaseThatCannotBeOpenedIsNotFatal() {
    // A directory is not a database file, which is the closest thing to an
    // unwritable path that behaves the same on every platform.
    const QString dir = QDir(mScratch->path()).filePath("not-a-db");
    QVERIFY(QDir().mkpath(dir));
    Database::setPath(dir);

    QVERIFY(!Database::isAvailable());
    QVERIFY(!Database::lastError().isEmpty());

    // Losing the history of a transfer is not a reason to refuse to run it.
    QVERIFY(!RunHistory::recordStarted(sample(QStringLiteral("r1"), 1000)));
    QVERIFY(RunHistory::recent().isEmpty());
    QCOMPARE(RunHistory::count(), 0);
  }

private:
  std::unique_ptr<QTemporaryDir> mScratch;
  int mCounter = 0;
};

QTEST_GUILESS_MAIN(TestRunHistory)
#include "test_run_history.moc"
