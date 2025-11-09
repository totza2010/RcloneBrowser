#include "preferences_dialog.h"
#include "utils.h"
#include <QFileDialog>
#include <QSystemTrayIcon>

PreferencesDialog::PreferencesDialog(QWidget *parent) : QDialog(parent) {

  ui.setupUi(this);

  // hide elements used for testing only

  ui.cb_font_6->hide();
  ui.cb_font_7->hide();
  ui.cb_font_8->hide();
  ui.cb_font_9->hide();

  ui.cb_button_4->hide();

  ui.cb_remote_xxl->hide();

// dark mode option for all systems but latest macOS
// on macOS Mojave or newer dark mode is managed by OS
#if defined(Q_OS_MACOS)
  QString sysInfo = QSysInfo::productVersion();
  if (sysInfo == "10.9" || sysInfo == "10.10" || sysInfo == "10.11" ||
      sysInfo == "10.12" || sysInfo == "10.13") {
  } else {
    ui.darkMode->hide();
    // ui.darkMode_info->hide();
  }
#endif

  // images as tooltips - one picture tells more...
  ui.cb_textandicon->setToolTip(
      tr("<img src=':media/images/tooltips/textandicon.png'>"));
  ui.cb_icononly->setToolTip(
      tr("<img src=':media/images/tooltips/icononly.png'>"));
  ui.cb_textonly->setToolTip(
      tr("<img src=':media/images/tooltips/textonly.png'>"));
  ui.cb_icons_colour_black->setToolTip(
      tr("<img src=':media/images/tooltips/iconsblack.png'>"));
  ui.cb_icons_colour_white->setToolTip(
      tr("<img src=':media/images/tooltips/iconswhite.png'>"));
  ui.cb_longList->setToolTip(
      tr("<img src=':media/images/tooltips/listlong.png'>"));
  ui.cb_list->setToolTip(tr("<img src=':media/images/tooltips/listwrap.png'>"));
  ui.cb_tiles->setToolTip(tr("<img src=':media/images/tooltips/tiles.png'>"));

  auto settings = GetSettings();

  // set minimumWidth based on font size
  int fontsize = 0;
  fontsize = (settings->value("Settings/fontSize").toInt());

  setMinimumWidth(minimumWidth() + (fontsize * 30));

  // lock vertical resizing
  resize(0, 0);
  adjustSize();
  resize(0, 0);
  setMaximumHeight(this->height());

  ui.rclone->setFocus(Qt::FocusReason::OtherFocusReason);

  QObject::connect(ui.rcloneBrowse, &QPushButton::clicked, this, [=]() {
    QString rclone = QFileDialog::getOpenFileName(
        this, "Select rclone executable", ui.rclone->text());
    if (rclone.isEmpty()) {
      return;
    }

    if (!QFileInfo(rclone).isExecutable()) {
      QMessageBox::critical(
          this, "Error",
          QString("File\n\n %1\n\n is not executable.").arg(rclone));
      return;
    }

    if (QFileInfo(rclone) == QFileInfo(qApp->applicationFilePath())) {
      QMessageBox::critical(this, "Error",
                            "You selected Rclone Browser executable!\nPlease "
                            "select rclone executable instead.");
      return;
    }

    ui.rclone->setText(rclone);
  });

  QObject::connect(ui.rcloneConfBrowse, &QPushButton::clicked, this, [=]() {
    QString rcloneConf = QFileDialog::getOpenFileName(
        this, "Select .rclone.conf location", ui.rcloneConf->text());
    if (rcloneConf.isEmpty()) {
      return;
    }

    ui.rcloneConf->setText(rcloneConf);
  });

  QObject::connect(ui.streamBrowse, &QPushButton::clicked, this, [=]() {
    QString stream = QFileDialog::getOpenFileName(
        this, "Select player for streaming", ui.stream->text());
    if (stream.isEmpty()) {
      return;
    }
    ui.stream->setText("\"" + stream + "\"" + " -");
  });

  QObject::connect(
      ui.defaultDownloadDirBrowse, &QPushButton::clicked, this, [=]() {
        QString defaultDownloadDir = QFileDialog::getExistingDirectory(
            this, "Select default download directory",
            ui.defaultDownloadDir->text());

        if (defaultDownloadDir.isEmpty()) {
          return;
        }

        ui.defaultDownloadDir->setText(defaultDownloadDir);
      });

  QObject::connect(
      ui.defaultUploadDirBrowse, &QPushButton::clicked, this, [=]() {
        QString defaultUploadDir = QFileDialog::getExistingDirectory(
            this, "Select default upload directory",
            ui.defaultUploadDir->text());

        if (defaultUploadDir.isEmpty()) {
          return;
        }

        ui.defaultUploadDir->setText(defaultUploadDir);
      });

  QObject::connect(ui.queueScriptBrowse, &QPushButton::clicked, this, [=]() {
    QString queueScript = QFileDialog::getOpenFileName(this, "Select script",
                                                       ui.queueScript->text());
    if (queueScript.isEmpty()) {
      return;
    }

    if (!QFileInfo(queueScript).isExecutable()) {
      QMessageBox::critical(
          this, "Error",
          QString("File\n\n %1\n\n is not executable.").arg(queueScript));
      return;
    }

    if (QFileInfo(queueScript) == QFileInfo(qApp->applicationFilePath())) {
      QMessageBox::critical(this, "Error",
                            "You selected RcloneBrowser executable!\nPlease "
                            "select your script executable instead.");
      return;
    }

    ui.queueScript->setText("\"" + queueScript + "\"");
  });

  QObject::connect(
      ui.transferOnScriptBrowse, &QPushButton::clicked, this, [=]() {
        QString transferOnScript = QFileDialog::getOpenFileName(
            this, "Select script", ui.transferOnScript->text());
        if (transferOnScript.isEmpty()) {
          return;
        }

        if (!QFileInfo(transferOnScript).isExecutable()) {
          QMessageBox::critical(this, "Error",
                                QString("File\n\n %1\n\n is not executable.")
                                    .arg(transferOnScript));
          return;
        }

        if (QFileInfo(transferOnScript) ==
            QFileInfo(qApp->applicationFilePath())) {
          QMessageBox::critical(
              this, "Error",
              "You selected RcloneBrowser executable!\nPlease "
              "select your script executable instead.");
          return;
        }
        ui.transferOnScript->setText("\"" + transferOnScript + "\"");
      });

  QObject::connect(
      ui.transferOffScriptBrowse, &QPushButton::clicked, this, [=]() {
        QString transferOffScript = QFileDialog::getOpenFileName(
            this, "Select script", ui.transferOffScript->text());
        if (transferOffScript.isEmpty()) {
          return;
        }

        if (!QFileInfo(transferOffScript).isExecutable()) {
          QMessageBox::critical(this, "Error",
                                QString("File\n\n %1\n\n is not executable.")
                                    .arg(transferOffScript));
          return;
        }

        if (QFileInfo(transferOffScript) ==
            QFileInfo(qApp->applicationFilePath())) {
          QMessageBox::critical(
              this, "Error",
              "You selected RcloneBrowser executable!\nPlease "
              "select your script executable instead.");
          return;
        }

        ui.transferOffScript->setText("\"" + transferOffScript + "\"");
      });

  QObject::connect(ui.queueRcloneRepoTest, &QPushButton::clicked, this, [=]() {
    QString repo = ui.queueRcloneRepo->text().trimmed();
    onRepoTestClicked(repo);
  });

  QObject::connect(ui.closeToTray, &QCheckBox::clicked, this, [=]() {
    if (ui.closeToTray->isChecked()) {
      ui.startMinimisedToTray->setDisabled(false);
    } else {
      ui.startMinimisedToTray->setChecked(false);
      ui.startMinimisedToTray->setDisabled(true);
    }
  });

  ui.rclone->setText(
      QDir::toNativeSeparators(settings->value("Settings/rclone").toString()));
  ui.rcloneConf->setText(QDir::toNativeSeparators(
      settings->value("Settings/rcloneConf").toString()));
  ui.stream->setText(settings->value("Settings/stream").toString());

#if defined(Q_OS_OPENBSD) || defined(Q_OS_NETBSD)
  ui.mount->setText(
      settings
          ->value("Settings/mount",
                  "* mount is not supported by rclone on this system *")
          .toString());
  ui.mount->setDisabled(true);
#else
  ui.mount->setText(settings->value("Settings/mount").toString());
#endif

  ui.defaultDownloadDir->setText(QDir::toNativeSeparators(
      settings->value("Settings/defaultDownloadDir").toString()));
  ui.defaultUploadDir->setText(QDir::toNativeSeparators(
      settings->value("Settings/defaultUploadDir").toString()));
  ui.defaultDownloadOptions->setText(
      settings->value("Settings/defaultDownloadOptions").toString());
  ui.defaultUploadOptions->setText(
      settings->value("Settings/defaultUploadOptions").toString());
  ui.defaultRcloneOptions->setText(
      settings->value("Settings/defaultRcloneOptions").toString());

  ui.checkRcloneBrowserUpdates->setChecked(
      settings->value("Settings/checkRcloneBrowserUpdates", true).toBool());
  ui.checkRcloneUpdates->setChecked(
      settings->value("Settings/checkRcloneUpdates", true).toBool());

  if (QSystemTrayIcon::isSystemTrayAvailable()) {
    ui.alwaysShowInTray->setChecked(
        settings->value("Settings/alwaysShowInTray", false).toBool());
    ui.closeToTray->setChecked(
        settings->value("Settings/closeToTray", false).toBool());
    ui.startMinimisedToTray->setChecked(
        settings->value("Settings/startMinimisedToTray", false).toBool());

    if (!(settings->value("Settings/closeToTray", false).toBool())) {
      ui.startMinimisedToTray->setChecked(false);
      ui.startMinimisedToTray->setDisabled(true);
    }

    ui.notifyFinishedTransfers->setChecked(
        settings->value("Settings/notifyFinishedTransfers", true).toBool());
  } else {
    ui.alwaysShowInTray->setChecked(false);
    ui.alwaysShowInTray->setDisabled(true);
    ui.closeToTray->setChecked(false);
    ui.closeToTray->setDisabled(true);
    ui.startMinimisedToTray->setChecked(false);
    ui.startMinimisedToTray->setDisabled(true);
    ui.notifyFinishedTransfers->setChecked(false);
    ui.notifyFinishedTransfers->setDisabled(true);
  }
  ui.cb_soundNotif->setChecked(
      settings->value("Settings/soundNotif", true).toBool());

  ui.showFolderIcons->setChecked(
      settings->value("Settings/showFolderIcons", true).toBool());
  ui.showFileIcons->setChecked(
      settings->value("Settings/showFileIcons", true).toBool());
  ui.rowColors->setChecked(
      settings->value("Settings/rowColors", false).toBool());
  ui.showHidden->setChecked(
      settings->value("Settings/showHidden", true).toBool());

  ui.darkMode->setChecked(settings->value("Settings/darkMode", true).toBool());

  ui.rememberLastOptions->setChecked(
      settings->value("Settings/rememberLastOptions", true).toBool());

  if ((settings->value("Settings/buttonStyle").toString()) == "icononly") {
    ui.cb_icononly->setChecked(true);
  } else {
    if (settings->value("Settings/buttonStyle").toString() == "textonly") {
      ui.cb_textonly->setChecked(true);
    } else {
      ui.cb_textandicon->setChecked(true);
    }
  }

  if ((settings->value("Settings/iconsLayout").toString()) == "list") {
    ui.cb_list->setChecked(true);
  } else {
    if ((settings->value("Settings/iconsLayout").toString()) == "tiles") {
      ui.cb_tiles->setChecked(true);
    } else {
      ui.cb_longList->setChecked(true);
    }
  }

  if ((settings->value("Settings/iconsColour").toString()) == "white") {
    ui.cb_icons_colour_white->setChecked(true);
  } else {
    ui.cb_icons_colour_black->setChecked(true);
  }

  QString fontSize = settings->value("Settings/fontSize").toString();
  if (fontSize == "9") {
    ui.cb_font_9->setChecked(true);
  } else {
    if (fontSize == "8") {
      ui.cb_font_8->setChecked(true);
    } else {
      if (fontSize == "7") {
        ui.cb_font_7->setChecked(true);
      } else {
        if (fontSize == "6") {
          ui.cb_font_6->setChecked(true);
        } else {
          if (fontSize == "5") {
            ui.cb_font_5->setChecked(true);
          } else {
            if (fontSize == "4") {
              ui.cb_font_4->setChecked(true);
            } else {
              if (fontSize == "3") {
                ui.cb_font_3->setChecked(true);
              } else {
                if (fontSize == "2") {
                  ui.cb_font_2->setChecked(true);
                } else {
                  if (fontSize == "1") {
                    ui.cb_font_1->setChecked(true);
                  } else {
                    ui.cb_font_0->setChecked(true);
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  QString buttonSize = settings->value("Settings/buttonSize").toString();
  if (buttonSize == "4") {
    ui.cb_button_4->setChecked(true);
  } else {
    if (buttonSize == "3") {
      ui.cb_button_3->setChecked(true);
    } else {
      if (buttonSize == "2") {
        ui.cb_button_2->setChecked(true);
      } else {
        if (buttonSize == "1") {
          ui.cb_button_1->setChecked(true);
        } else {
          ui.cb_button_0->setChecked(true);
        }
      }
    }
  }

  if ((settings->value("Settings/iconSize").toString()) == "S") {
    ui.cb_remote_s->setChecked(true);
  } else {
    if (settings->value("Settings/iconSize").toString() == "L") {
      ui.cb_remote_l->setChecked(true);
    } else {
      if (settings->value("Settings/iconSize").toString() == "XL") {
        ui.cb_remote_xl->setChecked(true);
      } else {
        if (settings->value("Settings/iconSize").toString() == "XXL") {
          ui.cb_remote_xxl->setChecked(true);
        } else {
          ui.cb_remote_m->setChecked(true);
        }
      }
    }
  }

  ui.info_2->setText(
      "See rclone <a "
      "href=\"https://github.com/rclone/rclone/blob/master/docs/content/"
      "faq.md#can-i-use-rclone-with-an-http-proxy\">FAQ</a> for details.");
  ui.info_2->setTextFormat(Qt::RichText);
  ui.info_2->setTextInteractionFlags(Qt::TextBrowserInteraction);
  ui.info_2->setOpenExternalLinks(true);

  if (settings->value("Settings/useProxy").toBool()) {
    ui.useProxy->setChecked(true);
  } else {
    ui.useSystemSettings->setChecked(true);
  }
  ui.http_proxy->setText(settings->value("Settings/http_proxy").toString());
  ui.https_proxy->setText(settings->value("Settings/https_proxy").toString());
  ui.no_proxy->setText(settings->value("Settings/no_proxy").toString());

  ui.cb_preemptiveLoading->setChecked(
      settings->value("Settings/preemptiveLoading", true).toBool());
  if ((settings->value("Settings/preemptiveLoadingLevel").toString()) == "2") {
    ui.rb_preemptiveLoading_2->setChecked(true);
  } else {
    if (settings->value("Settings/preemptiveLoadingLevel").toString() == "1") {
      ui.rb_preemptiveLoading_1->setChecked(true);
    } else {
      ui.rb_preemptiveLoading_0->setChecked(true);
    }
  }

  ui.queueScript->setText(QDir::toNativeSeparators(
      settings->value("Settings/queueScript").toString()));
  ui.transferOnScript->setText(QDir::toNativeSeparators(
      settings->value("Settings/transferOnScript").toString()));
  ui.transferOffScript->setText(QDir::toNativeSeparators(
      settings->value("Settings/transferOffScript").toString()));

  ui.queueScriptRun->setChecked(
      settings->value("Settings/queueScriptRun", true).toBool());
  ui.jobStartScriptRun->setChecked(
      settings->value("Settings/jobStartScriptRun", true).toBool());
  ui.jobLastFinishedScriptRun->setChecked(
      settings->value("Settings/jobLastFinishedScriptRun", true).toBool());

  ui.queueRcloneRepo->setText(
      settings->value("Settings/queueRcloneRepo").toString());
  ui.queueRcloneRepoUse->setChecked(
      settings->value("Settings/queueRcloneRepoUse", true).toBool());
  ui.queueRcloneRepoTest->setIcon(
      QIcon(":media/images/qbutton_icons/refresh.png"));
  ui.queueRcloneRepoTest->setIconSize(QSize(16, 16));
  ui.queueRcloneRepoTest->setToolButtonStyle(Qt::ToolButtonIconOnly);

  if (ui.queueScript->text().trimmed().isEmpty()) {
    ui.queueScriptRun->setEnabled(false);
  } else {
    ui.queueScriptRun->setEnabled(true);
  }
  if (ui.transferOnScript->text().trimmed().isEmpty()) {
    ui.jobStartScriptRun->setEnabled(false);
  } else {
    ui.jobStartScriptRun->setEnabled(true);
  }
  if (ui.transferOffScript->text().trimmed().isEmpty()) {
    ui.jobLastFinishedScriptRun->setEnabled(false);
  } else {
    ui.jobLastFinishedScriptRun->setEnabled(true);
  }

  if (ui.queueRcloneRepo->text().trimmed().isEmpty()) {
    ui.queueRcloneRepoUse->setEnabled(false);
    ui.queueRcloneRepoUse->setChecked(false);
  } else {
    ui.queueRcloneRepoUse->setEnabled(true);
    ui.queueRcloneRepoUse->setChecked(true);
  }

  QObject::connect(ui.queueScript, &QLineEdit::textChanged, this, [=]() {
    if (ui.queueScript->text().trimmed().isEmpty()) {
      ui.queueScriptRun->setEnabled(false);
      ui.queueScriptRun->setChecked(false);
    } else {
      ui.queueScriptRun->setEnabled(true);
    }
  });

  QObject::connect(ui.transferOnScript, &QLineEdit::textChanged, this, [=]() {
    if (ui.transferOnScript->text().trimmed().isEmpty()) {
      ui.jobStartScriptRun->setEnabled(false);
      ui.jobStartScriptRun->setChecked(false);
    } else {
      ui.jobStartScriptRun->setEnabled(true);
    }
  });

  QObject::connect(ui.transferOffScript, &QLineEdit::textChanged, this, [=]() {
    if (ui.transferOffScript->text().trimmed().isEmpty()) {
      ui.jobLastFinishedScriptRun->setEnabled(false);
      ui.jobLastFinishedScriptRun->setChecked(false);
    } else {
      ui.jobLastFinishedScriptRun->setEnabled(true);
    }
  });
}

PreferencesDialog::~PreferencesDialog() {}

QString PreferencesDialog::getRclone() const {
  return QDir::fromNativeSeparators(ui.rclone->text());
}

QString PreferencesDialog::getRcloneConf() const {
  return QDir::fromNativeSeparators(ui.rcloneConf->text());
}

QString PreferencesDialog::getStream() const { return ui.stream->text(); }

QString PreferencesDialog::getMount() const { return ui.mount->text(); }

QString PreferencesDialog::getDefaultDownloadDir() const {
  return QDir::fromNativeSeparators(ui.defaultDownloadDir->text());
}

QString PreferencesDialog::getDefaultUploadDir() const {
  return QDir::fromNativeSeparators(ui.defaultUploadDir->text());
}

QString PreferencesDialog::getDefaultDownloadOptions() const {
  return ui.defaultDownloadOptions->text();
}

QString PreferencesDialog::getDefaultUploadOptions() const {
  return ui.defaultUploadOptions->text();
}

QString PreferencesDialog::getDefaultRcloneOptions() const {
  return ui.defaultRcloneOptions->text();
}

bool PreferencesDialog::getCheckRcloneBrowserUpdates() const {
  return ui.checkRcloneBrowserUpdates->isChecked();
}

bool PreferencesDialog::getCheckRcloneUpdates() const {
  return ui.checkRcloneUpdates->isChecked();
}

bool PreferencesDialog::getAlwaysShowInTray() const {
  return ui.alwaysShowInTray->isChecked();
}

bool PreferencesDialog::getStartMinimisedToTray() const {
  return ui.startMinimisedToTray->isChecked();
}

bool PreferencesDialog::getCloseToTray() const {
  return ui.closeToTray->isChecked();
}

bool PreferencesDialog::getNotifyFinishedTransfers() const {
  return ui.notifyFinishedTransfers->isChecked();
}

bool PreferencesDialog::getSoundNotif() const {
  return ui.cb_soundNotif->isChecked();
}

bool PreferencesDialog::getShowFolderIcons() const {
  return ui.showFolderIcons->isChecked();
}

bool PreferencesDialog::getShowFileIcons() const {
  return ui.showFileIcons->isChecked();
}

bool PreferencesDialog::getRowColors() const {
  return ui.rowColors->isChecked();
}

bool PreferencesDialog::getShowHidden() const {
  return ui.showHidden->isChecked();
}

QString PreferencesDialog::getButtonStyle() const {
  if (ui.cb_icononly->isChecked()) {
    return "icononly";
  } else {
    if (ui.cb_textandicon->isChecked()) {
      return "textandicon";
    } else {
      return "textonly";
    }
  }
}

QString PreferencesDialog::getIconsLayout() const {
  if (ui.cb_list->isChecked()) {
    return "list";
  }
  if (ui.cb_longList->isChecked()) {
    return "longlist";
  }
  return "tiles";
}

QString PreferencesDialog::getIconsColour() const {
  if (ui.cb_icons_colour_white->isChecked()) {
    return "white";
  } else {
    return "black";
  }
}

QString PreferencesDialog::getFontSize() const {
  if (ui.cb_font_1->isChecked()) {
    return "1";
  }
  if (ui.cb_font_2->isChecked()) {
    return "2";
  }
  if (ui.cb_font_3->isChecked()) {
    return "3";
  }
  if (ui.cb_font_4->isChecked()) {
    return "4";
  }
  if (ui.cb_font_5->isChecked()) {
    return "5";
  }
  if (ui.cb_font_6->isChecked()) {
    return "6";
  }
  if (ui.cb_font_7->isChecked()) {
    return "7";
  }
  if (ui.cb_font_8->isChecked()) {
    return "8";
  }
  if (ui.cb_font_9->isChecked()) {
    return "9";
  }
  return "0";
}

QString PreferencesDialog::getButtonSize() const {
  if (ui.cb_button_1->isChecked()) {
    return "1";
  }
  if (ui.cb_button_2->isChecked()) {
    return "2";
  }
  if (ui.cb_button_3->isChecked()) {
    return "3";
  }
  if (ui.cb_button_4->isChecked()) {
    return "4";
  }
  return "0";
}

QString PreferencesDialog::getIconSize() const {
  if (ui.cb_remote_s->isChecked()) {
    return "S";
  } else {
    if (ui.cb_remote_l->isChecked()) {
      return "L";
    } else {
      if (ui.cb_remote_xl->isChecked()) {
        return "XL";
      } else {
        if (ui.cb_remote_xxl->isChecked()) {
          return "XXL";
        } else {
          return "M";
        }
      }
    }
  }
}

QString PreferencesDialog::getHttpProxy() const {
  return ui.http_proxy->text();
}

QString PreferencesDialog::getHttpsProxy() const {
  return ui.https_proxy->text();
}

QString PreferencesDialog::getNoProxy() const { return ui.no_proxy->text(); }

bool PreferencesDialog::getUseProxy() const {
  if (ui.useSystemSettings->isChecked()) {
    return false;
  } else {
    return true;
  }
}

bool PreferencesDialog::getPreemptiveLoading() const {
  return ui.cb_preemptiveLoading->isChecked();
}

bool PreferencesDialog::getDarkMode() const { return ui.darkMode->isChecked(); }

bool PreferencesDialog::getRememberLastOptions() const {
  return ui.rememberLastOptions->isChecked();
}

QString PreferencesDialog::getPreemptiveLoadingLevel() const {
  if (ui.rb_preemptiveLoading_2->isChecked()) {
    return "2";
  } else {
    if (ui.rb_preemptiveLoading_1->isChecked()) {
      return "1";
    } else {
      return "0";
    }
  }
}

QString PreferencesDialog::getQueueScript() const {
  return ui.queueScript->text();
}

QString PreferencesDialog::getTransferOnScript() const {
  return ui.transferOnScript->text();
}

QString PreferencesDialog::getTransferOffScript() const {
  return ui.transferOffScript->text();
}

bool PreferencesDialog::getQueueScriptRun() const {
  return ui.queueScriptRun->isChecked();
}

bool PreferencesDialog::getQueueRcloneRepoUse() const {
  return ui.queueRcloneRepoUse->isChecked();
}

QString PreferencesDialog::getQueueRcloneRepo() const {
  return ui.queueRcloneRepo->text();
}

bool PreferencesDialog::getJobStartScriptRun() const {
  return ui.jobStartScriptRun->isChecked();
}

bool PreferencesDialog::getJobLastFinishedScriptRun() const {
  return ui.jobLastFinishedScriptRun->isChecked();
}

void PreferencesDialog::onRepoTestClicked(const QString &repo) {
  logDialog = new QDialog(this);
  logDialog->setWindowTitle("Rclone Repo Check");
  layout = new QVBoxLayout(logDialog);

  logEdit = new QTextEdit(logDialog);
  logEdit->setReadOnly(true);
  layout->addWidget(logEdit);

  logEdit->append(QString("⏳ Checking repository %1 ...").arg(repo));
  logDialog->setLayout(layout);
  logDialog->resize(500, 300);
  logDialog->show();

  ui.queueRcloneRepoUse->setEnabled(false);
  ui.queueRcloneRepoUse->setChecked(false);

  QRegularExpression re("^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$");
  if (!re.match(repo).hasMatch()) {
    logEdit->append("<p>❌ Invalid format. Use owner/repo (e.g. <b>tgdrive/rclone</b>).</p>");
    return;
  }

  checkRcloneRepo(repo);
}

void PreferencesDialog::checkRcloneRepo(const QString &repo) {
  auto manager = new QNetworkAccessManager(this);
  QString repoApi = QString("https://api.github.com/repos/%1").arg(repo);

  connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply *reply) {
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
      logEdit->append("❌ Request failed: " + reply->errorString());
      return;
    }

    QJsonParseError jsonError;
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &jsonError);
    if (jsonError.error != QJsonParseError::NoError) {
      logEdit->append("❌ Invalid JSON response.");
      return;
    }

    QJsonObject repoObj = doc.object();
    QString branch = repoObj.value("default_branch").toString();

    checkRcloneCoreFiles(repo, branch, repoObj);
  });
  manager->get(QNetworkRequest(QUrl(repoApi)));
}

void PreferencesDialog::checkRcloneCoreFiles(const QString &repo, const QString &branch, const QJsonObject &repoObj) {
  auto manager = new QNetworkAccessManager(this);
  QStringList mustHaveFiles = { "rclone.1", "rclone.go", "librclone/librclone.go" };
  auto haveFiles = std::make_shared<int>(0);

  auto checkFile = [this, repo, branch, repoObj, manager, haveFiles](const QString &file) {
    QString fileUrl = QString("https://api.github.com/repos/%1/contents/%2?ref=%3")
                        .arg(repo, file, branch);

    QNetworkReply *reply = manager->get(QNetworkRequest(QUrl(fileUrl)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, repo, repoObj, file, haveFiles]() {
      reply->deleteLater();
      if (reply->error() == QNetworkReply::NoError) {
        logEdit->append(QString("✅ <b>%1</b> Core file detected, fetching latest release...").arg(file));
        (*haveFiles)++;
        if (*haveFiles == 3) {
          logEdit->append("✅ All core files detected.");
          checkRcloneRelease(repo, repoObj);
        }
        return;
      }
      logEdit->append(QString("❌ <b>%1</b> Core file not detected.").arg(file));
      return;
    });
  };

  for (const QString &file : mustHaveFiles) {
    checkFile(file);
  }
}

void PreferencesDialog::checkRcloneRelease(const QString &repo, const QJsonObject &repoObj) {
  auto manager = new QNetworkAccessManager(this);
  QString releaseApi = QString("https://api.github.com/repos/%1/releases/latest").arg(repo);
  QNetworkReply *reply = manager->get(QNetworkRequest(QUrl(releaseApi)));

  connect(reply, &QNetworkReply::finished, this, [=]() {
    reply->deleteLater();

    QJsonParseError jsonError;
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &jsonError);
    if (jsonError.error != QJsonParseError::NoError) {
      logEdit->append("✅ Repository detected, but no release found.");
      showRepoSummary(repoObj, QJsonObject());
      return;
    }
    showRepoSummary(repoObj, doc.object());
  });
}

void PreferencesDialog::showRepoSummary(const QJsonObject &repoObj, const QJsonObject &releaseObj) {
  QString fullName = repoObj.value("full_name").toString();
  QString description = repoObj.value("description").toString();
  QString stars = QString::number(repoObj.value("stargazers_count").toInt());
  QString forks = QString::number(repoObj.value("forks_count").toInt());
  QString updatedAt = repoObj.value("pushed_at").toString();

  QString tag = releaseObj.value("tag_name").toString("(no release)");
  QString published = releaseObj.value("published_at").toString();
  QString releaseUrl = releaseObj.value("html_url").toString();

  QString summary = QString(
    "<p><b>Name:        </b> <a href='%8'>%1</a><br>"
    "<b>Description:    </b> %2<br>"
    "<b>Latest Release: </b> %3<br>"
    "<b>Published:      </b> %4<br>"
    "<b>Last Updated:   </b> %7<br>"
    "<b>Stars:          </b> ⭐ %5 &nbsp;&nbsp; <b>Forks:</b> 🔁 %6</p>"
  ).arg(fullName, description, tag, published, stars, forks, updatedAt, releaseUrl);
  
  if (releaseObj.isEmpty()) {
      logEdit->append("✅ Repository detected, but no release found.");
  } else {
      logEdit->append("✅ Repository and release detected.");
  }

  okButton = new QPushButton("OK", logDialog);
  layout->addWidget(okButton);
  
  connect(okButton, &QPushButton::clicked, this, [=]() {
    ui.summaryRepo->setText(summary);
    logDialog->accept();
    logDialog->deleteLater();
  });

  ui.queueRcloneRepoUse->setEnabled(true);
  ui.queueRcloneRepoUse->setChecked(true);
}