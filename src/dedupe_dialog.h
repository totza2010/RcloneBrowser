#pragma once

#include "ui_dedupe_dialog.h"

class DedupeDialog : public QDialog {
  Q_OBJECT

public:
  DedupeDialog(const QString &remote, const QDir &path,
               const QString &remoteType, QWidget *parent = nullptr);
  ~DedupeDialog();

  //  QString getSource() const;
  //  bool isCheck() const;
  QStringList getOptions() const;
  bool isDryRun() const;

  // The two things the dialog decides, apart from each other, so the command
  // can be put together in one place rather than here. See remote_ops.h.
  QString mode() const;
  QStringList extra() const;

private:
  Ui::DedupeDialog ui;
  QString mTarget;
  bool mDryRun = false;

  void done(int r) override;
};
