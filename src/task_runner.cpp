#include "debug_log.h"
#include "task_runner.h"

#include "database.h"
#include "job_options.h"
#include "list_of_job_options.h"
#include "app_settings.h"
#include "run_history.h"
#include <QFileInfo>
#include "running_job.h"
#include "job_registry.h"
#include "script_runner.h"
#include "utils.h"

#include <QDateTime>
#include <QEventLoop>
#include <QProcess>
#include <QTextStream>
#include <QUuid>

namespace {

// Finds the task the user meant, or explains why it could not.
//
// The id is tried first because it is unambiguous; a description that happens
// to look like a UUID would be found by the second pass anyway.
JobOptions *resolve(const QString &nameOrId, QTextStream &err, int *exitCode) {
  ListOfJobOptions *store = ListOfJobOptions::getInstance();

  if (JobOptions *byId = store->find(nameOrId)) {
    return byId;
  }

  const int matches = store->countByName(nameOrId);
  if (matches == 0) {
    err << "no task called \"" << nameOrId << "\"\n"
        << "run with --list-tasks to see what there is\n";
    *exitCode = TaskRunner::TaskNotFound;
    return nullptr;
  }

  // Nothing stops two tasks from carrying the same description, and picking
  // the first would run something the user did not ask for.
  if (matches > 1) {
    err << matches << " tasks are called \"" << nameOrId
        << "\"; name one by id instead:\n";
    for (const JobOptions *task : store->getTasks()) {
      if (task->description == nameOrId) {
        err << "  " << task->uniqueId.toString() << "\n";
      }
    }
    *exitCode = TaskRunner::AmbiguousName;
    return nullptr;
  }

  return store->findByName(nameOrId);
}

QString operationName(JobOptions::Operation operation) {
  switch (operation) {
  case JobOptions::Copy:
    return QStringLiteral("copy");
  case JobOptions::Move:
    return QStringLiteral("move");
  case JobOptions::Sync:
    return QStringLiteral("sync");
  case JobOptions::Mount:
    return QStringLiteral("mount");
  case JobOptions::Check:
    return QStringLiteral("check");
  case JobOptions::CryptCheck:
    return QStringLiteral("cryptcheck");
  case JobOptions::UnknownOp:
    break;
  }
  return QStringLiteral("unknown");
}

} // namespace

namespace TaskRunner {

int listTasks(QTextStream &out) {
  const QList<JobOptions *> &tasks = ListOfJobOptions::getInstance()->getTasks();

  if (tasks.isEmpty()) {
    out << "no saved tasks\n";
    return Ok;
  }

  for (const JobOptions *task : tasks) {
    out << task->uniqueId.toString() << "  "
        << operationName(task->operation).leftJustified(10) << "  "
        << task->description << "\n";
  }
  return Ok;
}

// TEST: (V-17) rclone-browser --list-tasks แล้ว --run-task "<ชื่อ>" ในเทอร์มินัล
// ต้อง**ไม่มีหน้าต่างเปิด** · exit code ตรงกับที่ rclone คืน · รันขณะที่เปิดโปรแกรม
// อยู่ก็ต้องได้ (ไม่ติด single-instance lock) · ตั้งใน Task Scheduler / cron ได้
int runTask(const QString &nameOrId, bool dryRun, QTextStream &out,
            QTextStream &err) {
  int exitCode = Ok;
  JobOptions *task = resolve(nameOrId, err, &exitCode);
  if (task == nullptr) {
    return exitCode;
  }

  if (task->operation == JobOptions::Mount) {
    err << "\"" << task->description
        << "\" is a mount task, which runs until it is stopped\n"
        << "--run-task is for tasks that finish\n";
    return UsageError;
  }

  // dryRun is deliberately not persisted, so setting it here cannot leak into
  // the saved task.
  task->dryRun = dryRun;

  // The same argument list the window would build. Nothing is assembled here
  // -- that is the rule L3 has been breaking (VIO-1) and there is no reason
  // to add another place that does it.
  //
  // The configuration file is not appended: RunningJob adds it.
  const QStringList args = task->getOptions();

  out << "task:   " << task->description << "\n"
      << "rclone: "
      << RedactArgs(args + GetRcloneConf()).join(QLatin1Char(' '))
      << "\n\n";
  out.flush();

  // Checked before starting, because "there is no rclone there" and "rclone
  // exited 1" are different things to a script reading exit codes, and once
  // the job has started they both arrive as an exit code.
  if (!QFileInfo::exists(GetRclone())) {
    err << "could not start rclone: " << GetRclone() << "\n"
        << "no such file\n";
    return RcloneUnavailable;
  }

  // Through JobRegistry, the same road the window takes. This used to run
  // rclone itself and write its own history row, which meant a run from cron
  // had no figures, could not be stopped from anywhere, and was invisible to
  // the queue -- two implementations of "run a task", one of them the one
  // nobody was watching. See docs/LAYER-SPLIT.md.
  //
  // transferMode says which one it was, so a run that went wrong can be told
  // apart from one somebody watched.
  RunningJob *job = JobRegistry::instance().start(
      JobKind::Transfer, args,
      DescribeTask(*task, QStringLiteral("cli"), dryRun),
      task->uniqueId.toString(), QStringLiteral("cli"), QString());

  QEventLoop loop;
  QObject::connect(job, &RunningJob::outputLine, &loop,
                   [&out](const QString &line) {
                     out << line << "\n";
                     out.flush();
                   });
  QObject::connect(job, &RunningJob::finished, &loop,
                   [&loop](JobState) { loop.quit(); });

  // The hooks a window would fire. ScriptRunner is not listening in this
  // mode -- AppCore installs it, and a run that does one thing and exits does
  // not start the core -- so the moments are announced here, and waited for:
  // this process is about to end and a script that is not waited for is a
  // script killed a moment after it starts.
  ScriptRunner::instance().run(ScriptRunner::Reason::TransferStarted, true);

  // Already over: rclone can refuse in less time than it takes to get here.
  if (job->isRunning()) {
    loop.exec();
  }

  if (!job->everStarted()) {
    err << "could not start rclone: " << GetRclone() << "\n";
    return RcloneUnavailable;
  }

  if (job->crashed()) {
    err << "rclone did not exit normally\n";
    return RcloneCrashed;
  }

  qCDebug(rbApp) << "headless run finished exit=" << job->exitCode();

  // One task at a time here, so both rules mean the same thing -- but the log
  // line should still name the one the user chose, or reading it would
  // suggest a setting that is not in force.
  ScriptRunner::instance().run(
      AppSettings::runFinishedScriptForEveryTransfer()
          ? ScriptRunner::Reason::TransferFinished
          : ScriptRunner::Reason::LastTransferFinished,
      true);

  return job->exitCode();
}

} // namespace TaskRunner
