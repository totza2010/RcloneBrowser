#include "debug_log.h"
#include "run_history.h"
#include "app_settings.h"
#include "database.h"
#include "utils.h"

#include <QDateTime>
#include <QFile>
#include <QSettings>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>

namespace {

JobRunRecord readRow(const QSqlQuery &query) {
  JobRunRecord r;
  r.requestId = query.value(QStringLiteral("request_id")).toString();
  r.taskId = query.value(QStringLiteral("task_id")).toString();
  r.taskName = query.value(QStringLiteral("task_name")).toString();
  r.kind = query.value(QStringLiteral("kind")).toString();
  r.transferMode = query.value(QStringLiteral("transfer_mode")).toString();
  r.info = query.value(QStringLiteral("info")).toString();
  r.source = query.value(QStringLiteral("source")).toString();
  r.dest = query.value(QStringLiteral("dest")).toString();
  r.startedAt = query.value(QStringLiteral("started_at")).toLongLong();
  r.finishedAt = query.value(QStringLiteral("finished_at")).toLongLong();
  r.state = query.value(QStringLiteral("state")).toString();
  r.exitCode = query.value(QStringLiteral("exit_code")).toInt();
  r.bytes = query.value(QStringLiteral("bytes")).toLongLong();
  r.totalBytes = query.value(QStringLiteral("total_bytes")).toLongLong();
  r.transfers = query.value(QStringLiteral("transfers")).toLongLong();
  r.errors = query.value(QStringLiteral("errors")).toLongLong();
  r.logPath = query.value(QStringLiteral("log_path")).toString();
  r.logBytes = query.value(QStringLiteral("log_bytes")).toLongLong();
  r.dryRun = query.value(QStringLiteral("dry_run")).toInt() != 0;
  return r;
}

QList<JobRunRecord> select(const QString &where, const QVariantList &binds,
                           int limit) {
  QList<JobRunRecord> rows;
  QSqlDatabase db = Database::connection();
  if (!db.isOpen()) {
    return rows;
  }

  QSqlQuery query(db);
  query.prepare(QStringLiteral("SELECT * FROM job_run %1 "
                               "ORDER BY started_at DESC LIMIT ?")
                    .arg(where));
  for (const QVariant &bind : binds) {
    query.addBindValue(bind);
  }
  query.addBindValue(limit > 0 ? limit : -1); // SQLite: -1 means no limit
  if (!query.exec()) {
    return rows;
  }
  while (query.next()) {
    rows.append(readRow(query));
  }
  return rows;
}

// Every column of the row, in one statement that works whether or not the
// start was recorded. Keeping insert and update as one statement means an
// ending can never be dropped because the beginning went missing -- which is
// exactly what happens when the database only becomes writable part way
// through a run.
bool upsert(const JobRunRecord &r) {
  QSqlDatabase db = Database::connection();
  if (!db.isOpen()) {
    return false;
  }

  QSqlQuery query(db);
  query.prepare(QStringLiteral(
      "INSERT INTO job_run (request_id, task_id, task_name, kind, "
      "transfer_mode, info, source, dest, started_at, finished_at, state, "
      "exit_code, bytes, total_bytes, transfers, errors, log_path, log_bytes, "
      "dry_run) "
      "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
      "ON CONFLICT(request_id) DO UPDATE SET "
      "task_id=excluded.task_id, task_name=excluded.task_name, "
      "kind=excluded.kind, transfer_mode=excluded.transfer_mode, "
      "info=excluded.info, source=excluded.source, dest=excluded.dest, "
      "finished_at=excluded.finished_at, state=excluded.state, "
      "exit_code=excluded.exit_code, bytes=excluded.bytes, "
      "total_bytes=excluded.total_bytes, transfers=excluded.transfers, "
      "errors=excluded.errors, log_path=excluded.log_path, "
      "log_bytes=excluded.log_bytes, dry_run=excluded.dry_run"));
  // started_at is deliberately not updated: the first record of when a job
  // began is the true one.

  query.addBindValue(r.requestId);
  query.addBindValue(r.taskId);
  query.addBindValue(r.taskName);
  query.addBindValue(r.kind);
  query.addBindValue(r.transferMode);
  query.addBindValue(r.info);
  query.addBindValue(r.source);
  query.addBindValue(r.dest);
  query.addBindValue(r.startedAt != 0
                         ? r.startedAt
                         : QDateTime::currentMSecsSinceEpoch());
  query.addBindValue(r.finishedAt);
  query.addBindValue(r.state);
  query.addBindValue(r.exitCode);
  query.addBindValue(r.bytes);
  query.addBindValue(r.totalBytes);
  query.addBindValue(r.transfers);
  query.addBindValue(r.errors);
  query.addBindValue(r.logPath);
  query.addBindValue(r.logBytes);
  query.addBindValue(r.dryRun ? 1 : 0);

  return query.exec();
}

// Rows and the log files they point at go together. Leaving the files would
// recreate the exact problem this table was added to solve: a logs directory
// nothing has an index of.
int deleteRows(QSqlDatabase &db,
               const QList<QPair<QString, QString>> &doomed) {
  int removed = 0;
  QSqlQuery del(db);
  del.prepare(QStringLiteral("DELETE FROM job_run WHERE request_id = ?"));
  for (const auto &row : doomed) {
    del.addBindValue(row.first);
    if (!del.exec()) {
      continue;
    }
    if (del.numRowsAffected() <= 0) {
      continue; // already gone: the two rules below can condemn the same row
    }
    ++removed;
    if (!row.second.isEmpty()) {
      QFile::remove(row.second);
    }
  }
  return removed;
}

} // namespace

