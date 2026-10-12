#include "app_core.h"
#include "config_store.h"
#include "database.h"
#include "job_options.h"
#include "job_queue.h"
#include "job_registry.h"
#include "list_of_job_options.h"
#include "run_history.h"
#include "shutdown_signal.h"
#include "utils.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <functional>
#include <memory>

// Being told to stop. A daemon is stopped by a signal -- "docker stop" sends
// SIGTERM and kills ten seconds later -- and what it leaves behind when that
// happens is the whole of what this checks: nothing still running, a history
// that says what happened, and a queue that is exactly as it was.
//
// In an executable of its own, because stopping is not something the core
// comes back from: once it has stopped, every later test in the same process
// would be testing a core that refuses to start anything.
//
// The transfer is tests/fake_rclone, told to do nothing for half a minute,
// so there is something still going at the moment of asking.
class TestShutdown : public QObject {
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

  JobOptions *makeTask(const QString &name) {
    auto *task = new JobOptions(false);
    task->description = name;
    task->operation = JobOptions::Copy;
    task->jobType = JobOptions::Upload;
    task->source = QStringLiteral("C:/nowhere");
    task->dest = QStringLiteral("fake:");
    task->isFolder = true;
    task->uniqueId = QUuid::createUuid();
    ListOfJobOptions::getInstance()->Persist(task);
    return task;
  }

  static bool waitUntil(const std::function<bool()> &holds, int ms = 8000) {
    QDeadlineTimer deadline(ms);
    while (!deadline.hasExpired()) {
      if (holds()) {
        return true;
      }
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return holds();
  }

  std::unique_ptr<QTemporaryDir> mScratch;
  QString mFirstRequest;

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

    Database::setPath(QDir(appDir()).filePath(QStringLiteral("shutdown.db")));
    QFile::remove(Database::path());
    QVERIFY2(Database::connection().isOpen(),
             qPrintable(Database::lastError()));

    qputenv("FAKE_RCLONE_SLEEP_MS", "30000");
    GetSettings()->setValue(QStringLiteral("Settings/rclone"),
                            QString::fromLatin1(RB_FAKE_RCLONE));
    GetSettings()->setValue(QStringLiteral("Settings/queueStatus"), "true");
    GetSettings()->sync();
  }

  void cleanupTestCase() {
    for (RunningJob *job : JobRegistry::instance().jobs()) {
      if (job->isRunning()) {
        job->stop();
      }
    }
    const QString path = Database::path();
    Database::closeForThread();
    QFile::remove(path);
    QFile::remove(iniPath());
  }

  // The message from the operating system is passed on once. Somebody who
  // presses Ctrl+C twice, or a supervisor that sends SIGTERM and then SIGINT,
  // is still only asking for one thing.
  void beingAskedTwiceIsBeingAskedOnce() {
    QSignalSpy requested(&ShutdownSignal::instance(),
                         &ShutdownSignal::requested);

    ShutdownSignal::instance().deliver(QStringLiteral("SIGTERM"));
    ShutdownSignal::instance().deliver(QStringLiteral("SIGINT"));

    QCOMPARE(requested.count(), 1);
    QCOMPARE(requested.first().at(0).toString(), QStringLiteral("SIGTERM"));
  }

  // Something has to be going for stopping to mean anything.
  void aQueuedTransferIsUnderWay() {
    JobOptions *first = makeTask(QStringLiteral("first"));
    JobOptions *second = makeTask(QStringLiteral("second"));

    AppCore::instance().start();
    JobQueue::instance().start();

    mFirstRequest = JobQueue::instance().enqueue(first->uniqueId.toString());
    JobQueue::instance().enqueue(second->uniqueId.toString());

    QVERIFY2(waitUntil([]() {
               return JobRegistry::instance().runningCount() == 1;
             }),
             "the queue never started its first entry");
    QCOMPARE(JobQueue::instance().count(), 2);
    QCOMPARE(JobQueue::instance().runningRequestId(), mFirstRequest);
  }

