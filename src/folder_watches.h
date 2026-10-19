#pragma once

// Orchestration (L1): an upload task that runs itself when its folder
// changes. A schedule runs a task at a time; this runs one when there is
// something to send -- a camera's card copied in, a document saved.
//
// Two ways of noticing, because neither is enough alone: the system's own
// notices (QFileSystemWatcher), which are quick but never arrive for a
// network share or, often, a folder mounted into a container; and a look
// over the whole folder every few minutes, which always works. Either only
// starts a wait: the task is queued once the folder has been quiet for a
// while, so a hundred files copied in are one run, not a hundred.
//
// Queued, never started directly, so a watched task never runs twice at once
// and waits its turn behind everything else -- and a paused queue holds it.
// Kept apart from the task (Settings/watches), so the task is unchanged.

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

class QFileSystemWatcher;
class QTimer;

struct FolderWatch {
  QString taskId;
  // How long the folder has to stay unchanged before the task is queued.
  int quietSeconds = 30;

  // Since the program started; not stored.
  QDateTime lastChange; // when a change was last seen
  QDateTime lastQueued; // when it last queued the task
  QString lastRequestId;
};

class FolderWatches : public QObject {
  Q_OBJECT

public:
  static FolderWatches &instance();

  // Reads the list and starts watching. Call once, after the tasks and the
  // queue exist.
  void install();

  QList<FolderWatch> list() const;
  const FolderWatch *find(const QString &taskId) const;

  // Watches a task's folder, or changes how long it waits. Refused for a
  // task that does not send a folder of this machine.
  bool watch(const QString &taskId, int quietSeconds, QString *why);
  bool unwatch(const QString &taskId);

  // Why this task cannot be watched, or empty.
  static QString WhyNot(const QString &taskId);

  // What a folder is now, as one number: every file's path, size and time.
  // Two looks give the same number when nothing has changed.
  static quint64 Fingerprint(const QString &folder, int *files = nullptr);

  // How often the whole folder is looked over, in seconds:
  // RB_WATCH_POLL_SECONDS, five minutes unless said.
  static int PollSeconds();

  // Looks now, as the poll does. For a test, and for whoever wants it.
  void checkAll();

signals:
  void changed();
  // A watched task was put in the queue because its folder changed.
  void queued(const QString &taskId, const QString &requestId);

private:
  FolderWatches() = default;
  void load();
  void save();
  void rewatch();
  void noticed(const QString &path);
  void check(const QString &taskId);
  QString folderOf(const QString &taskId) const;

  QList<FolderWatch> mWatches;
  // The folder as it was when its task was last queued (or when the watch
  // began): what "changed" is measured against.
  QHash<QString, quint64> mSent;
  // The folder at the last look: a look that finds it the same as this has
  // found it quiet since then.
  QHash<QString, quint64> mLastSeen;
  QHash<QString, QTimer *> mWaits;
  QFileSystemWatcher *mSystem = nullptr;
  QTimer *mPoll = nullptr;
  bool mLoaded = false;
  bool mInstalled = false;
};
