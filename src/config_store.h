#pragma once

// Core (L1): the queue and the schedules, kept in the database.
//
// Both used to be a file of comma-separated lines written by hand from inside
// the window -- queue.conf and scheduler.conf -- with no version, no schema,
// and no reader outside MainWindow. See docs/PLAN.md 6.8 (S13).
//
// What is stored is deliberately unchanged: the same task id and request id
// for a queue entry, and for a schedule the very argument list the scheduler
// widget produces. Only the place changed. Reinterpreting the contents at the
// same time as moving them is how data gets lost in a migration.
//
// The old files are imported once and then renamed to ".migrated" rather than
// deleted, so there is always something to go back to.

#include <QList>
#include <QString>
#include <QStringList>

struct QueueEntry {
  QString taskId;
  QString requestId;
  bool dryRun = false;
};

namespace QueueStore {

// In the order they were queued. Empty when there is no database, which is
// the same thing the file gave when it could not be opened.
QList<QueueEntry> load();

// Replaces the whole queue. The queue is short and always rewritten as a
// whole, exactly as the file was.
bool save(const QList<QueueEntry> &entries);

} // namespace QueueStore

namespace ScheduleStore {

// One argument list per schedule, in the form SchedulerWidget both produces
// and consumes ("key", "value", "key", "value", ...).
QList<QStringList> load();
bool save(const QList<QStringList> &schedules);

// The task a schedule belongs to, read out of its argument list. Kept here so
// the one place that knows the shape of the list is not the window.
QString taskIdOf(const QStringList &args);

} // namespace ScheduleStore
