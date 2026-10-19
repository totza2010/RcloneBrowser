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

#include <QString>
#include <QStringList>

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

} // namespace ProgramSettings
