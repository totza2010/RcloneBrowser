#include "job_options.h"
#include "utils.h"
#include <QRegularExpression>
#include <qexception.h>
#include <qlogging.h>
#ifdef _WIN32
#pragma warning(disable : 4505)
#endif
JobOptions::JobOptions(bool isDownload) : JobOptions() {
  setJobType(isDownload);
  uniqueId = QUuid::createUuid();
}

JobOptions::JobOptions()
    : jobType(UnknownJobType), operation(UnknownOp), dryRun(false), sync(false),
      syncTiming(UnknownTiming), skipNewer(false), skipExisting(false),
      compare(false), compareOption(), verbose(false), sameFilesystem(false),
      dontUpdateModified(false), maxDepth(0), deleteExcluded(false),
      isFolder(false), DriveSharedWithMe(false), mountReadOnly(false),
      mountCacheLevel(UnknownCacheLevel), mountAutoStart(false),
      mountWinDriveMode(false), noTraverse(false), createEmptySrcDirs(false),
      deleteEmptySrcDirs(false) {}

const qint32 JobOptions::classVersion = 8;

JobOptions::~JobOptions() {}

/*
 * Turn the options held here into a string list for
 * use in the rclone command.
 *
 * This logic was originally in transfer_dialog.cpp.
 *
 * This needs to change whenever e.g. new options are
 * added to the dialog.
 */
// Splits the free-text extra options into arguments, respecting quotes.
//
// Shared so a mount and a transfer treat the same text the same way. There
// used to be a regex for each, differing only by a capture group -- the same
// intention written twice, which is how two things drift apart.
static QStringList SplitExtraOptions(const QString &extra) {
  QStringList args;
  if (extra.trimmed().isEmpty()) {
    return args;
  }

  for (const QString &line : extra.split(QLatin1Char('\n'))) {
    args << SplitRcloneOptions(line);
  }
  return args;
}

QStringList JobOptions::getOptions() const {
  QStringList list;

  if (operation == Copy) {
    list << "copy";
  } else if (operation == Move) {
    list << "move";
  } else if (operation == Sync) {
    list << "sync";
  }

  list << source;
  list << dest;

  if (!GetDefaultOptionsList("defaultRcloneOptions").isEmpty()) {
    list << GetDefaultOptionsList("defaultRcloneOptions");
  }

  if (jobType == JobOptions::JobType::Download) {
    if (!GetDefaultOptionsList("defaultDownloadOptions").isEmpty()) {
      list << GetDefaultOptionsList("defaultDownloadOptions");
    }
  }

  if (jobType == JobOptions::JobType::Upload) {
    if (!GetDefaultOptionsList("defaultUploadOptions").isEmpty()) {
      list << GetDefaultOptionsList("defaultUploadOptions");
    }
  }

  if (sync) {
    switch (syncTiming) {
    case During:
      list << "--delete-during";
      break;
    case After:
      list << "--delete-after";
      break;
    case Before:
      list << "--delete-before";
      break;
    default:
      break;
      ;
    }
  }

  if (skipNewer) {
    list << "--update";
  }
  if (skipExisting) {
    list << "--ignore-existing";
  }

  if (compare) {
    switch (compareOption) {
    case Checksum:
      list << "--checksum";
      break;
    case IgnoreSize:
      list << "--ignore-size";
      break;
    case SizeOnly:
      list << "--size-only";
      break;
    case ChecksumIgnoreSize:
      list << "--checksum"
           << "--ignore-size";
      break;
    default:
      break;
    }
  }

  if (sameFilesystem) {
    list << "--one-file-system";
  }

  if (dontUpdateModified) {
    list << "--no-update-modtime";
  }

  if (noTraverse) {
    list << "--no-traverse";
  }

  if (createEmptySrcDirs) {
    list << "--create-empty-src-dirs";
  }

  if (deleteEmptySrcDirs) {
    list << "--delete-empty-src-dirs";
  }

  // Only when there is a value to pass. An empty one produces "--transfers"
  // followed by an empty argument, and rclone refuses to start:
  //
  //   invalid argument "" for "--transfers" flag: parsing "" as int failed
  //
  // The dialog always fills these in, which is why it went unnoticed; a task
  // built any other way -- headless, or later through the API -- need not.
  // Leaving the flag out gives rclone's own default, which is what every
  // other option here already does.
  if (!transfers.isEmpty()) {
    list << "--transfers" << transfers;
  }
  if (!checkers.isEmpty()) {
    list << "--checkers" << checkers;
  }

  if (!bandwidth.isEmpty()) {
    list << "--bwlimit" << bandwidth;
  }
  if (!minSize.isEmpty()) {
    list << "--min-size" << minSize;
  }
  if (!minAge.isEmpty()) {
    list << "--min-age" << minAge;
  }
  if (!maxAge.isEmpty()) {
    list << "--max-age" << maxAge;
  }

  if (maxDepth != 0) {
    list << "--max-depth" << QString::number(maxDepth);
  }

  // Same again -- and the timeouts are worse, because an empty value still
  // gets its unit appended and becomes the bare string "s".
  if (!connectTimeout.isEmpty()) {
    list << "--contimeout" << (connectTimeout + "s");
  }
  if (!idleTimeout.isEmpty()) {
    list << "--timeout" << (idleTimeout + "s");
  }
  if (!retries.isEmpty()) {
    list << "--retries" << retries;
  }
  if (!lowLevelRetries.isEmpty()) {
    list << "--low-level-retries" << lowLevelRetries;
  }

  if (deleteExcluded) {
    list << "--delete-excluded";
  }

  list << SplitExtraOptions(extra);

  if (!included.isEmpty()) {
    for (auto line : included.split('\n')) {
      list << "--include" << line;
    }
  }

  // excluded after included and extra options as they can also contain included
  if (!excluded.isEmpty()) {
    for (auto line : excluded.split('\n')) {
      list << "--exclude" << line;
    }
  }

  if (!filtered.isEmpty()) {
    for (auto line : filtered.split('\n')) {
      list << "--filter" << line;
    }
  }

  // get Google Drive mode option
  if (remoteType == "drive") {
    if (remoteMode == "shared") {
      list << "--drive-shared-with-me";
    } else {
      if (remoteMode == "trash") {
        list << "--drive-trashed-only";
      } else {
        if (remoteMode == "main") {
        } else {
          // older tasks dont't have googleDriveMode
          // and value from DriveSharedWithMe has to be used
          if (DriveSharedWithMe) {
            list << "--drive-shared-with-me";
          }
        }
      }
    }
  }

  // Progress comes from the remote control (core/stats), so rclone no longer
  // has to print the periodic stats block for anything to parse. Turning it
  // off leaves the job output free for actual log lines -- previously the
  // forced "--stats 1s" buried them under a seven-line report every second.
  //
  // --stats 0 only stops the printing; the figures are still collected and
  // still served over the remote control.
  list << "--stats"
       << "0";

  // -v as a baseline so the output pane shows what was copied. rclone
  // accumulates verbosity rather than letting the last flag win, so a user
  // who puts -vv or -vvv in the extra options still gets that level; verified
  // that "-vvv --verbose" and "--verbose -vvv" both come out at DEBUG.
  list << "--verbose";

  if (dryRun) {
    list << "--dry-run";
  }

  return list;
}

