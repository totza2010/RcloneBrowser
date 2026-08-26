#pragma once

// Orchestration (L1): the scripts the user asks to be run at certain moments.
//
// These used to be started by MainWindow, from four places that each decided
// for themselves whether the switch was on and the path was set. That made
// "run my backup verification when the queue empties" a feature of having a
// window open: with --run-task, or later with the daemon, the moment came
// and went and nothing happened. Nothing said so either.
//
// See docs/LAYER-SPLIT.md block 1.

#include <QObject>
#include <QString>

class ScriptRunner : public QObject {
  Q_OBJECT

public:
  // Why a script is being run. The reason is in the log line, because "a
  // script ran" is not useful on its own -- three different settings can
  // produce it and the interesting question is always which.
  // The two finished reasons share one setting for what to run; which of
  // them fires is the user's choice in Preferences. They are kept apart so
  // that the log line says which rule was in force, rather than leaving
  // "why did this run three times" to be worked out from the settings.
  enum class Reason {
    QueueEmpty,
    TransferStarted,
    TransferFinished,     // after every transfer
    LastTransferFinished, // only once nothing is transferring
  };

  static ScriptRunner &instance();

  // Starts listening to the queue and the job registry. Call once, after
  // both exist. Until this is called nothing runs, which is what keeps the
  // window and this from both starting the same script -- the trap the queue
  // move ran into. See JobQueue::setDrivesItself().
  void install();

  // Runs the script configured for this moment, if there is one and it is
  // switched on. Returns whether anything was started. Safe to call when
  // nothing is configured; that is the usual case.
  //
  // waitForIt is for a caller that is about to end: a headless --run-task
  // returns as soon as rclone does, and a script started and not waited for
  // is a script killed a moment later by the process exiting. A window must
  // never pass true -- it would freeze until the script finished.
  bool run(Reason reason, bool waitForIt = false);

  // What is configured for this moment, empty when nothing should run.
  static QString scriptFor(Reason reason);

  static QString reasonName(Reason reason);

signals:
  // A script was started, and how it ended. Nothing needs these yet; they
  // exist so that the API can report a script failing rather than the user
  // finding out from the log.
  void started(Reason reason, const QString &command);
  void ended(Reason reason, int exitCode);

private:
  explicit ScriptRunner(QObject *parent = nullptr);

  bool mInstalled = false;
};
