#include "debug_log.h"
#include "scheduler_store.h"
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

QList<Schedule> SchedulerStore::due(const QDateTime &now) {
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
    qCDebug(rbApp) << "schedule due" << schedule.name << "task=" << schedule.taskName;
    mLastFired[schedule.id] = minute;
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
