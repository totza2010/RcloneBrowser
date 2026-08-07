#include "running_job.h"
#include "utils.h"

RunningJob::RunningJob(JobKind kind, const QStringList &args,
                       const JobDescription &description, const QString &taskId,
                       const QString &transferMode, const QString &requestId,
                       QObject *parent)
    : QObject(parent), mKind(kind),
      // Turn on the remote control here rather than at the call site: every
      // job wants progress, and a job whose progress came from somewhere else
      // would be a second mechanism to keep working. Port 0 lets rclone pick
      // a free one and announce it, which avoids reserving a port that
      // something else could take between the check and the bind.
      mArgs(args + QStringList{QStringLiteral("--rc"),
                               QStringLiteral("--rc-addr=localhost:0")}),
      mDescription(description), mTaskId(taskId), mTransferMode(transferMode),
      mRequestId(requestId) {}

RunningJob::~RunningJob() {
  if (mRc) {
    mRc->stop();
  }
}

QString RunningJob::finalStatus() const {
  switch (mState) {
  case JobState::Finished:
    return QStringLiteral("finished");
  case JobState::Error:
    return QStringLiteral("error");
  case JobState::Stopped:
    return QStringLiteral("stopped");
  case JobState::Running:
    break;
  }
  return QString();
}

QStringList RunningJob::displayCommand() const {
  return RedactArgs(GetRcloneCmd(mArgs));
}

bool RunningJob::start() {
  mProcess = new QProcess(this);
  mProcess->setProcessChannelMode(QProcess::MergedChannels);

  mRcUser = GenerateRcCredential(10);
  mRcPass = GenerateRcCredential(22);

  mRc = new RcClient(this);
  QObject::connect(mRc, &RcClient::statsReceived, this,
                   [this](const JobStats &stats) {
                     mStats = stats;
                     emit statsUpdated(stats);
                   });
  QObject::connect(mRc, &RcClient::unavailable, this,
                   &RunningJob::progressUnavailable);

  if (JobLogWriter::isEnabled()) {
    // args[0] is the rclone subcommand ("copy", "sync", "move").
    // transferMode is often empty and describes the queue, not the operation.
    // The redacted form is what gets written; the log outlives the window.
    mLog.begin(mArgs.value(0), mTaskId, RedactArgs(mArgs));
  }

  QObject::connect(mProcess, &QProcess::readyRead, this,
                   &RunningJob::handleOutput);
  QObject::connect(
      mProcess,
      static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
          &QProcess::finished),
      this, [this](int exitCode, QProcess::ExitStatus) {
        handleFinished(exitCode);
      });

  // A process that never starts emits errorOccurred and never emits
  // finished, so without this the job would sit in Running for the life of
  // the application -- and so would the card showing it.
  QObject::connect(mProcess, &QProcess::errorOccurred, this,
                   [this](QProcess::ProcessError error) {
                     if (error != QProcess::FailedToStart || !isRunning()) {
                       return;
                     }
                     emit outputLine(QStringLiteral("could not start %1: %2")
                                         .arg(GetRclone(),
                                              mProcess->errorString()));
                     handleFinished(1);
                   });

  UseRclonePassword(mProcess);
  UseRcCredentials(mProcess, mRcUser, mRcPass);

  // Held locally because handleFinished() clears the member, and on Windows a
  // missing executable reports FailedToStart from inside start() itself -- so
  // mProcess can already be null by the time start() returns.
  QProcess *process = mProcess;
  process->start(GetRclone(), mArgs + GetRcloneConf(), QIODevice::ReadOnly);

  if (!isRunning()) {
    return false; // the failure has already been reported
  }
  return process->state() != QProcess::NotRunning ||
         process->waitForStarted(10000);
}

void RunningJob::handleOutput() {
  while (mProcess->canReadLine()) {
    const QString line = QString(mProcess->readLine()).trimmed();

    // rclone announces the port it settled on. This is the only thing still
    // taken from the output; every figure comes from core/stats instead.
    if (mRc && !mRc->isRunning()) {
      if (const quint16 port = ParseRcServingPort(line)) {
        mRc->start(port, mRcUser, mRcPass);
      }
    }

    // Our own polling would otherwise dominate the log at -vv and above.
    if (IsRcPollingNoise(line)) {
      continue;
    }

    // SECURITY: at -vv rclone echoes the remote-control password it read out
    // of the environment. Never let that reach a view, the clipboard, or a
    // log file (docs/ARCHITECTURE.md section 5).
    const QString safe = RedactOutputLine(line, mRcUser, mRcPass);
    mLog.appendLine(safe);
    emit outputLine(safe);
  }
}

void RunningJob::handleFinished(int exitCode) {
  if (mRc) {
    mRc->stop();
  }

  // A job the user cancelled is not a job that failed, even though rclone
  // exits non-zero either way.
  if (mStopRequested) {
    mState = JobState::Stopped;
  } else {
    mState = exitCode == 0 ? JobState::Finished : JobState::Error;
  }

  mLog.finish(finalStatus());

  mProcess->deleteLater();
  mProcess = nullptr;

  emit finished(mState);
}

void RunningJob::stop() {
  if (!isRunning() || mProcess == nullptr) {
    return;
  }

  mStopRequested = true;
  if (mRc) {
    mRc->stop();
  }
  mProcess->kill();
  mProcess->waitForFinished();
}
