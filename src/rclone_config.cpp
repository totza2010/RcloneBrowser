#include "rclone_config.h"
#include "debug_log.h"
#include "job_options.h"
#include "job_registry.h"
#include "list_of_job_options.h"
#include "remote_registry.h"
#include "running_job.h"
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

  const auto add = [&settings, &section, provider](const QString &key,
                                                   const ProviderOption *option) {
    QJsonObject setting;
    setting.insert(QStringLiteral("name"), key);
    // With no description of the backend at all, nothing is shown: a name
    // is not enough to tell a secret by. It was tried, and a teldrive
    // host -- which rclone marks sensitive -- went out under a name that
    // looked harmless. A key the backend does describe is judged by it; one
    // it does not is judged by its name, and the backend's own list is
    // what makes that a second line rather than the only one.
    const bool secret = provider == nullptr || LooksSecret(key) ||
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

// ---- changing remotes ---------------------------------------------------------

namespace {

// One "rclone config ..." run, and what it said.
void runConfig(QObject *context, const QStringList &args,
               std::function<void(bool ok, const QString &said)> done) {
  auto *process = new QProcess(context);
  process->setProcessChannelMode(QProcess::MergedChannels);
  UseRclonePassword(process);
  QObject::connect(
      process,
      static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
          &QProcess::finished),
      context, [process, done](int status, QProcess::ExitStatus) {
        process->deleteLater();
        const QString said =
            QString::fromUtf8(process->readAll()).trimmed().section(QLatin1Char('\n'), -1);
        done(status == 0, said);
      });
  QObject::connect(process, &QProcess::errorOccurred, context,
                   [process, done](QProcess::ProcessError error) {
                     if (error != QProcess::FailedToStart) {
                       return;
                     }
                     process->deleteLater();
                     done(false, QStringLiteral("rclone could not be started"));
                   });
  process->start(GetRclone(), QStringList{QStringLiteral("config")} + args +
                                  GetRcloneConf(),
                 QIODevice::ReadOnly);
}

bool namesRemote(const QString &path, const QString &remote) {
  return path.startsWith(remote + QLatin1Char(':'));
}

QStringList tasksUsing(const QString &remote) {
  QStringList names;
  for (const JobOptions *task : ListOfJobOptions::getInstance()->getTasks()) {
    if (namesRemote(task->source, remote) || namesRemote(task->dest, remote)) {
      names << task->description;
    }
  }
  return names;
}

bool aJobIsUsing(const QString &remote) {
  for (const RunningJob *job : JobRegistry::instance().jobs()) {
    if (job->isRunning() && (namesRemote(job->description().source, remote) ||
                             namesRemote(job->description().dest, remote))) {
      return true;
    }
  }
  return false;
}

RemoteChange refused(const QString &code, const QString &why,
                     const QStringList &tasks = QStringList()) {
  RemoteChange change;
  change.code = code;
  change.why = why;
  change.tasks = tasks;
  return change;
}

} // namespace

QString WhyNotARemoteName(const QString &name) {
  static const QRegularExpression rule(
      QStringLiteral("^[\\p{L}\\p{N}_.+@][\\p{L}\\p{N}_.+@\\p{M} -]*$"));
  if (name.isEmpty()) {
    return QStringLiteral("a remote needs a name");
  }
  if (name.endsWith(QLatin1Char(' ')) || !rule.match(name).hasMatch()) {
    return QStringLiteral(
        "a remote's name is letters, digits and _ - . + @ or a space, not "
        "starting with - or a space and not ending with a space");
  }
  return QString();
}

