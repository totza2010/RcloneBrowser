#include "database.h"
#include "utils.h"

#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>

namespace {

QMutex gMutex;
QString gPath;
QString gLastError;

// Bumped by setPath(). It is part of the connection name so that a thread
// which already opened the old file gets a new connection instead of silently
// carrying on with the old one.
int gGeneration = 0;

// The connection this thread opened, so closeForThread() can remove exactly
// that one. QSqlDatabase::removeDatabase() must be called from the thread that
// owns the connection, which rules out closing other threads' connections from
// here.
thread_local QString tConnectionName;

void setLastError(const QString &text) {
  QMutexLocker lock(&gMutex);
  gLastError = text;
}

bool run(QSqlDatabase &db, const QString &sql) {
  QSqlQuery query(db);
  if (query.exec(sql)) {
    return true;
  }
  setLastError(query.lastError().text() + QStringLiteral(" [") + sql +
               QStringLiteral("]"));
  return false;
}

int readVersion(QSqlDatabase &db) {
  QSqlQuery query(db);
  if (!query.exec(QStringLiteral(
          "SELECT value FROM meta WHERE key = 'schema_version'"))) {
    return 0; // no meta table yet: a database that has never been written to
  }
  if (!query.next()) {
    return 0;
  }
  return query.value(0).toInt();
}

// Every version step is written out rather than generated, so upgrading an
// existing file follows the same path that created it.
bool migrateTo1(QSqlDatabase &db) {
  if (!run(db, QStringLiteral("CREATE TABLE IF NOT EXISTS meta ("
                              "key TEXT PRIMARY KEY, value TEXT)"))) {
    return false;
  }

  // One row per run of a job. The columns are the questions history is asked:
  // what ran, when, how it ended, how much moved, and where its log is.
  //
  // task_id is not a foreign key to task(id): the tasks table arrives with a
  // later version, and a run has to outlive the task it came from anyway --
  // which is why the name is copied in as well.
  if (!run(db, QStringLiteral(
                   "CREATE TABLE IF NOT EXISTS job_run ("
                   "request_id TEXT PRIMARY KEY,"
                   "task_id TEXT,"
                   "task_name TEXT,"
                   "kind TEXT,"
                   "transfer_mode TEXT,"
                   "info TEXT,"
                   "source TEXT,"
                   "dest TEXT,"
                   "started_at INTEGER NOT NULL,"
                   "finished_at INTEGER,"
                   "state TEXT,"
                   "exit_code INTEGER,"
                   "bytes INTEGER DEFAULT 0,"
                   "total_bytes INTEGER DEFAULT 0,"
                   "transfers INTEGER DEFAULT 0,"
                   "errors INTEGER DEFAULT 0,"
                   "log_path TEXT,"
                   "log_bytes INTEGER DEFAULT 0)"))) {
    return false;
  }

  // The two orders history is ever read in: one task's runs, and everything
  // newest first.
  if (!run(db, QStringLiteral("CREATE INDEX IF NOT EXISTS job_run_by_task "
                              "ON job_run(task_id, started_at DESC)"))) {
    return false;
  }
  return run(db, QStringLiteral("CREATE INDEX IF NOT EXISTS job_run_by_time "
                                "ON job_run(started_at DESC)"));
}

// The three hand-made files: tasks.bin, queue.conf, scheduler.conf.
bool migrateTo2(QSqlDatabase &db) {
  // The 45 fields of a task live in one JSON column. A column each would have
  // to be altered every time a field is added, which is the brittleness that
  // made tasks.bin painful in the first place; what gets searched is lifted
  // out into columns beside it.
  if (!run(db, QStringLiteral("CREATE TABLE IF NOT EXISTS task ("
                              "id TEXT PRIMARY KEY,"
                              "name TEXT NOT NULL,"
                              "operation INTEGER,"
                              "source TEXT,"
                              "dest TEXT,"
                              "options TEXT NOT NULL,"
                              "position INTEGER DEFAULT 0,"
                              "created_at INTEGER,"
                              "updated_at INTEGER)"))) {
    return false;
  }

  // position is what queue.conf recorded by being a file: the order of the
  // lines. A set of rows has no order until one is written down.
  if (!run(db, QStringLiteral("CREATE TABLE IF NOT EXISTS queue_entry ("
                              "request_id TEXT PRIMARY KEY,"
                              "task_id TEXT NOT NULL,"
                              "position INTEGER NOT NULL,"
                              "dry_run INTEGER NOT NULL DEFAULT 0)"))) {
    return false;
  }

  // A schedule is stored as the argument list the scheduler itself produces,
  // as JSON. Splitting it into columns would mean this file having to know
  // what a schedule is made of, which is knowledge that belongs to the
  // scheduler and would then exist in two places.
  return run(db, QStringLiteral("CREATE TABLE IF NOT EXISTS schedule ("
                                "id TEXT PRIMARY KEY,"
                                "task_id TEXT,"
                                "position INTEGER NOT NULL,"
                                "args TEXT NOT NULL)"));
}

bool migrate(QSqlDatabase &db) {
  int version = readVersion(db);
  if (version == Database::kSchemaVersion) {
    return true;
  }
  if (version > Database::kSchemaVersion) {
    // Written by a newer build. Guessing at columns that did not exist yet is
    // how data gets corrupted; refusing costs the user their history for this
    // session only.
    setLastError(
        QStringLiteral("database schema is version %1, this build understands "
                       "%2 -- history is disabled")
            .arg(version)
            .arg(Database::kSchemaVersion));
    return false;
  }

  // IMMEDIATE takes the write lock up front. The window and a --run-task from
  // cron can reach an empty database at the same moment, and a deferred
  // transaction only discovers the conflict at the first write -- by which
  // point both think they are creating the tables.
  if (!run(db, QStringLiteral("BEGIN IMMEDIATE"))) {
    return false;
  }

  // Another process may have migrated while we waited for the lock.
  version = readVersion(db);
  bool ok = true;
  if (version < 1) {
    ok = migrateTo1(db);
  }
  if (ok && version < 2) {
    ok = migrateTo2(db);
  }

  if (ok) {
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO meta(key, value) VALUES('schema_version', ?) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    query.addBindValue(QString::number(Database::kSchemaVersion));
    if (!query.exec()) {
      setLastError(query.lastError().text());
      ok = false;
    }
  }

  run(db, ok ? QStringLiteral("COMMIT") : QStringLiteral("ROLLBACK"));
  return ok;
}

} // namespace

