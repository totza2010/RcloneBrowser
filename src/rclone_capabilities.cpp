#include "rclone_capabilities.h"
#include "utils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>

// TEST: (V-09) เปิดแท็บ teldrive -> ปุ่ม Dedupe และ Cleanup ต้องถูกปิด/ซ่อน
// ส่วน Link และ Info ต้องยังใช้ได้ · เทียบกับ gdrive ที่ Dedupe ต้องเปิดได้
// ข้อมูลอ้างอิง: tests/fixtures/teldrive_features.json
RcloneCapabilities
RcloneCapabilities::fromBackendFeatures(const QByteArray &json) {
  RcloneCapabilities caps;

  QJsonParseError error{};
  const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject()) {
    return caps; // stays known == false, so nothing gets disabled
  }

  const QJsonObject root = doc.object();
  const QJsonObject features = root.value("Features").toObject();

  // Missing keys default to false rather than being treated as an error:
  // custom backends do not always report the full feature set.
  caps.about = features.value("About").toBool(false);
  caps.cleanUp = features.value("CleanUp").toBool(false);
  caps.command = features.value("Command").toBool(false);
  caps.copy = features.value("Copy").toBool(false);
  caps.duplicateFiles = features.value("DuplicateFiles").toBool(false);
  caps.listR = features.value("ListR").toBool(false);
  caps.move = features.value("Move").toBool(false);
  caps.publicLink = features.value("PublicLink").toBool(false);
  caps.purge = features.value("Purge").toBool(false);
  caps.putStream = features.value("PutStream").toBool(false);

  const QJsonArray hashes = root.value("Hashes").toArray();
  for (const QJsonValue &hash : hashes) {
    const QString name = hash.toString();
    if (!name.isEmpty()) {
      caps.hashes << name;
    }
  }

  caps.precisionNs = root.value("Precision").toVariant().toLongLong();

  caps.known = true;
  return caps;
}

RcloneCapabilityRegistry &RcloneCapabilityRegistry::instance() {
  static RcloneCapabilityRegistry registry;
  return registry;
}

RcloneCapabilityRegistry::RcloneCapabilityRegistry(QObject *parent)
    : QObject(parent) {}

RcloneCapabilities RcloneCapabilityRegistry::get(const QString &remote) {
  auto it = mCache.constFind(remote);
  if (it != mCache.constEnd()) {
    return it.value();
  }
  query(remote);
  return RcloneCapabilities(); // known == false
}

void RcloneCapabilityRegistry::invalidate(const QString &remote) {
  mCache.remove(remote);
}

void RcloneCapabilityRegistry::query(const QString &remote) {
  if (mInFlight.contains(remote)) {
    return;
  }
  mInFlight.insert(remote);

  auto *process = new QProcess(this);
  // Keep stderr out of the JSON: rclone writes warnings there and merging the
  // channels would corrupt the document.
  process->setProcessChannelMode(QProcess::SeparateChannels);
  UseRclonePassword(process);

  QPointer<RcloneCapabilityRegistry> guard(this);
  QObject::connect(
      process,
      static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
          &QProcess::finished),
      this, [this, process, remote, guard](int status, QProcess::ExitStatus) {
        process->deleteLater();
        if (!guard) {
          return;
        }
        mInFlight.remove(remote);

        if (status != 0) {
          // Leave the remote uncached so a later attempt can retry, and leave
          // the UI permissive rather than disabling actions on a failed probe.
          return;
        }

        const RcloneCapabilities caps =
            RcloneCapabilities::fromBackendFeatures(
                process->readAllStandardOutput());
        if (!caps.known) {
          return;
        }
        mCache.insert(remote, caps);
        emit capabilitiesChanged(remote, caps);
      });

  QObject::connect(process, &QProcess::errorOccurred, this,
                   [this, process, remote, guard]() {
                     process->deleteLater();
                     if (guard) {
                       mInFlight.remove(remote);
                     }
                   });

  QStringList args;
  args << "backend"
       << "features" << GetRcloneConf() << (remote + ":");
  process->start(GetRclone(), args, QIODevice::ReadOnly);
}
