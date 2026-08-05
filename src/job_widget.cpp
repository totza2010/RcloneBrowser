#include "job_widget.h"
#include "utils.h"
#include <QProgressBar>
#include <QLabel>

namespace {

// The most of a progress line that fits inside the bar.
//
// A QProgressBar wraps its text and then clips the second line, so a long
// reading -- "0%  ·  44.0 KiB / 3.14 GiB  ·  2.04 KiB/s  ·  18d 15h left" --
// came out as a smear of half-cut characters. Parts are dropped from the end
// rather than eliding, because half a figure is worse than no figure: the
// percentage survives on the narrowest bar, and the ETA is the first to go.
QString FitProgressText(QProgressBar *bar, QStringList parts) {
  const int width = qMax(bar->width(), bar->minimumWidth()) - 16;
  if (width < 80 || parts.isEmpty()) {
    return JoinProgressParts(parts); // not laid out yet; measuring is noise
  }

  const QFontMetrics fm(bar->font());
  while (parts.size() > 1 &&
         fm.horizontalAdvance(JoinProgressParts(parts)) > width) {
    parts.removeLast();
  }
  return JoinProgressParts(parts);
}

} // namespace

JobWidget::JobWidget(QProcess *process, const QString &info,
                     const QStringList &args, const QString &source,
                     const QString &dest, const QString &uniqueID,
                     const QString &transferMode, const QString &requestId,
                     const QString &rcUser, const QString &rcPass,
                     QWidget *parent)
    : QWidget(parent), mProcess(process) {
  ui.setupUi(this);

  updateStartInfo();

  mRcUser = rcUser;
  mRcPass = rcPass;
  if (!rcUser.isEmpty() && !rcPass.isEmpty()) {
    mRc = new RcClient(this);
    QObject::connect(mRc, &RcClient::statsReceived, this,
                     &JobWidget::applyStats);
    // The job itself is unaffected -- it is only the figures on the card that
    // stop updating. Say so rather than leaving a card frozen at zero.
    QObject::connect(mRc, &RcClient::unavailable, this, [this]() {
      ui.overall->hide();
      ui.progress_info->show();
      ui.progress_info->setStyleSheet(
          "QLabel { color: orange; font-weight: bold;}");
      ui.progress_info->setText("(no progress info)");
      ui.progress_info->setToolTip(
          "rclone's remote control did not respond, so progress cannot be "
          "reported. The transfer itself is unaffected; see the output for "
          "details.");
    });
  }

  if (mRc) {
    // Busy until the first reading arrives: rclone has not said how much there
    // is to do, so a bar sitting at 0% would be claiming to know that nothing
    // has happened.
    ui.overall->setRange(0, 0);
    ui.progress_info->setText("Starting");
  } else {
    // Without the remote control there is nothing to fill either of these
    // with. Showing an empty bar and "(0%)" for the whole job, as this did
    // before, reads as a transfer that never moves.
    ui.overall->hide();
    ui.progress_info->hide();
  }

  mArgs = GetRcloneCmd(args);

  ui.showOutput->setToolTip(RedactArgs(mArgs).join(" "));

  if (JobLogWriter::isEnabled()) {
    // args[0] is the rclone subcommand ("copy", "sync", "move"). transferMode
    // is often empty and describes the queue, not the operation.
    // The redacted form is what gets written; the log outlives the window.
    mLog.begin(args.value(0), uniqueID, RedactArgs(mArgs));
  }

  ui.source->setText(source);
  ui.source->setCursorPosition(0);
  ui.source->setToolTip(source);

  ui.dest->setText(dest);
  ui.dest->setCursorPosition(0);
  ui.dest->setToolTip(dest);

  QString infoTrimmed;

  mRequestId = requestId;
  mUniqueID = uniqueID;
  mTransferMode = transferMode;

  if (info.length() > 140) {
    infoTrimmed = info.left(57) + "..." + info.right(80);
  } else {
    infoTrimmed = info;
  }

  ui.info->setText(infoTrimmed);
  ui.info->setCursorPosition(0);

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
  ui.output->setVisible(false);
  // Keep the last 10000 lines instead of wiping the whole view once that many
  // have been seen -- a long transfer used to lose its entire output.
  // TEST: (V-04) copy โฟลเดอร์ที่มีไฟล์ >10,000 ไฟล์ ด้วย -v แล้วกางช่อง output
  // ระหว่างงานวิ่ง: ต้องไม่มีจังหวะที่ช่องว่างเปล่า และเลื่อนขึ้นไปต้องยังเห็นบรรทัดเก่า
  ui.output->setMaximumBlockCount(10000);

  QString iconsColour = settings->value("Settings/iconsColour").toString();

  QString img_add = "";

  if (iconsColour == "white") {
    img_add = "_inv";
  }

  ui.showDetails->setIcon(
      QIcon(":media/images/qbutton_icons/vrightarrow" + img_add + ".png"));
  ui.showOutput->setIcon(
      QIcon(":media/images/qbutton_icons/vrightarrow" + img_add + ".png"));

  ui.showDetails->setIconSize(QSize(24, 24));
  ui.showOutput->setIconSize(QSize(24, 24));

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

  QObject::connect(
      ui.showOutput, &QToolButton::toggled, this, [=](bool checked) {
        ui.output->setVisible(checked);

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

  ui.cancel->setIcon(
      QIcon(":media/images/qbutton_icons/cancel" + img_add + ".png"));
  ui.cancel->setIconSize(QSize(24, 24));

  QObject::connect(ui.cancel, &QToolButton::clicked, this, [=]() {
    if (isRunning) {
      int button = QMessageBox::question(
          this, "Transfer",
          QString(
              "rclone process is still running.\n\nDo you want to stop it?"),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
      if (button == QMessageBox::Yes) {
        cancel();
      }
    } else {
      emit closed();
    }
  });

  ui.copy->setIcon(
      QIcon(":media/images/qbutton_icons/copy" + img_add + ".png"));
  ui.copy->setIconSize(QSize(24, 24));

  QObject::connect(ui.copy, &QToolButton::clicked, this, [=]() {
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(RedactArgs(mArgs).join(" "));
  });

  QObject::connect(mProcess, &QProcess::readyRead, this, [=]() {
    while (mProcess->canReadLine()) {
      const QString line = QString(mProcess->readLine()).trimmed();

      // rclone announces the port it settled on. This is the only thing the
      // job card still takes from the output; every figure on the card comes
      // from core/stats instead.
      if (mRc && !mRc->isRunning()) {
        if (const quint16 port = ParseRcServingPort(line)) {
          mRc->start(port, mRcUser, mRcPass);
        }
      }

      // Our own polling would otherwise dominate the log at -vv and above.
      if (IsRcPollingNoise(line)) {
        continue;
      }

      // SECURITY: at -vv rclone echoes the remote-control password it read
      // out of the environment. Never let that reach the output pane, the
      // clipboard, or a log file (docs/ARCHITECTURE.md section 5).
      const QString safe = RedactOutputLine(line, mRcUser, mRcPass);
      ui.output->appendPlainText(safe);
      mLog.appendLine(safe);
    }
  });

  QObject::connect(
      mProcess,
      static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
          &QProcess::finished),
      this, [=](int status, QProcess::ExitStatus) {
        if (mRc) {
          mRc->stop();
        }
        mProcess->deleteLater();
        for (auto label : mActive) {
          ui.progress->removeRow(label); // deletes the label and its bar
        }
        mActive.clear();

        isRunning = false;
        ui.overall->hide();
        if (status == 0) {
          if (iconsColour == "white") {
            ui.showDetails->setStyleSheet(
                "QToolButton { border: 0; font-weight: bold;}");
          } else {
            ui.showDetails->setStyleSheet(
                "QToolButton { border: 0; color: black; font-weight: bold;}");
          }
          ui.showDetails->setText("  Finished");
          mJobFinalStatus = "finished";
          mStatus = "1_transfer_finished";
          ui.progress_info->hide();
        } else {
          ui.showDetails->setStyleSheet(
              "QToolButton { border: 0; color: red; font-weight: bold;}");
          ui.showDetails->setText("  Error");

          if (mJobFinalStatus == "stopped") {
          } else {
            mJobFinalStatus = "error";
            mStatus = "2_transfer_error";
          }

          ui.progress_info->hide();
        }

        mLog.finish(mJobFinalStatus.isEmpty() ? QStringLiteral("finished")
                                              : mJobFinalStatus);

        updateFinishInfo();

        ui.cancel->setToolTip("Close");
        ui.cancel->setStatusTip("Close");
        emit finished(ui.info->text(), mJobFinalStatus);
      });

  ui.showDetails->setStyleSheet(
      "QToolButton { border: 0; color: green; font-weight: bold;}");
  ui.showDetails->setText("  Running");
}

JobWidget::~JobWidget() {
  if (mRc) {
    mRc->stop();
  }
}

void JobWidget::applyStats(const JobStats &stats) {
  ui.size->setText(stats.sizeText());
  ui.totalsize->setText(FormatBytes(stats.totalBytes));
  ui.bandwidth->setText(stats.speedText());
  ui.eta->setText(stats.etaText());
  ui.checks->setText(stats.checksText());
  ui.transferred->setText(stats.transfersText());
  ui.elapsed->setText(stats.elapsedText());

  if (stats.errors > 0) {
    ui.errors->setStyleSheet("QLabel { color: red; font-weight: bold;}");
    ui.errors->setText(QString::number(stats.errors));
  }

  // TEST: (V-13) copy โฟลเดอร์ใหญ่ไป remote แล้วดูหัวการ์ดตั้งแต่วินาทีแรก:
  // ต้องเป็นแถบวิ่ง + "Scanning" ก่อน แล้วค่อยเป็น % จริง · ตอนท้ายต้องขึ้น
  // "Finishing" ไม่ใช่ 100% ค้าง · ลองกับ teldrive ที่ไม่รู้ขนาดไฟล์ล่วงหน้าด้วย
  //
  // A percentage is only worth showing once rclone has something to divide by.
  // Before that the bar runs as a busy indicator and the phase is spelled out
  // beside it, rather than a figure that will jump when the count lands.
  const JobPhase phase = stats.phase();
  if (phase == JobPhase::Starting || phase == JobPhase::Scanning) {
    ui.overall->setRange(0, 0);
    ui.overall->setToolTip("rclone is still counting what has to be done.");
  } else {
    ui.overall->setRange(0, 100);
    ui.overall->setValue(stats.percent());
    ui.overall->setFormat(FitProgressText(ui.overall, stats.progressParts()));
    // Whatever did not fit on the bar is still one hover away.
    ui.overall->setToolTip(stats.progressText());
  }

  // Qt draws no text at all on a busy bar, so the words go beside it. While
  // transferring the bar says everything and the label would only repeat it.
  const QString phaseText = stats.phaseText();
  ui.progress_info->setVisible(!phaseText.isEmpty());
  ui.progress_info->setStyleSheet("QLabel { color: green; font-weight: bold;}");
  ui.progress_info->setText(phaseText);

  if (stats.etaSeconds >= 0) {
    updateFinishInfo(stats.etaSeconds);
  }

  updateTransferBars(stats);
}

void JobWidget::updateTransferBars(const JobStats &stats) {
  QFontMetrics fm(ui.output->font());
  QSet<QLabel *> seen;

  // Names are elided against the width the card actually has rather than a
  // fixed 420px, so widening the window shows more of the path. Re-elided on
  // every reading, which is how a resize gets picked up.
  const int nameWidth = qMax(240, ui.details->width() / 3);

  for (const JobTransferItem &item : stats.transferring) {
    QLabel *label = nullptr;
    QProgressBar *bar = nullptr;

    auto it = mActive.find(item.name);
    if (it == mActive.end()) {
      label = new QLabel();

      bar = new QProgressBar();
      bar->setTextVisible(true);
      bar->setAlignment(Qt::AlignCenter);
      label->setBuddy(bar);

      ui.progress->addRow(label, bar);
      mActive.insert(item.name, label);
    } else {
      label = it.value();
      bar = static_cast<QProgressBar *>(label->buddy());
    }

    label->setText(fm.elidedText(item.name, Qt::ElideMiddle, nameWidth));

    const QString summary = item.progressText();

    if (item.size >= 0) {
      bar->setRange(0, 100);
      bar->setValue(item.percentage);
      bar->setFormat(FitProgressText(bar, item.progressParts()));
    } else {
      // Nothing to divide by. A busy bar carries no figure, so the byte count
      // goes on the row's label, where a filled-in 0% bar used to sit.
      bar->setRange(0, 0);
      label->setText(label->text() + QStringLiteral("  (%1)").arg(summary));
    }

    bar->setToolTip(
        QStringLiteral("Path: %1\nStats: %2").arg(item.name, summary));

    seen.insert(label);
  }

  // core/stats lists exactly what is in flight, so anything missing from this
  // reading has finished. The output-parsing path had to infer that from a
  // blank line instead.
  //
  // removeRow, not removeWidget: removeWidget empties the row but leaves it in
  // the layout, so a job that moved a few hundred files grew a card with
  // hundreds of blank rows on it.
  for (auto it = mActive.begin(); it != mActive.end();) {
    QLabel *label = it.value();
    if (seen.contains(label)) {
      ++it;
      continue;
    }
    it = mActive.erase(it);
    ui.progress->removeRow(label); // deletes the label and its bar
  }
}

void JobWidget::showDetails() { ui.showDetails->setChecked(true); }

void JobWidget::cancel() {
  if (!isRunning) {
    return;
  }

  mJobFinalStatus = "stopped";
  mStatus = "2_transfer_stopped";

  if (mRc) {
    mRc->stop();
  }

  mProcess->kill();
  mProcess->waitForFinished();

  ui.showDetails->setStyleSheet(
      "QToolButton { border: 0; color: red; font-weight: bold;}");
  ui.showDetails->setText("  Stopped");
  ui.overall->hide();
  ui.progress_info->hide();
  ui.cancel->setToolTip("Close");
  ui.cancel->setStatusTip("Close");
}

QString JobWidget::getUniqueID() { return mUniqueID; }

QString JobWidget::getRequestId() { return mRequestId; }

QString JobWidget::getTransferMode() { return mTransferMode; }

QDateTime JobWidget::getStartDateTime() { return mStartDateTime; }

void JobWidget::updateStartInfo() {
  ui.le_StartInfo->setText(
      "Started:   " +
      QLocale::system().toString(mStartDateTime, QLocale::LongFormat));
}

void JobWidget::updateFinishInfo(qint64 etaSeconds) {
  QString finishText = (etaSeconds > 0) ? "Finished (ETA):  " : "Finished:  ";

  ui.le_FinishInfo->setText(
    finishText +
    QLocale::system().toString(QDateTime::currentDateTime().addSecs(etaSeconds), QLocale::LongFormat));
}

QString JobWidget::getStatus() { return mStatus; }
