#pragma once

// Orchestration (L1): every job running right now.
//
// The list used to be the layout holding the job cards, which is why
// "how many transfers are running" was counted by walking widgets, and why
// nothing outside the window could answer it at all. See docs/API.md S2.

#include "running_job.h"

#include <QList>
#include <QObject>

class JobRegistry : public QObject {
  Q_OBJECT

public:
  static JobRegistry &instance();

  // Every job this process has started and not yet been told to forget,
  // finished ones included -- the window keeps their cards until they are
  // closed, and an API would want the same recent history.
  const QList<RunningJob *> &jobs() const { return mJobs; }

  RunningJob *find(const QString &requestId) const;

  // How many are still going. Counting only what is running is the question
  // the queue and the quit path actually ask.
  int runningCount() const;
  int runningCount(JobKind kind) const;

  // Creates the job, announces it, and starts rclone. Returns the job even
  // when rclone could not be started, so the failure is visible in the same
  // place as any other ending.
  RunningJob *start(JobKind kind, const QStringList &args,
                    const JobDescription &description, const QString &taskId,
                    const QString &transferMode, const QString &requestId);

  // Drops a finished job. Running jobs are left alone: forgetting one would
  // leave rclone running with nothing holding it.
  void forget(RunningJob *job);

signals:
  void jobStarted(RunningJob *job);
  void jobFinished(RunningJob *job);

private:
  explicit JobRegistry(QObject *parent = nullptr);

  QList<RunningJob *> mJobs;
};
