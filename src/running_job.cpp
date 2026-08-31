#include "debug_log.h"
#include "running_job.h"
#include "utils.h"

#include <QDir>
#include <QMetaEnum>

RunningJob::RunningJob(JobKind kind, const QStringList &args,
                       const JobDescription &description, const QString &taskId,
                       const QString &transferMode, const QString &requestId,
                       QObject *parent)
    : QObject(parent), mKind(kind),
      // A transfer gets its remote control turned on here rather than at the
      // call site: every transfer wants progress, and one whose progress came
      // from somewhere else would be a second mechanism to keep working. Port
      // 0 lets rclone pick a free one and announce it, which avoids reserving
      // a port that something else could take between the check and the bind.
      //
      // A mount brings its own: the port is part of the saved task, because
      // unmounting on Windows has to reach the same one every time -- there
      // is nobody to read an announcement back to after a restart.
      mArgs(kind == JobKind::Transfer
                ? args + QStringList{QStringLiteral("--rc"),
                                     QStringLiteral("--rc-addr=localhost:0")}
                : args),
      mDescription(description), mTaskId(taskId), mTransferMode(transferMode),
      mRequestId(requestId) {}

QString JobKindToString(JobKind kind) {
  switch (kind) {
  case JobKind::Transfer:
    return QStringLiteral("transfer");
  case JobKind::Mount:
    return QStringLiteral("mount");
  case JobKind::Stream:
    return QStringLiteral("stream");
  case JobKind::Check:
    return QStringLiteral("check");
  case JobKind::Dedupe:
    return QStringLiteral("dedupe");
  case JobKind::Export:
    return QStringLiteral("export");
  case JobKind::Tree:
    return QStringLiteral("tree");
  case JobKind::Size:
    return QStringLiteral("size");
  case JobKind::Link:
    return QStringLiteral("link");
  case JobKind::About:
    return QStringLiteral("about");
  case JobKind::Cleanup:
    return QStringLiteral("cleanup");
  }
  return QStringLiteral("transfer");
}

RunningJob::~RunningJob() {
  if (mRc) {
    mRc->stop();
  }
}

