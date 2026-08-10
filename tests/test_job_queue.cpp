#include "database.h"
#include "job_options.h"
#include "job_queue.h"
#include "job_registry.h"
#include "list_of_job_options.h"
#include "utils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

// The criterion for S3: a queue runs itself to the end with no widget
// anywhere. These tests link rbcore only, so if any of the rules for starting
// the next task had stayed in the window, none of this would build -- let
// alone pass.
class TestJobQueue : public QObject {
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

  static QString dbPath() {
    return QDir(appDir()).filePath(QStringLiteral("queue-test.db"));
  }

  // A task that copies one small file, which rclone finishes in well under a
  // second -- the queue is what is under test, not the transfer.
  JobOptions *makeCopyTask(const QString &name, const QString &dest) {
    auto *task = new JobOptions(false);
    task->description = name;
    task->operation = JobOptions::Copy;
    task->jobType = JobOptions::Upload;
    task->source = mSource->path();
    task->dest = dest;
    task->isFolder = true;
    task->uniqueId = QUuid::createUuid();
    ListOfJobOptions::getInstance()->Persist(task);
    return task;
  }

  // Runs the event loop until the queue has nothing left to do, or gives up.
  bool waitForQueue(int timeoutMs = 60000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
      if (JobQueue::instance().isEmpty() &&
          !JobQueue::instance().taskIsRunning()) {
        return true;
      }
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
      QThread::msleep(10);
    }
    return false;
  }

  std::unique_ptr<QTemporaryDir> mScratch;
  std::unique_ptr<QTemporaryDir> mSource;
  std::unique_ptr<QTemporaryDir> mDest;
  QString mRclone;

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
    QVERIFY(ini.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ini.close();
    QVERIFY2(IsPortableMode(), "portable mode did not take effect");

    Database::closeForThread();
    QFile::remove(dbPath());
    Database::setPath(dbPath());
    QVERIFY2(Database::connection().isOpen(),
             qPrintable(Database::lastError()));

    SetRclone(mRclone);
    SetRcloneConf(QString());

    // Nothing here competes with the queue, so it drives itself.
    JobQueue::instance().setDrivesItself(true);

    mSource.reset(new QTemporaryDir);
    mDest.reset(new QTemporaryDir);
    QVERIFY(mSource->isValid() && mDest->isValid());

    QFile payload(QDir(mSource->path()).filePath("hello.txt"));
    QVERIFY(payload.open(QIODevice::WriteOnly));
    payload.write("queued without a window\n");
    payload.close();
  }

  void cleanupTestCase() {
    const QString db = Database::path();
    Database::closeForThread();
    QFile::remove(db);
    QFile::remove(iniPath());
  }

  void cleanup() {
    JobQueue::instance().pause();
    JobQueue::instance().clear();
  }

  void enqueueAddsToTheBackAndGivesEachRunAnId() {
    JobQueue &queue = JobQueue::instance();
    queue.pause();

    JobOptions *first = makeCopyTask("first", mDest->path());
    JobOptions *second = makeCopyTask("second", mDest->path());

    const QString a = queue.enqueue(first->uniqueId.toString());
    const QString b = queue.enqueue(second->uniqueId.toString());

    QCOMPARE(queue.count(), 2);
    QCOMPARE(queue.entries()[0].requestId, a);
    QCOMPARE(queue.entries()[1].requestId, b);

    // Two runs of the same task have to be tellable apart.
    QVERIFY(a != b);
    QVERIFY(!queue.taskIsRunning()); // paused: nothing may start
  }

  void aPausedQueueStartsNothing() {
    JobQueue &queue = JobQueue::instance();
    queue.pause();

    JobOptions *task = makeCopyTask("paused", mDest->path());
    queue.enqueue(task->uniqueId.toString());

    QVERIFY(!queue.advance());
    QVERIFY(!queue.taskIsRunning());
    QCOMPARE(queue.count(), 1);
  }

  void movingReordersButNeverPastTheRunningEntry() {
    JobQueue &queue = JobQueue::instance();
    queue.pause();

    JobOptions *a = makeCopyTask("a", mDest->path());
    JobOptions *b = makeCopyTask("b", mDest->path());
    JobOptions *c = makeCopyTask("c", mDest->path());
    queue.enqueue(a->uniqueId.toString());
    queue.enqueue(b->uniqueId.toString());
    const QString third = queue.enqueue(c->uniqueId.toString());

    QVERIFY(queue.move(2, 1));
    QCOMPARE(queue.entries()[1].requestId, third);

    QVERIFY(!queue.move(0, 5));  // outside the list
    QVERIFY(!queue.move(1, 1));  // nowhere to go
  }

  void clearingLeavesTheEntriesAlone_data() {
    QTest::addColumn<int>("count");
    QTest::newRow("three") << 3;
  }

  void clearingLeavesTheEntriesAlone() {
    JobQueue &queue = JobQueue::instance();
    queue.pause();

    QFETCH(int, count);
    for (int i = 0; i < count; ++i) {
      JobOptions *task = makeCopyTask(QStringLiteral("t%1").arg(i),
                                      mDest->path());
      queue.enqueue(task->uniqueId.toString());
    }
    QCOMPARE(queue.count(), count);

    queue.clear();
    QCOMPARE(queue.count(), 0);
  }

  void anEntryWhoseTaskIsGoneDoesNotStopTheQueue() {
    JobQueue &queue = JobQueue::instance();
    queue.pause();

    JobOptions *doomed = makeCopyTask("deleted later", mDest->path());
    JobOptions *good = makeCopyTask("still here", mDest->path());

    queue.enqueue(doomed->uniqueId.toString());
    queue.enqueue(good->uniqueId.toString());

    ListOfJobOptions::getInstance()->Forget(doomed);

    queue.start();
    QVERIFY2(waitForQueue(), "the queue did not finish");

    // The dead entry went without a fight and the live one still ran.
    QVERIFY(QFile::exists(QDir(mDest->path()).filePath("hello.txt")));
  }

  // Step 0 of docs/QUEUE-MOVE.md: while the window still drives the queue,
  // this one must not. Two drivers would both start the head entry, and the
  // second start is a second rclone writing the same destination.
  void withTheSwitchOffTheQueueDoesNotMoveOnItsOwn() {
    JobQueue &queue = JobQueue::instance();
    queue.pause();
    queue.setDrivesItself(false);

    QTemporaryDir first, second;
    QVERIFY(first.isValid() && second.isValid());
    const QString a =
        queue.enqueue(makeCopyTask("first", first.path())->uniqueId.toString());
    queue.enqueue(makeCopyTask("second", second.path())->uniqueId.toString());

    QSignalSpy started(&queue, &JobQueue::taskStarted);

    // Switching the queue on records that it is on. It must not start
    // anything: while the window drives, the window starts.
    queue.start();
    for (int i = 0; i < 20; ++i) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      QThread::msleep(10);
    }
    QCOMPARE(started.count(), 0);
    QCOMPARE(queue.count(), 2);
    QVERIFY(queue.isRunning());
    QVERIFY(!QFile::exists(QDir(first.path()).filePath("hello.txt")));

    // Handing the wheel over is all it takes for it to carry on by itself.
    Q_UNUSED(a);
    queue.setDrivesItself(true);
    QVERIFY2(waitForQueue(), "the queue did not finish once it was driving");
    QVERIFY(QFile::exists(QDir(first.path()).filePath("hello.txt")));
    QVERIFY(QFile::exists(QDir(second.path()).filePath("hello.txt")));
  }

  // The whole point of S3.
  void aQueueRunsItselfToTheEndWithNoWidget() {
    JobQueue &queue = JobQueue::instance();
    queue.pause();

    QTemporaryDir first, second, third;
    QVERIFY(first.isValid() && second.isValid() && third.isValid());

    queue.enqueue(makeCopyTask("one", first.path())->uniqueId.toString());
    queue.enqueue(makeCopyTask("two", second.path())->uniqueId.toString());
    queue.enqueue(makeCopyTask("three", third.path())->uniqueId.toString());
    QCOMPARE(queue.count(), 3);

    QSignalSpy emptied(&queue, &JobQueue::emptied);
    QSignalSpy startedSpy(&queue, &JobQueue::taskStarted);

    queue.start();
    QVERIFY2(waitForQueue(), "the queue did not empty on its own");

    QCOMPARE(queue.count(), 0);
    QVERIFY(!queue.taskIsRunning());
    QCOMPARE(startedSpy.count(), 3);
    QCOMPARE(emptied.count(), 1);

    // Each one really copied, in its own destination.
    QVERIFY(QFile::exists(QDir(first.path()).filePath("hello.txt")));
    QVERIFY(QFile::exists(QDir(second.path()).filePath("hello.txt")));
    QVERIFY(QFile::exists(QDir(third.path()).filePath("hello.txt")));
  }

  // The queue survives a restart because it is stored, not because the window
  // remembered it.
  void theQueueIsReadBackAfterARestart() {
    JobQueue &queue = JobQueue::instance();
    queue.pause();

    JobOptions *task = makeCopyTask("kept", mDest->path());
    const QString requestId = queue.enqueue(task->uniqueId.toString());
    QCOMPARE(queue.count(), 1);

    // What a restart amounts to for the store.
    queue.load();

    QCOMPARE(queue.count(), 1);
    QCOMPARE(queue.entries().first().requestId, requestId);
    QCOMPARE(queue.entries().first().taskId, task->uniqueId.toString());
    QVERIFY(!queue.isRunning()); // it was paused, and that is remembered too
  }
};

QTEST_MAIN(TestJobQueue)
#include "test_job_queue.moc"
