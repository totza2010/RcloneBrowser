#pragma once

// Core (L0): rclone invocation and settings access -- see docs/ARCHITECTURE.md.
// Must not declare or depend on anything from QtWidgets. The recursive
// QSettings <-> widget binding lives in widget_settings.h.
//
// Self-contained on purpose: the GUI build force-includes pch.h into every
// translation unit, which used to hide the missing includes here. The tests
// link rbcore without that header.

#include <QDir>
#include <QSettings>
#include <QString>
#include <QStringList>

#include <memory>
#include <string>

class QProcess;

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

// Replaces the value of any credential-bearing argument with a placeholder.
// Every path that shows, copies, logs or transmits a command line must go
// through this: the free-form option fields let users pass backend tokens
// such as --drive-token or --sftp-pass. See docs/ARCHITECTURE.md section 5.
QStringList RedactArgs(const QStringList &args);

// Random alphanumeric string for the per-mount rclone remote-control login.
QString GenerateRcCredential(int length);

// Passes the remote-control login through the environment instead of the
// command line, where any local process could read it out of the process list.
// rclone maps --rc-user/--rc-pass to RCLONE_RC_USER/RCLONE_RC_PASS, for both
// the server ("rcd", "mount --rc") and the client ("rc").
// Call after UseRclonePassword(), which replaces the whole environment.
void UseRcCredentials(QProcess *process, const QString &user,
                      const QString &pass);

QDir GetConfigDir(void);

unsigned int compareVersion(std::string, std::string);
