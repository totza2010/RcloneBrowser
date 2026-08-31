#include "app_core.h"
#include "debug_log.h"
#include "job_options.h"
#include "job_queue.h"
#include "job_registry.h"
#include "list_of_job_options.h"
#include "schedule.h"
#include "scheduler_store.h"
#include "script_runner.h"
#include "utils.h"

#include <QDateTime>
#include <QSettings>
#include <QTimer>
#include <QUuid>

AppCore &AppCore::instance() {
  static AppCore core;
  return core;
}

AppCore::AppCore(QObject *parent) : QObject(parent) {}

void AppCore::start() {
  if (mStarted) {
    // Two clocks would each start the schedule that came due, which is two
    // copies of one transfer writing the same destination. The queue move
    // ran into exactly this; see JobQueue::setDrivesItself().
    return;
  }
  mStarted = true;

  qCDebug(rbApp) << "starting";

  // Where rclone is, before anything can try to run it.
  //
  // The window used to read this in its constructor, which was early enough
  // while the window was also what read the stored queue. It is not any more:
  // a queue left running starts its first job here, and starting it with no
  // path meant rclone failed in a few milliseconds and the entry was consumed
  // and gone. The queue did not come back, and nothing said why except one
  // line in the job log reading: running "" with 25 arguments.
  SetRclone(GetSettings()->value("Settings/rclone").toString());
  SetRcloneConf(GetSettings()->value("Settings/rcloneConf").toString());

  // Scripts before anything that could run: ScriptRunner listens for jobs
  // starting, and a queue that begins work as soon as it is read would
  // otherwise have started one before anything was listening.
  ScriptRunner::instance().install();

  // The queue keeps itself moving from here. This used to be switched on by
  // the window, which meant a queue left running only resumed if somebody
  // opened one.
  JobQueue::instance().setDrivesItself(true);

  // Schedules before the queue, because a schedule's queued run has to find
  // the queue already holding its entry rather than adding a second.
  SchedulerStore::instance().load();
  JobQueue::instance().load();

  qCDebug(rbApp) << "loaded schedules=" << SchedulerStore::instance().count()
                 << "queue=" << JobQueue::instance().count();

  // The same five seconds the window used to wait before its first look, so
  // a schedule due in the minute the program starts in behaves as it did.
  QTimer::singleShot(5000, Qt::VeryCoarseTimer, this, [this]() { tick(); });
}

void AppCore::checkSchedules() { tick(); }

void AppCore::tick() {
  const QDateTime now = QDateTime::currentDateTime();

  // Every schedule says where it stands, every minute, due or not.
  //
  // This line used to be written by the card, which meant it stopped existing
  // exactly when it was most needed: with no window there was no heartbeat,
  // so a clock that had stopped and a clock with nothing to do looked the
  // same. The whole reason for saying it every time is that silence should
  // be evidence. See debug_log.h.
  for (const Schedule &schedule : SchedulerStore::instance().schedules()) {
    const QDateTime next = schedule.nextRun(now);
    qCDebug(rbSched) << "check" << schedule.name
                     << "next=" << (next.isValid() ? next.toString(Qt::ISODate)
                                                   : QStringLiteral("never"))
                     << "in=" << (next.isValid() ? now.secsTo(next) : -1) << "s"
                     << "status="
                     << (schedule.active ? "activated" : "paused");
  }

  const QList<Schedule> due = SchedulerStore::instance().due(now);

  for (const Schedule &schedule : due) {
    fire(schedule);
  }

  emit ticked();
  scheduleNextTick();
}

void AppCore::scheduleNextTick() {
  // On the next full minute, plus a second, so a schedule set for 07:00 is
  // looked at inside the minute it names rather than on its edge.
  QDateTime next = QDateTime::currentDateTime();
  next.setTime(QTime(next.time().hour(), next.time().minute()));
  next = next.addSecs(60);

  qint64 wait = QDateTime::currentDateTime().msecsTo(next);
  if (wait < 0) {
    wait = 0;
  }
  QTimer::singleShot(wait + 1000, Qt::VeryCoarseTimer, this,
                     [this]() { tick(); });
}

QString AppCore::runNow(const QString &scheduleId, QString *reason) {
  const Schedule *schedule = SchedulerStore::instance().find(scheduleId);
  if (schedule == nullptr) {
    if (reason != nullptr) {
      *reason = QStringLiteral("there is no such schedule");
    }
    return QString();
  }
  qCDebug(rbSched) << "asked to run now" << schedule->name;
  return fire(*schedule, reason);
}

QString AppCore::fire(const Schedule &schedule, QString *reason) {
  const auto hold = [this, &schedule, reason](const QString &why) {
    qCDebug(rbSched) << "held" << schedule.name << "reason=" << why;
    if (reason != nullptr) {
      *reason = why;
    }
    emit scheduleHeld(schedule.id, why);
    return QString();
  };

  JobOptions *task = ListOfJobOptions::getInstance()->find(schedule.taskId);
  if (task == nullptr) {
    // The task was deleted while the schedule still points at it. Saying so
    // rather than doing nothing: a run that never happens and says nothing
    // is indistinguishable from a clock that has stopped.
    return hold(QStringLiteral("its task no longer exists"));
  }

  // The run this schedule started last time, if it is still going. Asked of
  // the registry rather than remembered, so it is true after a restart as
  // well.
  if (!schedule.requestId.isEmpty()) {
    if (const RunningJob *previous =
            JobRegistry::instance().find(schedule.requestId)) {
      if (previous->isRunning()) {
        return hold(QStringLiteral("its last run has not finished"));
      }
    }
  }

  const QString requestId = QUuid::createUuid().toString();

  // 1 means "add it to the queue"; anything else means start it now. The
  // queue gives the run its turn when nothing else is transferring, which is
  // the whole reason somebody chooses it.
  if (schedule.executionMode == 1) {
    for (const QueueEntry &entry : JobQueue::instance().entries()) {
      if (entry.taskId == schedule.taskId) {
        return hold(QStringLiteral("that task is already waiting in the queue"));
      }
    }
    qCDebug(rbSched) << "fired" << schedule.name << "request=" << requestId
                     << "mode=" << "queue";
    JobQueue::instance().enqueue(schedule.taskId, false, requestId);
  } else {
    qCDebug(rbSched) << "fired" << schedule.name << "request=" << requestId
                     << "mode=" << "now";
    StartTask(task, QStringLiteral("scheduler"), requestId, false);
  }

  // Remembered so that the next time this comes due it can tell whether the
  // last run is still going -- and so a window can find the card.
  Schedule updated = schedule;
  updated.requestId = requestId;
  SchedulerStore::instance().update(updated);

  emit scheduleFired(schedule.id, requestId);
  return requestId;
}
