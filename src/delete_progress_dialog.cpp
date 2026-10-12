#include "delete_progress_dialog.h"
#include "global.h"
#include "running_job.h"
#include "utils.h"

DeleteProgressDialog::DeleteProgressDialog(
    const QList<RemoteOps::Item> &pDataList,
                                           QWidget *parent, bool close)
    : QDialog(parent) {

  mClose = close;

  // remove window close button
  setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                 Qt::WindowMinMaxButtonsHint);

  ui.setupUi(this);

  resize(0, 0);

  setWindowTitle("Delete");

  // manually control window size to ensure resizing take into account output
  // field which can be hidden
  mMinimumWidth = minimumWidth();
  mWidth = this->width();
  mHeight = this->height();
  ui.output->setVisible(true);
  adjustSize();
  mMinimumHeight = this->height();
  ui.output->setVisible(false);
  adjustSize();

  ui.labelOperation->setText("Running ");

  ui.labelOperation->setStyleSheet(
      "QLabel { color: green; font-weight: bold; }");

  ui.info->setCursorPosition(0);

  auto settings = GetSettings();

  if (settings->value("Settings/preemptiveLoading").toBool()) {
    mPreemptiveLoading = true;
  } else {
    mPreemptiveLoading = false;
  }

  if (mPreemptiveLoading) {
    // we use the same pool of rclone processes as for preemptive data loading
    int preemptiveLoadingLevel =
        settings->value("Settings/preemptiveLoadingLevel").toInt();
    if (preemptiveLoadingLevel == 0) {
      mMaxRcloneLsProcessCount = 10;
    }
    if (preemptiveLoadingLevel == 1) {
      mMaxRcloneLsProcessCount = 20;
    }
    if (preemptiveLoadingLevel == 2) {
      mMaxRcloneLsProcessCount = 40;
    }
  } else {
    // rclone delete/purge is light process so even with preloading off we can
    // allow 5 processes (so later we check +5)
    mMaxRcloneLsProcessCount = 0;
  }

  // apply font size preferences
  int fontsize = 0;
  fontsize = (settings->value("Settings/fontSize").toInt());

#if !defined(Q_OS_MACOS)
  fontsize--;
#endif

  QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  QFontMetrics fm(font);

  font.setPointSize(font.pointSize() + fontsize);

  ui.output->setFont(font);

  // icons style
  QString iconsColour = settings->value("Settings/iconsColour").toString();

  QString img_add = "";

  if (iconsColour == "white") {
    img_add = "_inv";
  }

  ui.output->setVisible(false);

  // set default arrow
  ui.buttonShowOutput->setIcon(
      QIcon(":media/images/qbutton_icons/vrightarrow" + img_add + ".png"));
  ui.buttonShowOutput->setIconSize(QSize(24, 24));

  mCancelButton =
      ui.buttonBox->addButton("&Cancel", QDialogButtonBox::RejectRole);

  QObject::connect(mCancelButton, &QPushButton::clicked, this, [=]() {
    if (mCancelled) {
      emit reject();
    }

    mQuitting = true;

    ui.labelOperation->setStyleSheet(
        "QLabel { color: red; font-weight: bold; }");
    ui.labelOperation->setText("Cancelling ");
  });

  //  QObject::connect(ui.buttonBox, &QDialogButtonBox::rejected, this,
  //                   &QDialog::reject);

  QObject::connect(
      ui.buttonShowOutput, &QPushButton::toggled, this, [=](bool checked) {
        ui.output->setVisible(checked);

        if (checked) {
          ui.buttonShowOutput->setIcon(QIcon(
              ":media/images/qbutton_icons/vdownarrow" + img_add + ".png"));
          ui.buttonShowOutput->setIconSize(QSize(24, 24));

          mWidth = this->width();
          setMinimumWidth(mWidth);
          setMaximumWidth(mWidth);
          adjustSize();
          setMinimumHeight(mHeight);
          setMaximumHeight(mHeight);
          setMinimumHeight(mMinimumHeight);
          setMaximumHeight(16777215);
          setMinimumWidth(mMinimumWidth);
          setMaximumWidth(16777215);
        } else {
          ui.buttonShowOutput->setIcon(QIcon(
              ":media/images/qbutton_icons/vrightarrow" + img_add + ".png"));
          ui.buttonShowOutput->setIconSize(QSize(24, 24));

          mWidth = this->width();
          mHeight = this->height();
          setMinimumHeight(0);
          setMinimumWidth(mWidth);
          adjustSize();
          setMinimumWidth(mMinimumWidth);
          //  when without output dont allow resize vertical
          int height = this->height();
          if (height != minimumHeight() || height != maximumHeight()) {
            setMinimumHeight(height);
            setMaximumHeight(height);
          }
        }
      });

  // local copy of items to delete
  mItemsToDelete = pDataList;

  mTotalToDelete = pDataList.count();

  // initiate deleting
  deleteProcessor();
}

DeleteProgressDialog::~DeleteProgressDialog() {}

