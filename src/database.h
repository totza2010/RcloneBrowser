#pragma once

// Core (L0): the SQLite file that everything durable lives in.
//
// Before this there were three hand-made formats -- tasks.bin (a QDataStream
// whose meaning depends on field order), queue.conf and scheduler.conf (comma
// separated, no version) -- and the run history was not stored at all: closing
// the window threw away every record of what had run. See docs/PLAN.md 6.8.
//
// Two processes write this file. E1 deliberately left --run-task without a
// single-instance lock so a scheduled run can happen while the window is open,
// which means concurrent writers are a design condition rather than an edge
// case: hence WAL and a busy timeout, set on every connection below.

#include <QSqlDatabase>
#include <QString>

class Database {
public:
  // Raised whenever the tables change shape. Migration is by version number
  // and runs on open, so an older file is upgraded in place and a newer one
  // is refused rather than half-read.
  //
  // 1: meta, job_run     2: task, queue_entry, schedule
  static constexpr int kSchemaVersion = 2;

  // Alongside the settings, so a portable installation keeps its history with
  // the rest of its data.
  static QString defaultPath();

  // Points every future connection at another file. Existing connections are
  // closed, so this is also how a test starts from an empty database.
  static void setPath(const QString &path);
  static QString path();

  // This thread's connection, opened and migrated on first use.
  //
  // QtSql forbids sharing a connection between threads, so there is one per
  // thread rather than one per process. An invalid database is returned when
  // the driver is missing or the file cannot be opened; callers keep working
  // without history rather than refusing to run a transfer over it.
  static QSqlDatabase connection();

  static bool isAvailable();

  // Why the last open or migration failed. Empty when nothing has.
  static QString lastError();

  // Drops this thread's connection. Every thread that opened one must call
  // this before it ends, or QtSql warns about a connection still in use.
  static void closeForThread();

  // The version recorded in the file, 0 for one that has just been created.
  static int schemaVersion();
};
