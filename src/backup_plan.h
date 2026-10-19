#pragma once

// Orchestration (L1): a backup, said the way a person says one -- "this
// folder to that remote, every night at two, keeping what changed" -- made
// into the task and the schedule that do it. The page's New backup and the
// window's New backup… both come here, so a backup made from either is the
// same two things.
//
// Nothing new runs a backup: it is an ordinary task on an ordinary schedule,
// changed afterwards like any other.

#include "job_options.h"
#include "schedule.h"

#include <QString>
#include <QStringList>

struct BackupPlan {
  QString name;   // empty: "Backup of <source>"
  QString source; // a folder of this machine, or remote:path
  QString dest;   // remote:path, or a folder of this machine
  // Mirror: the backup is made the same as the source, so a file deleted
  // there goes from the backup too (sync). Otherwise nothing is ever deleted
  // from the backup (copy).
  bool mirror = true;
  // What is changed or deleted in the backup is moved, not lost, to a
  // folder of its own beside it, one per run: <dest>-versions/<date>.
  bool keepVersions = true;
  // "mon".."sun"; empty for every day. And when, "HH:mm".
  QStringList days;
  QString time = QStringLiteral("02:00");
};

namespace Backup {

// Where the old versions go: beside the backup, never inside it -- inside,
// the next run would back up the versions as well, and a mirror would then
// delete them. Empty when the backup is a whole remote or a drive's root,
// which has nothing beside it.
QString VersionsFolder(const QString &dest);

// The task and the schedule, not saved, so that whoever asked can check them
// the way it checks any task first. One side has to be this machine: a task
// is an upload or a download.
bool Build(const BackupPlan &plan, JobOptions *task, Schedule *schedule,
           QString *why);

// Saves both: the task as TaskBuilder::Create does, then the schedule for
// it. Takes the task.
bool Save(JobOptions *task, Schedule *schedule, QString *why);

} // namespace Backup