bool RunHistory::recordStarted(const JobRunRecord &record) {
  if (record.requestId.isEmpty()) {
    return false;
  }
  JobRunRecord r = record;
  if (r.state.isEmpty()) {
    r.state = QStringLiteral("running");
  }
  return upsert(r);
}

bool RunHistory::recordFinished(const JobRunRecord &record) {
  if (record.requestId.isEmpty()) {
    return false;
  }
  JobRunRecord r = record;
  if (r.finishedAt == 0) {
    r.finishedAt = QDateTime::currentMSecsSinceEpoch();
  }
  return upsert(r);
}

QList<JobRunRecord> RunHistory::recent(int limit) {
  return select(QString(), {}, limit);
}

namespace {

// The newest transfer whose `column` is on that remote.
//
// Matched on the leading "name:" with substr() rather than LIKE, on purpose:
// in LIKE an underscore stands for any one character, and remotes here are
// called things like tgdrive_main_01 -- so LIKE would also have matched a
// remote named tgdriveXmain-01, and reported one remote's upload as
// another's.
// Whether the path in `column` is on a remote rather than on this machine,
// as SQL.
//
// A remote path is "name:rest", so it has a colon before any slash. A
// Windows path has one too -- "C:/films" -- but after a single letter, and a
// drive is not a remote; TaskBuilder::RemoteOf draws the same line. A path
// beginning with a slash is local wherever its colons are.
QString onARemote(const char *column) {
  // An end that was never recorded is not known to be on a remote, and in
  // SQL a comparison with nothing is itself nothing -- which would quietly
  // drop the row. Treated as empty, and so as local.
  const QString c = QStringLiteral("COALESCE(%1, '')").arg(QLatin1String(column));
  return QStringLiteral("(instr(%1, ':') > 2 AND substr(%1, 1, 1) <> '/' "
                        "AND (instr(%1, '/') = 0 OR instr(%1, ':') < instr(%1, '/')))")
      .arg(c);
}

// The newest transfer between this machine and that remote: `column` is the
// end that is on the remote, `otherEnd` the one that has to be local.
JobRunRecord lastOnRemote(const char *column, const char *otherEnd,
                          const QString &remote) {
  if (remote.isEmpty()) {
    return JobRunRecord();
  }
  const QString prefix = remote + QLatin1Char(':');
  const QList<JobRunRecord> rows = select(
      // A dry run sent nothing, so it is not the last time anything was sent.
      //
      // And the other end has to be this machine. Without that, copying a
      // file from one folder of a remote to another showed up as that
      // remote's last upload *and* its last download at once, which is how
      // it was found.
      QStringLiteral("WHERE kind = 'transfer' AND substr(%1, 1, ?) = ? "
                     "AND dry_run = 0 AND NOT %2")
          .arg(QLatin1String(column), onARemote(otherEnd)),
      {prefix.size(), prefix}, 1);
  return rows.isEmpty() ? JobRunRecord() : rows.first();
}

} // namespace

