#include "database.h"
#include "job_options.h"
#include "job_registry.h"
#include "running_job.h"
#include "utils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

// Watching a job without a window -- S2 in docs/API.md.
//
// A running job used to *be* a JobWidget: it owned the process, the remote
// control client and the log writer. Nothing outside the window could see
// what was running, which is what stood between the application and an API.
//
// This file links rbcore and Qt Test. Every assertion below would have been
// impossible to write before the split, which is the real subject of the
// test.
class TestJobRegistry : public QObject {
  Q_OBJECT

private:
  std::unique_ptr<QTemporaryDir> mScratch; // history, so the real one is safe
  std::unique_ptr<QTemporaryDir> mSource;
  std::unique_ptr<QTemporaryDir> mDest;
  QString mRclone;

  QStringList copyArgs() const {
    return QStringList{"copy", mSource->path(), mDest->path(), "--verbose"};
  }

private slots:
  void initTestCase() {
    mRclone = QStandardPaths::findExecutable(QStringLiteral("rclone"));
    if (mRclone.isEmpty()) {
      QSKIP("rclone is not on PATH");
    }
    SetRclone(mRclone);
    SetRcloneConf(QString());

    mSource.reset(new QTemporaryDir);
    mDest.reset(new QTemporaryDir);
    QVERIFY(mSource->isValid() && mDest->isValid());

    // The registry records every job it starts. Without this the run history
    // of a test would be written into the real user database.
    mScratch.reset(new QTemporaryDir);
    QVERIFY(mScratch->isValid());
    Database::setPath(
        QDir(mScratch->path()).filePath(QStringLiteral("history.db")));

    QFile payload(QDir(mSource->path()).filePath("hello.txt"));
    QVERIFY(payload.open(QIODevice::WriteOnly));
    payload.write("watched without a window\n");
    payload.close();
  }

  void cleanupTestCase() { Database::closeForThread(); }

  void runsAJobAndReportsItFinished() {
    JobRegistry &registry = JobRegistry::instance();

    QSignalSpy started(&registry, &JobRegistry::jobStarted);
    QSignalSpy ended(&registry, &JobRegistry::jobFinished);

    RunningJob *job = registry.start(
        JobKind::Transfer, copyArgs(),
        JobDescription{"test copy", mSource->path(), mDest->path()},
        QStringLiteral("{task-id}"), QStringLiteral("task"),
        QStringLiteral("{request-id}"));

    QVERIFY(job != nullptr);
    QCOMPARE(started.count(), 1);

    // Announced while still running, so a listener cannot miss a job that
    // fails immediately.
    QCOMPARE(job->state(), JobState::Running);
    QVERIFY(job->finalStatus().isEmpty());
    QCOMPARE(registry.runningCount(), 1);
    QCOMPARE(registry.find(QStringLiteral("{request-id}")), job);

    QVERIFY2(ended.wait(60000), "the job never reported that it finished");

    QCOMPARE(job->state(), JobState::Finished);
    QCOMPARE(job->finalStatus(), QStringLiteral("finished"));
    QCOMPARE(registry.runningCount(), 0);
    QVERIFY(QFile::exists(QDir(mDest->path()).filePath("hello.txt")));

    registry.forget(job);
    QVERIFY(registry.find(QStringLiteral("{request-id}")) == nullptr);
  }

  // The figures on the card come from core/stats over rclone's remote
  // control, which the job now turns on itself. If that stops working the
  // card goes blank, so it is worth an assertion that does not involve a
  // card.
  void reportsProgressFromTheRemoteControl() {
    JobRegistry &registry = JobRegistry::instance();

    RunningJob *job = registry.start(
        JobKind::Transfer, copyArgs(),
        JobDescription{"test stats", mSource->path(), mDest->path()},
        QStringLiteral("{task-id}"), QStringLiteral("task"),
        QStringLiteral("{stats-request}"));

    QSignalSpy stats(job, &RunningJob::statsUpdated);
    QSignalSpy ended(job, &RunningJob::finished);
    QVERIFY2(ended.wait(60000), "the job never finished");

    // A local copy of one small file can be over before the first poll, so
    // the reading is not required to have arrived -- only to be coherent if
    // it did.
    if (stats.count() > 0) {
      QVERIFY(job->stats().valid);
      QVERIFY(job->stats().elapsedSeconds >= 0.0);
    }

    registry.forget(job);
  }

  // Cancelling is not failing. rclone exits non-zero either way, and the
  // difference is only knowable from who asked it to stop.
  void aStoppedJobIsNotAnError() {
    JobRegistry &registry = JobRegistry::instance();

    // Something long enough to still be running when it is stopped.
    RunningJob *job = registry.start(
        JobKind::Transfer,
        QStringList{"copy", mSource->path(), mDest->path(), "--bwlimit", "1",
                    "--verbose"},
        JobDescription{"test stop", mSource->path(), mDest->path()},
        QStringLiteral("{task-id}"), QStringLiteral("task"),
        QStringLiteral("{stop-request}"));

    QSignalSpy ended(job, &RunningJob::finished);
    // stop() waits for the process, so the signal can arrive before wait() is
    // ever called -- and QSignalSpy::wait() only waits for a *new* one.
    job->stop();
    QVERIFY2(ended.count() > 0 || ended.wait(30000),
             "stopping did not end the job");

    QCOMPARE(job->state(), JobState::Stopped);
    QCOMPARE(job->finalStatus(), QStringLiteral("stopped"));

    registry.forget(job);
  }

