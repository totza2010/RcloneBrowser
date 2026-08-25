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

bool AppSettings::debugLog() {
  return GetSettings()->value("Settings/debugLog", Default::kDebugLog).toBool();
}

void AppSettings::setDebugLog(bool on) {
  GetSettings()->setValue("Settings/debugLog", on);
}

int AppSettings::logMaxFileKb() {
  // Zero would mean a file that rotates on every line, which is not a
  // sensible reading of "no limit" for something that rotates rather than
  // expires. The floor is a size a header and a few lines still fit inside,
  // so that a file set that small rolls over rather than thrashing.
  const int kb =
      GetSettings()->value("Settings/logMaxFileKb", Default::kLogMaxFileKb)
          .toInt();
  return kb < Default::kLogMinFileKb ? Default::kLogMinFileKb : kb;
}

int AppSettings::logKeepFiles() {
  // Zero is allowed and means "only the one being written", which is a
  // reasonable thing to ask for on a small disk.
  return nonNegative(
      GetSettings()->value("Settings/logKeepFiles", Default::kLogKeepFiles)
          .toInt(),
      Default::kLogKeepFiles);
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

bool AppSettings::schedulerIsRunning() {
  // True when the key has never been written: the scheduler has always
  // started switched on, and main() writes the default for the same reason.
  return GetSettings()->value("Settings/schedulerStatus", true).toBool();
}

void AppSettings::setSchedulerIsRunning(bool running) {
  GetSettings()->setValue("Settings/schedulerStatus",
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
