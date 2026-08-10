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
  QString enqueue(const QString &taskId, bool dryRun = false);

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
  void load();
  bool save();

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

  QList<QueueEntry> mEntries;
  bool mRunning = false;
  QString mRunningRequestId;
};
