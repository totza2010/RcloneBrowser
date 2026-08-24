#include "debug_log.h"
#include "task_runner.h"

#include "database.h"
#include "job_options.h"
#include "list_of_job_options.h"
#include "run_history.h"
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
  const QStringList args = task->getOptions() + GetRcloneConf();

  out << "task:   " << task->description << "\n"
      << "rclone: " << RedactArgs(args).join(QLatin1Char(' ')) << "\n\n";
  out.flush();

  // A run from cron counts as much as one from the window, and this is the
  // half of "two processes writing the same history" that has no window at
  // all. transferMode says which one it was, so a run that went wrong can be
  // told apart from one somebody watched.
  JobRunRecord history;
  history.requestId =
      QUuid::createUuid().toString(QUuid::WithoutBraces);
  history.taskId = task->uniqueId.toString(QUuid::WithoutBraces);
  history.taskName = task->description;
  history.kind = QStringLiteral("transfer");
  history.transferMode = QStringLiteral("cli");
  history.info = task->description;
  history.source = task->source;
  history.dest = task->dest;
  history.startedAt = QDateTime::currentMSecsSinceEpoch();
  if (!RunHistory::recordStarted(history)) {
    // Said out loud rather than swallowed: a run that leaves no trace looks
    // exactly like one that never happened, and this is the path nobody is
    // watching. The transfer goes ahead either way.
    err << "warning: this run will not be recorded in the history: "
        << Database::lastError() << "\n";
    err.flush();
  }

  // Whatever happens below, the row must not be left saying "running": that
  // reading is reserved for a run whose process died without a word.
  struct Ending {
    JobRunRecord &record;
    ~Ending() {
      if (record.state.isEmpty()) {
        record.state = QStringLiteral("unknown");
      }
      RunHistory::recordFinished(record);
    }
  } ending{history};

  QProcess process;
  process.setProcessChannelMode(QProcess::MergedChannels);
  UseRclonePassword(&process);

  QEventLoop loop;
  QObject::connect(&process, &QProcess::readyRead, &process, [&]() {
    while (process.canReadLine()) {
      out << QString::fromUtf8(process.readLine());
    }
    out.flush();
  });
  QObject::connect(&process,
                   QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                   &loop, &QEventLoop::quit);
  QObject::connect(&process, &QProcess::errorOccurred, &loop, &QEventLoop::quit);

  process.start(GetRclone(), args, QIODevice::ReadOnly);
  if (!process.waitForStarted(10000)) {
    err << "could not start rclone: " << GetRclone() << "\n"
        << process.errorString() << "\n";
    history.state = QStringLiteral("error");
    history.exitCode = RcloneUnavailable;
    return RcloneUnavailable;
  }

  loop.exec();

  // Whatever is left in the buffer after the process ends: the last lines of
  // a transfer are usually the ones that say what went wrong.
  while (process.canReadLine()) {
    out << QString::fromUtf8(process.readLine());
  }
  out << QString::fromUtf8(process.readAll());
  out.flush();

  if (process.state() != QProcess::NotRunning) {
    process.kill();
    process.waitForFinished(5000);
  }

  if (process.exitStatus() != QProcess::NormalExit) {
    err << "rclone did not exit normally: " << process.errorString() << "\n";
    history.state = QStringLiteral("error");
    history.exitCode = RcloneCrashed;
    return RcloneCrashed;
  }

  qCDebug(rbApp) << "headless run finished exit=" << process.exitCode();
  history.exitCode = process.exitCode();
  history.state = history.exitCode == 0 ? QStringLiteral("finished")
                                        : QStringLiteral("error");
  return history.exitCode;
}

} // namespace TaskRunner
