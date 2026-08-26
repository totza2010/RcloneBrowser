#pragma once

#include "ui_progress_dialog.h"

class ProgressDialog : public QDialog {
  Q_OBJECT

public:
  ProgressDialog(const QString &title, const QString &operation,
                 const QString &message, QProcess *process,
                 QWidget *parent = nullptr, bool close = true,
                 bool trim = false, QString toolTip = "");

  // The same window watching a job the registry is running. Preferred for
  // anything new: a job goes through one path, so it gets a history row and
  // a log file, and can be stopped from somewhere other than this window.
  // See docs/LAYER-SPLIT.md block 5.
  ProgressDialog(const QString &title, const QString &operation,
                 const QString &message, class RunningJob *job,
                 QWidget *parent = nullptr, bool close = true,
                 bool trim = false, QString toolTip = "");
  ~ProgressDialog();

  void expand();
  void allowToClose();

signals:
  void outputAvailable(const QString &output) const;

private:
  void closeEvent(QCloseEvent *ev) override;

  // The window itself -- titles, fonts, the show-output button, the sizing
  // rules. Shared by both constructors; returns the cancel button, which is
  // the one piece each of them wires differently.
  class QPushButton *buildChrome(const QString &title, const QString &operation,
                                 const QString &message,
                                 const QString &toolTip);

  void appendOutput(QString output, bool trim);
  void reportEnd(class QPushButton *cancelButton, bool ok, bool close);

  Ui::ProgressDialog ui;
  QString mIconsColour;
  int mWidth;
  int mMinimumWidth;
  int mMinimumHeight;
  int mHeight;
  bool mIsRunning = true;
};
