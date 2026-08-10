#pragma once

// Orchestration (L1): the schedules, and when each one is next due.
//
// "Is anything due?" used to be answered by a timer inside every
// SchedulerWidget, so the answer existed only while a window did. This holds
// the schedules as data and says which one has come due, without knowing
// anything about how one is drawn. See docs/API.md S4.

#include "schedule.h"

#include <QObject>

class SchedulerStore : public QObject {
  Q_OBJECT

public:
  static SchedulerStore &instance();

  const QList<Schedule> &schedules() const { return mSchedules; }
  int count() const { return mSchedules.size(); }

  const Schedule *find(const QString &id) const;

  void add(const Schedule &schedule);
  void update(const Schedule &schedule);
  void remove(const QString &id);

  // Everything that is active and whose next run is not in the future.
  // Returns the schedules that should be started now, and records the check
  // so that the same minute cannot fire twice.
  QList<Schedule> due(const QDateTime &now);

  void load();
  bool save();

signals:
  void changed();

private:
  explicit SchedulerStore(QObject *parent = nullptr);

  QList<Schedule> mSchedules;

  // When each schedule was last found due. A schedule that runs at 07:00 is
  // due for the whole of that minute, and something asking every second must
  // not start it sixty times.
  QHash<QString, QDateTime> mLastFired;
};
