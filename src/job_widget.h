#pragma once

#include "running_job.h"
#include "ui_job_widget.h"

// A view of a RunningJob. It owns no process, no remote-control client and
// no log writer -- those belong to the job (L1), which outlives the card and
// can be watched by anything else that wants to. See docs/API.md S2.
class JobWidget : public QWidget {
  Q_OBJECT

public:
  JobWidget(RunningJob *job, QWidget *parent = nullptr);

  void showDetails();
  bool isRunning = true;
  QDateTime getStartDateTime();
  QString getStatus();

public slots:
  void cancel();
  QString getUniqueID();
  QString getRequestId();
  QString getTransferMode();
  // False for a serve or a stream, which "stop all transfers" leaves alone
  // as it leaves mounts.
  bool isTransfer() const;

signals:
  void finished(const QString &info, const QString &jobFinalStatus);
  void closed();

private:
  Ui::JobWidget ui;

  RunningJob *mJob;

  void applyStats(const JobStats &stats);
  void updateTransferBars(const JobStats &stats);
  void applyFinished(JobState state);

  // Changes how fast a running transfer may go. Only transfers have one.
  QToolButton *mSpeed = nullptr;
  void askForSpeed();
  void showBandwidth(const QString &rate);

  QHash<QString, QLabel *> mActive;

  // The sort key the jobs tab uses. Kept in the widget because the leading
  // digit and the "z" in "zmount" exist only to make the list sort the way
  // the tab wants -- that is a display decision, not part of the job.
  QString mStatus = "0_transfer_running";

  void updateStartInfo();
  void updateFinishInfo(qint64 etaSeconds = 0);
};