QStringList JobOptions::getMountOptions() const {
  QStringList list;

  list << "mount";
  list << source;
  list << dest;

  // Only the address. The login goes through the environment, so it stays out
  // of the argument list and therefore out of saved tasks and logs.
  if (!mountRcPort.isEmpty()) {
    list << "--rc";
    list << "--rc-addr";
    list << "localhost:" + mountRcPort;
  }

  if (remoteType == "drive") {
    if (remoteMode == "shared") {
      list << "--drive-shared-with-me";
      if (!mountReadOnly) {
        list << "--read-only";
      }
    }
    if (remoteMode == "trash") {
      list << "--drive-trashed-only";
    }
  }

  if (mountReadOnly) {
    list << "--read-only";
  }

  if (!mountVolume.trimmed().isEmpty()) {
    list << "--volname";
    list << mountVolume;
  }

  switch (mountCacheLevel) {
  case MountCacheLevel::Minimal:
    list << "--vfs-cache-mode"
         << "minimal";
    break;
  case MountCacheLevel::Writes:
    list << "--vfs-cache-mode"
         << "writes";
    break;
  case MountCacheLevel::Full:
    list << "--vfs-cache-mode"
         << "full";
    break;
  case MountCacheLevel::Off:
  case MountCacheLevel::UnknownCacheLevel:
    break;
  }

  list << SplitExtraOptions(extra);

  return list;
}

namespace {

// The enums are stored as their numbers, the same values QDataStream wrote.
// They are pinned to the order of items in the dialogs (see job_options.h),
// so writing the names instead would only add a second thing to keep in step.
template <typename Enum>
Enum readEnum(const QJsonObject &json, const QString &key, Enum fallback) {
  const QJsonValue value = json.value(key);
  return value.isDouble() ? static_cast<Enum>(value.toInt())
                          : fallback;
}

QString readString(const QJsonObject &json, const QString &key,
                   const QString &fallback) {
  const QJsonValue value = json.value(key);
  return value.isString() ? value.toString() : fallback;
}

bool readBool(const QJsonObject &json, const QString &key, bool fallback) {
  const QJsonValue value = json.value(key);
  return value.isBool() ? value.toBool() : fallback;
}

} // namespace

