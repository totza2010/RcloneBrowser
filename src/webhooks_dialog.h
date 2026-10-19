#pragma once

#include <QDialog>

class QLabel;
class QTableWidget;

// The window's list of webhooks: the same Webhooks the page's API tab
// manages, with the same add, change, remove and test. See webhooks.h.
class WebhooksDialog : public QDialog {
  Q_OBJECT

public:
  explicit WebhooksDialog(QWidget *parent = nullptr);

private:
  void draw();
  QString chosenId() const;
  void edit(const QString &id);

  QTableWidget *mTable;
  QLabel *mNote;
};
