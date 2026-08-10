#include "app_settings.h"
#include "utils.h"

#include <QSettings>

namespace {

// A count of days or of rows. Zero means "no limit" and is kept; anything
// negative cannot mean anything, so the default is used rather than letting
// it delete everything or nothing depending on where it is compared.
int nonNegative(int value, int fallback) {
  return value < 0 ? fallback : value;
}

} // namespace

QString AppSettings::rclonePath() {
  return GetSettings()->value("Settings/rclone").toString();
}

void AppSettings::setRclonePath(const QString &path) {
  GetSettings()->setValue("Settings/rclone", path);
}

QString AppSettings::rcloneConfPath() {
  return GetSettings()->value("Settings/rcloneConf").toString();
}

void AppSettings::setRcloneConfPath(const QString &path) {
  GetSettings()->setValue("Settings/rcloneConf", path);
}

bool AppSettings::logToFile() {
  return GetSettings()->value("Settings/logToFile", Default::kLogToFile)
      .toBool();
}

int AppSettings::logRetentionDays() {
  return nonNegative(GetSettings()
                         ->value("Settings/logRetentionDays",
                                 Default::kLogRetentionDays)
                         .toInt(),
                     Default::kLogRetentionDays);
}

int AppSettings::historyRetentionDays() {
  return nonNegative(GetSettings()
                         ->value("Settings/historyRetentionDays",
                                 Default::kHistoryRetentionDays)
                         .toInt(),
                     Default::kHistoryRetentionDays);
}

int AppSettings::historyMaxRuns() {
  return nonNegative(
      GetSettings()
          ->value("Settings/historyMaxRuns", Default::kHistoryMaxRuns)
          .toInt(),
      Default::kHistoryMaxRuns);
}

bool AppSettings::queueIsRunning() {
  return GetSettings()->value("Settings/queueStatus", false).toBool();
}

void AppSettings::setQueueIsRunning(bool running) {
  // Written as the words the file has always held, so a settings file stays
  // readable by an older build.
  GetSettings()->setValue("Settings/queueStatus",
                          running ? QStringLiteral("true")
                                  : QStringLiteral("false"));
}

QString AppSettings::queueFinishedScript() {
  auto settings = GetSettings();
  if (!settings->value("Settings/queueScriptRun", false).toBool()) {
    return QString();
  }
  return settings->value("Settings/queueScript").toString();
}