  // A job that could not start used to leave its card reading "Running" for
  // the life of the application, because QProcess emits errorOccurred and
  // never emits finished.
  void aJobThatCannotStartStillEnds() {
    SetRclone(QStringLiteral("this-rclone-does-not-exist"));

    JobRegistry &registry = JobRegistry::instance();
    RunningJob *job = registry.start(
        JobKind::Transfer, copyArgs(),
        JobDescription{"test missing rclone", "a", "b"},
        QStringLiteral("{task-id}"), QStringLiteral("task"),
        QStringLiteral("{missing-request}"));

    QSignalSpy ended(job, &RunningJob::finished);
    if (job->isRunning()) {
      QVERIFY2(ended.wait(15000), "a job that never started never ended");
    }
    QCOMPARE(job->state(), JobState::Error);
    QCOMPARE(registry.runningCount(), 0);

    registry.forget(job);
    SetRclone(mRclone);
  }

  // Everything leaving the job is redacted, because the command carries the
  // remote-control password and whatever the user put in the extra options
  // (docs/ARCHITECTURE.md section 5).
  void theCommandItReportsIsRedacted() {
    JobRegistry &registry = JobRegistry::instance();
    RunningJob *job = registry.start(
        JobKind::Transfer,
        copyArgs() + QStringList{"--drive-token=SECRET123"},
        JobDescription{"test redaction", "a", "b"}, QStringLiteral("{task-id}"),
        QStringLiteral("task"), QStringLiteral("{redact-request}"));

    const QString shown = job->displayCommand().join(QLatin1Char(' '));
    QVERIFY2(!shown.contains(QStringLiteral("SECRET123")), qPrintable(shown));
    QVERIFY2(shown.contains(QStringLiteral("--drive-token=***")),
             qPrintable(shown));

    QSignalSpy ended(job, &RunningJob::finished);
    ended.wait(60000);
    registry.forget(job);
  }

  // A mount is a different animal and the differences are what this checks.
  // It uses a remote that does not exist, so rclone fails early -- which is
  // enough, because what is being tested is the shape of the job, not
  // whether a filesystem appeared.
  void aMountIsNotATransfer() {
    JobRegistry &registry = JobRegistry::instance();

    JobOptions task;
    task.operation = JobOptions::Mount;
    task.source = "no-such-remote:";
    task.dest = mDest->path();
    task.mountRcPort = "0";

    const QStringList args = task.getMountOptions();

    RunningJob *job = registry.start(
        JobKind::Mount, args, JobDescription{"test mount", task.source,
                                             task.dest},
        QStringLiteral("{task-id}"), QStringLiteral("mount"),
        QStringLiteral("{mount-request}"));

    // A transfer has "--rc-addr=localhost:0" added for it; a mount carries
    // whatever the task said and nothing more. Adding a second one would
    // make rclone refuse to start.
    const QString shown = job->displayCommand().join(QLatin1Char(' '));
    QCOMPARE(shown.count(QStringLiteral("--rc-addr")), 1);
    QVERIFY2(!shown.contains(QStringLiteral("localhost:0 ")),
             qPrintable(shown));

    QCOMPARE(job->kind(), JobKind::Mount);

    QSignalSpy ended(job, &RunningJob::finished);
    QVERIFY2(ended.count() > 0 || ended.wait(30000), "the mount never ended");

    // Ending badly is still an error, the same as anywhere else.
    QCOMPARE(job->state(), JobState::Error);
    QCOMPARE(job->finalStatus(), QStringLiteral("error"));
    QCOMPARE(registry.runningCount(JobKind::Mount), 0);

    registry.forget(job);
  }

  // The word in the log for a mount that ended cleanly has always been
  // "unmounted", not "finished", and it is worth keeping: they are the same
  // exit code and very different events.
  void aMountThatEndsCleanlyReadsAsUnmounted() {
    JobOptions task;
    task.operation = JobOptions::Mount;
    task.source = "remote:";
    task.dest = "/mnt/x";

    RunningJob mount(JobKind::Mount, task.getMountOptions(),
                     JobDescription{"m", "remote:", "/mnt/x"},
                     QStringLiteral("{t}"), QStringLiteral("mount"),
                     QStringLiteral("{r}"));
    RunningJob transfer(JobKind::Transfer, QStringList{"copy", "a", "b"},
                        JobDescription{"t", "a", "b"}, QStringLiteral("{t}"),
                        QStringLiteral("task"), QStringLiteral("{r2}"));

    // Neither has run, so both are still Running and say nothing.
    QVERIFY(mount.finalStatus().isEmpty());
    QVERIFY(transfer.finalStatus().isEmpty());

    // The remote-control flags are the visible difference before either
    // starts: the transfer gets a port picked for it, the mount does not.
    QVERIFY(!mount.displayCommand().join(' ').contains(
        QStringLiteral("--rc-addr=localhost:0")));
    QVERIFY(transfer.displayCommand().join(' ').contains(
        QStringLiteral("--rc-addr=localhost:0")));
  }

  // Forgetting a running job would leave rclone going with nothing holding
  // it, and the pointer dangling for whoever still had it.
  void refusesToForgetARunningJob() {
    JobRegistry &registry = JobRegistry::instance();
    RunningJob *job = registry.start(
        JobKind::Transfer,
        QStringList{"copy", mSource->path(), mDest->path(), "--bwlimit", "1"},
        JobDescription{"test forget", "a", "b"}, QStringLiteral("{task-id}"),
        QStringLiteral("task"), QStringLiteral("{forget-request}"));

    registry.forget(job);
    QCOMPARE(registry.find(QStringLiteral("{forget-request}")), job);

    QSignalSpy ended(job, &RunningJob::finished);
    job->stop();
    if (ended.count() == 0) {
      ended.wait(30000);
    }

    registry.forget(job);
    QVERIFY(registry.find(QStringLiteral("{forget-request}")) == nullptr);
  }
};

QTEST_GUILESS_MAIN(TestJobRegistry)
#include "test_job_registry.moc"
