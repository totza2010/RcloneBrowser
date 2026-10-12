#include "task_json.h"
#include "job_options.h"

#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>

namespace {

// Each choice, as the word for it. The order is the order of the enum, which
// is the order storage depends on; the words are what the API depends on.
const QStringList kOperations = {QStringLiteral("unknown"),
                                 QStringLiteral("copy"),
                                 QStringLiteral("move"),
                                 QStringLiteral("sync"),
                                 QStringLiteral("mount"),
                                 QStringLiteral("check"),
                                 QStringLiteral("cryptcheck")};

const QStringList kTimings = {QStringLiteral("during"), QStringLiteral("after"),
                              QStringLiteral("before")};

const QStringList kCompare = {
    QStringLiteral("sizeAndModTime"), QStringLiteral("checksum"),
    QStringLiteral("ignoreSize"), QStringLiteral("sizeOnly"),
    QStringLiteral("checksumIgnoreSize")};

QString word(const QStringList &words, int index, const QString &fallback) {
  return index >= 0 && index < words.size() ? words.at(index) : fallback;
}

bool fail(QString *why, const QString &reason) {
  if (why != nullptr) {
    *why = reason;
  }
  return false;
}

// Text that may arrive as text or as a number: "4" and 4 are the same thing
// to say about --transfers, and refusing one of them would only be a trap.
bool readText(const QJsonObject &json, const char *key, QString *into,
              QString *why) {
  const QJsonValue value = json.value(QLatin1String(key));
  if (value.isUndefined()) {
    return true;
  }
  if (value.isString()) {
    *into = value.toString();
    return true;
  }
  if (value.isDouble()) {
    *into = QString::number(value.toVariant().toLongLong());
    return true;
  }
  if (value.isNull()) {
    into->clear();
    return true;
  }
  return fail(why, QStringLiteral("\"%1\" has to be text or a number")
                       .arg(QLatin1String(key)));
}

bool readFlag(const QJsonObject &json, const char *key, bool *into,
              QString *why) {
  const QJsonValue value = json.value(QLatin1String(key));
  if (value.isUndefined()) {
    return true;
  }
  if (!value.isBool()) {
    return fail(why, QStringLiteral("\"%1\" has to be true or false")
                         .arg(QLatin1String(key)));
  }
  *into = value.toBool();
  return true;
}

} // namespace

