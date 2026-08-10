#pragma once

// Orchestration (L1): one rclone process that is running right now.
//
// This used to be the job card. A JobWidget owned the QProcess, the remote
// control client and the log writer, which meant a running job *was* a
// widget: nothing without a window could see what was running, and closing
// the window would have taken the transfer with it.
//
// The card is now a view of this. See docs/API.md S2.

#include "job_log.h"
#include "job_stats.h"
#include "rc_client.h"

#include <QDateTime>
#include <QObject>
#include <QProcess>
#include <QStringList>

// What kind of thing rclone was asked to do. Mount and stream still run
// through their own widgets (S10) and are here so the vocabulary is settled
// before they move.
enum class JobKind { Transfer, Mount, Stream };

enum class JobState {
  Running,
  Finished, // rclone exited 0
  Error,    // rclone exited non-zero
  Stopped,  // we killed it
};

// "transfer" | "mount" | "stream". Stored with the run history and reported
// by the API, so the words are settled in one place rather than at each site
// that has to write them down.
QString JobKindToString(JobKind kind);

// What the job is, in words, for whoever is showing it. Derived from the task
// when the job is created; kept here so an API can report it without asking
// the window.
struct JobDescription {
  QString info;   // "Task: "photos", Copy from C:/photos"
  QString source;
  QString dest;
};

class RunningJob : public QObject {
  Q_OBJECT

public:
  RunningJob(JobKind kind, const QStringList &args,
             const JobDescription &description, const QString &taskId,
             const QString &transferMode, const QString &requestId,
             QObject *parent = nullptr);
  ~RunningJob() override;

  // Starts rclone with its remote control on a port it picks itself, so
  // progress can be read from core/stats instead of scraped from the output.
  // Returns false only when the process could not be started at all.
  bool start();

  // Ends the job. A transfer is killed; a mount is unmounted, which is a
  // different thing entirely -- see stopFailed().
  //
  // The final status becomes Stopped rather than Error, so a job the user
  // cancelled does not read as one that failed.
  void stop();

  // The script to run once the mount is up, empty for none. Set before
  // start(). It is handed the rclone path, the remote-control port and
  // login, and the mount point -- which is why it lives with the job rather
  // than with the card: a headless mount needs it too.
  void setMountScript(const QString &script) { mMountScript = script; }

  // The remote-control port rclone settled on, read from its output. Zero
  // until it is announced, and zero forever when the task set no port.
  QString rcPort() const { return mRcPort; }

  JobKind kind() const { return mKind; }
  QString taskId() const { return mTaskId; }
  QString requestId() const { return mRequestId; }
  QString transferMode() const { return mTransferMode; }
  const JobDescription &description() const { return mDescription; }
  QDateTime startedAt() const { return mStartedAt; }

  // Invalid until the job ends. Kept rather than computed on the spot so the
  // history records when rclone actually stopped, not when someone asked.
  QDateTime finishedAt() const { return mFinishedAt; }
  int exitCode() const { return mExitCode; }

  // The log file this job is writing, empty when logging is off or when
  // rclone has not printed anything yet. Only the path is stored in the
  // history; the lines stay in the file. See docs/PLAN.md 6.8.
  QString logPath() const { return mLog.filePath(); }
  qint64 logBytes() const { return mLog.bytesWritten(); }

  JobState state() const { return mState; }
  bool isRunning() const { return mState == JobState::Running; }

  // "finished" | "error" | "stopped", the words the rest of the application
  // and the saved history already use. Empty while still running.
  QString finalStatus() const;

  // The last reading from core/stats. Not valid until the first one lands.
  const JobStats &stats() const { return mStats; }

  // The command as it can safely be shown: redacted, and quoted the way the
  // user would have to type it.
  QStringList displayCommand() const;

signals:
  void statsUpdated(const JobStats &stats);

  // The remote control stopped answering. The transfer itself is unaffected;
  // only the figures stop.
  void progressUnavailable();

  // One line of rclone's output, already redacted.
  void outputLine(const QString &line);

  void finished(JobState state);

  // The remote control is up. For a mount this is when the script can run
  // and when unmounting becomes possible at all.
  void rcPortDiscovered(const QString &port);

  // Unmounting failed and the mount is still there -- usually because a file
  // on it is open in another program. A transfer has no equivalent: killing
  // it always works.
  void stopFailed(const QString &reason);

  void scriptOutputLine(const QString &line);

  // How the mount script ended. Kept as an exit code and an error string
  // rather than as text for a label -- what to write on screen is the view's
  // decision, not the job's.
  void scriptFinished(int exitCode);
  void scriptFailed(const QString &error);

private:
  void handleOutput();
  void handleFinished(int exitCode);
  void startMountScript();
  void unmount();

  const JobKind mKind;
  const QStringList mArgs; // includes the remote-control flags
  const JobDescription mDescription;
  const QString mTaskId;
  const QString mTransferMode;
  const QString mRequestId;
  const QDateTime mStartedAt = QDateTime::currentDateTime();
  QDateTime mFinishedAt;
  int mExitCode = -1;

  QProcess *mProcess = nullptr;
  RcClient *mRc = nullptr;
  QString mRcUser;
  QString mRcPass;
  JobLogWriter mLog;

  QString mMountScript;
  QProcess *mScriptProcess = nullptr;
  QString mRcPort;

  JobState mState = JobState::Running;
  bool mStopRequested = false;
  JobStats mStats;
};
