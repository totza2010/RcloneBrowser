#pragma once

// L3: completion for the free-text fields in Preferences.
//
// The flag list itself comes from L0 (rclone_flags.h), which asks the
// configured rclone what it accepts. This file is only about getting either
// list in front of the cursor.

#include <QLineEdit>
#include <QPlainTextEdit>

// Completes rclone flags in a field that holds a whole options string.
//
// Completion applies to the word under the cursor rather than the field, so
// "--transfers=8 --check" offers flags for the second word and leaves the
// first alone. Nothing appears until the word starts with a dash, which keeps
// the popup out of the way when a value is being typed.
//
// Preferences holds its options on one line; the transfer and mount dialogs
// give them a box, where flags are often one per line. Both behave the same
// way, and in the box a word never spans a line break.
void InstallRcloneFlagCompleter(QLineEdit *edit);
void InstallRcloneFlagCompleter(QPlainTextEdit *edit);

// Completes filesystem paths as they are typed.
//
// Every path field here has a Browse button beside it, but a dialog is a poor
// way to reach a path you already know: it opens somewhere else and takes
// several clicks to get back. Typing is faster when the field helps.
enum class PathKind { AnyFile, Directory };
void InstallPathCompleter(QLineEdit *edit, PathKind kind);
