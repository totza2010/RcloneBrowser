#include "debug_log.h"
#include "job_registry.h"
#include "list_of_job_options.h"
#include "run_history.h"

#include <QUuid>

#include <memory>

namespace {

// What the history keeps about a job. Deliberately not the arguments: they
// carry backend tokens and remote-control logins, and a database file is
// exactly the kind of thing that gets copied around (docs/ARCHITECTURE.md
// section 5).
JobRunRecord recordFor(const RunningJob *job) {
  JobRunRecord r;
  r.requestId = job->requestId();
  r.taskId = job->taskId();
  r.kind = JobKindToString(job->kind());
  r.transferMode = job->transferMode();
  r.info = job->description().info;
  r.source = job->description().source;
  r.dest = job->description().dest;
  r.startedAt = job->startedAt().toMSecsSinceEpoch();

  // Copied in at the time it ran, because the task can be renamed or deleted
  // and the history still has to say what this was.
  if (!r.taskId.isEmpty()) {
    if (const JobOptions *task =
            ListOfJobOptions::getInstance()->find(r.taskId)) {
      r.taskName = task->description;
    }
  }
  return r;
}

// "Copy", "Move", "Sync" -- the words the card shows. A task with any other
// operation has no verb worth printing.
QString operationWord(const JobOptions &task) {
  switch (task.operation) {
  case JobOptions::Copy:
    return QStringLiteral("Copy");
  case JobOptions::Move:
    return QStringLiteral("Move");
  case JobOptions::Sync:
    return QStringLiteral("Sync");
  default:
    return QString();
  }
}

} // namespace

JobDescription DescribeTask(const JobOptions &task,
                            const QString &transferMode, bool dryRun) {
  JobDescription description;
  description.source = task.source;
  description.dest = task.dest;

  if (task.operation == JobOptions::Mount) {
    description.info = QStringLiteral("Mounting task: ") + task.description;
    return description;
  }

  const QString operation = operationWord(task);

  if (dryRun) {
    description.info = QStringLiteral("Dry run, task: \"%1\", %2 from %3")
                           .arg(task.description, operation.toLower(),
                                task.source);
  } else if (transferMode == QStringLiteral("queue")) {
    description.info = QStringLiteral("Queue task: \"%1\", %2 from %3")
                           .arg(task.description, operation, task.source);
  } else if (transferMode == QStringLiteral("scheduler")) {
    description.info = QStringLiteral("Scheduled task: \"%1\", %2 from %3")
                           .arg(task.description, operation, task.source);
  } else {
    description.info = QStringLiteral("Task: \"%1\", %2 from %3")
                           .arg(task.description, operation, task.source);
  }
  return description;
}

RunningJob *StartTask(JobOptions *task, const QString &transferMode,
                      const QString &requestId, bool dryRun) {
  if (task == nullptr) {
    return nullptr;
  }

  const bool isMount = task->operation == JobOptions::Mount;

  // dryRun is not persisted, and setting it here rather than in the task is
  // what stops a dry run turning into a real one on the next start.
  task->dryRun = dryRun && !isMount;

  const QStringList args = isMount ? task->getMountOptions()
                                   : task->getOptions();

  qCDebug(rbJob) << "start task" << task->description << "mode=" << transferMode
                 << "dryRun=" << dryRun << "request=" << requestId;

  RunningJob *job = JobRegistry::instance().start(
      isMount ? JobKind::Mount : JobKind::Transfer, args,
      DescribeTask(*task, transferMode, dryRun), task->uniqueId.toString(),
      transferMode, requestId);

  if (isMount && !task->mountScript.isEmpty()) {
    job->setMountScript(task->mountScript);
  }
  return job;
}

JobRegistry &JobRegistry::instance() {
  static JobRegistry registry;
  return registry;
}

JobRegistry::JobRegistry(QObject *parent) : QObject(parent) {}

RunningJob *JobRegistry::find(const QString &requestId) const {
  if (requestId.isEmpty()) {
    return nullptr;
  }
  for (RunningJob *job : mJobs) {
    if (job->requestId() == requestId) {
      return job;
    }
  }
  return nullptr;
}

int JobRegistry::runningCount() const {
  int count = 0;
  for (const RunningJob *job : mJobs) {
    if (job->isRunning()) {
      ++count;
    }
  }
  return count;
}

int JobRegistry::runningCount(JobKind kind) const {
  int count = 0;
  for (const RunningJob *job : mJobs) {
    if (job->isRunning() && job->kind() == kind) {
      ++count;
    }
  }
  return count;
}

RunningJob *JobRegistry::start(JobKind kind, const QStringList &args,
                               const JobDescription &description,
                               const QString &taskId,
                               const QString &transferMode,
                               const QString &requestId) {
  // A run with no id leaves no history: RunHistory refuses a record without
  // one, because the id is what an ending is matched back to a beginning by.
  //
  // Minted here rather than demanded of the caller. Every caller that had to
  // remember was a caller that could forget, and forgetting is silent -- the
  // job runs perfectly and simply never appears in the history. That is
  // exactly what happened when check, dedupe and export were first moved
  // here. See docs/LAYER-SPLIT.md block 5.
  const QString id =
      requestId.isEmpty() ? QUuid::createUuid().toString() : requestId;

  auto *job =
      new RunningJob(kind, args, description, taskId, transferMode, id, this);
  mJobs.append(job);

  QObject::connect(job, &RunningJob::finished, this, [this, job](JobState) {
    qCDebug(rbJob) << "job ended" << JobKindToString(job->kind())
                   << "request=" << job->requestId()
                   << "status=" << job->finalStatus()
                   << "exit=" << job->exitCode()
                   << "bytes=" << job->stats().bytes;
    JobRunRecord record = recordFor(job);
    record.state = job->finalStatus();
    record.exitCode = job->exitCode();
    record.finishedAt = job->finishedAt().toMSecsSinceEpoch();
    record.bytes = job->stats().bytes;
    record.totalBytes = job->stats().totalBytes;
    record.transfers = job->stats().transfers;
    record.errors = job->stats().errors;
    record.logPath = job->logPath();
    record.logBytes = job->logBytes();
    RunHistory::recordFinished(record);

    emit jobFinished(job);
  });

  // Written before rclone is started rather than after it ends, so a job that
  // dies with the machine still leaves a row saying it was running -- which is
  // the case the history existed to cover in the first place.
  RunHistory::recordStarted(recordFor(job));

  // The log file does not exist until rclone prints its first line, so the row
  // above cannot name it. Filling it in at the first line rather than at the
  // end means the log of a job that never got to finish is still reachable.
  auto named = std::make_shared<bool>(false);
  QObject::connect(job, &RunningJob::outputLine, this,
                   [job, named](const QString &) {
                     if (*named || job->logPath().isEmpty()) {
                       return;
                     }
                     *named = true;
                     JobRunRecord record = recordFor(job);
                     record.logPath = job->logPath();
                     RunHistory::recordStarted(record);
                   });

  // Announced before it is started, so a listener sees the job in its
  // Running state and cannot miss a transfer that fails immediately.
  emit jobStarted(job);
  job->start();

  return job;
}

void JobRegistry::forget(RunningJob *job) {
  if (job == nullptr || job->isRunning()) {
    return;
  }
  mJobs.removeAll(job);
  job->deleteLater();
}
