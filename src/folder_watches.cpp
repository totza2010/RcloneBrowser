#include "folder_watches.h"
#include "debug_log.h"
#include "job_options.h"
#include "job_queue.h"
#include "job_registry.h"
#include "list_of_job_options.h"
#include "running_job.h"
#include "utils.h"

#include <QCryptographicHash>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTimer>

namespace {

const char *const kStored = "Settings/watches";

// The most folders handed to the system to watch, all watches together.
// Each costs the system something (on Linux, one of a limited number), and
// past this the look every few minutes is what notices.
const int kMostFolders = 2000;

} // namespace

FolderWatches &FolderWatches::instance() {
  static FolderWatches *watches = new FolderWatches();
  return *watches;
}

int FolderWatches::PollSeconds() {
  bool ok = false;
  const int asked = qEnvironmentVariableIntValue("RB_WATCH_POLL_SECONDS", &ok);
  return ok && asked > 0 ? asked : 300;
}

quint64 FolderWatches::Fingerprint(const QString &folder, int *files) {
  QStringList seen;
  QDirIterator it(folder, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    const QFileInfo info = it.fileInfo();
    seen << QStringLiteral("%1|%2|%3")
                .arg(info.filePath())
                .arg(info.size())
                .arg(info.lastModified().toMSecsSinceEpoch());
  }
  // In order, so two looks at an unchanged folder agree whatever order the
  // system lists it in.
  seen.sort();
  if (files != nullptr) {
    *files = int(seen.size());
  }
  QCryptographicHash hash(QCryptographicHash::Sha1);
  for (const QString &line : seen) {
    hash.addData(line.toUtf8());
    hash.addData(QByteArrayView("\n", 1));
  }
  const QByteArray digest = hash.result();
  quint64 value = 0;
  for (int i = 0; i < 8; ++i) {
    value = (value << 8) | quint8(digest.at(i));
  }
  return value;
}

QString FolderWatches::WhyNot(const QString &taskId) {
  JobOptions *task = ListOfJobOptions::getInstance()->find(taskId);
  if (task == nullptr) {
    return QStringLiteral("there is no such task");
  }
  if (task->jobType != JobOptions::Upload ||
      task->operation == JobOptions::Mount) {
    return QStringLiteral("only a task that sends a folder of this machine "
                          "can watch it");
  }
  if (!QFileInfo(task->source).isDir()) {
    return QStringLiteral("\"%1\" is not a folder of this machine")
        .arg(task->source);
  }
  return QString();
}

QString FolderWatches::folderOf(const QString &taskId) const {
  JobOptions *task = ListOfJobOptions::getInstance()->find(taskId);
  return task != nullptr ? task->source : QString();
}

void FolderWatches::install() {
  if (mInstalled) {
    return;
  }
  mInstalled = true;
  load();

  mSystem = new QFileSystemWatcher(this);
  QObject::connect(mSystem, &QFileSystemWatcher::directoryChanged, this,
                   &FolderWatches::noticed);

  mPoll = new QTimer(this);
  mPoll->setInterval(PollSeconds() * 1000);
  QObject::connect(mPoll, &QTimer::timeout, this, &FolderWatches::checkAll);
  mPoll->start();

  // What each folder is now counts as sent: watching starts from here, not
  // from everything that was already there.
  for (const FolderWatch &watch : mWatches) {
    const QString folder = folderOf(watch.taskId);
    if (!folder.isEmpty()) {
      mSent.insert(watch.taskId, Fingerprint(folder));
      mLastSeen.insert(watch.taskId, mSent.value(watch.taskId));
    }
  }
  rewatch();
}

void FolderWatches::load() {
  if (mLoaded) {
    return;
  }
  mLoaded = true;
  mWatches.clear();
  const QJsonArray stored =
      QJsonDocument::fromJson(GetSettings()->value(kStored).toString().toUtf8())
          .array();
  for (const QJsonValue &value : stored) {
    FolderWatch watch;
    watch.taskId = value.toObject().value(QStringLiteral("taskId")).toString();
    watch.quietSeconds =
        qBound(1, value.toObject().value(QStringLiteral("quietSeconds")).toInt(30),
               86400);
    if (!watch.taskId.isEmpty()) {
      mWatches.append(watch);
    }
  }
}

void FolderWatches::save() {
  QJsonArray stored;
  for (const FolderWatch &watch : mWatches) {
    QJsonObject object;
    object.insert(QStringLiteral("taskId"), watch.taskId);
    object.insert(QStringLiteral("quietSeconds"), watch.quietSeconds);
    stored.append(object);
  }
  GetSettings()->setValue(
      kStored,
      QString::fromUtf8(QJsonDocument(stored).toJson(QJsonDocument::Compact)));
  GetSettings()->sync();
  emit changed();
}

QList<FolderWatch> FolderWatches::list() const {
  const_cast<FolderWatches *>(this)->load();
  return mWatches;
}

const FolderWatch *FolderWatches::find(const QString &taskId) const {
  const_cast<FolderWatches *>(this)->load();
  for (const FolderWatch &watch : mWatches) {
    if (watch.taskId == taskId) {
      return &watch;
    }
  }
  return nullptr;
}

