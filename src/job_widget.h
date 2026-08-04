#pragma once

#include "job_stats.h"
#include "rc_client.h"
#include "ui_job_widget.h"

class JobWidget : public QWidget {
  Q_OBJECT

public:
  JobWidget(QProcess *process, const QString &info, const QStringList &args,
            const QString &source, const QString &dest, const QString &uniqueID,
            const QString &transferMode, const QString &requestId,
            const QString &rcUser = QString(),
            const QString &rcPass = QString(), QWidget *parent = nullptr);
  ~JobWidget();

  void showDetails();
  bool isRunning = true;
  QDateTime getStartDateTime();
  QString getStatus();

public slots:
  void cancel();
  QString getUniqueID();
  QString getRequestId();
  QString getTransferMode();

signals:
  void finished(const QString &info, const QString &jobFinalStatus);
  void closed();

private:
  Ui::JobWidget ui;

  QProcess *mProcess;

  // Every figure on the card comes from rclone's remote control. Nothing is
  // read out of the printed output any more except the line announcing which
  // port the control ended up on.
  RcClient *mRc = nullptr;
  QString mRcUser;
  QString mRcPass;
  void applyStats(const JobStats &stats);
  void updateTransferBars(const JobStats &stats);

  QStringList mArgs;
  QHash<QString, QLabel *> mActive;

  QString mUniqueID = "";
  QString mTransferMode = "";
  QString mRequestId = "";
  QString mJobFinalStatus = "";

  // 0 - running, 1 - finished, 2 - error
  QString mStatus = "0_transfer_running";

  QDateTime mStartDateTime = QDateTime::currentDateTime();
  void updateStartInfo();
  void updateFinishInfo(qint64 etaSeconds = 0);
};
