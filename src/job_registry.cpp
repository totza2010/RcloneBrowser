#include "job_registry.h"
#include "list_of_job_options.h"
#include "run_history.h"

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

} // namespace

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
  auto *job = new RunningJob(kind, args, description, taskId, transferMode,
                             requestId, this);
  mJobs.append(job);

  QObject::connect(job, &RunningJob::finished, this, [this, job](JobState) {
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
