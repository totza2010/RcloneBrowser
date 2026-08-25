#include "job_queue.h"
#include "app_settings.h"
#include "debug_log.h"
#include "job_options.h"
#include "job_registry.h"
#include "list_of_job_options.h"
#include "utils.h"

#include <QSettings>
#include <QUuid>

JobQueue &JobQueue::instance() {
  static JobQueue queue;
  return queue;
}

JobQueue::JobQueue(QObject *parent) : QObject(parent) {
  // The queue keeps itself moving. Before this, something had to notice a job
  // had ended and tell the queue -- and that something was a handler in the
  // window, which is why a queue stopped dead without one.
  QObject::connect(&JobRegistry::instance(), &JobRegistry::jobFinished, this,
                   [this](RunningJob *job) {
                     if (mDrivesItself) {
                       jobFinished(job->requestId());
                     } else {
                       // Not driving, but still watching: the window needs to
                       // be able to ask whether a queued job is running.
                       syncRunningFromRegistry();
                     }
                   });

  QObject::connect(&JobRegistry::instance(), &JobRegistry::jobStarted, this,
                   [this](RunningJob *) { syncRunningFromRegistry(); });

  // A task that is deleted takes its queued runs with it. Waiting until
  // something tried to start one left the count wrong in the meantime.
  QObject::connect(ListOfJobOptions::getInstance(),
                   &ListOfJobOptions::tasksListUpdated, this,
                   [this]() { dropMissingTasks(); });
}

JobOptions *JobQueue::taskFor(const QueueEntry &entry) const {
  return ListOfJobOptions::getInstance()->find(entry.taskId);
}

QString JobQueue::enqueue(const QString &taskId, bool dryRun,
                          const QString &requestId) {
  QueueEntry entry;
  entry.taskId = taskId;
  entry.requestId =
      requestId.isEmpty() ? QUuid::createUuid().toString() : requestId;
  entry.dryRun = dryRun;

  qCDebug(rbQueue) << "enqueue task=" << taskId << "request=" << entry.requestId
                   << "count=" << mEntries.size() + 1;
  mEntries.append(entry);
  save();
  emit changed();

  // Asking every time something changes is what makes the rule for starting
  // one rule rather than a copy of it at each call site. It does nothing
  // unless everything says it should.
  advance();
  return entry.requestId;
}

void JobQueue::remove(const QString &requestId) {
  if (requestId.isEmpty() || requestId == mRunningRequestId) {
    // Removing the entry whose job is running would leave the job with
    // nothing to report back to. Stop the job instead.
    return;
  }
  const int before = mEntries.size();
  qCDebug(rbQueue) << "remove request=" << requestId << "count=" << before;
  for (int i = 0; i < mEntries.size(); ++i) {
    if (mEntries[i].requestId == requestId) {
      mEntries.removeAt(i);
      break;
    }
  }
  if (mEntries.size() != before) {
    save();
    emit changed();
  }
}

bool JobQueue::move(int from, int to) {
  if (from < 0 || to < 0 || from >= mEntries.size() || to >= mEntries.size() ||
      from == to) {
    return false;
  }
  // Everything here depends on the running job being the head entry.
  if (taskIsRunning() && (from == 0 || to == 0)) {
    return false;
  }
  qCDebug(rbQueue) << "move from=" << from << "to=" << to
                   << "count=" << mEntries.size();
  mEntries.move(from, to);
  save();
  emit changed();
  return true;
}

void JobQueue::clear() {
  QList<QueueEntry> kept;
  for (const QueueEntry &entry : mEntries) {
    if (entry.requestId == mRunningRequestId) {
      kept.append(entry);
    }
  }
  if (kept.size() == mEntries.size()) {
    return;
  }
  qCDebug(rbQueue) << "clear kept=" << kept.size() << "of" << mEntries.size();
  mEntries = kept;
  save();
  emit changed();
}

void JobQueue::start() {
  if (mRunning) {
    return;
  }
  qCDebug(rbQueue) << "started, count=" << mEntries.size();
  mRunning = true;
  AppSettings::setQueueIsRunning(true);
  emit changed();
  advance();
}

void JobQueue::pause() {
  if (!mRunning) {
    return;
  }
  qCDebug(rbQueue) << "paused, count=" << mEntries.size();
  mRunning = false;
  AppSettings::setQueueIsRunning(false);
  emit changed();
}

void JobQueue::setDrivesItself(bool on) {
  if (mDrivesItself == on) {
    return;
  }
  mDrivesItself = on;
  advance();
}

