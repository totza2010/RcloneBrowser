#pragma once

// Core (L1): one entry in the scheduler, as data.
//
// A schedule used to exist only as 22 fields inside a SchedulerWidget, saved
// as a comma-separated line of key/value pairs with the values in base64.
// Nothing could read a schedule without building a widget, which is why
// nothing but the window could tell you when the next run was due. See
// docs/API.md S4.
//
// The stored form is still that argument list -- S13 moved it into the
// database without reinterpreting it, on purpose. This is the interpretation,
// kept apart from the storage so that a reading error cannot corrupt what is
// already saved.

#include <QDateTime>
#include <QString>
#include <QStringList>

// Turns the cron expression a person types into the one qcron understands:
// day and month names become numbers, and a sixth field is added because this
// implementation expects a year.
//
// It lived in SchedulerWidget, which meant a cron expression could only be
// read by something with a window. Two copies of this would be two answers to
// "when does this run".
QString NormalizeCron(const QString &cron);

struct Schedule {
  QString id;
  QString name;
  QString taskId;
  QString taskName;

  // The request id of the run this schedule started, empty when it has never
  // started one. It is how a finished job is matched back to the schedule
  // that asked for it.
  QString requestId;

  // "activated" or "paused", the two words the file has always used.
  bool active = false;

  // Daily: run on the ticked days at hour:minute.
  bool dailyMode = true;
  bool monday = true, tuesday = true, wednesday = true, thursday = true;
  bool friday = true, saturday = true, sunday = true;
  int hour = 0;
  int minute = 0;

  // Cron: a five-field expression, checked by qcron.
  bool cronMode = false;
  QString cron = QStringLiteral("30 6,18 * * MON-FRI");

  // 0 = run immediately, 1 = add to the queue.
  int executionMode = 0;

  QString lastRun = QStringLiteral("never");
  QString lastFinished;
  QString lastStatus;

  bool runsOn(Qt::DayOfWeek day) const;

  // Whether this schedule wants to run at this very minute. Seconds are
  // ignored: a schedule set for 07:00 is due for the whole of 07:00, and
  // whoever asks decides how often to ask.
  bool isDue(const QDateTime &now) const;

  // When this schedule next comes due, counting from `from`. Invalid when it
  // never will -- a daily schedule with no day ticked, or a cron expression
  // qcron cannot read.
  QDateTime nextRun(const QDateTime &from) const;

  // The argument list the scheduler widget reads and writes, and the form the
  // database holds. Values that were base64 in the file stay base64 here:
  // this is a translation, not a new format.
  QStringList toArgs() const;
  static Schedule fromArgs(const QStringList &args);
};
