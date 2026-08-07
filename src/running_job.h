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

  // Kills rclone. The final status becomes Stopped rather than Error, so a
  // job the user cancelled does not read as one that failed.
  void stop();

  JobKind kind() const { return mKind; }
  QString taskId() const { return mTaskId; }
  QString requestId() const { return mRequestId; }
  QString transferMode() const { return mTransferMode; }
  const JobDescription &description() const { return mDescription; }
  QDateTime startedAt() const { return mStartedAt; }

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

private:
  void handleOutput();
  void handleFinished(int exitCode);

  const JobKind mKind;
  const QStringList mArgs; // includes the remote-control flags
  const JobDescription mDescription;
  const QString mTaskId;
  const QString mTransferMode;
  const QString mRequestId;
  const QDateTime mStartedAt = QDateTime::currentDateTime();

  QProcess *mProcess = nullptr;
  RcClient *mRc = nullptr;
  QString mRcUser;
  QString mRcPass;
  JobLogWriter mLog;

  JobState mState = JobState::Running;
  bool mStopRequested = false;
  JobStats mStats;
};
