#pragma once

// Core (L0): the program's own settings that the window and the page both
// change -- how long a stream may sit unplayed, and which folders of this
// machine the API may use. One place, so the two cannot disagree about where
// a value lives or what it may be.
//
// Each can also be set by the environment (RB_STREAM_IDLE_MINUTES,
// RB_LOCAL_ROOTS), which wins: that is how a container is configured. A
// value the environment sets is shown, and refused for change here -- it has
// to be changed where it is set.

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace ProgramSettings {

// Minutes a stream may sit unplayed before it stops itself; 0 for never.
int StreamIdleMinutes();
bool StreamIdleFromEnvironment();
bool SetStreamIdleMinutes(int minutes, QString *why);

// The folders of this machine the API may use, as written (one each).
// Empty: every folder -- the API is not held to any.
QStringList LocalRoots();
bool LocalRootsFromEnvironment();
// Refused when one is not a folder that is there.
bool SetLocalRoots(const QStringList &roots, QString *why);

// How much rclone writes into a transfer's log: ERROR, NOTICE, INFO (what
// was copied -- the default, and what -v always gave) or DEBUG (everything,
// -vv). RB_RCLONE_LOG_LEVEL sets it in a container.
QStringList RcloneLogLevels();
QString RcloneLogLevel();
bool RcloneLogLevelFromEnvironment();
bool SetRcloneLogLevel(const QString &level, QString *why);
// The flags for it, as a transfer is given them. -q / -v / -vv rather than
// --log-level: rclone refuses --log-level beside a -v, and a task's own extra
// options may hold one.
QStringList RcloneLogLevelFlags();

// The rest of the window's Preferences that are about the program rather
// than about how the window looks: rclone and its options, the default
// folders, the program's own log, the scripts. Listed once, with what each
// may be, so the page can show and change the same keys the window does
// (2026-10-21: the page had three of them, under its API tab).
struct Preference {
  QString name;     // as the API and the page call it
  QString key;      // where it is kept
  QString group;    // rclone, folders, log, scripts
  QString kind;     // text, flags, bool, number, choice, folder, file,
                    // rclone (a file called rclone*), command
  QString label;
  QString help;
  QStringList choices; // kind choice
  int min = 0;         // kind number
  int max = 0;
  // Set by the environment, which wins: shown, and refused for change.
  QString environment;
};

QList<Preference> Preferences();
// Empty name when there is no such one.
Preference FindPreference(const QString &name);
QVariant PreferenceValue(const Preference &preference);
bool PreferenceFromEnvironment(const Preference &preference);
// Checked against what it may be, kept, and put into effect where it can
// be now (rclone's path, the proxies, the log); the rest from the next run
// that reads it. A text-like value comes as a string, bool as a bool, a
// number as a number.
bool SetPreference(const QString &name, const QVariant &value, QString *why);
// The proxies, as the settings say now, given to every rclone started from
// here on.
void ApplyProxies();

} // namespace ProgramSettings
