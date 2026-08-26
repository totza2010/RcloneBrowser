#include "progress_dialog.h"
#include "running_job.h"
#include "utils.h"

ProgressDialog::ProgressDialog(const QString &title, const QString &operation,
                               const QString &message, QProcess *process,
                               QWidget *parent, bool close, bool trim,
                               QString toolTip)
    : QDialog(parent) {
  QPushButton *cancelButton = buildChrome(title, operation, message, toolTip);

  QObject::connect(cancelButton, &QPushButton::clicked, this, [=]() {
    if (mIsRunning) {
      process->kill();
    }
  });

  QObject::connect(process,
                   static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
                       &QProcess::finished),
                   this, [=](int code, QProcess::ExitStatus status) {
                     reportEnd(cancelButton,
                               status == QProcess::NormalExit && code == 0,
                               close);
                   });

  QObject::connect(process, &QProcess::readyRead, this, [=]() {
    appendOutput(QString::fromUtf8(process->readAll()), trim);
  });

  process->setProcessChannelMode(QProcess::MergedChannels);
  process->start(QIODevice::ReadOnly);
}

// The same window, watching a job the registry is running rather than a
// process of its own. One path for starting rclone means these commands get
// a history row and a log file like every other job; what is left here is
// showing what it says. See docs/LAYER-SPLIT.md block 5.
ProgressDialog::ProgressDialog(const QString &title, const QString &operation,
                               const QString &message, RunningJob *job,
                               QWidget *parent, bool close, bool trim,
                               QString toolTip)
    : QDialog(parent) {
  QPushButton *cancelButton = buildChrome(title, operation, message, toolTip);

  QObject::connect(cancelButton, &QPushButton::clicked, this, [=]() {
    if (mIsRunning) {
      job->stop();
    }
  });

  QObject::connect(job, &RunningJob::finished, this,
                   [=](JobState state) {
                     reportEnd(cancelButton, state == JobState::Finished,
                               close);
                   });

  QObject::connect(job, &RunningJob::outputLine, this,
                   [=](const QString &line) { appendOutput(line + "\n", trim); });

  // A job that has already ended before this window was built -- rclone can
  // refuse in a few milliseconds -- would otherwise sit here saying
  // "Running..." for ever.
  if (!job->isRunning()) {
    reportEnd(cancelButton, job->state() == JobState::Finished, close);
  }
}

void ProgressDialog::appendOutput(QString output, bool trim) {
  if (trim) {
    output = output.trimmed();
  }
  ui.output->appendPlainText(output);
  emit outputAvailable(output);
}

void ProgressDialog::reportEnd(QPushButton *cancelButton, bool ok, bool close) {
  mIsRunning = false;
  cancelButton->setText("&Close");

  if (ok) {
    if (mIconsColour == "white") {
      ui.labelOperation->setStyleSheet("QLabel { font-weight: bold; }");
    } else {
      ui.labelOperation->setStyleSheet(
          "QLabel { color: black; font-weight: bold; }");
    }
    ui.labelOperation->setText("Finished ");

    if (close) {
      emit accept();
    }
  } else {
    ui.labelOperation->setStyleSheet("QLabel { color: red; font-weight: bold; }");
    ui.labelOperation->setText("Error ");

    ui.buttonShowOutput->setChecked(true);
    ui.buttonBox->setEnabled(true);
  }
}

QPushButton *ProgressDialog::buildChrome(const QString &title,
                                         const QString &operation,
                                         const QString &message,
                                         const QString &toolTip) {
  ui.setupUi(this);

  // remove window close button
  setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                 Qt::WindowMinMaxButtonsHint);

  resize(0, 0);

  setWindowTitle("Rclone Browser - " + title);

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

  if (!toolTip.isEmpty()) {
    QString toolTipPlacer =
        QString("<html><head/><body><p>%1</p></body></html>").arg(toolTip);
    ui.frame->setToolTip(tr(toolTipPlacer.toLocal8Bit().constData()));
  }

  ui.labelOperation->setText(operation);

  ui.labelOperation->setStyleSheet(
      "QLabel { color: green; font-weight: bold; }");

  ui.info->setText(message);
  ui.info->setCursorPosition(0);

  // ui.output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

  // apply font size preferences
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

  // icons style
  mIconsColour = settings->value("Settings/iconsColour").toString();
  const QString iconsColour = mIconsColour;

  QString img_add = "";

  if (iconsColour == "white") {
    img_add = "_inv";
  }

  ui.output->setVisible(false);

  // set default arrow
  ui.buttonShowOutput->setIcon(
      QIcon(":media/images/qbutton_icons/vrightarrow" + img_add + ".png"));
  ui.buttonShowOutput->setIconSize(QSize(24, 24));

  QPushButton *cancelButton =
      ui.buttonBox->addButton("&Cancel", QDialogButtonBox::RejectRole);

  QObject::connect(ui.buttonBox, &QDialogButtonBox::rejected, this,
                   &QDialog::reject);

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

  return cancelButton;
}

ProgressDialog::~ProgressDialog() {}

void ProgressDialog::expand() { ui.buttonShowOutput->setChecked(true); }

void ProgressDialog::allowToClose() { ui.buttonBox->setEnabled(true); }
//
// QString ProgressDialog::getOutput() const
//{
//    return ui.output->toPlainText();
//}

void ProgressDialog::closeEvent(QCloseEvent *ev) {
  ev->ignore();
  return;
}
