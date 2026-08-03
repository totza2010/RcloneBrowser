#pragma once

// Core (L0): rclone invocation and settings access -- see docs/ARCHITECTURE.md.
// Must not declare or depend on anything from QtWidgets. The recursive
// QSettings <-> widget binding lives in widget_settings.h.

std::unique_ptr<QSettings> GetSettings();

bool IsPortableMode();

QString GetRclone();
void SetRclone(const QString &rclone);

QStringList GetRcloneConf();
void SetRcloneConf(const QString &rcloneConf);

void UseRclonePassword(QProcess *process);
void SetRclonePassword(const QString &rclonePassword);

QStringList GetDefaultOptionsList(const QString &settingsOptions);
QStringList GetRemoteModeRcloneOptions();
QStringList GetShowHidden();
QStringList GetRcloneCmd(const QStringList &args);

QDir GetConfigDir(void);

unsigned int compareVersion(std::string, std::string);