bool FolderWatches::watch(const QString &taskId, int quietSeconds,
                          QString *why) {
  load();
  const QString problem = WhyNot(taskId);
  if (!problem.isEmpty()) {
    if (why != nullptr) {
      *why = problem;
    }
    return false;
  }
  const int quiet = qBound(1, quietSeconds > 0 ? quietSeconds : 30, 86400);
  for (FolderWatch &watch : mWatches) {
    if (watch.taskId == taskId) {
      watch.quietSeconds = quiet;
      save();
      return true;
    }
  }
  FolderWatch watch;
  watch.taskId = taskId;
  watch.quietSeconds = quiet;
  mWatches.append(watch);
  const quint64 now = Fingerprint(folderOf(taskId));
  mSent.insert(taskId, now);
  mLastSeen.insert(taskId, now);
  save();
  rewatch();
  qCDebug(rbApp) << "watching the folder of task" << taskId;
  return true;
}

bool FolderWatches::unwatch(const QString &taskId) {
  load();
  for (int i = 0; i < mWatches.size(); ++i) {
    if (mWatches.at(i).taskId == taskId) {
      mWatches.removeAt(i);
      mSent.remove(taskId);
      mLastSeen.remove(taskId);
      delete mWaits.take(taskId);
      save();
      rewatch();
      return true;
    }
  }
  return false;
}

void FolderWatches::rewatch() {
  if (mSystem == nullptr) {
    return; // not installed: a test, or a program that does not watch
  }
  const QStringList old = mSystem->directories();
  if (!old.isEmpty()) {
    mSystem->removePaths(old);
  }
  QStringList folders;
  for (const FolderWatch &watch : mWatches) {
    const QString folder = folderOf(watch.taskId);
    if (folder.isEmpty() || !QFileInfo(folder).isDir()) {
      continue;
    }
    folders << folder;
    QDirIterator it(folder, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                    QDirIterator::Subdirectories);
    while (it.hasNext() && folders.size() < kMostFolders) {
      folders << it.next();
    }
  }
  if (!folders.isEmpty()) {
    mSystem->addPaths(folders);
  }
}

void FolderWatches::noticed(const QString &path) {
  for (const FolderWatch &watch : mWatches) {
    const QString folder = folderOf(watch.taskId);
    if (folder.isEmpty() ||
        !(path == folder || path.startsWith(folder + QLatin1Char('/')) ||
          path.startsWith(folder + QLatin1Char('\\')))) {
      continue;
    }
    // Not yet: when it has been quiet for long enough.
    QTimer *wait = mWaits.value(watch.taskId);
    if (wait == nullptr) {
      wait = new QTimer(this);
      wait->setSingleShot(true);
      const QString id = watch.taskId;
      QObject::connect(wait, &QTimer::timeout, this, [this, id]() { check(id); });
      mWaits.insert(watch.taskId, wait);
    }
    wait->start(watch.quietSeconds * 1000);
  }
}

void FolderWatches::checkAll() {
  for (const FolderWatch &watch : list()) {
    check(watch.taskId);
  }
}

void FolderWatches::check(const QString &taskId) {
  FolderWatch *watch = nullptr;
  for (FolderWatch &one : mWatches) {
    if (one.taskId == taskId) {
      watch = &one;
    }
  }
  if (watch == nullptr) {
    return;
  }
  // A task deleted since: its watch goes with it.
  if (ListOfJobOptions::getInstance()->find(taskId) == nullptr) {
    unwatch(taskId);
    return;
  }

  const quint64 now = Fingerprint(folderOf(taskId));
  if (now == mSent.value(taskId)) {
    mLastSeen.insert(taskId, now);
    return; // nothing new since it was last sent
  }
  if (now != mLastSeen.value(taskId)) {
    // Still changing, or changed since the last look: wait for quiet.
    mLastSeen.insert(taskId, now);
    watch->lastChange = QDateTime::currentDateTime();
    noticed(folderOf(taskId));
    rewatch(); // a new folder inside has to be watched too
    return;
  }

  // Quiet, and different from what was last sent.
  for (RunningJob *job : JobRegistry::instance().jobs()) {
    if (job->isRunning() && job->taskId() == taskId) {
      // Going now, and it may have started before the change: looked at
      // again when the poll comes round, rather than queued behind itself.
      return;
    }
  }
  for (const QueueEntry &entry : JobQueue::instance().entries()) {
    if (entry.taskId == taskId) {
      mSent.insert(taskId, now); // that run will send it
      return;
    }
  }
  const QString requestId = JobQueue::instance().enqueue(
      taskId, false, QString(), QStringLiteral("folder watch"));
  mSent.insert(taskId, now);
  watch->lastQueued = QDateTime::currentDateTime();
  watch->lastRequestId = requestId;
  qCDebug(rbApp) << "the folder of task" << taskId << "changed; queued"
                 << requestId;
  emit queued(taskId, requestId);
  emit changed();
}
