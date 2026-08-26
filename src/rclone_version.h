#pragma once

// Core (L0): which rclone this is.
//
// Reading "rclone version" was done inline in MainWindow, with the string
// surgery -- strip "rclone v", strip "-DEV", take the first line unless there
// is only one -- written out in the middle of a lambda that also raised
// message boxes and worked out macOS bundle paths. So "what rclone is this"
// could only be asked by something with a window, and could not be tested at
// all.
//
// It matters more than it used to. More than one build is commonly installed
// -- the official rclone and a fork such as tgdrive -- and they differ in
// which backends exist, so "which one answered" is part of explaining almost
// anything. See docs/RCLONE-MANAGER.md.

#include <QString>

struct RcloneVersion {
  // "1.71.1". Empty when the output could not be read, which is the only
  // "invalid" this has: a version that is present but unexpected is kept.
  QString number;

  // The lines rclone prints after the version, already stripped of the
  // leading "- ". Shown in the status bar; kept as text because their shape
  // is rclone's business and changes between releases.
  QString osLine;
  QString goLine;

  // What was printed, in full, for a log or a bug report.
  QString raw;

  bool isValid() const { return !number.isEmpty(); }

  // Whether this is at least the given version, compared field by field so
  // that 1.10 counts as newer than 1.9. An unreadable version is never
  // "at least" anything -- refusing to guess is what stops a feature being
  // offered on a build that cannot do it.
  bool atLeast(const QString &wanted) const;
};

// Reads the output of "rclone version".
//
// Handles the one-line form very old builds print, and the "-DEV" and "-beta"
// suffixes a build from source carries -- those are the ones on this machine
// as often as not, and dropping the suffix is what makes the number
// comparable.
RcloneVersion ParseRcloneVersion(const QByteArray &output);
