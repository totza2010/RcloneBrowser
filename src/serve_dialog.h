#pragma once

#include "remote_ops.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

// Asks how a folder of a remote is to be offered to other programs: the
// same questions the page's Serve form asks, answered into the same
// RemoteOps::ServeOptions. See RemoteOps::Serve().
class ServeDialog : public QDialog {
  Q_OBJECT

public:
  ServeDialog(const QString &what, QWidget *parent = nullptr);

  RemoteOps::ServeOptions options() const;

private:
  QComboBox *mProtocol;
  QSpinBox *mPort;
  QCheckBox *mEveryone;
  QCheckBox *mReadOnly;
  QLineEdit *mUser;
  QLineEdit *mPassword;
};
