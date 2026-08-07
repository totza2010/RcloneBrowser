#include "job_options.h"
#include "utils.h"
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

  if (!extra.isEmpty()) {

    for (auto line : extra.split('\n')) {
      QRegularExpression re(R"( (?=[^"]*(?:"[^"]*"[^"]*)*$))");

      for (QString arg : line.split(re)) {
        if (!arg.isEmpty()) {
          list << arg.replace("\"", "");
        }
      }
    }
  }

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

SerializationException::SerializationException(QString msg)
    : QException(), Message(msg) {}
