#include "debug_log.h"
#include "list_of_job_options.h"
#include "database.h"
#include <QDataStream>
#include <QDateTime>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <qdir.h>
#include <qlogging.h>
#include <qstandardpaths.h>
#include <utils.h>

static QDataStream &operator>>(QDataStream &dataStream, JobOptions &jo);
static QDataStream &operator<<(QDataStream &dataStream, JobOptions &jo);
static QDataStream &operator>>(QDataStream &in, JobOptions::Operation &e);
static QDataStream &operator>>(QDataStream &in, JobOptions::SyncTiming &e);
static QDataStream &operator>>(QDataStream &in, JobOptions::CompareOption &e);
static QDataStream &operator>>(QDataStream &in, JobOptions::JobType &e);
static QDataStream &operator>>(QDataStream &in, JobOptions::MountCacheLevel &e);

ListOfJobOptions *ListOfJobOptions::SavedJobOptions = nullptr;
const QString ListOfJobOptions::persistenceFileName = "tasks.bin";

ListOfJobOptions::ListOfJobOptions() {}

ListOfJobOptions *ListOfJobOptions::getInstance() {
  if (SavedJobOptions == nullptr) {
    SavedJobOptions = new ListOfJobOptions();
    RestoreFromUserData(*SavedJobOptions);
  }
  return SavedJobOptions;
}

JobOptions *ListOfJobOptions::find(const QUuid &id) const {
  if (id.isNull()) {
    return nullptr;
  }
  for (JobOptions *task : tasks) {
    if (task->uniqueId == id) {
      return task;
    }
  }
  return nullptr;
}

JobOptions *ListOfJobOptions::find(const QString &id) const {
  // QUuid::fromString returns a null uuid for anything it cannot read, and a
  // null uuid never matches a real task, so a malformed id simply finds
  // nothing rather than needing its own error path.
  return find(QUuid::fromString(id));
}

JobOptions *ListOfJobOptions::findByName(const QString &description) const {
  if (description.isEmpty()) {
    return nullptr;
  }
  for (JobOptions *task : tasks) {
    if (task->description == description) {
      return task;
    }
  }
  return nullptr;
}

int ListOfJobOptions::countByName(const QString &description) const {
  if (description.isEmpty()) {
    return 0;
  }
  int count = 0;
  for (JobOptions *task : tasks) {
    if (task->description == description) {
      ++count;
    }
  }
  return count;
}

bool ListOfJobOptions::Persist(JobOptions *jo) {
  bool isNew = !this->tasks.contains(jo);
  if (isNew)
    this->tasks.append(jo);
  else {
    //    int ix = tasks.indexOf(jo);
    //    JobOptions *old = tasks[ix];
    //    qDebug() << QString("old [%1] New [%2]")
    //                    .arg(old->description)
    //                    .arg(jo->description);
  }
  PersistToUserData();
  return isNew;
}

bool ListOfJobOptions::Forget(JobOptions *jo) {
  bool isKnown = this->tasks.contains(jo);
  if (!isKnown)
    return false;
  int ix = tasks.indexOf(jo);
  tasks.removeAt(ix);
  //  qDebug() << QString("removed [%1]").arg(jo->description);
  PersistToUserData();
  return isKnown;
}

QFile *ListOfJobOptions::GetPersistenceFile(QIODevice::OpenModeFlag mode) {

  QDir outputDir;

  if (IsPortableMode()) {
    // in portable mode tasks' file will be saved in the same folder as
    // excecutable
#ifdef Q_OS_MACOS
    // on macOS excecutable file is located in
    // ./rclone-browser.app/Contents/MasOS/
    // to get actual bundle folder we have
    // to traverse three levels up
    outputDir = QDir(qApp->applicationDirPath() + "/../../..");
#else
#ifdef Q_OS_WIN
    // not macOS
    outputDir = QDir(qApp->applicationDirPath());
#else
    QString xdg_config_home = qgetenv("XDG_CONFIG_HOME");
    outputDir = QDir(xdg_config_home + "/rclone-browser");
#endif
#endif

  } else {

    // get data location folder from Qt  - OS dependend
    outputDir =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
  }

  if (!outputDir.exists()) {
    outputDir.mkpath(".");
  }
  QString filePath = outputDir.absoluteFilePath(persistenceFileName);
  
  QFile *file = new QFile(filePath);

  if (!file->open(mode)) {
    //    qDebug() << QString("Could not open ") << file->fileName();
    delete file;
    file = nullptr;
  }
  return file;
}

bool ListOfJobOptions::RestoreFromUserData(ListOfJobOptions &dataIn) {
  QSqlDatabase db = Database::connection();
  if (db.isOpen()) {
    return RestoreFromDatabase(dataIn);
  }

  // No database driver, no tasks lost: the old file is still read and still
  // written. An installation missing the SQLite plugin keeps working exactly
  // as it did before, minus the history.
  return ReadLegacyFile(dataIn.tasks);
}

