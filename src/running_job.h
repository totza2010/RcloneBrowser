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
#include <QMap>
#include <QObject>
#include <QProcess>
#include <QStringList>

// What kind of thing rclone was asked to do. Mount and stream still run
// through their own widgets (S10) and are here so the vocabulary is settled
// before they move.
// Check, Dedupe and Export are the maintenance commands the remote tab
// offers. They ran as a bare QProcess handed to a ProgressDialog, so they
// left no history row, wrote no log file, could not be stopped from anywhere
// else, and could not be started by anything but a click. See
// docs/LAYER-SPLIT.md block 5.
enum class JobKind {
  Transfer,
  Mount,
  Stream,
  // The tools in the remote tab. Each runs one rclone command and shows its
  // output in the window that asked for it. They ran as a bare QProcess
  // handed to a ProgressDialog, so they left no history row, wrote no log
  // file, could not be stopped from anywhere else, and could not be started
  // by anything but a click.
  //
  // Kept apart rather than lumped into one "tool" kind because the history
  // is read by a person: "cleanup" and "size" are not the same news.
  Check,
  Dedupe,
  Export,
  Tree,
  Size,
  Link,
  About,
  Cleanup,
  // What is done to a remote's files from its tab, one kind each for the
  // same reason as the tools above: the history is read by a person, and
  // "made a folder" is not the same news as "deleted one".
  Mkdir,
  Rename,
  Delete,
  // A remote, or a folder of one, offered to other programs over HTTP,
  // WebDAV, FTP or SFTP for as long as the job runs.
  Serve,
};

enum class JobState {
  Running,
  Finished, // rclone exited 0
  Error,    // rclone exited non-zero
  Stopped,  // we killed it
};

// "transfer" | "mount" | "stream" | "check" | "dedupe" | "export". Stored
// with the run history and reported
// by the API, so the words are settled in one place rather than at each site
// that has to write them down.
QString JobKindToString(JobKind kind);

// A question: something only read -- how big, how full, what is in it -- and
// answered at once to whoever asked. It leaves no history row and no log
// file: the page asks how full a remote is every time it is opened, and a
// history of that is a history nobody can find their transfers in.
// Everything that does something (check, link, dedupe, cleanup, a transfer)
// is kept.
bool JobKindIsQuestion(JobKind kind);

// What "rclone size" is given to count a transfer's source as the transfer
// will see it: the source, and the job's filters (and nothing else -- a
// flag of copy's own would be refused). Empty for what is not a copy, sync
// or move of a source to a destination.
QStringList SourceCountArgs(const QStringList &jobArgs);

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
  QString mountScript() const { return mMountScript; }

  // What the mount script has done, for whoever shows the job: the window's
  // card draws it from the signals below, the page reads it from here. The
  // lines are the redacted ones (the script is handed the remote-control
  // login), the last kScriptLinesKept of them.
  struct ScriptReport {
    bool started = false;
    bool running = false;
    int exitCode = -1;  // -1 until it has ended
    QString error;      // why it could not run, when it could not
    QStringList lines;
  };
  static constexpr int kScriptLinesKept = 200;
  const ScriptReport &scriptReport() const { return mScriptReport; }

  // Where a stream is being served: the port, and the path under it that
  // has to be asked for. Kept with the job so that whoever shows the job can
  // give the address again -- it cannot be worked out from the command,
  // which is shown with the secret part of the path taken out.
  void setStreamAddress(quint16 port, const QString &path) {
    mStreamPort = port;
    mStreamPath = path;
  }
  quint16 streamPort() const { return mStreamPort; }
  QString streamPath() const { return mStreamPath; }

  // Where a serve is listening, kept with the job for whoever shows it.
  // Never the password: that went to rclone and nowhere else.
  struct ServeAddress {
    QString protocol; // "http" | "webdav" | "ftp" | "sftp"
    quint16 port = 0;
    bool everyone = false; // every address of this machine, not just its own
    bool readOnly = true;
    QString user; // empty when nobody has to log in
  };
  void setServeAddress(const ServeAddress &address) { mServe = address; }
  const ServeAddress &serveAddress() const { return mServe; }

  // Added to rclone's environment, for what must not be among its
  // arguments: those can be read by anyone on the machine while it runs,
  // and are written to the log and the history. Set before start().
  void setEnvironment(const QMap<QString, QString> &extra) {
    mEnvironment = extra;
  }

  // Who asked for this run when it came from the queue (QueueEntry::askedBy).
  // Set before start(), so the history row has it from the beginning.
  void setAskedBy(const QString &who) { mAskedBy = who; }
  QString askedBy() const { return mAskedBy; }

  // How fast it may go, changed while it runs. Only a transfer can be
  // asked, and only once its remote control is up. `done` gets whether
  // rclone took it and the rate rclone says it now has.
  void setBandwidth(const QString &rate,
                    std::function<void(bool ok, const QString &now)> done);
  // The rate last set this way, empty when it never was. What the job was
  // started with is in its command.
  QString bandwidth() const { return mBandwidth; }

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

  // Whether rclone died rather than exited, and whether it ever started at
  // all. Both are kept because a caller that reports an exit code has to
  // tell these apart from rclone's own: "--run-task" documents 69 for an
  // rclone that would not start and 70 for one that did not exit normally.
  // Whether rclone was told to rehearse. Read from the arguments it was
  // actually given, not from how the job was started: the Dry run button is
  // one way to ask for it and a task's own flags are another, and only the
  // command line knows about both.
  bool isDryRun() const;

  bool crashed() const { return mCrashed; }
  bool everStarted() const { return mEverStarted; }

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
  // rclone took a new speed limit, from whichever view asked for it.
  void bandwidthChanged(const QString &rate);

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
  bool mCrashed = false;
  bool mEverStarted = false;

  JobLogWriter mLog;

  QString mMountScript;
  ScriptReport mScriptReport;
  QString mBandwidth;
  quint16 mStreamPort = 0;
  QString mStreamPath;
  ServeAddress mServe;
  QMap<QString, QString> mEnvironment;
  QString mAskedBy;
  QProcess *mScriptProcess = nullptr;
  QString mRcPort;

  JobState mState = JobState::Running;
  bool mStopRequested = false;
  JobStats mStats;
  JobStats mTotals; // what rclone stated on its way out
  qint64 mSourceFiles = -1;
  QProcess *mCounter = nullptr; // "rclone size" of the source, while it counts
  void countSource();
  bool mHaveTotals = false;
};
