#include "file_reader.h"
#include "debug_log.h"
#include "utils.h"

#include <QMimeDatabase>
#include <QProcess>

FileReader::FileReader(const QString &remote, const QString &path,
                       qint64 limit, QObject *parent)
    : QObject(parent), mRemote(remote), mPath(path), mLimit(limit) {}

FileReader::~FileReader() { cancel(); }

QString FileReader::ViewOf(const QString &name) {
  const QMimeType type =
      QMimeDatabase().mimeTypeForFile(name, QMimeDatabase::MatchExtension);
  const QString mime = type.name();
  if (mime == QLatin1String("application/pdf")) {
    return QStringLiteral("pdf");
  }
  if (mime.startsWith(QLatin1String("image/"))) {
    return QStringLiteral("image");
  }
  if (mime.startsWith(QLatin1String("video/")) ||
      mime.startsWith(QLatin1String("audio/"))) {
    return QStringLiteral("media");
  }
  // Text is whatever is a kind of plain text: source code, JSON, subtitles,
  // HTML -- which is shown as what it is written in, not as a page.
  if (type.inherits(QStringLiteral("text/plain")) ||
      mime.startsWith(QLatin1String("text/"))) {
    return QStringLiteral("text");
  }
  return QString();
}

QString FileReader::TypeOf(const QString &name) {
  const QString view = ViewOf(name);
  if (view == QLatin1String("text") || view.isEmpty()) {
    return QStringLiteral("text/plain; charset=utf-8");
  }
  return QMimeDatabase()
      .mimeTypeForFile(name, QMimeDatabase::MatchExtension)
      .name();
}

void FileReader::start() {
  if (mProcess != nullptr) {
    return;
  }
  mCancelled = false;
  mData.clear();

  mProcess = new QProcess(this);
  mProcess->setProcessChannelMode(QProcess::SeparateChannels);

  QObject::connect(mProcess, &QProcess::readyReadStandardOutput, this,
                   [this]() { mData += mProcess->readAllStandardOutput(); });

  QObject::connect(
      mProcess,
      static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
          &QProcess::finished),
      this, [this](int exitCode, QProcess::ExitStatus status) {
        if (mCancelled || mProcess == nullptr) {
          return;
        }
        mData += mProcess->readAllStandardOutput();
        const QString err =
            QString::fromUtf8(mProcess->readAllStandardError()).trimmed();
        QProcess *done = mProcess;
        mProcess = nullptr;
        done->deleteLater();

        if (exitCode != 0 || status != QProcess::NormalExit) {
          qCDebug(rbRemote) << "reading file failed" << mRemote << mPath
                            << "exit=" << exitCode << "reason=" << err;
          emit failed(err.isEmpty()
                          ? QStringLiteral("rclone exited with code %1")
                                .arg(exitCode)
                          : err);
          return;
        }
        const bool truncated = mData.size() > mLimit;
        if (truncated) {
          mData.truncate(mLimit);
        }
        emit finished(mData, truncated);
      });

  QObject::connect(mProcess, &QProcess::errorOccurred, this,
                   [this](QProcess::ProcessError) {
                     if (mCancelled || mProcess == nullptr) {
                       return;
                     }
                     const QString why = mProcess->errorString();
                     QProcess *bad = mProcess;
                     mProcess = nullptr;
                     bad->deleteLater();
                     emit failed(why);
                   });

  const QString target = mRemote + QLatin1Char(':') + mPath;
  qCDebug(rbRemote) << "reading file" << target << "up to" << mLimit
                    << "bytes";

  UseRclonePassword(mProcess);
  mProcess->start(GetRclone(),
                  QStringList() << "cat" << GetRcloneConf()
                                << GetRemoteModeRcloneOptions()
                                << GetDefaultOptionsList("defaultRcloneOptions")
                                // One byte more than may be kept, so a file
                                // of exactly `limit` bytes is not said to
                                // be cut short.
                                << "--count" << QString::number(mLimit + 1)
                                << target,
                  QIODevice::ReadOnly);
}

void FileReader::cancel() {
  mCancelled = true;
  if (mProcess != nullptr) {
    QProcess *process = mProcess;
    mProcess = nullptr;
    process->disconnect(this);
    process->kill();
    process->waitForFinished(2000);
    process->deleteLater();
  }
}