void DeleteProgressDialog::closeEvent(QCloseEvent *ev) {

  mQuitting = true;

  if (mCancelled) {
    emit reject();
  }

  ui.labelOperation->setStyleSheet("QLabel { color: red; font-weight: bold; }");
  ui.labelOperation->setText("Cancelling ");

  ev->ignore();
  return;
}

void DeleteProgressDialog::expand() { ui.buttonShowOutput->setChecked(true); }

void DeleteProgressDialog::allowToClose() { ui.buttonBox->setEnabled(true); }

void DeleteProgressDialog::addNewDeleteProcess(const RemoteOps::Item &item) {

  // Through the core, like everything else that runs rclone. This built a
  // QProcess per item here, which is why a deletion left no trace in the
  // history. See remote_ops.h.
  QString why;
  RunningJob *job = RemoteOps::Delete(item, &why);
  if (job == nullptr) {
    // Refused before anything ran -- the root of a remote, say. Shown where
    // rclone's own complaints are shown, and counted as the failure it is.
    mError = true;
    ui.output->appendPlainText(why);
    ui.labelOperation->setStyleSheet(
        "QLabel { color: red; font-weight: bold; }");
    ui.labelOperation->setText("Error ");
    ui.buttonShowOutput->setChecked(true);
    ui.buttonBox->setEnabled(true);
    mCancelled = true;
    mCancelButton->setText("&Close");
    return;
  }

  {
    QMutexLocker locker(&deleteProcessMutex);

    global.rcloneLsProcessCount++;
    mDeleting++;
    mDeletingProcessesCount++;

    updateInfo();
  }

  QObject::connect(job, &RunningJob::outputLine, this,
                   [=](const QString &line) {
                     ui.output->appendPlainText(line);
                     emit outputAvailable(line);
                   });

  const auto ended = [=](JobState state) {
                     QMutexLocker locker(&deleteProcessMutex);

                     global.rcloneLsProcessCount--;
                     mDeletingProcessesCount--;

                     if (state == JobState::Finished) {
                       mDeleted++;

                     } else {
                       mError = true;

                       ui.labelOperation->setStyleSheet(
                           "QLabel { color: red; font-weight: bold; }");
                       ui.labelOperation->setText("Error ");
                       ui.buttonShowOutput->setChecked(true);
                       ui.buttonBox->setEnabled(true);
                     }

                     updateInfo();

                     if (!mQuitting) {
                       if (mItemsToDelete.count() == 0 &&
                           mDeletingProcessesCount == 0) {
                         // all done - bye bye
                         if (mClose && !mError) {

                           QTimer::singleShot(1000, Qt::CoarseTimer, this,
                                              SLOT(emitAccept()));
                         } else {

                           mCancelled = true;
                           mCancelButton->setText("&Close");
                         }
                       }

                     } else {

                       if (mDeletingProcessesCount == 0) {

                         if (!mError) {
                           ui.labelOperation->setStyleSheet(
                               "QLabel { color: red; font-weight: bold; }");
                           ui.labelOperation->setText("Cancelled ");
                         } else {

                           ui.labelOperation->setStyleSheet(
                               "QLabel { color: red; font-weight: bold; }");
                           ui.labelOperation->setText("Error ");
                         }

                         mCancelled = true;

                         mCancelButton->setText("&Close");
                       }
                     }
                   };

  QObject::connect(job, &RunningJob::finished, this, ended);

  // A job that could not be started at all has ended already, and said so
  // before anything here was listening. Without this the count of deletions
  // in progress never comes back down and the dialog waits for ever.
  if (!job->isRunning()) {
    ended(job->state());
  }
}

void DeleteProgressDialog::updateInfo() {
  ui.info->setText(
      QString("Deleting: %1 out of %2 remaining,  Deleted: %3 out of %4 total")
          .arg(mDeletingProcessesCount)
          .arg(mItemsToDelete.count() + mDeletingProcessesCount)
          .arg(mDeleted)
          .arg(mTotalToDelete));
}

void DeleteProgressDialog::deleteProcessor() {

  QMutexLocker locker(&deleteProcessorMutex);

  bool runAgain = false;

  if (mItemsToDelete.count() > 0 && !mQuitting) {
    if (global.rcloneLsProcessCount < mMaxRcloneLsProcessCount + 5) {

      int rcloneLsProcessesFreeCount =
          (mMaxRcloneLsProcessCount + 5) - global.rcloneLsProcessCount;

      for (int i = 0; i < rcloneLsProcessesFreeCount; ++i) {
        if (rcloneLsProcessesFreeCount <= 0) {
          break;
        }

        for (int j = 0; j < mItemsToDelete.count(); ++j) {
          if (rcloneLsProcessesFreeCount <= 0) {
            break;
          }

          const RemoteOps::Item item =
              mItemsToDelete.takeAt(mItemsToDelete.count() - 1);
          rcloneLsProcessesFreeCount--;
          addNewDeleteProcess(item);
        }
      }
    }
    runAgain = true;
  }

  if (runAgain) {
    QTimer::singleShot(200, Qt::CoarseTimer, this, SLOT(deleteProcessor()));
  }

  return;
}

void DeleteProgressDialog::emitAccept() { emit accept(); }
