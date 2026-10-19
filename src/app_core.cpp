#include "app_core.h"
#include "debug_log.h"
#include "job_options.h"
#include "job_queue.h"
#include "job_registry.h"
#include "list_of_job_options.h"
#include "rclone_version.h"
#include "remote_registry.h"
#include "running_job.h"
#include "schedule.h"
#include "scheduler_store.h"
#include "script_runner.h"
#include "webhooks.h"
#include "utils.h"

#include <QDateTime>
#include <functional>
#include <QEventLoop>
#include <QProcess>
#include <QStandardPaths>
#include <QSettings>
#include <QTimer>
#include <QUuid>

AppCore &AppCore::instance() {
  static AppCore core;
  return core;
}

namespace {

// Writes what happened to a schedule's run onto the schedule in the store.
//
// This was the scheduler card's to do: the card heard how its run went and
// the window wrote every card into the store. So with no window -- the
// Docker image's web mode -- a schedule's last run, when it finished and how
// were never written at all, and the page went on showing whatever the last
// window had saved. The card still does its own; this is the record that
// does not depend on there being one.
void recordOn(const QString &requestId,
              const std::function<void(Schedule &)> &change) {
  if (requestId.isEmpty()) {
    return;
  }
  for (const Schedule &schedule : SchedulerStore::instance().schedules()) {
    if (schedule.requestId == requestId) {
      Schedule updated = schedule;
      change(updated);
      SchedulerStore::instance().update(updated);
      return;
    }
  }
}

} // namespace

AppCore::AppCore(QObject *parent) : QObject(parent) {
  // A queued run that comes to the front and starts.
  QObject::connect(&JobRegistry::instance(), &JobRegistry::jobStarted, this,
                   [](RunningJob *job) {
                     recordOn(job->requestId(), [](Schedule &s) {
                       s.lastStatus = QStringLiteral("running");
                     });
                   });
  // And how it ended: "finished", "error" or "stopped", the card's words.
  QObject::connect(&JobRegistry::instance(), &JobRegistry::jobFinished, this,
                   [](RunningJob *job) {
                     const QString how = job->finalStatus();
                     recordOn(job->requestId(), [&how](Schedule &s) {
                       s.lastStatus = how;
                       s.lastFinished =
                           ScheduleStamp(QDateTime::currentDateTime());
                     });
                   });

  // A schedule's run taken out of the queue before it ran is recorded on the
  // schedule, wherever it was taken out from. This was the window's to do,
  // card by card, so a run taken out through the API left the schedule
  // saying "in the queue" about a run that would never happen.
  QObject::connect(&JobQueue::instance(), &JobQueue::removed, this,
                   [](const QString &requestId) {
                     for (const Schedule &schedule :
                          SchedulerStore::instance().schedules()) {
                       if (!requestId.isEmpty() &&
                           schedule.requestId == requestId) {
                         Schedule updated = schedule;
                         updated.lastStatus =
                             QStringLiteral("removed from the queue");
                         updated.lastFinished =
                             ScheduleStamp(QDateTime::currentDateTime());
                         SchedulerStore::instance().update(updated);
                         return;
                       }
                     }
                   });
}

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
  QString rclone = GetSettings()->value("Settings/rclone").toString();
  if (rclone.trimmed().isEmpty()) {
    // Nobody has said where rclone is, which is every first start. The
    // window used to be what went looking, so a daemon on a fresh
    // installation had no rclone at all: it listed no remotes, reported no
    // version, and would have failed every job with an empty program name.
    // In a container that is the normal case, not an unusual one.
    rclone = QStandardPaths::findExecutable(QStringLiteral("rclone"));
    if (rclone.isEmpty()) {
      qCDebug(rbApp) << "no rclone is configured and none is on PATH; nothing "
                        "can run until Settings/rclone says where it is";
    } else {
      qCDebug(rbApp) << "no rclone was configured; using the one on PATH:"
                     << rclone;
      GetSettings()->setValue("Settings/rclone", rclone);
    }
  }
  SetRclone(rclone);
  SetRcloneConf(GetSettings()->value("Settings/rcloneConf").toString());

  readRcloneVersion();

  // Which remotes exist. Only the window ever asked before, so a daemon knew
  // of none: the API answered "no remotes" to anything that asked, and a Web
  // UI could show no folder at all. The same half-migrated shape as the
  // scripts below -- the ability had moved and the wiring had not.
  //
  // Nothing waits for it. The answer arrives through RemoteRegistry's own
  // signal, and everything that reads the list reads it when asked.
  RemoteRegistry::instance().refresh();

  // Scripts before anything that could run: ScriptRunner listens for jobs
  // starting, and a queue that begins work as soon as it is read would
  // otherwise have started one before anything was listening.
  ScriptRunner::instance().install();
  // And whoever wants to be told when a job ends, however it was started.
  Webhooks::instance().install();

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

  reconcileSchedules();

  // The same five seconds the window used to wait before its first look, so
  // a schedule due in the minute the program starts in behaves as it did.
  QTimer::singleShot(5000, Qt::VeryCoarseTimer, this, [this]() { tick(); });
}

void AppCore::checkSchedules() { tick(); }

