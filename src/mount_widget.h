#pragma once

#include "running_job.h"
#include "ui_mount_widget.h"

// A view of a RunningJob that happens to be a mount. Like JobWidget it owns
// no process: the mount, its log, its remote-control login and the script
// that follows it all belong to the job (L1). See docs/API.md S10.
class MountWidget : public QWidget {
  Q_OBJECT

public:
  MountWidget(RunningJob *job, const QString &remote, const QString &folder,
              const QString &script, QWidget *parent = nullptr);

  bool isRunning = true;
  QDateTime getStartDateTime();
  QString getStatus();

public slots:
  void cancel();
  QString getUniqueID();
  QString getUnmountingError();

signals:
  void finished();
  void closed();

private:
  Ui::MountWidget ui;

  RunningJob *mJob;

  void applyFinished(JobState state);

  QString mUnmountingError = "0";

  // 0 - running, 1 - finished, 2 - error
  // we add "z" to make mounts listed after transfers
  QString mStatus = "0_zmount_mounted";

  void updateStartInfo();
  void updateFinishInfo();
};
