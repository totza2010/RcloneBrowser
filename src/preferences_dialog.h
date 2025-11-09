#pragma once

#include "ui_preferences_dialog.h"
#include <QTextEdit>

class PreferencesDialog : public QDialog {
  Q_OBJECT

public:
  PreferencesDialog(QWidget *parent = nullptr);
  ~PreferencesDialog();

  QString getRclone() const;
  QString getRcloneConf() const;
  QString getStream() const;
  QString getMount() const;
  QString getDefaultDownloadDir() const;
  QString getDefaultUploadDir() const;
  QString getDefaultDownloadOptions() const;
  QString getDefaultUploadOptions() const;
  QString getDefaultRcloneOptions() const;

  bool getCheckRcloneBrowserUpdates() const;
  bool getCheckRcloneUpdates() const;

  bool getAlwaysShowInTray() const;
  bool getCloseToTray() const;
  bool getStartMinimisedToTray() const;
  bool getNotifyFinishedTransfers() const;
  bool getSoundNotif() const;

  bool getShowFolderIcons() const;
  bool getShowFileIcons() const;
  bool getRowColors() const;
  bool getShowHidden() const;

  bool getDarkMode() const;

  bool getRememberLastOptions() const;

  QString getButtonStyle() const;
  QString getIconsLayout() const;
  QString getIconsColour() const;

  QString getFontSize() const;
  QString getButtonSize() const;
  QString getIconSize() const;

  bool getUseProxy() const;
  QString getHttpProxy() const;
  QString getHttpsProxy() const;
  QString getNoProxy() const;

  bool getPreemptiveLoading() const;
  QString getPreemptiveLoadingLevel() const;

  QString getQueueScript() const;
  QString getTransferOnScript() const;
  QString getTransferOffScript() const;

  bool getQueueScriptRun() const;
  bool getJobStartScriptRun() const;
  bool getJobLastFinishedScriptRun() const;

  bool getQueueRcloneRepoUse() const;
  QString getQueueRcloneRepo() const;
  QDialog *logDialog = nullptr;
  QVBoxLayout *layout = nullptr;
  QTextEdit *logEdit = nullptr;
  QPushButton *okButton = nullptr;
  
private:
  Ui::PreferencesDialog ui;
  void showRepoSummary(const QJsonObject &repoObj, const QJsonObject &releaseObj);
  void checkRcloneRelease(const QString &repo, const QJsonObject &repoObj);
  void checkRcloneCoreFiles(const QString &repo, const QString &branch, const QJsonObject &repoObj);
  void checkRcloneRepo(const QString &repo);
  void onRepoTestClicked(const QString &repo);
};
