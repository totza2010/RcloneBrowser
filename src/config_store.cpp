#include "debug_log.h"
#include "config_store.h"
#include "database.h"
#include "utils.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>
#include <QUuid>

namespace {

// Renamed rather than removed once its contents are in the database. The
// import only runs when the table is empty, so the rename is also what stops
// it running twice.
void retire(const QString &fileName) {
  const QString path = GetConfigDir().absoluteFilePath(fileName);
  if (QFile::exists(path)) {
    QFile::rename(path, path + QStringLiteral(".migrated"));
  }
}

bool tableIsEmpty(QSqlDatabase &db, const QString &table) {
  QSqlQuery query(db);
  return query.exec(QStringLiteral("SELECT COUNT(*) FROM ") + table) &&
         query.next() && query.value(0).toInt() == 0;
}

QStringList readLines(const QString &fileName) {
  QFile file(GetConfigDir().absoluteFilePath(fileName));
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  QStringList lines;
  QTextStream in(&file);
  while (!in.atEnd()) {
    const QString line = in.readLine();
    if (!line.isEmpty()) {
      lines.append(line);
    }
  }
  return lines;
}

QList<QueueEntry> readLegacyQueue() {
  QList<QueueEntry> entries;
  for (const QString &line : readLines(QStringLiteral("queue.conf"))) {
    QueueEntry entry;
    const int comma = line.indexOf(QLatin1Char(','));
    if (comma == -1) {
      // A file from a version that recorded only the task. The request id is
      // what everything since keys off, so one is invented rather than
      // dropping the entry.
      entry.taskId = line;
      entry.requestId = QUuid::createUuid().toString();
    } else {
      entry.taskId = line.left(comma);
      entry.requestId = line.mid(comma + 1);
    }
    entries.append(entry);
  }
  return entries;
}

QList<QStringList> readLegacySchedules() {
  QList<QStringList> schedules;
  for (const QString &line : readLines(QStringLiteral("scheduler.conf"))) {
    schedules.append(line.split(QLatin1Char(',')));
  }
  return schedules;
}

QString valueOf(const QStringList &args, const QString &key) {
  const int at = args.indexOf(key);
  if (at == -1 || at + 1 >= args.size()) {
    return QString();
  }
  return args.at(at + 1);
}

} // namespace

QList<QueueEntry> QueueStore::load() {
  QSqlDatabase db = Database::connection();
  if (!db.isOpen()) {
    return readLegacyQueue(); // no driver: behave as before, from the file
  }

  if (tableIsEmpty(db, QStringLiteral("queue_entry"))) {
    const QList<QueueEntry> legacy = readLegacyQueue();
    qCDebug(rbDb) << "importing queue.conf entries=" << legacy.size();
    if (!legacy.isEmpty() && save(legacy)) {
      retire(QStringLiteral("queue.conf"));
      return legacy;
    }
  }

  QList<QueueEntry> entries;
  QSqlQuery query(db);
  if (!query.exec(QStringLiteral("SELECT task_id, request_id, dry_run FROM "
                                 "queue_entry ORDER BY position"))) {
    return entries;
  }
  while (query.next()) {
    QueueEntry entry;
    entry.taskId = query.value(0).toString();
    entry.requestId = query.value(1).toString();
    entry.dryRun = query.value(2).toBool();
    entries.append(entry);
  }
  return entries;
}

bool QueueStore::save(const QList<QueueEntry> &entries) {
  QSqlDatabase db = Database::connection();
  if (!db.isOpen()) {
    return false;
  }

  QSqlQuery begin(db);
  if (!begin.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
    return false;
  }

  // Replaced whole rather than reconciled: the queue is short, it is rewritten
  // on every change, and its order is the whole point -- which makes "what
  // moved where" a harder question than "here is the queue now".
  QSqlQuery clear(db);
  bool ok = clear.exec(QStringLiteral("DELETE FROM queue_entry"));

  for (int i = 0; i < entries.size() && ok; ++i) {
    QSqlQuery query(db);
    query.prepare(QStringLiteral("INSERT INTO queue_entry (request_id, "
                                 "task_id, position, dry_run) "
                                 "VALUES (?,?,?,?)"));
    query.addBindValue(entries[i].requestId);
    query.addBindValue(entries[i].taskId);
    query.addBindValue(i);
    query.addBindValue(entries[i].dryRun ? 1 : 0);
    ok = query.exec();
  }

  QSqlQuery end(db);
  end.exec(ok ? QStringLiteral("COMMIT") : QStringLiteral("ROLLBACK"));
  return ok;
}

QString ScheduleStore::taskIdOf(const QStringList &args) {
  return valueOf(args, QStringLiteral("mTaskId"));
}

QList<QStringList> ScheduleStore::load() {
  QSqlDatabase db = Database::connection();
  if (!db.isOpen()) {
    return readLegacySchedules();
  }

  if (tableIsEmpty(db, QStringLiteral("schedule"))) {
    const QList<QStringList> legacy = readLegacySchedules();
    qCDebug(rbDb) << "importing scheduler.conf entries=" << legacy.size();
    if (!legacy.isEmpty() && save(legacy)) {
      retire(QStringLiteral("scheduler.conf"));
      return legacy;
    }
  }

  QList<QStringList> schedules;
  QSqlQuery query(db);
  if (!query.exec(
          QStringLiteral("SELECT args FROM schedule ORDER BY position"))) {
    return schedules;
  }
  while (query.next()) {
    const QJsonDocument doc =
        QJsonDocument::fromJson(query.value(0).toString().toUtf8());
    if (!doc.isArray()) {
      continue;
    }
    QStringList args;
    for (const QJsonValue &value : doc.array()) {
      args.append(value.toString());
    }
    schedules.append(args);
  }
  return schedules;
}

bool ScheduleStore::save(const QList<QStringList> &schedules) {
  QSqlDatabase db = Database::connection();
  if (!db.isOpen()) {
    return false;
  }

  QSqlQuery begin(db);
  if (!begin.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
    return false;
  }

  QSqlQuery clear(db);
  bool ok = clear.exec(QStringLiteral("DELETE FROM schedule"));

  for (int i = 0; i < schedules.size() && ok; ++i) {
    const QStringList &args = schedules[i];

    // A schedule without an id of its own would be indistinguishable from
    // every other one the moment two of them were saved.
    QString id = valueOf(args, QStringLiteral("mSchedulerId"));
    if (id.isEmpty()) {
      id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    QJsonArray json;
    for (const QString &arg : args) {
      json.append(arg);
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral("INSERT INTO schedule (id, task_id, "
                                 "position, args) VALUES (?,?,?,?)"));
    query.addBindValue(id);
    query.addBindValue(taskIdOf(args));
    query.addBindValue(i);
    query.addBindValue(
        QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
    ok = query.exec();
  }

  QSqlQuery end(db);
  end.exec(ok ? QStringLiteral("COMMIT") : QStringLiteral("ROLLBACK"));
  return ok;
}
