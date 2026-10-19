#include "program_settings.h"
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

} // namespace ProgramSettings
