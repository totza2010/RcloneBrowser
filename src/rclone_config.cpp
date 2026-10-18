#include "rclone_config.h"
#include "debug_log.h"
#include "utils.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>

namespace {

QString textOf(const QJsonValue &value) {
  if (value.isString()) {
    return value.toString();
  }
  if (value.isBool()) {
    return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
  }
  if (value.isDouble()) {
    return QString::number(value.toDouble(), 'g', 15);
  }
  return QString();
}

QString firstLine(const QString &help) {
  return help.section(QLatin1Char('\n'), 0, 0).trimmed();
}

} // namespace

QList<Provider> ParseProviders(const QByteArray &json) {
  QList<Provider> providers;
  const QJsonArray all = QJsonDocument::fromJson(json).array();
  for (const QJsonValue &value : all) {
    const QJsonObject object = value.toObject();
    Provider provider;
    provider.name = object.value(QStringLiteral("Name")).toString();
    if (provider.name.isEmpty()) {
      continue;
    }
    provider.description = object.value(QStringLiteral("Description")).toString();
    provider.prefix = object.value(QStringLiteral("Prefix")).toString();
    for (const QJsonValue &each : object.value(QStringLiteral("Options")).toArray()) {
      const QJsonObject o = each.toObject();
      ProviderOption option;
      option.name = o.value(QStringLiteral("Name")).toString();
      if (option.name.isEmpty()) {
        continue;
      }
      option.help = o.value(QStringLiteral("Help")).toString();
      option.type = o.value(QStringLiteral("Type")).toString();
      option.defaultValue = o.contains(QStringLiteral("DefaultStr"))
                                ? o.value(QStringLiteral("DefaultStr")).toString()
                                : textOf(o.value(QStringLiteral("Default")));
      option.required = o.value(QStringLiteral("Required")).toBool();
      option.isPassword = o.value(QStringLiteral("IsPassword")).toBool();
      option.sensitive = o.value(QStringLiteral("Sensitive")).toBool();
      option.advanced = o.value(QStringLiteral("Advanced")).toBool();
      option.exclusive = o.value(QStringLiteral("Exclusive")).toBool();
      option.hide = o.value(QStringLiteral("Hide")).toInt();
      for (const QJsonValue &example : o.value(QStringLiteral("Examples")).toArray()) {
        const QJsonObject e = example.toObject();
        option.examples.append({textOf(e.value(QStringLiteral("Value"))),
                                e.value(QStringLiteral("Help")).toString()});
      }
      provider.options.append(option);
    }
    providers.append(provider);
  }
  return providers;
}

const Provider *FindProvider(const QList<Provider> &providers,
                             const QString &type) {
  for (const Provider &provider : providers) {
    if (provider.name == type) {
      return &provider;
    }
  }
  return nullptr;
}

bool LooksSecret(const QString &key) {
  static const QRegularExpression secret(
      QStringLiteral("(pass|secret|token|key|cookie|credential|auth|private)"),
      QRegularExpression::CaseInsensitiveOption);
  return secret.match(key).hasMatch();
}

QJsonArray DescribeSettings(const QJsonObject &section,
                            const Provider *provider) {
  QJsonArray settings;
  QStringList described;

  const auto add = [&settings, &section](const QString &key,
                                         const ProviderOption *option) {
    QJsonObject setting;
    setting.insert(QStringLiteral("name"), key);
    const bool secret = LooksSecret(key) ||
                        (option != nullptr &&
                         (option->isPassword || option->sensitive));
    setting.insert(QStringLiteral("secret"), secret);
    if (!secret) {
      setting.insert(QStringLiteral("value"), textOf(section.value(key)));
    }
    if (option != nullptr) {
      setting.insert(QStringLiteral("help"), firstLine(option->help));
      setting.insert(QStringLiteral("type"), option->type);
      setting.insert(QStringLiteral("advanced"), option->advanced);
      setting.insert(QStringLiteral("default"), option->defaultValue);
    } else {
      setting.insert(QStringLiteral("unknown"), true);
    }
    settings.append(setting);
  };

  if (provider != nullptr) {
    for (const ProviderOption &option : provider->options) {
      if (section.contains(option.name)) {
        add(option.name, &option);
        described.append(option.name);
      }
    }
  }
  for (const QString &key : section.keys()) {
    if (key != QLatin1String("type") && !described.contains(key)) {
      add(key, nullptr);
    }
  }
  return settings;
}

// ---- the registry -----------------------------------------------------------

RcloneConfigRegistry &RcloneConfigRegistry::instance() {
  static RcloneConfigRegistry registry;
  return registry;
}

RcloneConfigRegistry::RcloneConfigRegistry(QObject *parent) : QObject(parent) {}

void RcloneConfigRegistry::ensureLoaded() {
  if (mInFlight || isLoaded() || GetRclone().isEmpty()) {
    return;
  }
  mInFlight = true;

  auto *process = new QProcess(this);
  process->setProcessChannelMode(QProcess::SeparateChannels);
  UseRclonePassword(process);
  QPointer<RcloneConfigRegistry> guard(this);
  QObject::connect(
      process,
      static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
          &QProcess::finished),
      this, [this, process, guard](int status, QProcess::ExitStatus) {
        process->deleteLater();
        if (!guard) {
          return;
        }
        mInFlight = false;
        if (status != 0) {
          return; // asked again next time
        }
        mProviders = ParseProviders(process->readAllStandardOutput());
        qCDebug(rbRemote) << "rclone describes" << mProviders.size()
                          << "backends";
        if (isLoaded()) {
          emit providersLoaded();
        }
      });
  process->start(GetRclone(),
                 QStringList{QStringLiteral("config"), QStringLiteral("providers")} +
                     GetRcloneConf(),
                 QIODevice::ReadOnly);
}

void RcloneConfigRegistry::readDump(
    QObject *context,
    std::function<void(bool, const QJsonObject &, const QString &)> done) {
  auto *process = new QProcess(context);
  process->setProcessChannelMode(QProcess::SeparateChannels);
  UseRclonePassword(process);
  QObject::connect(
      process,
      static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
          &QProcess::finished),
      context, [process, done](int status, QProcess::ExitStatus) {
        process->deleteLater();
        // Never logged: this is the whole file, secrets and all.
        const QByteArray out = process->readAllStandardOutput();
        const QString err =
            QString::fromUtf8(process->readAllStandardError()).trimmed();
        if (status != 0) {
          done(false, QJsonObject(),
               err.isEmpty() ? QStringLiteral("rclone could not read its "
                                              "configuration")
                             : err.section(QLatin1Char('\n'), -1));
          return;
        }
        done(true, QJsonDocument::fromJson(out).object(), QString());
      });
  QObject::connect(process, &QProcess::errorOccurred, context,
                   [process, done](QProcess::ProcessError error) {
                     if (error != QProcess::FailedToStart) {
                       return;
                     }
                     process->deleteLater();
                     done(false, QJsonObject(),
                          QStringLiteral("rclone could not be started"));
                   });
  process->start(GetRclone(),
                 QStringList{QStringLiteral("config"), QStringLiteral("dump")} +
                     GetRcloneConf(),
                 QIODevice::ReadOnly);
}