bool ListOfJobOptions::RestoreFromDatabase(ListOfJobOptions &dataIn) {
  QSqlDatabase db = Database::connection();
  if (!db.isOpen()) {
    return false;
  }

  // tasks.bin is imported once, then renamed rather than deleted: if anything
  // about this turns out to be wrong, the original is still there to go back
  // to (docs/PLAN.md 6.8).
  QSqlQuery count(db);
  if (count.exec(QStringLiteral("SELECT COUNT(*) FROM task")) && count.next() &&
      count.value(0).toInt() == 0) {
    QList<JobOptions *> legacy;
    if (ReadLegacyFile(legacy) && !legacy.isEmpty()) {
      dataIn.tasks = legacy;
      if (dataIn.WriteToDatabase()) {
        QFile *file = GetPersistenceFile(QIODevice::ReadOnly);
        if (file != nullptr) {
          const QString name = file->fileName();
          file->close();
          delete file;
          QFile::rename(name, name + QStringLiteral(".migrated"));
        }
        return true;
      }
      // Could not write: leave the file alone and keep what was read, so the
      // session works and the next start tries again.
      return true;
    }
    qDeleteAll(legacy);
  }

  QSqlQuery query(db);
  qCDebug(rbDb) << "reading tasks from the database";
  if (!query.exec(QStringLiteral(
          "SELECT options FROM task ORDER BY position, name"))) {
    return false;
  }
  while (query.next()) {
    const QJsonDocument doc =
        QJsonDocument::fromJson(query.value(0).toString().toUtf8());
    if (!doc.isObject()) {
      continue; // a row we cannot read is skipped, not fatal
    }
    auto *jo = new JobOptions();
    jo->readJson(doc.object());
    dataIn.tasks.append(jo);
  }
  return true;
}

bool ListOfJobOptions::WriteToDatabase() {
  QSqlDatabase db = Database::connection();
  if (!db.isOpen()) {
    return false;
  }

  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  QSqlQuery begin(db);
  if (!begin.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
    return false;
  }

  bool ok = true;
  QStringList kept;

  for (int i = 0; i < tasks.size() && ok; ++i) {
    JobOptions *jo = tasks[i];
    const QString id = jo->uniqueId.toString();
    kept.append(id);

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO task (id, name, operation, source, dest, options, "
        "position, created_at, updated_at) VALUES (?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET name=excluded.name, "
        "operation=excluded.operation, source=excluded.source, "
        "dest=excluded.dest, options=excluded.options, "
        "position=excluded.position, updated_at=excluded.updated_at"));
    query.addBindValue(id);
    query.addBindValue(jo->description);
    query.addBindValue(static_cast<int>(jo->operation));
    query.addBindValue(jo->source);
    query.addBindValue(jo->dest);
    query.addBindValue(QString::fromUtf8(
        QJsonDocument(jo->toJson()).toJson(QJsonDocument::Compact)));
    query.addBindValue(i);
    query.addBindValue(now); // created_at is only used by the insert half
    query.addBindValue(now);
    ok = query.exec();
  }

  // Deleting what is no longer in the list is how Forget() takes effect. Done
  // by naming what to keep rather than by emptying the table first, so
  // created_at survives and a failure part way through changes nothing.
  if (ok) {
    QSqlQuery del(db);
    QStringList marks;
    for (int i = 0; i < kept.size(); ++i) {
      marks.append(QStringLiteral("?"));
    }
    del.prepare(kept.isEmpty()
                    ? QStringLiteral("DELETE FROM task")
                    : QStringLiteral("DELETE FROM task WHERE id NOT IN (%1)")
                          .arg(marks.join(QLatin1Char(','))));
    for (const QString &id : kept) {
      del.addBindValue(id);
    }
    ok = del.exec();
  }

  QSqlQuery end(db);
  end.exec(ok ? QStringLiteral("COMMIT") : QStringLiteral("ROLLBACK"));
  return ok;
}

// Reads what tasks.bin holds into a list of its own, so a failed import
// cannot leave half the tasks loaded over the ones already there.
bool ListOfJobOptions::ReadLegacyFile(QList<JobOptions *> &into) {
  QFile *file = GetPersistenceFile(QIODevice::ReadOnly);
  if (file == nullptr) {
    file = GetPersistenceFile(QIODevice::WriteOnly);
  }

  if (file == nullptr) {
      return false;
  }

  QDataStream instream(file);
  instream.setVersion(QDataStream::Qt_5_2);

  while (!instream.atEnd()) {
    try {
      JobOptions *jo = new JobOptions();
      instream >> *jo;
      into.append(jo);
    } catch (SerializationException &ex) {
      //      qDebug() << QString("failed to restore tasks: ") << ex.Message;
      file->close();
      delete file;
      return false;
    }
  }

  file->close();
  delete file;

  return true;
}

bool ListOfJobOptions::PersistToUserData() {
  if (Database::connection().isOpen()) {
    const bool ok = WriteToDatabase();
    emit tasksListUpdated();
    return ok;
  }
  return WriteLegacyFile();
}

