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

  // ---- when a remote was last used ----------------------------------------

  JobRunRecord transfer(const QString &id, qint64 started, const QString &source,
                        const QString &dest) {
    JobRunRecord r = sample(id, started);
    r.kind = QStringLiteral("transfer");
    r.source = source;
    r.dest = dest;
    return r;
  }

private slots:
  // The question the remotes tab could not ask: when was this one last
  // uploaded to, and when did that begin and end.
  void theLastUploadToARemoteIsTheNewestOneWrittenThere() {
    QVERIFY(RunHistory::recordStarted(transfer(
        QStringLiteral("old"), 1000, QStringLiteral("C:/a"),
        QStringLiteral("gdrive:backup"))));
    JobRunRecord newer = transfer(QStringLiteral("new"), 5000,
                                  QStringLiteral("C:/b"),
                                  QStringLiteral("gdrive:films/2026"));
    newer.finishedAt = 9000;
    newer.state = QStringLiteral("finished");
    QVERIFY(RunHistory::recordFinished(newer));

    const JobRunRecord last = RunHistory::lastUploadTo(QStringLiteral("gdrive"));
    QCOMPARE(last.requestId, QStringLiteral("new"));
    QCOMPARE(last.startedAt, qint64(5000));
    QCOMPARE(last.finishedAt, qint64(9000));
    QCOMPARE(last.state, QStringLiteral("finished"));
  }

  // Up and down are different questions. A download from a remote is not an
  // upload to it, however recently it happened.
  void anUploadAndADownloadAreToldApart() {
    QVERIFY(RunHistory::recordStarted(transfer(
        QStringLiteral("up"), 1000, QStringLiteral("C:/a"),
        QStringLiteral("gdrive:in"))));
    QVERIFY(RunHistory::recordStarted(transfer(
        QStringLiteral("down"), 2000, QStringLiteral("gdrive:out"),
        QStringLiteral("C:/b"))));

    QCOMPARE(RunHistory::lastUploadTo(QStringLiteral("gdrive")).requestId,
             QStringLiteral("up"));
    QCOMPARE(RunHistory::lastDownloadFrom(QStringLiteral("gdrive")).requestId,
             QStringLiteral("down"));
  }

  // The names in use are tgdrive_main_01 and the like, and in SQL's LIKE an
  // underscore matches any one character. One remote's upload must not be
  // reported as another's because their names differ only where an
  // underscore is -- nor because one name begins with the other.
  void oneRemoteIsNotMistakenForAnotherWithASimilarName() {
    QVERIFY(RunHistory::recordStarted(transfer(
        QStringLiteral("x"), 1000, QStringLiteral("C:/a"),
        QStringLiteral("tgdriveXmain:folder"))));
    QVERIFY(RunHistory::recordStarted(transfer(
        QStringLiteral("long"), 2000, QStringLiteral("C:/a"),
        QStringLiteral("gdrive2:folder"))));

    QVERIFY2(RunHistory::lastUploadTo(QStringLiteral("tgdrive_main"))
                 .requestId.isEmpty(),
             "an underscore in a remote's name matched a different remote");
    QVERIFY2(RunHistory::lastUploadTo(QStringLiteral("gdrive"))
                 .requestId.isEmpty(),
             "a remote was matched by one whose name merely begins the same");
  }

  // Never used is an answer too, and it is an empty one rather than an error
  // or somebody else's run.
  void aRemoteNeverUsedHasNoLastRun() {
    QVERIFY(RunHistory::lastUploadTo(QStringLiteral("gdrive"))
                .requestId.isEmpty());
    QVERIFY(RunHistory::lastUploadTo(QString()).requestId.isEmpty());

    // A local folder that happens to have a colon in it is not a remote.
    QVERIFY(RunHistory::recordStarted(transfer(
        QStringLiteral("local"), 1000, QStringLiteral("gdrive:a"),
        QStringLiteral("C:/gdrive:odd"))));
    QVERIFY(RunHistory::lastUploadTo(QStringLiteral("gdrive"))
                .requestId.isEmpty());
  }

  // A dry run sent nothing, so it is not the last time anything was sent --
  // however recent it is, and whatever size rclone said it would have been.
  //
  // Told by the run's own flag and by nothing else. The first version looked
  // for the words "Dry run" in how the run described itself, and those are
  // only there when the Dry run button was used: a task that carries
  // --dry-run among its own flags describes itself as an ordinary task, and
  // every remote in the real history was showing one of those as its last
  // upload.
  void aDryRunIsNotAnUpload() {
    JobRunRecord real = transfer(QStringLiteral("real"), 1000,
                                 QStringLiteral("C:/a"),
                                 QStringLiteral("gdrive:in"));
    real.info = QStringLiteral("Task: \"films\", Copy from C:/a");
    QVERIFY(RunHistory::recordStarted(real));

    JobRunRecord dry = transfer(QStringLiteral("dry"), 9000,
                                QStringLiteral("C:/a"),
                                QStringLiteral("gdrive:in"));
    dry.info = QStringLiteral("Task: \"films\", Copy from C:/a"); // no hint
    dry.dryRun = true;
    dry.bytes = 123456;
    QVERIFY(RunHistory::recordStarted(dry));

    QCOMPARE(RunHistory::lastUploadTo(QStringLiteral("gdrive")).requestId,
             QStringLiteral("real"));

    // And the flag survives the round trip, so a view can say which it was.
    QVERIFY(RunHistory::find(QStringLiteral("dry")).dryRun);
    QVERIFY(!RunHistory::find(QStringLiteral("real")).dryRun);
  }

  // A history written before runs said whether they were rehearsals. What can
  // be worked out about the old rows is worked out once, when the database is
  // brought up to date -- and what cannot be, is left as a real run.
  void anOlderHistoryLearnsWhichRunsWereRehearsals() {
    const QString path = Database::path();
    {
      // The database as the previous build left it: version 2, no dry_run.
      QSqlDatabase old =
          QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), "old-shape");
      old.setDatabaseName(path);
      QVERIFY(old.open());
      QSqlQuery q(old);
      QVERIFY(q.exec("CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT)"));
      QVERIFY(q.exec("INSERT INTO meta VALUES('schema_version','2')"));
      QVERIFY(q.exec(
          "CREATE TABLE job_run (request_id TEXT PRIMARY KEY, task_id TEXT, "
          "task_name TEXT, kind TEXT, transfer_mode TEXT, info TEXT, "
          "source TEXT, dest TEXT, started_at INTEGER NOT NULL, "
          "finished_at INTEGER, state TEXT, exit_code INTEGER, "
          "bytes INTEGER DEFAULT 0, total_bytes INTEGER DEFAULT 0, "
          "transfers INTEGER DEFAULT 0, errors INTEGER DEFAULT 0, "
          "log_path TEXT, log_bytes INTEGER DEFAULT 0)"));
      QVERIFY(q.exec(
          "CREATE TABLE task (id TEXT PRIMARY KEY, name TEXT NOT NULL, "
          "operation INTEGER, source TEXT, dest TEXT, options TEXT NOT NULL, "
          "position INTEGER DEFAULT 0, created_at INTEGER, updated_at INTEGER)"));

      QVERIFY(q.exec("INSERT INTO task (id, name, options) VALUES "
                     "('rehearsing', 'r', '{\"extra\":\"--dry-run\"}'), "
                     "('plain', 'p', '{\"extra\":\"\"}')"));

      QVERIFY(q.exec(
          "INSERT INTO job_run (request_id, task_id, kind, info, dest, "
          "started_at) VALUES "
          // started from the Dry run button: it said so itself
          "('button', 'plain', 'transfer', 'Dry run, task: x', 'gdrive:a', 3000), "
          // a task whose own flags make every run a rehearsal
          "('flagged', 'rehearsing', 'transfer', 'Task: r', 'gdrive:a', 2000), "
          // an ordinary upload
          "('real', 'plain', 'transfer', 'Task: p', 'gdrive:a', 1000)"));
      old.close();
    }
    QSqlDatabase::removeDatabase("old-shape");

    // Opening it brings it up to date.
    QVERIFY2(Database::connection().isOpen(), qPrintable(Database::lastError()));
    QCOMPARE(Database::schemaVersion(), Database::kSchemaVersion);

    QVERIFY(RunHistory::find(QStringLiteral("button")).dryRun);
    QVERIFY(RunHistory::find(QStringLiteral("flagged")).dryRun);
    QVERIFY(!RunHistory::find(QStringLiteral("real")).dryRun);

    // Which is the whole point: the newest two were rehearsals, and the
    // remote's last upload is the older, real one.
    QCOMPARE(RunHistory::lastUploadTo(QStringLiteral("gdrive")).requestId,
             QStringLiteral("real"));
    QCOMPARE(RunHistory::count(), 3); // nothing was lost on the way
  }

  // Only transfers. A mount of the remote, or a size check on it, is not
  // "the last time something was uploaded".
  void onlyATransferCounts() {
    JobRunRecord mount = transfer(QStringLiteral("m"), 3000,
                                  QStringLiteral("C:/x"),
                                  QStringLiteral("gdrive:"));
    mount.kind = QStringLiteral("mount");
    QVERIFY(RunHistory::recordStarted(mount));

    QVERIFY(RunHistory::lastUploadTo(QStringLiteral("gdrive"))
                .requestId.isEmpty());
  }
};

QTEST_GUILESS_MAIN(TestRunHistory)
#include "test_run_history.moc"
