#pragma once

#include <QDialog>
#include <QList>

class QCheckBox;
class QLineEdit;
class QRadioButton;
class QTimeEdit;

// The page's New backup, in the window: the same questions, answered into
// the same BackupPlan, made into a task and a schedule by the same
// Backup::Build and Backup::Save. See backup_plan.h.
class BackupDialog : public QDialog {
  Q_OBJECT

public:
  explicit BackupDialog(QWidget *parent = nullptr);

private:
  void make();

  QLineEdit *mSource;
  QLineEdit *mDest;
  QRadioButton *mMirror;
  QCheckBox *mVersions;
  QList<QCheckBox *> mDays;
  QTimeEdit *mTime;
  QLineEdit *mName;
};
