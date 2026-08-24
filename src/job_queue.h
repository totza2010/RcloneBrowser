#pragma once

// Orchestration (L1): the queue of tasks waiting to run.
//
// The queue used to *be* the list widget: its order was the order of the
// rows, "is a queued task running" was the colour of row 0, and the rules for
// when the next one may start were spread across five handlers in
// MainWindow. Nothing outside the window could see the queue, add to it, or
// keep it moving -- which is why a queue could only run while a window was
// open. See docs/API.md S3.
//
// What is stored lives in the database (S13); this is the part that decides
// what happens next.

#include "config_store.h"

#include <QObject>
#include <QString>

class JobOptions;

class JobQueue : public QObject {
  Q_OBJECT

public:
  static JobQueue &instance();

  const QList<QueueEntry> &entries() const { return mEntries; }
  int count() const { return mEntries.size(); }
  bool isEmpty() const { return mEntries.isEmpty(); }

  // Adds a task to the back and returns the request id given to it. The id
  // is what tells two runs of the same task apart -- one from the queue and
  // one the user started by hand.
  // requestId lets a caller that already has one -- a schedule, which has to
  // recognise its own run when the job ends -- keep it. Left empty, the queue
  // mints one.
  QString enqueue(const QString &taskId, bool dryRun = false,
                  const QString &requestId = QString());

  void remove(const QString &requestId);

  // Moving the entry that is running would mean the running job was no longer
  // at the head, which every rule here depends on. Refused rather than
  // silently allowed, the same as the buttons have always been greyed out.
  bool move(int from, int to);

  void clear();

  // Whether the queue is started. A paused queue keeps its entries and
  // starts nothing. Remembered across restarts, as it always has been.
  bool isRunning() const { return mRunning; }
  void start();
  void pause();

  // Whether the entry at the head has a job going right now.
  bool taskIsRunning() const { return !mRunningRequestId.isEmpty(); }
  QString runningRequestId() const { return mRunningRequestId; }

  // Starts the head entry if every condition for starting it holds: the queue
  // is running, nothing else is transferring, and that task is not already
  // running because somebody started it by hand. Returns true if it started
  // something.
  //
  // Safe to call whenever anything changes; it does nothing when it should do
  // nothing, which is what makes the rule one rule instead of five.
  bool advance();

  // Tells the queue how a job ended. When it is the queue's own job, the head
  // is dropped and the next one is offered its turn.
  void jobFinished(const QString &requestId);

  // Reads the stored queue. Entries whose task no longer exists are dropped,
  // exactly as the file-reading code used to skip them.
  // Drops entries whose task no longer exists. Returns how many went.
  //
  // A task can be deleted while the queue is paused, and until now the entry
  // for it sat there until something tried to start it -- so the tab counted
  // a run that could never happen.
  int dropMissingTasks();

  void load();
  bool save();

  // Whether the queue reacts to a job ending by starting the next one.
  //
  // Off until the window stops doing that itself: while both are on they
  // race to start the same entry, and the one that loses starts a second
  // copy of a transfer -- two processes writing the same destination. See
  // docs/QUEUE-MOVE.md step 0.
  // Taking the wheel also means looking at the road: switching this on gives
  // the head entry its turn straight away, in case it has been waiting.
  void setDrivesItself(bool on);
  bool drivesItself() const { return mDrivesItself; }

signals:
  // The list or the running state changed and anything showing it should look
  // again. One signal rather than a set of finer ones: the queue is short and
  // a view that rebuilds is a view that cannot drift.
  void changed();

  // The last entry finished and the queue is now empty. This is what the
  // "run a script when the queue empties" setting hangs off.
  void emptied();

  void taskStarted(const QString &taskId, const QString &requestId);

private:
  explicit JobQueue(QObject *parent = nullptr);

  JobOptions *taskFor(const QueueEntry &entry) const;

  // Works out which entry is running by asking the registry, rather than
  // only remembering the ones this started. While the window is still the
  // one starting them, that is the only way to know -- and it stays true
  // afterwards, including for a run somebody started by hand.
  void syncRunningFromRegistry();

  QList<QueueEntry> mEntries;
  bool mRunning = false;
  QString mRunningRequestId;
  bool mDrivesItself = false;
};
