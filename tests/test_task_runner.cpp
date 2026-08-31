#include "database.h"
#include "job_options.h"
#include "list_of_job_options.h"
#include "task_runner.h"
#include "run_history.h"
#include "utils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

#include <memory>

// Running a saved task without a window -- E1 in the plan, S1 in docs/API.md.
//
// The value of this file is not that it exercises TaskRunner: it is that it
// can exist at all. It links rbcore and Qt Test, and nothing else. If someone
// later reaches for a widget from the run path, this stops linking, which is
// a better alarm than noticing that "--run-task" opened a window.
class TestTaskRunner : public QObject {
  Q_OBJECT

private:
  std::unique_ptr<QTemporaryDir> mScratch; // config, so the real one is safe
  std::unique_ptr<QTemporaryDir> mSource;
  std::unique_ptr<QTemporaryDir> mDest;
  QString mRclone;

  // Same portable-mode redirection as test_task_store: without it the store
  // would read, and then rewrite, the developer's own task file.
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

  static QString taskFilePath() { return QDir(appDir()).filePath("tasks.bin"); }

  JobOptions *makeCopyTask(const QString &name, const QString &from,
                           const QString &to) {
    auto *task = new JobOptions();
    task->description = name;
    task->uniqueId = QUuid::createUuid();
    task->operation = JobOptions::Copy;
    task->jobType = JobOptions::Upload;
    task->source = from;
    task->dest = to;
    return task;
  }

private slots:
  void initTestCase() {
    mRclone = QStandardPaths::findExecutable(QStringLiteral("rclone"));
    if (mRclone.isEmpty()) {
      QSKIP("rclone is not on PATH");
    }

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    mScratch.reset(new QTemporaryDir);
    QVERIFY(mScratch->isValid());
    qputenv("XDG_CONFIG_HOME", mScratch->path().toLocal8Bit());
    QVERIFY(QDir().mkpath(appDir()));
#endif

    QFile ini(iniPath());
    QVERIFY2(ini.open(QIODevice::WriteOnly | QIODevice::Truncate),
             qPrintable(ini.errorString()));
    ini.close();
    QVERIFY2(IsPortableMode(),
             "portable mode did not take effect; the store would read the "
             "real user task file");

    QFile::remove(taskFilePath());

    SetRclone(mRclone);
    SetRcloneConf(QString());

    // runTask() records the run, and the tasks it runs live in the same
    // database. Point it at the scratch directory -- and start from an empty
    // file, or every run of this test would add its tasks to the ones the
    // last run left and the names would stop being unique.
    Database::closeForThread();
    QFile::remove(QDir(appDir()).filePath(QStringLiteral("history.db")));
    Database::setPath(
        QDir(appDir()).filePath(QStringLiteral("history.db")));

    mSource.reset(new QTemporaryDir);
    mDest.reset(new QTemporaryDir);
    QVERIFY(mSource->isValid() && mDest->isValid());

    QFile payload(QDir(mSource->path()).filePath("hello.txt"));
    QVERIFY(payload.open(QIODevice::WriteOnly));
    payload.write("moved by the headless runner\n");
    payload.close();
  }

  void cleanupTestCase() {
    const QString db = Database::path();
    Database::closeForThread();
    QFile::remove(db);
    QFile::remove(iniPath());
    QFile::remove(taskFilePath());
  }

  // The criterion for E1: the job finishes and the exit code is rclone's.
  void runsATaskAndReturnsRclonesExitCode() {
    JobOptions *task =
        makeCopyTask(QStringLiteral("headless copy"), mSource->path(),
                     mDest->path());
    QVERIFY(ListOfJobOptions::getInstance()->Persist(task));

    QString outText, errText;
    QTextStream out(&outText), err(&errText);

    const int code =
        TaskRunner::runTask(QStringLiteral("headless copy"), false, out, err);
    out.flush();
    err.flush();

    QVERIFY2(code == TaskRunner::Ok, qPrintable(outText + errText));
    QVERIFY2(QFile::exists(QDir(mDest->path()).filePath("hello.txt")),
             qPrintable(outText));

    // The command that ran is echoed so a failure can be reproduced by hand.
    QVERIFY2(outText.contains(QStringLiteral("copy")), qPrintable(outText));
  }

  // --dry-run has to reach rclone, or a user checking what a task would do
  // gets it done to them instead.
  void dryRunCopiesNothing() {
    QTemporaryDir untouched;
    QVERIFY(untouched.isValid());

    JobOptions *task = makeCopyTask(QStringLiteral("headless dry run"),
                                    mSource->path(), untouched.path());
    QVERIFY(ListOfJobOptions::getInstance()->Persist(task));

    QString outText, errText;
    QTextStream out(&outText), err(&errText);

    const int code = TaskRunner::runTask(QStringLiteral("headless dry run"),
                                         true, out, err);
    out.flush();
    err.flush();

    QCOMPARE(code, int(TaskRunner::Ok));
    QVERIFY2(outText.contains(QStringLiteral("--dry-run")), qPrintable(outText));
    QVERIFY2(!QFile::exists(QDir(untouched.path()).filePath("hello.txt")),
             "a dry run copied the file anyway");
  }

