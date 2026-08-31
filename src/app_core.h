#pragma once

// Orchestration (L1): the running application, without a window.
//
// Every part of this program can now answer for itself without a window --
// the queue, the schedules, the jobs, the history, the scripts. But nothing
// assembled them except MainWindow's constructor: it read the stored queue,
// read the schedules, installed ScriptRunner and owned the one clock. So a
// build with no window had all the pieces and none of the wiring, and a
// schedule set for 07:00 simply never came round.
//
// That is the same fault ScriptRunner had, one level up: the ability had
// moved and the wiring had not. This is the wiring, in something that is not
// a window. See docs/LAYER-SPLIT.md.

#include <QObject>
#include <QString>

struct Schedule;

class AppCore : public QObject {
  Q_OBJECT

public:
  static AppCore &instance();

  // Reads what was stored and starts the clock. Call once, from main(),
  // before any window is built -- a window attaches to this rather than
  // creating it.
  //
  // Calling it twice does nothing: two clocks would each start the schedule
  // that came due, which is two copies of a transfer writing one
  // destination.
  void start();

  bool isStarted() const { return mStarted; }

  // Looks at the schedules now instead of waiting for the next minute. For a
  // test, and for anything that wants to be sure it has not just missed one.
  void checkSchedules();

  // Starts a schedule's run because somebody asked, rather than because its
  // minute came. Returns the request id, or an empty string with *reason
  // saying why not.
  //
  // The same road the clock takes. The Run button used to have its own copy
  // of all of it -- find the task, mint an id, queue it or start it -- and
  // two implementations of "run this schedule" is one too many, whichever of
  // them is right today.
  QString runNow(const QString &scheduleId, QString *reason = nullptr);

signals:
  // A schedule came due and its run was started, with the id that run will
  // be known by everywhere else -- in the queue, in the job registry and in
  // the history. A window uses it to show which card the run belongs to.
  void scheduleFired(const QString &scheduleId, const QString &requestId);

  // A schedule came due and was not started, and why. Kept apart from
  // scheduleFired because "nothing happened" is the thing people report, and
  // it deserves a reason rather than silence.
  void scheduleHeld(const QString &scheduleId, const QString &reason);

  // The clock ticked, whether or not anything was due. What a view hangs its
  // countdown off.
  void ticked();

private:
  explicit AppCore(QObject *parent = nullptr);

  void tick();
  void scheduleNextTick();

  // Starts the run a schedule is asking for. Returns the request id, or an
  // empty string with the reason reported through scheduleHeld() and, when
  // asked for, written to *reason.
  QString fire(const Schedule &schedule, QString *reason = nullptr);

  bool mStarted = false;
};
