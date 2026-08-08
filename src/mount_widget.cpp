#include "mount_widget.h"
#include "global.h"
#include "utils.h"

MountWidget::MountWidget(RunningJob *job, const QString &remote,
                         const QString &folder, const QString &script,
                         QWidget *parent)
    : QWidget(parent), mJob(job) {
  ui.setupUi(this);

  updateStartInfo();

  QObject::connect(mJob, &RunningJob::outputLine, this,
                   [this](const QString &line) {
                     ui.output->appendPlainText(line);
                   });
  QObject::connect(mJob, &RunningJob::scriptOutputLine, this,
                   [this](const QString &line) {
                     ui.sOutput->appendPlainText(line);
                   });
  QObject::connect(mJob, &RunningJob::finished, this,
                   &MountWidget::applyFinished);

  // Unmounting can be refused while the mount stays up -- a file on it open
  // in another program is enough. The card has to go back to saying
  // "Mounted", because that is what it still is.
  QObject::connect(mJob, &RunningJob::stopFailed, this,
                   [this](const QString &reason) {
                     mUnmountingError = reason;
                     mStatus = "0_zmount_mounted";
                     ui.cancel->setEnabled(true);
                     ui.showDetails->setStyleSheet(
                         "QToolButton { border: 0; color: red; "
                         "font-weight: bold;}");
                     ui.showDetails->setText("  Mounted");
                     ui.showDetails->setToolTip(
                         "Unmounting failed - check if mount"
                         " is not used by other programs");
                     ui.showDetails->setStatusTip(
                         "Unmounting failed - check if mount"
                         " is not used by other programs");
                   });

  // SECURITY: (docs/ARCHITECTURE.md 5) the remote-control login never reaches
  // the argument list, but users can still put backend tokens in the
  // free-form option fields. Any new sink for arguments -- log files, API
  // responses, diagnostics -- must go through RedactArgs() as well.
  ui.showOutput->setToolTip(mJob->displayCommand().join(" "));

  const QString info = mJob->description().info;

  QString screenInfo;
  if (info == "") {
    screenInfo = QString("%1 on %2").arg(remote).arg(folder);
  } else {
    screenInfo = info;
  }

  QString screenInfoTrimmed;
  if (screenInfo.length() > 140) {
    screenInfoTrimmed = screenInfo.left(57) + "..." + screenInfo.right(80);
  } else {
    screenInfoTrimmed = screenInfo;
  }

  ui.info->setText(screenInfoTrimmed);
  ui.info->setCursorPosition(0);

  ui.remote->setText(remote);
  ui.remote->setCursorPosition(0);
  ui.remote->setToolTip(remote);

  ui.folder->setText(folder);
  ui.folder->setCursorPosition(0);
  ui.folder->setToolTip(folder);

  ui.details->setVisible(false);

  auto settings = GetSettings();

  int fontsize = 0;
  fontsize = (settings->value("Settings/fontSize").toInt());

#if !defined(Q_OS_MACOS)
  fontsize--;
#endif

  QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  QFontMetrics fm(font);

  font.setPointSize(font.pointSize() + fontsize);

  ui.output->setFont(font);
  ui.sOutput->setFont(font);

  ui.details->setVisible(false);
  ui.output->setVisible(false);
  ui.sOutput->setVisible(false);
  ui.l_script->setVisible(false);

  if (script.isEmpty()) {
    ui.showScriptOutput->setVisible(false);
    ui.l_script->setVisible(false);
  } else {
    ui.l_script->setStyleSheet("QLabel { color: green; font-weight: bold;}");
    ui.l_script->setText("Running");
    ui.showScriptOutput->setToolTip(script);
  }

  QString iconsColour = settings->value("Settings/iconsColour").toString();

  QString img_add = "";

  if (iconsColour == "white") {
    img_add = "_inv";
  }

  ui.showDetails->setIcon(
      QIcon(":media/images/qbutton_icons/vrightarrow" + img_add + ".png"));
  ui.showDetails->setIconSize(QSize(24, 24));
  ui.showOutput->setIcon(
      QIcon(":media/images/qbutton_icons/vrightarrow" + img_add + ".png"));
  ui.showOutput->setIconSize(QSize(24, 24));

  ui.showScriptOutput->setIcon(
      QIcon(":media/images/qbutton_icons/vrightarrow" + img_add + ".png"));
  ui.showScriptOutput->setIconSize(QSize(24, 24));

  ui.cancel->setIcon(
      QIcon(":media/images/qbutton_icons/cancel" + img_add + ".png"));
  ui.cancel->setIconSize(QSize(24, 24));

  ui.copy->setIcon(
      QIcon(":media/images/qbutton_icons/copy" + img_add + ".png"));
  ui.copy->setIconSize(QSize(24, 24));

  ui.showDetails->setStyleSheet(
      "QToolButton { border: 0; color: green; font-weight: bold;}");
  ui.showDetails->setText("  Mounted");
  ui.showDetails->setToolTip("Show details");
  ui.showDetails->setStatusTip("Show details");

  QObject::connect(
      ui.showDetails, &QToolButton::toggled, this, [=](bool checked) {
        ui.details->setVisible(checked);
        if (checked) {
          ui.showDetails->setIcon(QIcon(
              ":media/images/qbutton_icons/vdownarrow" + img_add + ".png"));
          ui.showDetails->setIconSize(QSize(24, 24));
        } else {
          ui.showDetails->setIcon(QIcon(
              ":media/images/qbutton_icons/vrightarrow" + img_add + ".png"));
          ui.showDetails->setIconSize(QSize(24, 24));
        }
      });

  QObject::connect(mJob, &RunningJob::scriptFailed, this,
                   [this](const QString &error) {
                     ui.l_script->setStyleSheet(
                         "QLabel { color: red; font-weight: bold;}");
                     ui.l_script->setText("Process error: " + error);
                   });

  QObject::connect(
      mJob, &RunningJob::scriptFinished, this, [this, iconsColour](int status) {
        if (status == 0) {
          if (iconsColour == "white") {
            ui.l_script->setStyleSheet("QLabel {font-weight: bold;}");
            ui.l_script->setText("Finished");
          } else {
            ui.l_script->setStyleSheet(
                "QLabel { color: black; font-weight: bold;}");
            ui.l_script->setText("Finished (returned error code: " +
                                 QString::number(status) + ")");
          }
        } else {
          ui.l_script->setStyleSheet(
              "QLabel { color: red; font-weight: bold;}");
          ui.l_script->setText(
              "Error (returned error code: " + QString::number(status) + ")");
        }
      });

  QObject::connect(ui.copy, &QToolButton::clicked, this, [=]() {
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(mJob->displayCommand().join(" "));
  });

  QObject::connect(
      ui.showScriptOutput, &QToolButton::toggled, this, [=](bool checked) {
        ui.sOutput->setVisible(checked);
        ui.l_script->setVisible(checked);

        if (checked) {
          ui.showScriptOutput->setIcon(QIcon(
              ":media/images/qbutton_icons/vdownarrow" + img_add + ".png"));
          ui.showScriptOutput->setIconSize(QSize(24, 24));
        } else {
          ui.showScriptOutput->setIcon(QIcon(
              ":media/images/qbutton_icons/vrightarrow" + img_add + ".png"));
          ui.showScriptOutput->setIconSize(QSize(24, 24));
        }
      });

  QObject::connect(
      ui.showOutput, &QToolButton::toggled, this, [=](bool checked) {
        ui.output->setVisible(checked);
        // ui.l_rclone->setVisible(checked);

        if (checked) {
          ui.showOutput->setIcon(QIcon(
              ":media/images/qbutton_icons/vdownarrow" + img_add + ".png"));
          ui.showOutput->setIconSize(QSize(24, 24));
        } else {
          ui.showOutput->setIcon(QIcon(
              ":media/images/qbutton_icons/vrightarrow" + img_add + ".png"));
          ui.showOutput->setIconSize(QSize(24, 24));
        }
      });

  QObject::connect(ui.cancel, &QToolButton::clicked, this, [=]() {
    if (isRunning) {
      int button = QMessageBox::question(
          this, "Unmount",
          QString("Do you want to unmount?\n\n %2 \n\n mounted to \n\n %1")
              .arg(folder)
              .arg(remote),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
      if (button == QMessageBox::Yes) {
        cancel();
      }
    } else {
      emit closed();
    }
  });
}

void MountWidget::cancel() {
  if (!isRunning) {
    return;
  }

  // Asking, not killing. Whether it worked is reported back through
  // stopFailed(), because a mount can refuse to go.
  mUnmountingError = "0";
  mJob->stop();

  ui.showDetails->setStyleSheet(
      "QToolButton { border: 0; color: green; font-weight: bold;}");
  ui.showDetails->setText("  Unmounting");
  ui.cancel->setEnabled(false);
}

void MountWidget::applyFinished(JobState state) {
  isRunning = false;

  const QString iconsColour =
      GetSettings()->value("Settings/iconsColour").toString();

  QString info = "Mounted " + ui.info->text();
  if (info.length() > 140) {
    info = info.left(57) + "..." + info.right(80);
  }
  ui.info->setText(info);
  ui.info->setCursorPosition(0);

  if (state == JobState::Error) {
    ui.showDetails->setStyleSheet(
        "QToolButton { border: 0; color: red; font-weight: bold;}");
    ui.showDetails->setText("  Error");
    mStatus = "1_zmount_erro";
  } else {
    if (iconsColour == "white") {
      ui.showDetails->setStyleSheet(
          "QToolButton { border: 0; wfont-weight: bold;}");
    } else {
      ui.showDetails->setStyleSheet(
          "QToolButton { border: 0; color: black; font-weight: bold;}");
    }
    ui.showDetails->setText("  Finished");
    mStatus = "1_zmount_finished";
  }

  ui.showDetails->setToolTip("Show details");
  ui.showDetails->setStatusTip("Show details");
  ui.cancel->setToolTip("Close");
  ui.cancel->setStatusTip("Close");
  ui.cancel->setEnabled(true);

  updateFinishInfo();
  emit finished();
}

QString MountWidget::getUniqueID() { return mJob->taskId(); }
QString MountWidget::getUnmountingError() { return mUnmountingError; }
QDateTime MountWidget::getStartDateTime() { return mJob->startedAt(); }

void MountWidget::updateStartInfo() {
  ui.le_StartInfo->setText(
    "Started:   " +
    QLocale::system().toString(mJob->startedAt(), QLocale::LongFormat));
}

void MountWidget::updateFinishInfo() {
  ui.le_FinishInfo->setText(
    "Finished:   " +
    QLocale::system().toString(QDateTime::currentDateTime(), QLocale::LongFormat));
}

QString MountWidget::getStatus() { return mStatus; }
