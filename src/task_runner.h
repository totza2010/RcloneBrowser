#pragma once

// Orchestration (L1): run one saved task without a window.
//
// This is E1 in the plan, and the first thing that proves the split works:
// everything it needs -- the task store, the argument builder, the rclone
// location -- already lives below the GUI, so the whole path here links
// against Qt Core alone. If that stops being true, this file stops building,
// which is the point of it living in rbcore.
//
// See docs/API.md S1.

#include <QString>

class QTextStream;

namespace TaskRunner {

// Exit codes above 63 follow the sysexits convention so they cannot be
// confused with rclone's own, which are 1-9 and are passed through unchanged.
enum ExitCode {
  Ok = 0,
  UsageError = 64,        // asked for something that is not a task to run
  TaskNotFound = 65,      // no task by that name or id
  AmbiguousName = 66,     // several tasks share that name
  RcloneUnavailable = 69, // rclone could not be started at all
  RcloneCrashed = 70,     // rclone died on a signal rather than exiting
};

// Prints every saved task as "<id>  <name>", which is what makes runTask()
// usable: task names are free text and need not be unique, so the id is the
// only reliable way to name one.
int listTasks(QTextStream &out);

// Runs the task named by id or by description and returns rclone's exit code.
//
// Mount tasks are refused: they run until stopped, so "did it finish, and
// with what code" has no answer for them. That belongs with the daemon work,
// not here.
int runTask(const QString &nameOrId, bool dryRun, QTextStream &out,
            QTextStream &err);

} // namespace TaskRunner