namespace TaskJson {

QJsonObject Write(const JobOptions &task) {
  const bool download = task.jobType == JobOptions::Download;

  QJsonObject json;
  json.insert(QStringLiteral("id"), task.uniqueId.toString());
  json.insert(QStringLiteral("name"), task.description);
  json.insert(QStringLiteral("type"), download ? QStringLiteral("download")
                                               : QStringLiteral("upload"));
  // Kept for whoever read the first shape of this, which had only this.
  json.insert(QStringLiteral("isDownload"), download);
  json.insert(QStringLiteral("operation"),
              word(kOperations, int(task.operation), QStringLiteral("unknown")));
  json.insert(QStringLiteral("source"), task.source);
  json.insert(QStringLiteral("dest"), task.dest);
  json.insert(QStringLiteral("isFolder"), task.isFolder);

  json.insert(QStringLiteral("syncTiming"),
              word(kTimings, int(task.syncTiming), QStringLiteral("during")));
  json.insert(QStringLiteral("skipNewer"), task.skipNewer);
  json.insert(QStringLiteral("skipExisting"), task.skipExisting);
  // One field for "whether, and how": "none" is the dialog's box unticked.
  json.insert(QStringLiteral("compare"),
              task.compare ? word(kCompare, int(task.compareOption),
                                  QStringLiteral("sizeAndModTime"))
                           : QStringLiteral("none"));

  json.insert(QStringLiteral("createEmptySrcDirs"), task.createEmptySrcDirs);
  json.insert(QStringLiteral("deleteEmptySrcDirs"), task.deleteEmptySrcDirs);
  json.insert(QStringLiteral("deleteExcluded"), task.deleteExcluded);
  json.insert(QStringLiteral("sameFilesystem"), task.sameFilesystem);
  json.insert(QStringLiteral("noTraverse"), task.noTraverse);
  json.insert(QStringLiteral("dontUpdateModified"), task.dontUpdateModified);
  json.insert(QStringLiteral("verbose"), task.verbose);

  json.insert(QStringLiteral("transfers"), task.transfers);
  json.insert(QStringLiteral("checkers"), task.checkers);
  json.insert(QStringLiteral("bandwidth"), task.bandwidth);
  json.insert(QStringLiteral("minSize"), task.minSize);
  json.insert(QStringLiteral("minAge"), task.minAge);
  json.insert(QStringLiteral("maxAge"), task.maxAge);
  json.insert(QStringLiteral("maxDepth"), task.maxDepth);
  json.insert(QStringLiteral("connectTimeout"), task.connectTimeout);
  json.insert(QStringLiteral("idleTimeout"), task.idleTimeout);
  json.insert(QStringLiteral("retries"), task.retries);
  json.insert(QStringLiteral("lowLevelRetries"), task.lowLevelRetries);

  json.insert(QStringLiteral("extra"), task.extra);
  json.insert(QStringLiteral("included"), task.included);
  json.insert(QStringLiteral("excluded"), task.excluded);
  json.insert(QStringLiteral("filtered"), task.filtered);

  if (task.operation == JobOptions::Mount) {
    // Shown, so a mount task can be read; not written through here.
    QJsonObject mount;
    mount.insert(QStringLiteral("readOnly"), task.mountReadOnly);
    mount.insert(QStringLiteral("volume"), task.mountVolume);
    mount.insert(QStringLiteral("autoStart"), task.mountAutoStart);
    mount.insert(QStringLiteral("hasScript"), !task.mountScript.isEmpty());
    json.insert(QStringLiteral("mount"), mount);
  }
  return json;
}

bool Read(const QJsonObject &json, JobOptions *task, QString *why) {
  if (task == nullptr) {
    return fail(why, QStringLiteral("there is no task to write to"));
  }

  // On a copy first, so a request that is wrong half way through leaves the
  // task exactly as it was rather than half changed.
  JobOptions next = *task;

  if (json.contains(QStringLiteral("operation"))) {
    const QString asked = json.value(QStringLiteral("operation")).toString();
    if (asked == QLatin1String("copy")) {
      next.operation = JobOptions::Copy;
    } else if (asked == QLatin1String("move")) {
      next.operation = JobOptions::Move;
    } else if (asked == QLatin1String("sync")) {
      next.operation = JobOptions::Sync;
    } else {
      return fail(why, QStringLiteral(
                           "\"operation\" has to be copy, move or sync"));
    }
  }
  // The flag the command is built from follows the choice, so the two cannot
  // disagree: a task that says "copy" and carries sync's deletions would be
  // the worst kind of surprise.
  next.sync = next.operation == JobOptions::Sync;

  if (json.contains(QStringLiteral("type"))) {
    const QString asked = json.value(QStringLiteral("type")).toString();
    if (asked == QLatin1String("upload")) {
      next.jobType = JobOptions::Upload;
    } else if (asked == QLatin1String("download")) {
      next.jobType = JobOptions::Download;
    } else {
      return fail(why,
                  QStringLiteral("\"type\" has to be upload or download"));
    }
  }

  if (json.contains(QStringLiteral("syncTiming"))) {
    const int at =
        kTimings.indexOf(json.value(QStringLiteral("syncTiming")).toString());
    if (at < 0) {
      return fail(why, QStringLiteral(
                           "\"syncTiming\" has to be during, after or before"));
    }
    next.syncTiming = static_cast<JobOptions::SyncTiming>(at);
  }

  if (json.contains(QStringLiteral("compare"))) {
    const QString asked = json.value(QStringLiteral("compare")).toString();
    if (asked == QLatin1String("none")) {
      next.compare = false;
    } else {
      const int at = kCompare.indexOf(asked);
      if (at < 0) {
        return fail(why, QStringLiteral("\"compare\" has to be none or one of: "
                                        "%1")
                             .arg(kCompare.join(QStringLiteral(", "))));
      }
      next.compare = true;
      next.compareOption = static_cast<JobOptions::CompareOption>(at);
    }
  }

  if (json.contains(QStringLiteral("maxDepth"))) {
    const QJsonValue value = json.value(QStringLiteral("maxDepth"));
    if (!value.isDouble() || value.toInt() < 0) {
      return fail(why, QStringLiteral(
                           "\"maxDepth\" has to be a number, 0 for no limit"));
    }
    next.maxDepth = value.toInt();
  }

  if (!readText(json, "name", &next.description, why) ||
      !readText(json, "source", &next.source, why) ||
      !readText(json, "dest", &next.dest, why) ||
      !readText(json, "transfers", &next.transfers, why) ||
      !readText(json, "checkers", &next.checkers, why) ||
      !readText(json, "bandwidth", &next.bandwidth, why) ||
      !readText(json, "minSize", &next.minSize, why) ||
      !readText(json, "minAge", &next.minAge, why) ||
      !readText(json, "maxAge", &next.maxAge, why) ||
      !readText(json, "connectTimeout", &next.connectTimeout, why) ||
      !readText(json, "idleTimeout", &next.idleTimeout, why) ||
      !readText(json, "retries", &next.retries, why) ||
      !readText(json, "lowLevelRetries", &next.lowLevelRetries, why) ||
      !readText(json, "extra", &next.extra, why) ||
      !readText(json, "included", &next.included, why) ||
      !readText(json, "excluded", &next.excluded, why) ||
      !readText(json, "filtered", &next.filtered, why)) {
    return false;
  }

  if (!readFlag(json, "isFolder", &next.isFolder, why) ||
      !readFlag(json, "skipNewer", &next.skipNewer, why) ||
      !readFlag(json, "skipExisting", &next.skipExisting, why) ||
      !readFlag(json, "createEmptySrcDirs", &next.createEmptySrcDirs, why) ||
      !readFlag(json, "deleteEmptySrcDirs", &next.deleteEmptySrcDirs, why) ||
      !readFlag(json, "deleteExcluded", &next.deleteExcluded, why) ||
      !readFlag(json, "sameFilesystem", &next.sameFilesystem, why) ||
      !readFlag(json, "noTraverse", &next.noTraverse, why) ||
      !readFlag(json, "dontUpdateModified", &next.dontUpdateModified, why) ||
      !readFlag(json, "verbose", &next.verbose, why)) {
    return false;
  }

  next.description = next.description.trimmed();
  next.source = next.source.trimmed();
  next.dest = next.dest.trimmed();

  // What each of these may look like. They are kept as text because rclone
  // reads them as text ("10M", "3d"), but text that rclone would refuse is
  // better refused here, by name, than at two in the morning when the task
  // runs. Only what was said is judged, and saying nothing is allowed: an
  // empty value is "leave it to rclone".
  static const QRegularExpression whole(QStringLiteral("^[0-9]+$"));
  static const QRegularExpression size(
      QStringLiteral("^[0-9]+(\\.[0-9]+)?([bBkKmMgGtTpP]i?[bB]?)?$"));
  // A length of time ("90", "3d", "1h30m") or, for the ages, a date.
  static const QRegularExpression age(QStringLiteral("^[0-9a-zA-Z.:-]+$"));
  // A rate, or rclone's timetable of them: "08:00,512k 18:00,off".
  static const QRegularExpression rate(QStringLiteral("^[0-9a-zA-Z.:, -]+$"));

  const auto shaped = [&json, why](const char *key, QString *value,
                                   const QRegularExpression &shape,
                                   const char *what) {
    *value = value->trimmed();
    if (!json.contains(QLatin1String(key)) || value->isEmpty() ||
        shape.match(*value).hasMatch()) {
      return true;
    }
    return fail(why, QStringLiteral("\"%1\" has to be %2")
                         .arg(QLatin1String(key), QLatin1String(what)));
  };

  if (!shaped("transfers", &next.transfers, whole, "a whole number") ||
      !shaped("checkers", &next.checkers, whole, "a whole number") ||
      !shaped("retries", &next.retries, whole, "a whole number") ||
      !shaped("lowLevelRetries", &next.lowLevelRetries, whole,
              "a whole number") ||
      !shaped("connectTimeout", &next.connectTimeout, whole,
              "a whole number of seconds") ||
      !shaped("idleTimeout", &next.idleTimeout, whole,
              "a whole number of seconds") ||
      !shaped("minSize", &next.minSize, size,
              "a size: a number, with b, k, M or G after it if wanted") ||
      !shaped("minAge", &next.minAge, age,
              "a length of time such as 90, 3d or 1h30m") ||
      !shaped("maxAge", &next.maxAge, age,
              "a length of time such as 90, 3d or 1h30m") ||
      !shaped("bandwidth", &next.bandwidth, rate,
              "a rate such as 500 or 10M")) {
    return false;
  }

  *task = next;
  return true;
}

QJsonObject Defaults(bool isDownload) {
  JobOptions task(isDownload);
  task.operation = JobOptions::Copy;
  task.syncTiming = JobOptions::During;

  // What the window's dialog comes up with. They are written in its .ui file
  // as well, which is two places for six numbers; that file is the one to
  // change if they ever move, and this the one to keep in step.
  task.transfers = QStringLiteral("4");
  task.checkers = QStringLiteral("8");
  task.connectTimeout = QStringLiteral("60");
  task.idleTimeout = QStringLiteral("300");
  task.retries = QStringLiteral("3");
  task.lowLevelRetries = QStringLiteral("10");

  QJsonObject json = Write(task);
  json.remove(QStringLiteral("id")); // a new task has none yet
  return json;
}

} // namespace TaskJson
