#include "backup_plan.h"
#include "debug_log.h"
#include "list_of_job_options.h"
#include "remote_registry.h"
#include "scheduler_store.h"
#include "task_builder.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QUuid>

namespace {

// Whether this names a remote the program knows: "gdrive:films" does,
// "C:/films" does not (a drive letter is not a remote, even with a colon).
bool onARemote(const QString &where) {
  const int colon = where.indexOf(QLatin1Char(':'));
  return colon > 1 &&
         RemoteRegistry::instance().find(where.left(colon)) != nullptr;
}

QString trimmedEnd(QString path) {
  path = path.trimmed();
  while (path.size() > 1 && (path.endsWith(QLatin1Char('/')) ||
                             path.endsWith(QLatin1Char('\\')))) {
    path.chop(1);
  }
  return path;
}

} // namespace

namespace Backup {

QString VersionsFolder(const QString &given) {
  const QString dest = trimmedEnd(given);
  // What is after the remote's colon, or the whole path on this machine.
  const int colon = dest.indexOf(QLatin1Char(':'));
  const QString inside = colon > 1 ? dest.mid(colon + 1) : dest;
  static const QRegularExpression root(QStringLiteral("^([A-Za-z]:)?[/\\\\]?$"));
  if (inside.isEmpty() || root.match(inside).hasMatch()) {
    return QString();
  }
  return dest + QStringLiteral("-versions");
}

bool Build(const BackupPlan &plan, JobOptions *task, Schedule *schedule,
           QString *why) {
  const auto refuse = [why](const QString &reason) {
    if (why != nullptr) {
      *why = reason;
    }
    return false;
  };

  const QString source = trimmedEnd(plan.source);
  const QString dest = trimmedEnd(plan.dest);
  if (source.isEmpty() || dest.isEmpty()) {
    return refuse(QStringLiteral("say what to back up and where to"));
  }
  const bool fromRemote = onARemote(source);
  const bool toRemote = onARemote(dest);
  if (fromRemote == toRemote) {
    return refuse(fromRemote
                      ? QStringLiteral("one side has to be a folder of this "
                                       "machine")
                      : QStringLiteral("one side has to be on a remote, "
                                       "written remote:path"));
  }
  if (source == dest) {
    return refuse(QStringLiteral("a backup has to go somewhere else"));
  }

  QStringList days;
  for (const QString &day : plan.days) {
    days << day.trimmed().toLower().left(3);
  }
  static const QStringList week = {"mon", "tue", "wed", "thu",
                                   "fri", "sat", "sun"};
  for (const QString &day : days) {
    if (!week.contains(day)) {
      return refuse(QStringLiteral("\"%1\" is not a day").arg(day));
    }
  }
  const QTime at = QTime::fromString(plan.time.trimmed(), QStringLiteral("H:mm"));
  if (!at.isValid()) {
    return refuse(QStringLiteral("say when as HH:mm, such as 02:00"));
  }

  QString versions;
  if (plan.keepVersions) {
    versions = VersionsFolder(dest);
    if (versions.isEmpty()) {
      return refuse(QStringLiteral("old versions are kept beside the backup, "
                                   "so it has to go into a folder, not a "
                                   "whole remote or drive"));
    }
  }

  // ---- the task
  *task = JobOptions(fromRemote);
  task->description = plan.name.trimmed().isEmpty()
                          ? QStringLiteral("Backup of %1").arg(source)
                          : plan.name.trimmed();
  task->operation = plan.mirror ? JobOptions::Sync : JobOptions::Copy;
  task->sync = plan.mirror;
  task->syncTiming = JobOptions::During;
  task->source = source;
  task->dest = dest;
  task->isFolder = true;
  if (!versions.isEmpty()) {
    // {date} is filled in each time the command is made: one folder per run.
    task->extra = QStringLiteral("--backup-dir \"%1/{date}\"").arg(versions);
  }

  // ---- the schedule
  *schedule = Schedule();
  schedule->id = QUuid::createUuid().toString();
  schedule->name = task->description;
  schedule->active = true;
  schedule->dailyMode = true;
  schedule->cronMode = false;
  const auto on = [&days](const char *day) {
    return days.isEmpty() || days.contains(QLatin1String(day));
  };
  schedule->monday = on("mon");
  schedule->tuesday = on("tue");
  schedule->wednesday = on("wed");
  schedule->thursday = on("thu");
  schedule->friday = on("fri");
  schedule->saturday = on("sat");
  schedule->sunday = on("sun");
  schedule->hour = at.hour();
  schedule->minute = at.minute();
  // Through the queue: two backups due at the same minute go one after the
  // other rather than both at once.
  schedule->executionMode = 1;
  return true;
}

bool Save(JobOptions *task, Schedule *schedule, QString *why) {
  TaskBuilder::Problem problem = TaskBuilder::Problem::None;
  const QString id = TaskBuilder::Create(task, QDateTime::currentDateTime(),
                                         &problem);
  if (id.isEmpty()) {
    if (why != nullptr) {
      *why = TaskBuilder::Explain(problem);
    }
    return false;
  }
  schedule->taskId = id;
  schedule->taskName = task->description;
  SchedulerStore::instance().add(*schedule);
  qCDebug(rbApp) << "backup made: task" << id << "schedule" << schedule->id;
  return true;
}

} // namespace Backup
