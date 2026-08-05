#pragma once

// Core (L0): the flags the rclone on this machine actually accepts -- see
// docs/ARCHITECTURE.md. Must not depend on QtWidgets.
//
// The options fields are free text, so getting a flag slightly wrong -- an
// underscore for a hyphen, a name that moved between releases, a backend flag
// that only exists on one fork -- fails at run time with rclone's own error,
// after the job has already been set up.
//
// Rather than shipping a list that goes stale, this asks the binary. Which
// means a teldrive build offers its --teldrive-* flags and a mainline one does
// not, without either knowing about the other.

#include <QList>
#include <QObject>
#include <QString>

struct RcloneFlag {
  QString name;        // "--checkers"
  QString shortName;   // "-c", empty for most
  QString type;        // "int", "SizeSuffix", "HARD|SOFT|CAUTIOUS"; empty = bool
  QString description; // the one-line help text, including any "(default ...)"
  QString group;       // "Copy", "Backend", "Filter", ...

  // Boolean flags stand alone; everything else needs a value after it.
  bool takesValue() const { return !type.isEmpty(); }

  // What to put in the line when this flag is picked: a boolean is complete on
  // its own, anything else wants the cursor left after an "=".
  QString insertion() const;
};

// Parses the output of "rclone help flags".
//
// The layout is two-space-indented flag lines under "(flag group X):"
// headers. Lines before the first header describe the help command itself and
// are skipped, which is why the group header is what starts the parse rather
// than the indentation.
QList<RcloneFlag> ParseRcloneHelpFlags(const QByteArray &output);

// Which repository a build came from, for the update check.
//
// Nothing in the binary says. Both forks report the same Go module path --
// tgdrive/rclone never renamed it, so "go version -m" on either gives
// "github.com/rclone/rclone" -- and "rclone version" prints the same seven
// lines with the same upstream version numbering. Measured on both, not
// assumed; see V-16.
//
// What does differ is the backends they carry: the teldrive backend exists
// only in the fork, and its flags come back in the list above. So this reads
// what the build can do and infers where it came from, which is an inference
// and is offered as a default the user can still override.
//
// Empty when the flag list has not arrived, so that "not known yet" is never
// mistaken for "stock rclone".
QString DetectRcloneRepo(const QList<RcloneFlag> &flags);

// Asks rclone for its flags once per process and hands the list out.
//
// The query costs about a tenth of a second and the answer cannot change
// while the binary stays the same, so it is done on first use and kept. A
// failure is left uncached: completion simply does not appear, and the next
// attempt tries again.
class RcloneFlagRegistry : public QObject {
  Q_OBJECT

public:
  static RcloneFlagRegistry &instance();

  // Empty until the query lands. Callers should also connect to flagsChanged.
  const QList<RcloneFlag> &flags() const { return mFlags; }
  bool isLoaded() const { return !mFlags.isEmpty(); }

  // Starts the query if it has not run yet. Safe to call repeatedly.
  void ensureLoaded();

  // Forgets the answer, for when the configured rclone binary changes.
  void invalidate();

signals:
  void flagsChanged();

private:
  explicit RcloneFlagRegistry(QObject *parent = nullptr);

  QList<RcloneFlag> mFlags;
  bool mInFlight = false;
};
