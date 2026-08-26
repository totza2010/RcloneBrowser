#include "directory_listing.h"
#include "debug_log.h"
#include "utils.h"

#include <QProcess>

DirectoryListing::DirectoryListing(const QString &remote, const QString &path,
                                   QObject *parent)
    : QObject(parent), mRemote(remote), mPath(path) {}

DirectoryListing::~DirectoryListing() { cancel(); }

QString DirectoryListing::target() const {
  return mRemote + QLatin1Char(':') + mPath;
}

bool DirectoryListing::isRunning() const {
  return mProcess != nullptr && mProcess->state() != QProcess::NotRunning;
}

void DirectoryListing::cancel() {
  if (mProcess == nullptr) {
    return;
  }
  mCancelled = true;

  // Disconnected before killing, so the kill does not arrive as a failure.
  // Somebody who closed the tab does not want to be told the listing they
  // stopped did not finish.
  mProcess->disconnect(this);
  mProcess->kill();
  mProcess->deleteLater();
  mProcess = nullptr;
}

void DirectoryListing::deliver() {
  if (mProcess == nullptr) {
    return;
  }
  const QVector<LsjsonEntry> batch =
      mParser.feed(mProcess->readAllStandardOutput());
  if (batch.isEmpty()) {
    return;
  }
  mCount += batch.size();
  emit entries(batch);
}

void DirectoryListing::start() {
  if (mProcess != nullptr) {
    return; // already going; asking twice is the same question
  }
  mCancelled = false;
  mCount = 0;

  mProcess = new QProcess(this);
  mProcess->setProcessChannelMode(QProcess::SeparateChannels);

  QObject::connect(mProcess, &QProcess::readyReadStandardOutput, this,
                   [this]() { deliver(); });

  QObject::connect(
      mProcess,
      static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
          &QProcess::finished),
      this, [this](int exitCode, QProcess::ExitStatus status) {
        // A process that failed to start reports errorOccurred first, and
        // that handler has already said so and let the process go. Qt may
        // still deliver finished() afterwards, and reading from the process
        // then is a crash rather than a second message.
        if (mCancelled || mProcess == nullptr) {
          return;
        }
        // Whatever is left in the buffer: the last entries of a directory
        // arrive with the process ending, and dropping them would show a
        // folder that is quietly short of a few files.
        deliver();

        const QString err =
            QString::fromUtf8(mProcess->readAllStandardError()).trimmed();

        QProcess *finishedProcess = mProcess;
        mProcess = nullptr;
        finishedProcess->deleteLater();

        if (exitCode != 0 || status != QProcess::NormalExit) {
          qCDebug(rbRemote) << "failed path=" << target() << "exit=" << exitCode
                            << "reason=" << err;
          emit failed(err.isEmpty()
                          ? QStringLiteral("rclone exited with code %1")
                                .arg(exitCode)
                          : err);
          return;
        }

        // Said even when it read nothing, because an empty directory and a
        // directory that was never read look the same on screen.
        qCDebug(rbRemote) << "read path=" << target() << "entries=" << mCount
                          << (mParser.sawMalformedObject()
                                  ? "(with unreadable entries)"
                                  : "");
        emit finished();
      });

  QObject::connect(mProcess, &QProcess::errorOccurred, this,
                   [this](QProcess::ProcessError) {
                     if (mCancelled || mProcess == nullptr) {
                       return;
                     }
                     const QString why = mProcess->errorString();
                     QProcess *badProcess = mProcess;
                     mProcess = nullptr;
                     badProcess->deleteLater();

                     qCDebug(rbRemote)
                         << "failed path=" << target() << "reason=" << why;
                     emit failed(why);
                   });

  qCDebug(rbRemote) << "reading path=" << target();

  UseRclonePassword(mProcess);
  mProcess->start(GetRclone(),
                  QStringList() << "lsjson" << GetRcloneConf()
                                << GetRemoteModeRcloneOptions()
                                << GetShowHidden()
                                << GetDefaultOptionsList("defaultRcloneOptions")
                                << target(),
                  QIODevice::ReadOnly);
}