JobRunRecord RunHistory::lastUploadTo(const QString &remote) {
  return lastOnRemote("dest", "source", remote);
}

JobRunRecord RunHistory::lastDownloadFrom(const QString &remote) {
  return lastOnRemote("source", "dest", remote);
}

QList<JobRunRecord> RunHistory::forTask(const QString &taskId, int limit) {
  if (taskId.isEmpty()) {
    return {};
  }
  return select(QStringLiteral("WHERE task_id = ?"), {taskId}, limit);
}

JobRunRecord RunHistory::find(const QString &requestId) {
  if (requestId.isEmpty()) {
    return {};
  }
  const QList<JobRunRecord> rows =
      select(QStringLiteral("WHERE request_id = ?"), {requestId}, 1);
  return rows.isEmpty() ? JobRunRecord() : rows.first();
}

int RunHistory::count() {
  QSqlDatabase db = Database::connection();
  if (!db.isOpen()) {
    return 0;
  }
  QSqlQuery query(db);
  if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM job_run")) ||
      !query.next()) {
    return 0;
  }
  return query.value(0).toInt();
}

int RunHistory::retentionDays() {
  return AppSettings::historyRetentionDays();
}

int RunHistory::retentionRows() { return AppSettings::historyMaxRuns(); }

int RunHistory::purge(int keepDays, int keepRows) {
  qCDebug(rbDb) << "purging history keepDays=" << keepDays
                << "keepRows=" << keepRows;
  QSqlDatabase db = Database::connection();
  if (!db.isOpen()) {
    return 0;
  }

  // A job that is still going is never dropped, however old it looks: a mount
  // left up for a month is still a live job, and deleting its log would take
  // the file out from under the process writing it.
  QList<QPair<QString, QString>> doomed; // request_id, log_path

  if (keepDays > 0) {
    const qint64 cutoff =
        QDateTime::currentDateTime().addDays(-keepDays).toMSecsSinceEpoch();
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT request_id, log_path FROM job_run "
        "WHERE state <> 'running' AND started_at < ?"));
    query.addBindValue(cutoff);
    if (query.exec()) {
      while (query.next()) {
        doomed.append({query.value(0).toString(), query.value(1).toString()});
      }
    }
  }

  if (keepRows > 0) {
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT request_id, log_path FROM job_run "
        "WHERE state <> 'running' AND request_id NOT IN "
        "(SELECT request_id FROM job_run ORDER BY started_at DESC LIMIT ?)"));
    query.addBindValue(keepRows);
    if (query.exec()) {
      while (query.next()) {
        doomed.append({query.value(0).toString(), query.value(1).toString()});
      }
    }
  }

  return deleteRows(db, doomed);
}

int RunHistory::clear() {
  QSqlDatabase db = Database::connection();
  if (!db.isOpen()) {
    return 0;
  }

  QList<QPair<QString, QString>> doomed;
  QSqlQuery query(db);
  if (query.exec(QStringLiteral("SELECT request_id, log_path FROM job_run "
                                "WHERE state <> 'running'"))) {
    while (query.next()) {
      doomed.append({query.value(0).toString(), query.value(1).toString()});
    }
  }
  return deleteRows(db, doomed);
}
