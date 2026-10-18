#pragma once

// GUI: a remote's settings, in the window -- the same as the page's.
//
// The page could see, make, change, rename and delete remotes before the
// window could, which is the wrong way round for the rule both are held to
// (whatever one can do, the other can). This is the window's half, on the
// same core: RcloneConfigRegistry for what a backend is made of,
// DescribeSettings for what may be shown, and CreateRemote, UpdateRemote,
// RenameRemote, DeleteRemote and ContinueRemote for every change. Nothing
// here talks to rclone itself.
//
// One dialog for both a new remote (an empty name) and an existing one.
// The fields are made from rclone's description of the backend, so a
// fork's backend gets a form as well. See docs/REMOTE-CONFIG-UI.md.

#include "rclone_config.h"

#include <QDialog>
#include <QJsonObject>
#include <QList>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;

class RemoteSettingsDialog : public QDialog {
  Q_OBJECT

public:
  // `remote` is the remote to show; empty to make a new one.
  explicit RemoteSettingsDialog(const QString &remote,
                                QWidget *parent = nullptr);

private:
  struct Field {
    ProviderOption option;
    QWidget *input = nullptr;
    QString before;   // what it shows now: the value, or empty
    bool wasSet = false;
  };

  void whenBackendsKnown();
  void readRemote();
  void chooseType();
  void buildForm(const Provider &provider, const QJsonArray &current);
  QWidget *makeInput(const ProviderOption &option, const QString &value,
                     bool isSet);
  QJsonObject changedSettings() const;
  void save();
  void finished(const RemoteChange &change);
  void showQuestion(const QJsonObject &question);
  void answer();
  void deleteRemote(bool evenIfTasksUseIt);
  void setBusy(bool busy);

  QString mRemote;   // as it is called now; empty while it does not exist
  QString mType;
  bool mCreating = false;

  QStackedWidget *mPages = nullptr;
  QLineEdit *mName = nullptr;
  QComboBox *mKind = nullptr;
  QLabel *mAbout = nullptr;
  QGroupBox *mSignIn = nullptr;
  QLabel *mAuthorize = nullptr;
  QPlainTextEdit *mToken = nullptr;
  QWidget *mFieldsBox = nullptr;
  QFormLayout *mForm = nullptr;
  QGroupBox *mAdvanced = nullptr;
  QFormLayout *mAdvancedForm = nullptr;
  QLabel *mError = nullptr;

  QLabel *mQuestionName = nullptr;
  QLabel *mQuestionHelp = nullptr;
  QLabel *mQuestionError = nullptr;
  QComboBox *mQuestionChoice = nullptr;
  QLineEdit *mQuestionText = nullptr;

  QPushButton *mSave = nullptr;
  QPushButton *mDelete = nullptr;
  QPushButton *mClose = nullptr;

  QList<Field> mFields;
  QJsonObject mQuestion;
};
