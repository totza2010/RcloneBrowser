#include "remote_registry.h"
#include "utils.h"

QList<Remote> ParseListRemotes(const QByteArray &output) {
  QList<Remote> remotes;

  const QStringList lines =
      QString::fromUtf8(output).split(QLatin1Char('\n'), Qt::SkipEmptyParts);

  for (const QString &raw : lines) {
    const QString line = raw.trimmed();
    if (line.isEmpty()) {
      continue;
    }

    // "name: type". Split on the first colon only: the name cannot contain
    // one -- rclone forbids it, because a colon is what ends a remote name in
    // a path -- but the type is whatever rclone prints, and being strict
    // about the count of colons is what made an unexpected line disappear.
    const int colon = line.indexOf(QLatin1Char(':'));
    if (colon <= 0) {
      continue;
    }

    Remote remote;
    remote.name = line.left(colon).trimmed();
    remote.type = line.mid(colon + 1).trimmed();

    // A remote with no type is not something to show: it would get the
    // unknown icon and no capabilities, and it means the line was not what
    // this expects.
    if (remote.name.isEmpty() || remote.type.isEmpty()) {
      continue;
    }
    remotes.append(remote);
  }

  return remotes;
}

RemoteRegistry &RemoteRegistry::instance() {
  static RemoteRegistry registry;
  return registry;
}

RemoteRegistry::RemoteRegistry(QObject *parent) : QObject(parent) {}

const Remote *RemoteRegistry::find(const QString &name) const {
  if (name.isEmpty()) {
    return nullptr;
  }
  QString wanted = name;
  if (wanted.endsWith(QLatin1Char(':'))) {
    wanted.chop(1);
  }
  for (const Remote &remote : mRemotes) {
    if (remote.name == wanted) {
      return &remote;
    }
  }
  return nullptr;
}

void RemoteRegistry::setRemotes(const QList<Remote> &remotes) {
  mRemotes = remotes;
  emit refreshed();
}

void RemoteRegistry::refresh() {
  if (mProcess != nullptr) {
    return; // one at a time; the answer is the same either way
  }

  mProcess = new QProcess(this);

  QObject::connect(
      mProcess,
      static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
          &QProcess::finished),
      this, [this](int exitCode, QProcess::ExitStatus) {
        const QByteArray out = mProcess->readAllStandardOutput();
        const QString err =
            QString::fromUtf8(mProcess->readAllStandardError()).trimmed();

        mProcess->deleteLater();
        mProcess = nullptr;

        if (exitCode != 0) {
          // An encrypted config is not a failure, it is a question. Told
          // apart here so the caller does not have to read rclone's stderr
          // for itself.
          if (err.contains(QStringLiteral("RCLONE_CONFIG_PASS"))) {
            emit passwordRequired();
          } else {
            emit failed(err.isEmpty()
                            ? QStringLiteral("rclone exited with code %1")
                                  .arg(exitCode)
                            : err);
          }
          return;
        }

        mRemotes = ParseListRemotes(out);
        emit refreshed();
      });

  QObject::connect(mProcess, &QProcess::errorOccurred, this,
                   [this](QProcess::ProcessError) {
                     const QString why = mProcess->errorString();
                     mProcess->deleteLater();
                     mProcess = nullptr;
                     emit failed(why);
                   });

  UseRclonePassword(mProcess);
  mProcess->start(GetRclone(),
                  QStringList() << "listremotes" << GetRcloneConf()
                                << GetDefaultOptionsList("defaultRcloneOptions")
                                << "--long"
                                << "--ask-password=false",
                  QIODevice::ReadOnly);
}
