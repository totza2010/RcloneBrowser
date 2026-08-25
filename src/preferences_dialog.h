#pragma once

#include "ui_preferences_dialog.h"

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

  // Diagnostics. Turning the log on takes effect at once rather than at the
  // next start, so that ticking the box and reproducing the problem is one
  // step; see DebugLog::setEnabledForNextRun().
  bool getDebugLog() const;
  int getLogMaxFileKb() const;
  int getLogKeepFiles() const;

  QString getQueueScript() const;
  QString getTransferOnScript() const;
  QString getTransferOffScript() const;

  bool getQueueScriptRun() const;
  bool getJobStartScriptRun() const;
  bool getJobLastFinishedScriptRun() const;

private:
  Ui::PreferencesDialog ui;

  // Which repository to watch for rclone updates is worked out from the
  // binary now (DetectRcloneRepo), so there is nothing here to type in and
  // nothing to validate against GitHub.
  void showDetectedRepo();
};