QString RunningJob::finalStatus() const {
  switch (mState) {
  case JobState::Finished:
    // A mount that exits cleanly has been unmounted. The log has always said
    // so and the word is worth keeping.
    return mKind == JobKind::Mount ? QStringLiteral("unmounted")
                                   : QStringLiteral("finished");
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

  // Only a transfer has figures worth polling. A mount serves core/stats too,
  // but it reports the traffic of a filesystem rather than the progress of
  // anything, and the card has never shown it.
  if (mKind == JobKind::Transfer) {
    mRc = new RcClient(this);
    QObject::connect(mRc, &RcClient::statsReceived, this,
                     [this](const JobStats &stats) {
                       mStats = stats;
                       emit statsUpdated(stats);
                     });
    QObject::connect(mRc, &RcClient::unavailable, this,
                     &RunningJob::progressUnavailable);
  }

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
      this, [this](int exitCode, QProcess::ExitStatus status) {
        mCrashed = status != QProcess::NormalExit;
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

  qCDebug(rbJob) << "running" << GetRclone() << "with" << mArgs.size()
                 << "arguments, kind=" << JobKindToString(mKind);

  UseRclonePassword(mProcess);
  UseRcCredentials(mProcess, mRcUser, mRcPass);

  // Held locally because handleFinished() clears the member, and on Windows a
  // missing executable reports FailedToStart from inside start() itself -- so
  // mProcess can already be null by the time start() returns.
  QProcess *process = mProcess;
  process->start(GetRclone(), mArgs + GetRcloneConf(), QIODevice::ReadOnly);
  mEverStarted = isRunning();

  // Deliberately not waitForStarted(): that spins the event loop, which would
  // let the first lines of output be emitted before the caller has had a
  // chance to connect to them. A failure to start arrives through
  // errorOccurred instead, which needs no waiting.
  return isRunning();
}

void RunningJob::handleOutput() {
  while (mProcess->canReadLine()) {
    const QString line = QString(mProcess->readLine()).trimmed();

    // rclone announces the port it settled on. This is the only thing still
    // taken from the output; every figure comes from core/stats instead.
    //
    // A mount needs it too, and for more than figures: it is how the mount
    // script is reached and how unmounting asks rclone to quit. The mount
    // card used to carry its own regex for this line; there is one parser
    // now, in the core, tested against real output.
    if (mRcPort.isEmpty()) {
      if (const quint16 port = ParseRcServingPort(line)) {
        mRcPort = QString::number(port);
        if (mRc && !mRc->isRunning()) {
          mRc->start(port, mRcUser, mRcPass);
        }
        qCDebug(rbJob) << "remote control on port" << mRcPort;
        emit rcPortDiscovered(mRcPort);
        startMountScript();
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

  // The script exists to serve the mount; when the mount is gone it has
  // nothing left to serve.
  if (mScriptProcess != nullptr &&
      mScriptProcess->state() != QProcess::NotRunning) {
    mScriptProcess->kill();
  }

  mExitCode = exitCode;
  mFinishedAt = QDateTime::currentDateTime();

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

void RunningJob::startMountScript() {
  if (mKind != JobKind::Mount || mMountScript.isEmpty() ||
      mScriptProcess != nullptr || mRcPort.isEmpty()) {
    return;
  }

  // Only once the remote control is up: the script is given the port and the
  // login so it can talk to this mount, and there is nothing to give it
  // before rclone says which port it is on.
  mScriptProcess = new QProcess(this);
  mScriptProcess->setProcessChannelMode(QProcess::MergedChannels);

  QObject::connect(mScriptProcess, &QProcess::readyRead, this, [this]() {
    while (mScriptProcess->canReadLine()) {
      const QString line = QString(mScriptProcess->readLine()).trimmed();
      // SECURITY: the script is handed the remote-control login as an
      // argument, and anything that echoes its arguments would print it back
      // (docs/ARCHITECTURE.md section 5).
      const QString safe = RedactOutputLine(line, mRcUser, mRcPass);
      mLog.appendLine(QStringLiteral("[script] ") + safe);
      emit scriptOutputLine(safe);
    }
  });

  QObject::connect(mScriptProcess, &QProcess::errorOccurred, this,
                   [this](QProcess::ProcessError error) {
                     emit scriptFailed(
                         QMetaEnum::fromType<QProcess::ProcessError>()
                             .valueToKey(error));
                   });

  QObject::connect(
      mScriptProcess,
      static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
          &QProcess::finished),
      this, [this](int exitCode, QProcess::ExitStatus) {
        emit scriptFinished(exitCode);
      });

  mScriptProcess->start(QDir::toNativeSeparators(mMountScript),
                        QStringList{GetRclone(), mRcPort, mRcUser, mRcPass,
                                    mDescription.dest},
                        QIODevice::ReadOnly);
}

void RunningJob::unmount() {
  // Unmounting is a request, not a kill: the mount stays up if a file on it
  // is open somewhere. Which is why this reports failure and the transfer
  // path has nothing like it.
  auto *client = new QProcess(this);
  client->setProcessChannelMode(QProcess::MergedChannels);

  QObject::connect(
      client,
      static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
          &QProcess::finished),
      this, [this, client](int status, QProcess::ExitStatus) {
        // Whatever the unmount had to say goes into the job's own output and
        // log. Without this an unmount that quietly failed left no trace at
        // all: the only evidence was a drive letter that would not go away.
        const QString reply = QString::fromUtf8(client->readAll()).trimmed();
        if (!reply.isEmpty()) {
          const QString safe = RedactOutputLine(reply, mRcUser, mRcPass);
          mLog.appendLine(QStringLiteral("[unmount] ") + safe);
          emit outputLine(QStringLiteral("[unmount] ") + safe);
        }

        const QString ending =
            QStringLiteral("[unmount] exit code %1").arg(status);
        mLog.appendLine(ending);
        emit outputLine(ending);

        client->deleteLater();
        if (status != 0 && isRunning()) {
          // Still mounted. Say so rather than leaving a card that claims to
          // be unmounting for ever.
          mStopRequested = false;
          emit stopFailed(QString::number(status));
        }
      });

  QObject::connect(client, &QProcess::errorOccurred, this,
                   [this, client](QProcess::ProcessError) {
                     const QString why =
                         QStringLiteral("[unmount] could not run: %1")
                             .arg(client->errorString());
                     mLog.appendLine(why);
                     emit outputLine(why);
                     if (isRunning()) {
                       mStopRequested = false;
                       emit stopFailed(client->errorString());
                     }
                   });

  // A mount with no remote control cannot be asked to quit on Windows, and
  // killing it leaves the drive letter behind. Better to say so than to look
  // like it worked.
#if defined(Q_OS_WIN32)
  if (mRcPort.isEmpty()) {
    const QString why = QStringLiteral(
        "[unmount] this mount has no remote control port, so it cannot be "
        "asked to quit -- unmount it from Windows instead");
    mLog.appendLine(why);
    emit outputLine(why);
    mStopRequested = false;
    emit stopFailed(QStringLiteral("no rc port"));
    return;
  }
#endif

#if defined(Q_OS_MACOS) || defined(Q_OS_FREEBSD)
  client->start("umount", QStringList{mDescription.dest});
#elif defined(Q_OS_WIN32)
  // The login goes through the environment, same as the mount itself, so it
  // does not show up in the process list of this short-lived client.
  UseRcCredentials(client, mRcUser, mRcPass);
  client->start(GetRclone(),
                QStringList{"rc", "core/quit", "--rc-addr",
                            "localhost:" + mRcPort},
                QIODevice::ReadOnly);
#else
  client->start("fusermount", QStringList{"-u", mDescription.dest});
#endif
}

void RunningJob::stop() {
  if (!isRunning() || mProcess == nullptr) {
    return;
  }

  mStopRequested = true;
  if (mRc) {
    mRc->stop();
  }

  if (mKind == JobKind::Mount) {
    unmount();
    return; // rclone exits on its own once the mount is released
  }

  mProcess->kill();
  mProcess->waitForFinished();
}
