#pragma once

// Core (L0): writes a job's rclone output to a file alongside the job card.
//
// Until now the output existed only in the card's text box, which is dropped
// when the card is closed and, before that, kept just the last ten thousand
// lines. Anything that went wrong overnight was gone by morning.
//
// The file is opened on the first line rather than up front, so a job that
// prints nothing leaves nothing behind.

#include <QFile>
#include <QString>
#include <QStringList>
#include <QTextStream>

class JobLogWriter {
public:
  JobLogWriter();
  ~JobLogWriter();

  // operation is used in the file name ("copy", "sync", "mount"); anything
  // outside letters, digits, dash and underscore is dropped.
  void begin(const QString &operation, const QString &jobId,
             const QStringList &redactedArgs);

  void appendLine(const QString &line);
  void finish(const QString &status);

  bool isOpen() const { return mFile.isOpen(); }
  QString filePath() const { return mFile.fileName(); }

  // How much has been written so far. Recorded alongside the run in the
  // history, so a listing can say how big a log is without stat-ing every
  // file in the directory.
  qint64 bytesWritten() const { return mBytes; }

  bool reachedSizeLimit() const { return mTruncated; }

  // Where logs are written -- logs/transfers, a folder of its own so that a
  // listing does not mix these with the application's own log. Follows the
  // configuration directory, so portable installations keep theirs alongside
  // the executable.
  static QString logDir();

  // Where they used to be written, kept only so that purging still reaches
  // the ones already on disk.
  static QString legacyLogDir();

  // Whether the user wants logs at all, and how many days to keep.
  static bool isEnabled();
  static int retentionDays();

  // Deletes logs older than retentionDays(). Returns how many went. Called at
  // startup; a job that never finished still leaves a readable file, so age is
  // the only thing worth going on.
  static int purgeOldLogs();

  static QString sanitizeForFileName(const QString &text);

private:
  void openFile();

  QFile mFile;
  QTextStream mStream;
  QString mOperation;
  QString mJobId;
  QStringList mHeaderArgs;
  qint64 mBytes = 0;
  bool mTruncated = false;
  bool mBegun = false;
};