void RenameRemote(QObject *context, const QString &from, const QString &to,
                  std::function<void(const RemoteChange &)> done) {
  const QString name = to.trimmed();
  const QString bad = WhyNotARemoteName(name);
  if (!bad.isEmpty()) {
    done(refused(QStringLiteral("bad_name"), bad));
    return;
  }
  if (aJobIsUsing(from)) {
    done(refused(QStringLiteral("in_use"),
                 QStringLiteral("a job is using %1: right now; let it finish "
                                "or stop it first")
                     .arg(from)));
    return;
  }

  RcloneConfigRegistry::instance().readDump(
      context, [context, from, name, done](bool ok, const QJsonObject &dump,
                                           const QString &why) {
        if (!ok) {
          done(refused(QStringLiteral("config_unreadable"), why));
          return;
        }
        if (!dump.contains(from)) {
          done(refused(QStringLiteral("no_such_remote"),
                       QStringLiteral("there is no remote called %1").arg(from)));
          return;
        }
        if (dump.contains(name)) {
          done(refused(QStringLiteral("taken"),
                       QStringLiteral("there is already a remote called %1").arg(name)));
          return;
        }

        // Everything it has, as it has it. --no-obscure because the
        // passwords in the dump are obscured already, and obscuring them
        // again would save something that opens nothing.
        QJsonObject section = dump.value(from).toObject();
        const QString type = section.take(QStringLiteral("type")).toString();
        QStringList args{QStringLiteral("create"), name, type};
        for (auto it = section.begin(); it != section.end(); ++it) {
          const QJsonValue value = it.value();
          args << it.key() + QLatin1Char('=') +
                      (value.isString() ? value.toString()
                                        : QString::fromUtf8(
                                              QJsonDocument(QJsonArray{value})
                                                  .toJson(QJsonDocument::Compact))
                                              .mid(1)
                                              .chopped(1));
        }
        args << QStringLiteral("--non-interactive") << QStringLiteral("--no-obscure");

        qCDebug(rbRemote) << "renaming remote" << from << "to" << name;
        runConfig(context, args, [context, from, name, done](bool created,
                                                            const QString &said) {
          if (!created) {
            done(refused(QStringLiteral("rclone_refused"), said));
            return;
          }
          runConfig(context, {QStringLiteral("delete"), from},
                    [from, name, done](bool deleted, const QString &said) {
                      if (!deleted) {
                        // Both are there now, which is a copy and not a loss.
                        done(refused(QStringLiteral("rclone_refused"),
                                     QStringLiteral("%1 was made, but %2 could "
                                                    "not be deleted: %3")
                                         .arg(name, from, said)));
                        RemoteRegistry::instance().refresh();
                        return;
                      }

                      // The tasks that name it now name it as it is called.
                      RemoteChange change;
                      change.ok = true;
                      for (JobOptions *task : ListOfJobOptions::getInstance()->getTasks()) {
                        bool changed = false;
                        for (QString *path : {&task->source, &task->dest}) {
                          if (namesRemote(*path, from)) {
                            *path = name + path->mid(from.size());
                            changed = true;
                          }
                        }
                        if (changed) {
                          ListOfJobOptions::getInstance()->Persist(task);
                          change.tasks << task->description;
                        }
                      }
                      RemoteRegistry::instance().refresh();
                      done(change);
                    });
        });
      });
}

void DeleteRemote(QObject *context, const QString &name, bool evenIfTasksUseIt,
                  std::function<void(const RemoteChange &)> done) {
  if (aJobIsUsing(name)) {
    done(refused(QStringLiteral("in_use"),
                 QStringLiteral("a job is using %1: right now; let it finish "
                                "or stop it first")
                     .arg(name)));
    return;
  }
  const QStringList tasks = tasksUsing(name);
  if (!tasks.isEmpty() && !evenIfTasksUseIt) {
    done(refused(QStringLiteral("used_by_tasks"),
                 QStringLiteral("%1 saved task(s) use %2:; they would be left "
                                "pointing at nothing")
                     .arg(tasks.size())
                     .arg(name),
                 tasks));
    return;
  }

  RcloneConfigRegistry::instance().readDump(
      context, [context, name, tasks, done](bool ok, const QJsonObject &dump,
                                            const QString &why) {
        if (!ok) {
          done(refused(QStringLiteral("config_unreadable"), why));
          return;
        }
        if (!dump.contains(name)) {
          done(refused(QStringLiteral("no_such_remote"),
                       QStringLiteral("there is no remote called %1").arg(name)));
          return;
        }
        qCDebug(rbRemote) << "deleting remote" << name;
        runConfig(context, {QStringLiteral("delete"), name},
                  [tasks, done](bool deleted, const QString &said) {
                    if (!deleted) {
                      done(refused(QStringLiteral("rclone_refused"), said));
                      return;
                    }
                    RemoteChange change;
                    change.ok = true;
                    change.tasks = tasks; // left as they were
                    RemoteRegistry::instance().refresh();
                    done(change);
                  });
      });
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
