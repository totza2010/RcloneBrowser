#pragma once
#include "icon_cache.h"
#include "job_queue.h"
#include "job_options.h"
#include "job_options_item.h"
#include "ui_main_window.h"
#ifdef Q_OS_MACOS
#include "mac_os_power_saving.h"
#endif
#include <QSystemTrayIcon>

class HistoryWidget;
class RunningJob;
class JobWidget;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow();
  ~MainWindow();

private slots:
  void rcloneGetVersion();
  void rcloneConfig();
  void rcloneListRemotes();

  // Draws the remotes tab from what RemoteRegistry holds. See
  // docs/LAYER-SPLIT.md block 2.
  void drawRemotes();
  void listTasks();

  void addTransfer(const QString &message, const QString &source,
                   const QString &dest, const QStringList &args,
                   const QString &uniqueId, const QString &transferMode,
                   const QString &requestId);
  void addStream(const QString &remote, const QString &stream,
                 const QString &remoteType);

  void addNewMount(const QString &remote, const QString &folder,
                   const QString &remoteType, const QStringList &args,
                   const QString &script, const QString &uniqueId,
                   const QString &info);

  void addScheduler(const QString &taskId, const QString &taskName,
                    const QStringList &args);

  void addSavedTransfer(const QString &uniqueId, bool dryRun, bool addToQueue);


  void slotCloseTab(int index);

  bool saveQueueFile(void);
  bool saveSchedulerFile(void);

  void autoStartMounts(void);

  // quit RB but only when all processes finished
  void quitApp(void);

private:
  Ui::MainWindow ui;

  QSystemTrayIcon *mSystemTray = nullptr;
  JobWidget *mLastFinished = nullptr;

  // The runs that have already happened, read back from the database (S13).
  HistoryWidget *mHistory = nullptr;

  bool mAlwaysShowInTray;
  bool mCloseToTray;
  bool mNotifyFinishedTransfers;
  bool mSoundNotif;

  QLabel *mStatusMessage;

  IconCache mIcons;

  bool mFirstTime = true;
  int mJobCount = 0;

  // keep track of number of active transfers
  int mTransferJobCount = 0;

  // The queue is JobQueue's (L1). These three keep their names but hold
  // nothing -- see docs/QUEUE-MOVE.md.
  struct QueueRunningFlag {
    operator bool() const { return JobQueue::instance().isRunning(); }
    QueueRunningFlag &operator=(bool on) {
      if (on) {
        JobQueue::instance().start();
      } else {
        JobQueue::instance().pause();
      }
      return *this;
    }
  };
  struct QueueCount {
    operator int() const { return JobQueue::instance().count(); }
    QueueCount &operator=(int) { return *this; }
    QueueCount &operator--() { return *this; }
    QueueCount &operator++() { return *this; }
  };
  struct QueueTaskFlag {
    operator bool() const { return JobQueue::instance().taskIsRunning(); }
    QueueTaskFlag &operator=(bool) { return *this; }
  };

  QueueRunningFlag mQueueStatus;

  // number of schedulers
  int mRunningSchedulersCount = 0;

  // number of queued tasks
  QueueCount mQueueCount;
  // is queued task running
  QueueTaskFlag mQueueTaskRunning;

  // make queue logic aware that app is quiting
  // so job is not removed from the queue
  bool mAppQuittingStatus = false;

  // don't sort then stopping all transfers
  bool mDoNotSort = false;

  bool canClose();
  void closeEvent(QCloseEvent *ev) override;
  bool getConfigPassword(QProcess *p);

  // sort QListWidget view/selection
  QList<QListWidgetItem *> sortListWidget(const QList<QListWidgetItem *> &list,
                                          bool sortOrder = false);

  // set screen buttons logic mess in one place
  void setQueueButtons(void);
  void setTasksButtons(void);

  void addEmptyJobsMessage();

  void runItem(JobOptions *jo, const QString &transferMode,
               const QString &requestId, bool dryrun = false);
  void editSelectedTask();
  QIcon mUploadIcon;
  QIcon mDownloadIcon;
  QIcon mMountIcon;
  QMessageBox *mQuittingErrorMsgBox = NULL;

#ifdef Q_OS_MACOS
  MacOsPowerSaving *mMacOsPowerSaving;
#endif

  // used for tasks transitions - prevent race conditions
  QMutex mMutex;
  QMutex mSaveQueueFileMutex;
  QMutex mSaveSchedulerFileMutex;
  QMutex mStopTaskMutex;
  QMutex mRunTaskMutex;
  QMutex mRunItemMutex;
  QMutex mJobsSortMutex;

  // if waiting for processes we show dialog - this is used to calculate delay
  int mQuitInfoDelay = 0;

  // Makes the card for a job that is already running. Called for every
  // transfer JobRegistry announces, whoever started it -- the window, the
  // queue, or later the API. Before this the card was made by the same
  // function that started the job, so a job started anywhere else appeared
  // nowhere. See docs/QUEUE-MOVE.md block C.
  void addJobCard(RunningJob *job);

  // Draws the queue tab from JobQueue: rows, tab text, buttons. Fifteen
  // places used to write that tab text; this is the one left.
  void refreshQueueView();

  void addTasksToQueue();

  void restoreSchedulersFromFile();

  // Draws the Scheduler tab from the counters and the global on/off, and is
  // the only thing that does. See docs/SCHEDULER-MOVE.md.
  void refreshSchedulerView();

  // Every schedule on the tab, in the order they are shown.
  //
  // The list of schedules is the widgets in a layout, with a separator line
  // between each pair, so reading it meant "walk backwards two at a time and
  // cast" -- written out sixteen times. This is the one copy until
  // SchedulerStore holds the list for real.
  QList<class SchedulerWidget *> schedulerWidgets() const;

  // Whether any schedule points at this task. Two places ask, and both used
  // to walk the layout to find out.
  bool taskIsScheduled(const QString &taskId) const;

  // The one clock. Asks SchedulerStore what is due, starts those, and lets
  // every schedule redraw its countdown. Every SchedulerWidget used to run a
  // timer of its own. See docs/SCHEDULER-MOVE.md block 7.
  void checkSchedules();

  // Tells every schedule what became of a run, and moves the count of
  // schedules with something going if one of them owns that run.
  void notifySchedulers(const QString &requestId, const QString &status,
                        int runningDelta = 0);

  void sortJobs();
  bool mJobsTimeSortOrder = false;
  bool mJobsStatusSortOrder = false;
  QString mJobsSort = "byDate";
  QString mIconsLayout;
};