QJsonObject JobOptions::toJson() const {
  QJsonObject json;

  // dryRun is deliberately absent: it is a decision about one run, and
  // persisting it is how a dry run turns into a real one by surprise.
  json["description"] = description;
  json["jobType"] = static_cast<int>(jobType);
  json["operation"] = static_cast<int>(operation);
  json["sync"] = sync;
  json["syncTiming"] = static_cast<int>(syncTiming);
  json["skipNewer"] = skipNewer;
  json["skipExisting"] = skipExisting;
  json["compare"] = compare;
  json["compareOption"] = static_cast<int>(compareOption);
  json["verbose"] = verbose;
  json["sameFilesystem"] = sameFilesystem;
  json["dontUpdateModified"] = dontUpdateModified;
  json["transfers"] = transfers;
  json["checkers"] = checkers;
  json["bandwidth"] = bandwidth;
  json["minSize"] = minSize;
  json["minAge"] = minAge;
  json["maxAge"] = maxAge;
  json["maxDepth"] = maxDepth;
  json["connectTimeout"] = connectTimeout;
  json["idleTimeout"] = idleTimeout;
  json["retries"] = retries;
  json["lowLevelRetries"] = lowLevelRetries;
  json["deleteExcluded"] = deleteExcluded;
  json["excluded"] = excluded;
  json["extra"] = extra;
  json["DriveSharedWithMe"] = DriveSharedWithMe;
  json["source"] = source;
  json["dest"] = dest;
  json["isFolder"] = isFolder;
  json["uniqueId"] = uniqueId.toString();
  json["remoteMode"] = remoteMode;
  json["remoteType"] = remoteType;
  json["mountReadOnly"] = mountReadOnly;
  json["mountCacheLevel"] = static_cast<int>(mountCacheLevel);
  json["mountVolume"] = mountVolume;
  json["mountAutoStart"] = mountAutoStart;
  json["mountRcPort"] = mountRcPort;
  json["mountScript"] = mountScript;
  json["mountWinDriveMode"] = mountWinDriveMode;
  json["included"] = included;
  json["noTraverse"] = noTraverse;
  json["createEmptySrcDirs"] = createEmptySrcDirs;
  json["filtered"] = filtered;
  json["deleteEmptySrcDirs"] = deleteEmptySrcDirs;

  return json;
}

void JobOptions::readJson(const QJsonObject &json) {
  description = readString(json, "description", description);
  jobType = readEnum(json, "jobType", jobType);
  operation = readEnum(json, "operation", operation);
  sync = readBool(json, "sync", sync);
  syncTiming = readEnum(json, "syncTiming", syncTiming);
  skipNewer = readBool(json, "skipNewer", skipNewer);
  skipExisting = readBool(json, "skipExisting", skipExisting);
  compare = readBool(json, "compare", compare);
  compareOption = readEnum(json, "compareOption", compareOption);
  verbose = readBool(json, "verbose", verbose);
  sameFilesystem = readBool(json, "sameFilesystem", sameFilesystem);
  dontUpdateModified = readBool(json, "dontUpdateModified", dontUpdateModified);
  transfers = readString(json, "transfers", transfers);
  checkers = readString(json, "checkers", checkers);
  bandwidth = readString(json, "bandwidth", bandwidth);
  minSize = readString(json, "minSize", minSize);
  minAge = readString(json, "minAge", minAge);
  maxAge = readString(json, "maxAge", maxAge);
  maxDepth = json.value("maxDepth").isDouble() ? json.value("maxDepth").toInt()
                                               : maxDepth;
  connectTimeout = readString(json, "connectTimeout", connectTimeout);
  idleTimeout = readString(json, "idleTimeout", idleTimeout);
  retries = readString(json, "retries", retries);
  lowLevelRetries = readString(json, "lowLevelRetries", lowLevelRetries);
  deleteExcluded = readBool(json, "deleteExcluded", deleteExcluded);
  excluded = readString(json, "excluded", excluded);
  extra = readString(json, "extra", extra);
  DriveSharedWithMe = readBool(json, "DriveSharedWithMe", DriveSharedWithMe);
  source = readString(json, "source", source);
  dest = readString(json, "dest", dest);
  isFolder = readBool(json, "isFolder", isFolder);

  const QString id = readString(json, "uniqueId", QString());
  if (!id.isEmpty()) {
    uniqueId = QUuid::fromString(id);
  }

  remoteMode = readString(json, "remoteMode", remoteMode);
  remoteType = readString(json, "remoteType", remoteType);
  mountReadOnly = readBool(json, "mountReadOnly", mountReadOnly);
  mountCacheLevel = readEnum(json, "mountCacheLevel", mountCacheLevel);
  mountVolume = readString(json, "mountVolume", mountVolume);
  mountAutoStart = readBool(json, "mountAutoStart", mountAutoStart);
  mountRcPort = readString(json, "mountRcPort", mountRcPort);
  mountScript = readString(json, "mountScript", mountScript);
  mountWinDriveMode = readBool(json, "mountWinDriveMode", mountWinDriveMode);
  included = readString(json, "included", included);
  noTraverse = readBool(json, "noTraverse", noTraverse);
  createEmptySrcDirs = readBool(json, "createEmptySrcDirs", createEmptySrcDirs);
  filtered = readString(json, "filtered", filtered);
  deleteEmptySrcDirs = readBool(json, "deleteEmptySrcDirs", deleteEmptySrcDirs);
}

SerializationException::SerializationException(QString msg)
    : QException(), Message(msg) {}
