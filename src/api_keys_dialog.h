#pragma once

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;

// The page's "Keys for other programs", in the window: the same
// ApiAuth::Keys, made and taken back the same way. A key is shown once,
// when it is made -- it is kept only as a hash.
class ApiKeysDialog : public QDialog {
  Q_OBJECT

public:
  explicit ApiKeysDialog(QWidget *parent = nullptr);

private:
  void draw();
  void make();
  void takeBack();

  QTableWidget *mTable;
  QLineEdit *mName;
  QComboBox *mScope;
  QLineEdit *mMade;
  QLabel *mMadeNote;
};
