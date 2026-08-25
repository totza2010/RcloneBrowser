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

  // Replaces the whole list and saves it, in the given order -- restoring
  // depends on the order, so it is kept rather than sorted.
  //
  // This is what the window calls when a schedule is edited, added or
  // deleted: it hands over what it has and the store is what writes the
  // file. Schedules that are no longer listed take their "last fired"
  // memory with them, so an id that comes back is not held to a minute the
  // one before it already used.
  bool setAll(const QList<Schedule> &schedules);

  // Whether the scheduler as a whole is switched on. A switched-off
  // scheduler keeps its schedules and starts nothing, the same as a paused
  // queue. Remembered across restarts, as it always has been.
  //
  // This used to be a settings key read in six places in the window, so
  // "is the scheduler on" could only be answered by something with a window
  // -- and the tab, the buttons and the labels each decided it again.
  bool isRunning() const;
  void start();
  void pause();

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