void AppCore::reconcileSchedules() {
  // The scheduler card used to do this for itself, by turning both words
  // into "unknown" whenever it was made -- which was right for "running" and
  // wrong for "in the queue": the queue is stored, the run is still waiting
  // in it, and it will start. Here it is decided once, against the queue,
  // and the card shows what the store says.
  for (const Schedule &schedule : SchedulerStore::instance().schedules()) {
    if (schedule.lastStatus != QLatin1String("running") &&
        schedule.lastStatus != QLatin1String("in the queue")) {
      continue;
    }
    bool waiting = false;
    for (const QueueEntry &entry : JobQueue::instance().entries()) {
      waiting = waiting || (!schedule.requestId.isEmpty() &&
                            entry.requestId == schedule.requestId);
    }
    const RunningJob *job = JobRegistry::instance().find(schedule.requestId);
    const QString now = waiting ? QStringLiteral("in the queue")
                        : (job != nullptr && job->isRunning())
                            ? QStringLiteral("running")
                            : QStringLiteral("unknown");
    if (now != schedule.lastStatus) {
      qCDebug(rbSched) << "after starting," << schedule.name << "was"
                       << schedule.lastStatus << "and is" << now;
      Schedule updated = schedule;
      updated.lastStatus = now;
      SchedulerStore::instance().update(updated);
    }
  }
}

void AppCore::readRcloneVersion() {
  if (GetRclone().isEmpty()) {
    return;
  }

  // Asked of rclone itself and written where everything that wants it already
  // looks. Only the window used to ask, so without one the API reported an
  // empty version -- and whatever a remote is allowed to do is decided from
  // that version.
  auto *process = new QProcess(this);
  QObject::connect(
      process, &QProcess::finished, this,
      [process](int code, QProcess::ExitStatus) {
        const RcloneVersion version =
            ParseRcloneVersion(process->readAllStandardOutput());
        if (code == 0 && !version.number.isEmpty()) {
          GetSettings()->setValue("Settings/rcloneVersion", version.number);
          qCDebug(rbApp) << "rclone version" << version.number;
        } else {
          qCDebug(rbApp) << "could not read the rclone version, exit" << code;
        }
        process->deleteLater();
      });
  QObject::connect(process, &QProcess::errorOccurred, this,
                   [process](QProcess::ProcessError) {
                     qCDebug(rbApp) << "rclone would not run:"
                                    << process->errorString();
                     process->deleteLater();
                   });
  process->start(GetRclone(), QStringList{QStringLiteral("version")},
                 QIODevice::ReadOnly);
}

int AppCore::shutdown(int waitMs) {
  if (mStopping) {
    return JobRegistry::instance().runningCount();
  }
  mStopping = true;
  emit stopping();

  JobQueue::instance().setHeld(true);

  int asked = 0;
  for (RunningJob *job : JobRegistry::instance().jobs()) {
    if (job->isRunning()) {
      job->stop();
      ++asked;
    }
  }
  qCDebug(rbApp) << "stopping: asked" << asked << "job(s) to stop, waiting up to"
                 << waitMs << "ms";

  // Waited for rather than assumed. A job told to stop writes its last line,
  // closes its log and has its row in the history marked "stopped" only when
  // rclone has actually gone; leaving before that is how a history ends up
  // saying "running" about something that is not.
  if (JobRegistry::instance().runningCount() > 0 && waitMs > 0) {
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&JobRegistry::instance(), &JobRegistry::jobFinished,
                     &loop, [&loop]() {
                       if (JobRegistry::instance().runningCount() == 0) {
                         loop.quit();
                       }
                     });
    deadline.start(waitMs);
    loop.exec();
  }

  const int left = JobRegistry::instance().runningCount();
  if (left > 0) {
    // Said, because this is the case that leaves something behind: an
    // unmount refused because a file on it is open, or an rclone that would
    // not die in time.
    qCDebug(rbApp) << "stopping:" << left << "job(s) had not stopped in time";
  } else {
    qCDebug(rbApp) << "stopping: everything has stopped";
  }
  return left;
}

void AppCore::tick() {
  if (mStopping) {
    return; // and no next tick is scheduled: the clock has stopped
  }

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
    // Written down, so a run that did not happen says why to whoever looks
    // later -- not only to a card that happened to be on screen. The run
    // this schedule started before is kept: it may still be going.
    if (const Schedule *stored = SchedulerStore::instance().find(schedule.id)) {
      Schedule updated = *stored;
      const QString now = ScheduleStamp(QDateTime::currentDateTime());
      updated.lastRun = now;
      updated.lastFinished = now;
      updated.lastStatus = why;
      SchedulerStore::instance().update(updated);
    }
    emit scheduleHeld(schedule.id, why);
    return QString();
  };

  if (mStopping) {
    return hold(QStringLiteral("the program is shutting down"));
  }

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
  updated.lastRun = ScheduleStamp(QDateTime::currentDateTime());
  updated.lastFinished.clear();
  updated.lastStatus = schedule.executionMode == 1
                           ? QStringLiteral("in the queue")
                           : QStringLiteral("running");
  SchedulerStore::instance().update(updated);

  emit scheduleFired(schedule.id, requestId);
  return requestId;
}