QString Database::defaultPath() {
  return GetConfigDir().filePath(QStringLiteral("rclone-browser.db"));
}

void Database::setPath(const QString &path) {
  closeForThread();
  QMutexLocker lock(&gMutex);
  gPath = path;
  ++gGeneration;
  gLastError.clear();
}

QString Database::path() {
  QMutexLocker lock(&gMutex);
  return gPath.isEmpty() ? defaultPath() : gPath;
}

QSqlDatabase Database::connection() {
  QString file;
  int generation = 0;
  {
    QMutexLocker lock(&gMutex);
    if (gPath.isEmpty()) {
      gPath = defaultPath();
    }
    file = gPath;
    generation = gGeneration;
  }

  const QString name =
      QStringLiteral("rbdb-%1-%2")
          .arg(generation)
          .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));

  if (QSqlDatabase::contains(name)) {
    {
      QSqlDatabase existing = QSqlDatabase::database(name, false);
      if (existing.isOpen()) {
        return existing;
      }
    }
    // Every QSqlDatabase referring to this connection has to be out of scope
    // first: Qt warns, and leaves the connection half-removed, otherwise.
    QSqlDatabase::removeDatabase(name);
  }

  if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
    setLastError(QStringLiteral("the Qt SQLite driver is not installed"));
    return QSqlDatabase();
  }

  // An in-memory database is per-connection by definition, so it is only ever
  // used by tests, and it has no directory to create.
  if (file != QStringLiteral(":memory:")) {
    const QDir dir = QFileInfo(file).absoluteDir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
      setLastError(QStringLiteral("could not create %1").arg(dir.path()));
      return QSqlDatabase();
    }
  }

  // Scoped so that nothing still refers to the connection if it has to be
  // removed below -- removeDatabase() with a live QSqlDatabase in hand leaves
  // it in a state where every later query fails, and only warns about it.
  bool ok = false;
  {
    QSqlDatabase db =
        QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(file);
    // Waiting is the right answer to a database another process is writing;
    // failing immediately would lose a record of a run that really happened.
    db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=10000"));

    if (db.open()) {
      if (file != QStringLiteral(":memory:")) {
        // WAL lets the window read history while a CLI run is writing it. It
        // is a property of the file, so this only has to succeed once, but
        // asking every time costs nothing and covers a file created by an
        // older build.
        run(db, QStringLiteral("PRAGMA journal_mode=WAL"));
        // With WAL, a commit does not have to reach the platter to be durable
        // against a crash of this process -- only against losing power, and
        // the history of a transfer is not worth an fsync per row.
        run(db, QStringLiteral("PRAGMA synchronous=NORMAL"));
      }
      ok = migrate(db);
      if (!ok) {
        db.close();
      }
    } else {
      setLastError(db.lastError().text());
    }
  }

  if (!ok) {
    QSqlDatabase::removeDatabase(name);
    return QSqlDatabase();
  }

  tConnectionName = name;
  return QSqlDatabase::database(name, false);
}

bool Database::isAvailable() { return connection().isOpen(); }

QString Database::lastError() {
  QMutexLocker lock(&gMutex);
  return gLastError;
}

void Database::closeForThread() {
  if (tConnectionName.isEmpty()) {
    return;
  }
  const QString name = tConnectionName;
  tConnectionName.clear();
  if (QSqlDatabase::contains(name)) {
    {
      // The QSqlDatabase must be out of scope before the connection is
      // removed, or Qt warns that it is still in use.
      QSqlDatabase db = QSqlDatabase::database(name, false);
      if (db.isOpen()) {
        db.close();
      }
    }
    QSqlDatabase::removeDatabase(name);
  }
}

int Database::schemaVersion() {
  QSqlDatabase db = connection();
  if (!db.isOpen()) {
    return 0;
  }
  return readVersion(db);
}
