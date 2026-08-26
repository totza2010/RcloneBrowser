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

// Splits a line of rclone options the way every field in this program that
// takes one has always split it: on spaces that are not inside double
// quotes, with the quotes then removed.
//
// This regular expression was written out nine times -- in the check,
// dedupe, mount and folder dialogs, in the task options, in the script
// runner, in the window twice, and here -- and had already drifted into two
// spellings. Nine copies of a rule about quoting is nine chances for
// "--exclude "My Films"" to mean something different depending on which box
// it was typed into. See docs/LAYER-SPLIT.md block 5.
QStringList SplitRcloneOptions(const QString &options);

QStringList GetDefaultOptionsList(const QString &settingsOptions);
QStringList GetRemoteModeRcloneOptions();
QStringList GetShowHidden();
QStringList GetRcloneCmd(const QStringList &args);

// Replaces the value of any credential-bearing argument with a placeholder.
// Every path that shows, copies, logs or transmits a command line must go
// through this: the free-form option fields let users pass backend tokens
// such as --drive-token or --sftp-pass. See docs/ARCHITECTURE.md section 5.
QStringList RedactArgs(const QStringList &args);

// Masks a credential wherever it appears in a line of rclone output. At -vv
// rclone echoes the values it picked up from the environment, including the
// remote-control password, so the raw line cannot be shown or logged as-is.
QString RedactOutputLine(const QString &line, const QString &rcUser,
                         const QString &rcPass);

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
