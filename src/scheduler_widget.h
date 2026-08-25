#pragma once

#include "hours_spinbox.h"
#include "minutes_spinbox.h"
#include "schedule.h"
#include "ui_scheduler_widget.h"

class SchedulerWidget : public QWidget {
  Q_OBJECT

public:
  /*
    StreamWidget(QProcess *rclone, QProcess *player, const QString &remote,
                 const QString &stream, const QStringList &args,
                 QWidget *parent = nullptr);
    ~StreamWidget();
  */

  SchedulerWidget(const QString &taskId, const QString &taskName,
                  const QStringList &args, QWidget *parent = nullptr);
  ~SchedulerWidget();

  bool isRunning = true;
  // return all scheduler parameters so we can store it
  QStringList getSchedulerParameters();

  // What this card is showing, as data. The stored form is Schedule's to
  // decide; this widget only says what it holds. See
  // docs/SCHEDULER-MOVE.md block 9.
  Schedule toSchedule() const;

  QString getSchedulerId();
  QString getSchedulerTaskId();
  QString getSchedulerRequestId();
  int getExecutionMode();

  // Asks this schedule to start its run, because something decided its
  // minute has come. Refused, with a reason in the log, when its own task is
  // still going from last time -- the one condition that is this schedule's
  // to know rather than the store's.
  //
  // The deciding used to be here: every widget ran a timer of its own and
  // worked out whether it was due. One clock for all of them means they
  // cannot disagree, and means the answer exists without a window. See
  // docs/SCHEDULER-MOVE.md block 7.
  void startScheduledRun();

  // Redraws "next run" from the current settings. Called on the same tick
  // that looks for due schedules, so the tab keeps counting down.
  void refreshNextRun();
  void updateTaskName(const QString newTaskName);
  void updateTaskStatus(const QString requestID, const QString taskStatus);
  void stopScheduler();
  void startScheduler();

public slots:
  //  void cancel();

signals:
  //  void finished();
  void closed();
  void save();
  void editTask();
  void runTask();
  void stopTask();

private:
  Ui::SchedulerWidget ui;
  QStringList mArgs;

  void applySettingsToScreen();
  void applyArgsToScheduler(QStringList args);
  void applySchedule(const Schedule &schedule);
  void applyScreenToSettings();
  void updateInfoFields();

  QString enhanceCron(QString cron);

  // The task's name as it is spelled now, looked up by id. mTaskName is only
  // the copy saved when the schedule was made, kept for when the task is gone.
  QString currentTaskName() const;

  QDateTime nextRun();

  // list of scheduler parameters to be persistent in file
  QString mSchedulerStatus = "paused"; // activated, paused

  QString mTaskId = "";
  QString mTaskName = "";      // b64
  QString mSchedulerName = ""; // b64
  QString mSchedulerId = QUuid::createUuid().toString();

  QString mLastRun = "never"; // b64
  QString mRequestId = "";
  QString mLastRunFinished = ""; // b64
  QString mLastRunStatus = "";   // b64

  bool mDailyState = true;
  bool mDailyMon = true;
  bool mDailyTue = true;
  bool mDailyWed = true;
  bool mDailyThu = true;
  bool mDailyFri = true;
  bool mDailySat = true;
  bool mDailySun = true;
  QString mDailyHour = "00";
  QString mDailyMinute = "00";

  bool mCronState = false;
  QString mCron = "30 6,18 * * MON-FRI"; // b64

  QString mExecutionMode = "0"; // 0,1

  bool mManualStart = false;
  bool mTaskRunning = false;
  QString mIconsColour;
  bool mGlobalStop = false;
  QDateTime mNextRun;

};
