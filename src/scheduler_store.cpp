#include "debug_log.h"
#include "scheduler_store.h"
#include "app_settings.h"
#include "config_store.h"

SchedulerStore &SchedulerStore::instance() {
  static SchedulerStore store;
  return store;
}

SchedulerStore::SchedulerStore(QObject *parent) : QObject(parent) {}

const Schedule *SchedulerStore::find(const QString &id) const {
  if (id.isEmpty()) {
    return nullptr;
  }
  for (const Schedule &schedule : mSchedules) {
    if (schedule.id == id) {
      return &schedule;
    }
  }
  return nullptr;
}

void SchedulerStore::add(const Schedule &schedule) {
  mSchedules.append(schedule);
  save();
  emit changed();
}

void SchedulerStore::update(const Schedule &schedule) {
  for (int i = 0; i < mSchedules.size(); ++i) {
    if (mSchedules[i].id == schedule.id) {
      mSchedules[i] = schedule;
      save();
      emit changed();
      return;
    }
  }
  add(schedule);
}

void SchedulerStore::remove(const QString &id) {
  for (int i = 0; i < mSchedules.size(); ++i) {
    if (mSchedules[i].id == id) {
      mSchedules.removeAt(i);
      mLastFired.remove(id);
      save();
      emit changed();
      return;
    }
  }
}

bool SchedulerStore::isRunning() const {
  return AppSettings::schedulerIsRunning();
}

void SchedulerStore::start() {
  if (isRunning()) {
    return;
  }
  qCDebug(rbSched) << "scheduler switched on, schedules=" << mSchedules.size();
  AppSettings::setSchedulerIsRunning(true);
  emit changed();
}

void SchedulerStore::pause() {
  if (!isRunning()) {
    return;
  }
  qCDebug(rbSched) << "scheduler switched off, schedules=" << mSchedules.size();
  AppSettings::setSchedulerIsRunning(false);
  emit changed();
}

QList<Schedule> SchedulerStore::due(const QDateTime &now) {
  // Nothing runs while the scheduler is switched off. Until now that test was
  // the window's, so a schedule could come due with nothing to stop it if
  // anything but the window ever asked.
  const bool running = isRunning();

  // The minute is the unit a schedule is written in, so it is the unit this
  // remembers. Whoever asks may ask every second.
  const QDateTime minute(now.date(), QTime(now.time().hour(),
                                           now.time().minute()));

  QList<Schedule> ready;
  for (const Schedule &schedule : mSchedules) {
    if (!schedule.active || !schedule.isDue(now)) {
      continue;
    }
    if (mLastFired.value(schedule.id) == minute) {
      continue;
    }

    // Marked as seen even when the scheduler is off, so that switching it
    // back on part way through a minute does not run that minute late. A
    // schedule set for 07:00 means 07:00, not "the next time anyone looks".
    mLastFired[schedule.id] = minute;

    if (!running) {
      qCDebug(rbSched) << "due but the scheduler is off" << schedule.name;
      continue;
    }
    qCDebug(rbSched) << "due" << schedule.name
                     << "task=" << schedule.taskName;
    ready.append(schedule);
  }
  return ready;
}

void SchedulerStore::load() {
  mSchedules.clear();
  mLastFired.clear();

  // The stored form is the scheduler's own argument list, kept word for word
  // by S13. Reading it into a Schedule is this layer's job, and a row that
  // cannot be read is skipped rather than taking the rest with it.
  for (const QStringList &args : ScheduleStore::load()) {
    const Schedule schedule = Schedule::fromArgs(args);
    if (!schedule.taskId.isEmpty()) {
      mSchedules.append(schedule);
    }
  }
  emit changed();
}

bool SchedulerStore::save() {
  QList<QStringList> rows;
  rows.reserve(mSchedules.size());
  for (const Schedule &schedule : mSchedules) {
    rows.append(schedule.toArgs());
  }
  return ScheduleStore::save(rows);
}
