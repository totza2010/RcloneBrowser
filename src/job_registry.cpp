#include "job_registry.h"

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

  QObject::connect(job, &RunningJob::finished, this,
                   [this, job](JobState) { emit jobFinished(job); });

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
