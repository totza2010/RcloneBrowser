#pragma once
// Core (L0/L1): must not depend on QtWidgets -- see docs/ARCHITECTURE.md.
// The QListWidgetItem adapter lives in job_options_item.h.
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <qexception.h>
#include <quuid.h>

class JobOptions {
public:
  explicit JobOptions(bool isDownload);
  JobOptions();

  ~JobOptions();

  enum Operation { UnknownOp, Copy, Move, Sync, Mount, Check, CryptCheck };
  enum JobType { UnknownJobType, Upload, Download };

  /*
   * The following enums have their int values synchronized with the
   * list indexes on the gui.  Changes needed to be synchronized.
   */
  enum MountCacheLevel { Off, Minimal, Writes, Full, UnknownCacheLevel };
  enum SyncTiming { During, After, Before, UnknownTiming };
  enum CompareOption {
    SizeAndModTime,
    Checksum,
    IgnoreSize,
    SizeOnly,
    ChecksumIgnoreSize
  };

  QString description;

  JobType jobType;
  Operation operation;
  bool dryRun; // not persisted
  bool sync;
  SyncTiming syncTiming;
  bool skipNewer;
  bool skipExisting;
  bool compare;
  CompareOption compareOption;
  bool verbose;
  bool sameFilesystem;
  bool dontUpdateModified;
  QString transfers;
  QString checkers;
  QString bandwidth;
  QString minSize;
  QString minAge;
  QString maxAge;
  int maxDepth;
  QString connectTimeout;
  QString idleTimeout;
  QString retries;
  QString lowLevelRetries;
  bool deleteExcluded;
  QString excluded;
  QString extra;
  QString source;
  QString dest;
  bool isFolder;
  QUuid uniqueId;
  bool DriveSharedWithMe;
  QString remoteMode;
  QString remoteType;

  // added for mount task
  bool mountReadOnly;
  MountCacheLevel mountCacheLevel;
  QString mountVolume;
  bool mountAutoStart;
  QString mountRcPort;
  QString mountScript;
  bool mountWinDriveMode;

  // added options for multi items operations
  QString included;
  bool noTraverse;
  bool createEmptySrcDirs;

  // additional options for multi items operations
  QString filtered;
  bool deleteEmptySrcDirs;

  void setJobType(bool isDownload) {
    jobType = (isDownload) ? Download : Upload;
  }

  QString myName() const {
    return "JobOptions"; // this->staticQtMetaObject.myName();
  }
  QStringList getOptions() const;

  // The argument list for a mount, built the same way getOptions() builds one
  // for a transfer.
  //
  // This lived in MainWindow::runItem, where it was the last half of VIO-1:
  // thirty lines of rclone arguments assembled in the window, which meant a
  // mount could not be started by anything else. See docs/API.md S10.
  //
  // The remote-control login is not in here on purpose. It goes through the
  // environment, so it never reaches the argument list and therefore never
  // reaches a saved task or a log (docs/ARCHITECTURE.md section 5).
  QStringList getMountOptions() const;

  bool operator==(const JobOptions &other) const {
    return uniqueId == other.uniqueId;
  }

  /*
   * This allows the de-serialization method to accomodate changes
   * to the class structure, especially (most easily) added members.
   *
   * Increment the value each time a change is made, emit the new field(s)
   * in the operator<< function, and in operator>> add conditional logic
   * based on the version for reading in the new field(s)
   */
  static const qint32 classVersion;
};

class SerializationException : public QException {
public:
  QString Message;
  explicit SerializationException(QString msg);
};