  // An rclone that is not there is 69, not rclone's own exit code. A script
  // that reads exit codes has to be able to tell "your path is wrong" from
  // "the transfer failed", and once the job has started both arrive the same
  // way -- which is why this is checked before starting rather than after.
  void anRcloneThatIsNotThereIsItsOwnExitCode() {
    QVERIFY(ListOfJobOptions::getInstance()->Persist(makeCopyTask(
        QStringLiteral("no-rclone-here"), mSource->path(), mDest->path())));

    const QString real = GetRclone();
    SetRclone(QStringLiteral("no-such-rclone-anywhere"));

    QString outText;
    QString errText;
    QTextStream out(&outText);
    QTextStream err(&errText);

    const int code = TaskRunner::runTask(QStringLiteral("no-rclone-here"),
                                         true, out, err);
    SetRclone(real);

    QCOMPARE(code, int(TaskRunner::RcloneUnavailable));
    QVERIFY2(errText.contains(QStringLiteral("could not start")),
             qPrintable(errText));
  }

  // The run goes through JobRegistry now, so it leaves what every other job
  // leaves: a history row saying it came from the command line. Before this
  // it wrote its own row and no log file at all.
  void aRunFromTheCommandLineIsAnOrdinaryJob() {
    QVERIFY(ListOfJobOptions::getInstance()->Persist(makeCopyTask(
        QStringLiteral("from-the-cli"), mSource->path(), mDest->path())));

    QString outText;
    QString errText;
    QTextStream out(&outText);
    QTextStream err(&errText);

    QCOMPARE(TaskRunner::runTask(QStringLiteral("from-the-cli"), true, out,
                                 err),
             int(TaskRunner::Ok));

    bool found = false;
    for (const JobRunRecord &row : RunHistory::recent(20)) {
      if (row.transferMode == QStringLiteral("cli")) {
        found = true;
        QCOMPARE(row.kind, QStringLiteral("transfer"));
        QCOMPARE(row.state, QStringLiteral("finished"));
        break;
      }
    }
    QVERIFY2(found, "a command-line run left no history row");
  }

  void refusesAnUnknownTask() {
    QString outText, errText;
    QTextStream out(&outText), err(&errText);

    QCOMPARE(TaskRunner::runTask(QStringLiteral("no such task"), false, out,
                                 err),
             int(TaskRunner::TaskNotFound));
    err.flush();
    QVERIFY2(errText.contains(QStringLiteral("no task called")),
             qPrintable(errText));
  }

  // Two tasks may share a description, and running whichever came first would
  // be the wrong one half the time.
  void refusesAnAmbiguousName() {
    ListOfJobOptions *store = ListOfJobOptions::getInstance();
    QVERIFY(store->Persist(makeCopyTask(QStringLiteral("twin"),
                                        mSource->path(), mDest->path())));
    QVERIFY(store->Persist(makeCopyTask(QStringLiteral("twin"),
                                        mSource->path(), mDest->path())));

    QString outText, errText;
    QTextStream out(&outText), err(&errText);

    QCOMPARE(TaskRunner::runTask(QStringLiteral("twin"), false, out, err),
             int(TaskRunner::AmbiguousName));
    err.flush();
    QVERIFY2(errText.contains(QStringLiteral("2 tasks")), qPrintable(errText));

    // Naming one by id still works, which is what the message tells the user
    // to do.
    JobOptions *first = store->findByName(QStringLiteral("twin"));
    QVERIFY(first != nullptr);
    QString outText2, errText2;
    QTextStream out2(&outText2), err2(&errText2);
    QCOMPARE(TaskRunner::runTask(first->uniqueId.toString(), true, out2, err2),
             int(TaskRunner::Ok));
  }

  // A mount does not finish, so "did it work, and what was the exit code" has
  // no answer. Refusing beats hanging.
  void refusesAMountTask() {
    auto *mount = new JobOptions();
    mount->description = "headless mount";
    mount->uniqueId = QUuid::createUuid();
    mount->operation = JobOptions::Mount;
    mount->jobType = JobOptions::Download;
    mount->source = "remote:";
    mount->dest = "/mnt/x";
    QVERIFY(ListOfJobOptions::getInstance()->Persist(mount));

    QString outText, errText;
    QTextStream out(&outText), err(&errText);

    QCOMPARE(TaskRunner::runTask(QStringLiteral("headless mount"), false, out,
                                 err),
             int(TaskRunner::UsageError));
    err.flush();
    QVERIFY2(errText.contains(QStringLiteral("mount task")),
             qPrintable(errText));
  }

  void listsEverySavedTask() {
    QString outText;
    QTextStream out(&outText);
    QCOMPARE(TaskRunner::listTasks(out), int(TaskRunner::Ok));
    out.flush();

    QVERIFY2(outText.contains(QStringLiteral("headless copy")),
             qPrintable(outText));
    // The id is what makes an ambiguous name recoverable, so it has to be in
    // the listing.
    JobOptions *task =
        ListOfJobOptions::getInstance()->findByName(QStringLiteral("headless "
                                                                  "copy"));
    QVERIFY(task != nullptr);
    QVERIFY2(outText.contains(task->uniqueId.toString()), qPrintable(outText));
  }
};

QTEST_GUILESS_MAIN(TestTaskRunner)
#include "test_task_runner.moc"