  // Asked of rclone by the core itself. Only the window used to ask, so a
  // daemon reported an empty version -- and what a remote is allowed to do
  // is decided from it.
  void theCoreFindsOutWhichRcloneItHas() {
    QVERIFY2(waitUntil([]() {
               return !GetSettings()
                           ->value(QStringLiteral("Settings/rcloneVersion"))
                           .toString()
                           .isEmpty();
             }),
             "the core never asked rclone for its version");
    QCOMPARE(GetSettings()
                 ->value(QStringLiteral("Settings/rcloneVersion"))
                 .toString(),
             QStringLiteral("1.99.0"));
  }

  // The one that matters. Everything "docker stop" needs to be true ten
  // seconds after it asks.
  void stoppingEndsWhatIsRunningAndLeavesTheQueueAsItWas() {
    QSignalSpy stopping(&AppCore::instance(), &AppCore::stopping);

    const int left = AppCore::instance().shutdown(8000);

    QCOMPARE(stopping.count(), 1);
    QVERIFY(AppCore::instance().isStopping());

    // Nothing is still going, and it did not take the full half minute the
    // transfer would have run for.
    QCOMPARE(left, 0);
    QCOMPARE(JobRegistry::instance().runningCount(), 0);

    // The history says what happened to it, rather than "running" forever
    // about a process that no longer exists.
    RunningJob *job = JobRegistry::instance().find(mFirstRequest);
    QVERIFY(job != nullptr);
    QCOMPARE(job->finalStatus(), QStringLiteral("stopped"));
    QCOMPARE(RunHistory::find(mFirstRequest).state, QStringLiteral("stopped"));

    // And the queue is exactly as it was: both entries still there, the one
    // that was running included, because it was stopped by the program going
    // away and not by anybody wanting it out.
    QCOMPARE(JobQueue::instance().count(), 2);
    QCOMPARE(JobQueue::instance().entries().first().requestId, mFirstRequest);

    // Not paused, either. Pausing is remembered across restarts, so a queue
    // paused in order to stop would come back paused -- and a daemon that
    // restarts into a stopped queue has quietly stopped doing its job.
    QVERIFY2(JobQueue::instance().isRunning(),
             "stopping left the queue paused for next time");
  }

  // Stopping the first job must not be the queue's cue to start the second.
  // That is what a job ending normally means, and it is exactly wrong here.
  void theNextEntryIsNotStartedBehindIt() {
    QTest::qWait(400);
    QCOMPARE(JobRegistry::instance().runningCount(), 0);
    QCOMPARE(JobRegistry::instance().jobs().size(), 1);
    QVERIFY(!JobQueue::instance().advance());
  }

  // And what was stored is what will be read back: a restart finds both
  // entries and a queue that is running.
  void aRestartFindsTheQueueWhereItWasLeft() {
    QVERIFY(JobQueue::instance().save());
    QCOMPARE(GetSettings()
                 ->value(QStringLiteral("Settings/queueStatus"))
                 .toBool(),
             true);
    QCOMPARE(QueueStore::load().size(), 2);
  }

  // Asking a second time is harmless and says the same thing.
  void stoppingTwiceIsStoppingOnce() {
    QSignalSpy stopping(&AppCore::instance(), &AppCore::stopping);
    QCOMPARE(AppCore::instance().shutdown(100), 0);
    QCOMPARE(stopping.count(), 0);
  }

  // A schedule asked to run while the program is going away is refused, and
  // says why rather than starting something only to kill it.
  void nothingNewIsStartedOnceStoppingHasBegun() {
    QString reason;
    const QString request =
        AppCore::instance().runNow(QStringLiteral("{no-such}"), &reason);
    QVERIFY(request.isEmpty());
    QVERIFY(!reason.isEmpty());
  }
};

QTEST_GUILESS_MAIN(TestShutdown)
#include "test_shutdown.moc"