bool JobQueue::advance() {
  // While the window is the one driving, this must start nothing at all --
  // not on a job ending, and not when the queue is switched on either. Both
  // would be a second start of the same entry. See docs/QUEUE-MOVE.md step 0.
  if (!mDrivesItself) {
    return false;
  }

  if (!mRunning || mEntries.isEmpty() || taskIsRunning()) {
    return false;
  }

  // One transfer at a time is what a queue means. Anything the user started
  // by hand counts, which is why this asks the registry rather than counting
  // its own jobs.
  if (JobRegistry::instance().runningCount(JobKind::Transfer) > 0) {
    return false;
  }

  // A task deleted while it sat in the queue leaves an entry pointing at
  // nothing. Dropping it here rather than refusing to go on means one dead
  // entry cannot stop the whole queue.
  while (!mEntries.isEmpty() && taskFor(mEntries.first()) == nullptr) {
    mEntries.removeFirst();
    save();
    emit changed();
  }
  if (mEntries.isEmpty()) {
    return false;
  }

  const QueueEntry entry = mEntries.first();
  JobOptions *task = taskFor(entry);

  // The same task may already be running because the user started it by hand.
  // Starting a second copy of it would have two jobs writing the same
  // destination.
  for (const RunningJob *job : JobRegistry::instance().jobs()) {
    if (job->isRunning() && job->taskId() == entry.taskId) {
      return false;
    }
  }

  qCDebug(rbQueue) << "starting task=" << entry.taskId
                   << "request=" << entry.requestId
                   << "waiting=" << mEntries.size() - 1;
  mRunningRequestId = entry.requestId;
  StartTask(task, QStringLiteral("queue"), entry.requestId, entry.dryRun);

  emit taskStarted(entry.taskId, entry.requestId);
  emit changed();
  return true;
}

void JobQueue::jobFinished(const QString &requestId) {
  qCDebug(rbQueue) << "job ended request=" << requestId
                   << "wasOurs=" << (requestId == mRunningRequestId)
                   << "queueRunning=" << mRunning << "count=" << mEntries.size();
  if (!requestId.isEmpty() && requestId == mRunningRequestId) {
    mRunningRequestId.clear();

    // A paused queue keeps its entries, including the one that was running.
    // Stopping the queue and then the job is how somebody puts a task back in
    // the queue rather than out of it -- taking it out would mean pressing
    // Stop lost the entry.
    if (!mRunning) {
      emit changed();
      return;
    }

    for (int i = 0; i < mEntries.size(); ++i) {
      if (mEntries[i].requestId == requestId) {
        mEntries.removeAt(i);
        break;
      }
    }
    save();
    emit changed();

    if (mEntries.isEmpty()) {
      emit emptied();
    }
  }

  // Either way: a job ending is the moment the queue may be able to move,
  // whether it was the queue's own job or one the user started that was in
  // the way.
  advance();
}

void JobQueue::syncRunningFromRegistry() {
  QString running;
  for (const RunningJob *job : JobRegistry::instance().jobs()) {
    if (!job->isRunning()) {
      continue;
    }
    for (const QueueEntry &entry : mEntries) {
      if (entry.requestId == job->requestId()) {
        running = entry.requestId;
        break;
      }
    }
  }
  if (running != mRunningRequestId) {
    mRunningRequestId = running;
    emit changed();
  }
}

int JobQueue::dropMissingTasks() {
  int dropped = 0;
  for (int i = mEntries.size() - 1; i >= 0; --i) {
    if (taskFor(mEntries[i]) != nullptr) {
      continue;
    }
    // The one that is running is left alone: its job is still going, and the
    // task being gone does not stop what has already started.
    if (mEntries[i].requestId == mRunningRequestId) {
      continue;
    }
    qCDebug(rbQueue) << "dropping entry whose task is gone request="
                     << mEntries[i].requestId;
    mEntries.removeAt(i);
    ++dropped;
  }
  if (dropped > 0) {
    save();
    emit changed();
  }
  return dropped;
}

void JobQueue::load() {
  mEntries.clear();
  mRunningRequestId.clear();

  qCDebug(rbQueue) << "reading the stored queue";
  for (const QueueEntry &entry : QueueStore::load()) {
    // An entry whose task has since been deleted is skipped, the same as the
    // file-reading code always did.
    if (taskFor(entry) != nullptr) {
      mEntries.append(entry);
    }
  }

  mRunning = AppSettings::queueIsRunning();

  // Reading the list again must not lose track of what is running -- and
  // after a restart this is what notices a job the window has just started.
  syncRunningFromRegistry();
  emit changed();

  // And then carry on. Starting the application with a queue left running
  // used to leave it sitting there: the wheel was taken before the entries
  // were read, and reading them told nobody. Nothing happens here unless
  // every condition for starting the head entry holds.
  advance();
}

bool JobQueue::save() { return QueueStore::save(mEntries); }