bool ListOfJobOptions::WriteLegacyFile() {
  QFile *file = GetPersistenceFile(QIODevice::ReadOnly);
  if (file == nullptr) {
      file = GetPersistenceFile(QIODevice::WriteOnly);
  }

  if (file == nullptr) {
      return false;
  }

  QSaveFile fileToSave(file->fileName());

  // note this mode implies Truncate also
  if (!fileToSave.open(QIODevice::WriteOnly)) {
    file->close();
    delete file;
    return false;
  }

  QDataStream outstream(&fileToSave);

  outstream.setVersion(QDataStream::Qt_5_2);
  for (JobOptions *it : tasks) {
    outstream << *it;
  }

  file->close();
  delete file;

  emit tasksListUpdated();

  return fileToSave.commit();
}

QDataStream &operator<<(QDataStream &stream, JobOptions &jo) {
  stream << jo.myName() << JobOptions::classVersion << jo.description
         << jo.jobType << jo.operation << /* jo.dryRun <<*/ jo.sync
         << jo.syncTiming << jo.skipNewer << jo.skipExisting << jo.compare
         << jo.compareOption << jo.verbose << jo.sameFilesystem
         << jo.dontUpdateModified << jo.transfers << jo.checkers << jo.bandwidth
         << jo.minSize << jo.minAge << jo.maxAge << jo.maxDepth
         << jo.connectTimeout << jo.idleTimeout << jo.retries
         << jo.lowLevelRetries << jo.deleteExcluded << jo.excluded << jo.extra
         << jo.DriveSharedWithMe << jo.source << jo.dest << jo.isFolder
         << jo.uniqueId << jo.remoteMode << jo.remoteType << jo.mountReadOnly
         << jo.mountCacheLevel << jo.mountVolume << jo.mountAutoStart
         << jo.mountRcPort << jo.mountScript << jo.mountWinDriveMode
         << jo.included << jo.noTraverse << jo.createEmptySrcDirs << jo.filtered
         << jo.deleteEmptySrcDirs;

  return stream;
}

QDataStream &operator>>(QDataStream &stream, JobOptions &jo) {
  QString actualName;
  qint32 actualVersion;

  stream >> actualName;
  if (QString::compare(actualName, jo.myName()) != 0)
    throw SerializationException("incorrect class");

  stream >> actualVersion;
  if (actualVersion > JobOptions::classVersion)
    throw SerializationException("stored version is newer");

  stream >> jo.description >> jo.jobType >> jo.operation >>
      /* jo.dryRun >> */ jo.sync >> jo.syncTiming >> jo.skipNewer >>
      jo.skipExisting >> jo.compare >> jo.compareOption >> jo.verbose >>
      jo.sameFilesystem >> jo.dontUpdateModified >> jo.transfers >>
      jo.checkers >> jo.bandwidth >> jo.minSize >> jo.minAge >> jo.maxAge >>
      jo.maxDepth >> jo.connectTimeout >> jo.idleTimeout >> jo.retries >>
      jo.lowLevelRetries >> jo.deleteExcluded >> jo.excluded >> jo.extra >>
      jo.DriveSharedWithMe >> jo.source >> jo.dest;

  // as fields are added in later revisions, check actualVersion here and
  // conditionally extract any new fields iff they are expected based on the
  // stream value
  if (actualVersion >= 2) {
    stream >> jo.isFolder;
  }

  if (actualVersion >= 3) {
    stream >> jo.uniqueId;
  }
  if (actualVersion >= 4) {
    stream >> jo.remoteMode;
    stream >> jo.remoteType;
  }

  if (actualVersion >= 5) {
    stream >> jo.mountReadOnly;
    stream >> jo.mountCacheLevel;
    stream >> jo.mountVolume;
    stream >> jo.mountAutoStart;
    stream >> jo.mountRcPort;
    stream >> jo.mountScript;
    stream >> jo.mountWinDriveMode;
  }

  if (actualVersion >= 6) {
    stream >> jo.included;
  }

  if (actualVersion >= 7) {
    stream >> jo.noTraverse;
    stream >> jo.createEmptySrcDirs;
  }

  if (actualVersion >= 8) {
    stream >> jo.filtered;
    stream >> jo.deleteEmptySrcDirs;
  }

  return stream;
}

QDataStream &operator>>(QDataStream &in, JobOptions::Operation &e) {
  in >> (quint32 &)e;
  return in;
}

QDataStream &operator>>(QDataStream &in, JobOptions::SyncTiming &e) {
  in >> (quint32 &)e;
  return in;
}

QDataStream &operator>>(QDataStream &in, JobOptions::CompareOption &e) {
  in >> (quint32 &)e;
  return in;
}

QDataStream &operator>>(QDataStream &in, JobOptions::JobType &e) {
  in >> (quint32 &)e;
  return in;
}

QDataStream &operator>>(QDataStream &in, JobOptions::MountCacheLevel &e) {
  in >> (quint32 &)e;
  return in;
}
