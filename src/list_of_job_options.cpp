#include "list_of_job_options.h"
#include <QDataStream>
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
        QDir(QStandardPaths::writableLocation(QStandardPaths::DataLocation));
  }

  if (!outputDir.exists()) {
    outputDir.mkpath(".");
  }
  QString filePath = outputDir.absoluteFilePath(persistenceFileName);
  
  if (!QFileInfo::exists(filePath) && mode == QIODevice::ReadOnly) {
    return nullptr;
  }

  QFile *file = new QFile(filePath);

  if (!file->open(mode)) {
    //    qDebug() << QString("Could not open ") << file->fileName();
    delete file;
    file = nullptr;
  }
  return file;
}

bool ListOfJobOptions::RestoreFromUserData(ListOfJobOptions &dataIn) {
  QFile *file = GetPersistenceFile(QFileInfo(persistenceFileName).exists() ? QIODevice::ReadOnly : QIODevice::WriteOnly);
  if (file == nullptr)
    return false;
  QDataStream instream(file);
  instream.setVersion(QDataStream::Qt_5_2);

  while (!instream.atEnd()) {
    try {
      JobOptions *jo = new JobOptions();
      instream >> *jo;
      dataIn.tasks.append(jo);
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
  QFile *file = GetPersistenceFile(QFileInfo(persistenceFileName).exists() ? QIODevice::ReadOnly : QIODevice::WriteOnly);

  if (file == nullptr)
    return false;

  QFileInfo fileToSaveInfo(*file);

  QSaveFile fileToSave(fileToSaveInfo.absoluteFilePath());

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
