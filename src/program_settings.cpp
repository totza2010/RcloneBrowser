#include "program_settings.h"
#include "app_settings.h"
#include "debug_log.h"
#include "rclone_flags.h"
#include "utils.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>

namespace {

bool refuse(QString *why, const QString &reason) {
  if (why != nullptr) {
    *why = reason;
  }
  return false;
}

} // namespace

namespace ProgramSettings {

bool StreamIdleFromEnvironment() {
  bool readable = false;
  qEnvironmentVariableIntValue("RB_STREAM_IDLE_MINUTES", &readable);
  return readable;
}

int StreamIdleMinutes() {
  bool readable = false;
  const int fromEnvironment =
      qEnvironmentVariableIntValue("RB_STREAM_IDLE_MINUTES", &readable);
  const int minutes =
      readable ? fromEnvironment
               : GetSettings()->value("Settings/streamIdleMinutes", 30).toInt();
  return qMax(0, minutes);
}

bool SetStreamIdleMinutes(int minutes, QString *why) {
  if (StreamIdleFromEnvironment()) {
    return refuse(why, QStringLiteral("RB_STREAM_IDLE_MINUTES sets it; change "
                                      "it where the program is started"));
  }
  if (minutes < 0 || minutes > 10080) {
    return refuse(why, QStringLiteral("a number of minutes from 0 (never) to "
                                      "10080 (a week)"));
  }
  GetSettings()->setValue("Settings/streamIdleMinutes", minutes);
  GetSettings()->sync();
  return true;
}

QStringList RcloneLogLevels() {
  return {QStringLiteral("ERROR"), QStringLiteral("NOTICE"), QStringLiteral("INFO"),
          QStringLiteral("DEBUG")};
}

bool RcloneLogLevelFromEnvironment() {
  return RcloneLogLevels().contains(qEnvironmentVariable("RB_RCLONE_LOG_LEVEL").trimmed().toUpper());
}

QString RcloneLogLevel() {
  const QString level = RcloneLogLevelFromEnvironment()
                            ? qEnvironmentVariable("RB_RCLONE_LOG_LEVEL")
                            : GetSettings()->value("Settings/rcloneLogLevel").toString();
  const QString upper = level.trimmed().toUpper();
  return RcloneLogLevels().contains(upper) ? upper : QStringLiteral("INFO");
}

bool SetRcloneLogLevel(const QString &level, QString *why) {
  if (RcloneLogLevelFromEnvironment()) {
    return refuse(why, QStringLiteral("RB_RCLONE_LOG_LEVEL sets it; change it "
                                      "where the program is started"));
  }
  const QString upper = level.trimmed().toUpper();
  if (!RcloneLogLevels().contains(upper)) {
    return refuse(why, QStringLiteral("one of ERROR, NOTICE, INFO, DEBUG"));
  }
  GetSettings()->setValue("Settings/rcloneLogLevel", upper);
  GetSettings()->sync();
  return true;
}

QStringList RcloneLogLevelFlags() {
  const QString level = RcloneLogLevel();
  if (level == QLatin1String("ERROR")) {
    return {QStringLiteral("--quiet")};
  }
  if (level == QLatin1String("NOTICE")) {
    return {}; // rclone's own default
  }
  if (level == QLatin1String("DEBUG")) {
    return {QStringLiteral("-vv")};
  }
  return {QStringLiteral("--verbose")};
}

bool LocalRootsFromEnvironment() {
  return !qEnvironmentVariable("RB_LOCAL_ROOTS").trimmed().isEmpty();
}

QStringList LocalRoots() {
  const QString raw = LocalRootsFromEnvironment()
                          ? qEnvironmentVariable("RB_LOCAL_ROOTS")
                          : GetSettings()->value("Settings/apiLocalRoots").toString();
  QStringList roots;
  for (const QString &part :
       raw.split(QRegularExpression(QStringLiteral("[;\\n]")), Qt::SkipEmptyParts)) {
    if (!part.trimmed().isEmpty()) {
      roots << part.trimmed();
    }
  }
  return roots;
}

bool SetLocalRoots(const QStringList &given, QString *why) {
  if (LocalRootsFromEnvironment()) {
    return refuse(why, QStringLiteral("RB_LOCAL_ROOTS sets them; change it "
                                      "where the program is started"));
  }
  QStringList roots;
  QStringList missing;
  for (const QString &one : given) {
    const QString root = one.trimmed();
    if (root.isEmpty()) {
      continue;
    }
    if (!QFileInfo(root).isDir()) {
      missing << root;
    } else if (!roots.contains(root)) {
      roots << QDir::cleanPath(root);
    }
  }
  if (!missing.isEmpty()) {
    return refuse(why, QStringLiteral("not a folder of this machine: %1")
                           .arg(missing.join(QStringLiteral(", "))));
  }
  // Semicolons, as RB_LOCAL_ROOTS is written: one rule for both.
  GetSettings()->setValue("Settings/apiLocalRoots",
                          roots.join(QLatin1Char(';')));
  GetSettings()->sync();
  return true;
}

QList<Preference> Preferences() {
  auto one = [](const char *name, const char *group, const char *kind,
                const char *label, const char *help) {
    Preference p;
    p.name = QLatin1String(name);
    p.key = QStringLiteral("Settings/") + p.name;
    p.group = QLatin1String(group);
    p.kind = QLatin1String(kind);
    p.label = QLatin1String(label);
    p.help = QLatin1String(help);
    return p;
  };
  QList<Preference> all;
  all << one("rclone", "rclone", "rclone", "rclone",
             "The rclone program this one runs. A file whose name begins with rclone.")
      << one("rcloneConf", "rclone", "file", "rclone.conf",
             "The config file rclone reads. Empty: rclone's own choice.")
      << one("defaultRcloneOptions", "rclone", "flags", "Options for every rclone command",
             "Added to every rclone this program runs, listings included.")
      << one("defaultUploadOptions", "rclone", "flags", "Options for every upload", "")
      << one("defaultDownloadOptions", "rclone", "flags", "Options for every download", "")
      << one("mount", "rclone", "flags", "Options for every mount", "")
      << one("useProxy", "rclone", "bool", "Use a proxy",
             "rclone is given the three below as HTTP_PROXY, HTTPS_PROXY and NO_PROXY.")
      << one("http_proxy", "rclone", "text", "HTTP proxy", "")
      << one("https_proxy", "rclone", "text", "HTTPS proxy", "")
      << one("no_proxy", "rclone", "text", "No proxy for", "")
      << one("defaultUploadDir", "folders", "folder", "Upload from",
             "Where Browse starts when choosing what to upload.")
      << one("defaultDownloadDir", "folders", "folder", "Download to",
             "Where Browse starts when choosing where a download goes.");
  Preference debug = one("debugLog", "log", "bool", "Write the program's own debug log",
                         "Switched on and off at once. RB_DEBUG=1 forces it on.");
  debug.environment = QStringLiteral("RB_DEBUG");
  Preference size = one("logMaxFileKb", "log", "number", "Largest log file (KB)",
                        "Then it starts a new one.");
  size.min = AppSettings::Default::kLogMinFileKb;
  size.max = 1024 * 1024;
  Preference keep = one("logKeepFiles", "log", "number", "Older log files kept", "");
  keep.max = 1000;
  all << debug << size << keep;
  all << one("queueScriptRun", "scripts", "bool", "When the queue empties, run", "")
      << one("queueScript", "scripts", "command", "Queue script", "")
      << one("jobStartScriptRun", "scripts", "bool", "When a transfer starts, run", "")
      << one("transferOnScript", "scripts", "command", "Transfer start script", "")
      << one("jobLastFinishedScriptRun", "scripts", "bool", "When a transfer ends, run", "");
  Preference when = one("jobFinishedScriptWhen", "scripts", "choice", "Run it after",
                        "every: each transfer that ends · last: once nothing is transferring");
  when.choices = {QStringLiteral("last"), QStringLiteral("every")};
  all << when
      << one("transferOffScript", "scripts", "command", "Transfer end script", "");
  return all;
}

Preference FindPreference(const QString &name) {
  for (const Preference &p : Preferences()) {
    if (p.name == name) {
      return p;
    }
  }
  return Preference();
}

bool PreferenceFromEnvironment(const Preference &preference) {
  if (preference.environment.isEmpty()) {
    return false;
  }
  const QByteArray set = qgetenv(preference.environment.toLatin1().constData());
  return set == "1" || set.toLower() == "true";
}

QVariant PreferenceValue(const Preference &p) {
  if (p.name == QLatin1String("debugLog")) {
    return PreferenceFromEnvironment(p) || AppSettings::debugLog();
  }
  if (p.name == QLatin1String("logMaxFileKb")) {
    return AppSettings::logMaxFileKb();
  }
  if (p.name == QLatin1String("logKeepFiles")) {
    return AppSettings::logKeepFiles();
  }
  const QVariant kept = GetSettings()->value(p.key);
  if (p.kind == QLatin1String("bool")) {
    return kept.toBool();
  }
  if (p.kind == QLatin1String("choice")) {
    const QString word = kept.toString();
    return p.choices.contains(word) ? word : p.choices.value(0);
  }
  return kept.toString();
}

bool SetPreference(const QString &name, const QVariant &value, QString *why) {
  const Preference p = FindPreference(name);
  if (p.name.isEmpty()) {
    return refuse(why, QStringLiteral("there is no setting called %1").arg(name));
  }
  if (PreferenceFromEnvironment(p)) {
    return refuse(why, QStringLiteral("%1 sets it; change it where the program "
                                      "is started").arg(p.environment));
  }

  QVariant kept;
  const bool isText = value.typeId() == QMetaType::QString;
  if (p.kind == QLatin1String("bool")) {
    if (value.typeId() != QMetaType::Bool) {
      return refuse(why, QStringLiteral("%1: true or false").arg(name));
    }
    kept = value.toBool();
  } else if (p.kind == QLatin1String("number")) {
    bool whole = false;
    const double given = value.toDouble(&whole);
    const int number = int(given);
    if (isText || !whole || double(number) != given || number < p.min || number > p.max) {
      return refuse(why, QStringLiteral("%1: a whole number from %2 to %3")
                             .arg(name).arg(p.min).arg(p.max));
    }
    kept = number;
  } else {
    if (!isText) {
      return refuse(why, QStringLiteral("%1: words, not a %2")
                             .arg(name, QString::fromLatin1(value.typeName())));
    }
    const QString text = value.toString().trimmed();
    if (p.kind == QLatin1String("choice") && !p.choices.contains(text)) {
      return refuse(why, QStringLiteral("%1: one of %2")
                             .arg(name, p.choices.join(QStringLiteral(", "))));
    }
    if (p.kind == QLatin1String("folder") && !text.isEmpty() && !QFileInfo(text).isDir()) {
      return refuse(why, QStringLiteral("%1: not a folder of this machine: %2").arg(name, text));
    }
    if (p.kind == QLatin1String("file") && !text.isEmpty() &&
        !QFileInfo(QFileInfo(text).absolutePath()).isDir()) {
      return refuse(why, QStringLiteral("%1: its folder is not there: %2").arg(name, text));
    }
    if (p.kind == QLatin1String("rclone")) {
      // Only a program called rclone: this is what every transfer runs, and
      // anything else put here would be run in its place.
      const QFileInfo program(text);
      if (text.isEmpty() || !program.isFile() || !program.isExecutable() ||
          !program.fileName().startsWith(QLatin1String("rclone"), Qt::CaseInsensitive)) {
        return refuse(why, QStringLiteral("%1: a program of this machine whose name "
                                          "begins with rclone").arg(name));
      }
    }
    kept = text;
  }

  if (p.name == QLatin1String("debugLog")) {
    DebugLog::setEnabledForNextRun(kept.toBool());
  } else {
    GetSettings()->setValue(p.key, kept);
  }
  GetSettings()->sync();

  // What can take effect now, does.
  if (p.name == QLatin1String("rclone")) {
    SetRclone(kept.toString());
    RcloneFlagRegistry::instance().invalidate();
    RcloneFlagRegistry::instance().ensureLoaded();
  } else if (p.name == QLatin1String("rcloneConf")) {
    SetRcloneConf(kept.toString());
  } else if (p.name == QLatin1String("useProxy") || p.name.endsWith(QLatin1String("_proxy"))) {
    ApplyProxies();
  }
  return true;
}

void ApplyProxies() {
  auto settings = GetSettings();
  const bool on = settings->value("Settings/useProxy").toBool();
  const QList<QPair<const char *, const char *>> names = {
      {"http_proxy", "Settings/http_proxy"},
      {"https_proxy", "Settings/https_proxy"},
      {"no_proxy", "Settings/no_proxy"}};
  for (const auto &name : names) {
    const QByteArray value = on ? settings->value(QLatin1String(name.second)).toByteArray()
                                : QByteArray();
    const QByteArray kept = settings->value(QLatin1String(name.second)).toByteArray();
    const QByteArray lower(name.first);
    for (const QByteArray &variable : {lower, lower.toUpper()}) {
      if (!value.isEmpty()) {
        qputenv(variable.constData(), value);
      } else if (!kept.isEmpty() && qgetenv(variable.constData()) == kept) {
        // Only what this put there: a proxy the container was started
        // with is the container's.
        qunsetenv(variable.constData());
      }
    }
  }
}

} // namespace ProgramSettings
