#pragma once

// Orchestration (L1): the remotes rclone knows about.
//
// The list existed only as rows in ui.remotes, built inside the same lambda
// that chose an icon size and a dark-mode suffix -- so "which remotes are
// there, and of what type" could not be answered without a window, and the
// parsing of rclone's output sat in the middle of forty lines about icons.
// See docs/API.md S5.

#include <QObject>
#include <QProcess>
#include <QString>

struct Remote {
  QString name;
  QString type;

  bool isValid() const { return !name.isEmpty(); }
};

// Reads the output of "rclone listremotes --long", which is one
// "name: type" per line.
//
// A line it cannot read is skipped rather than taken apart wrongly: a remote
// shown with the wrong type gets the wrong icon and the wrong capabilities.
QList<Remote> ParseListRemotes(const QByteArray &output);

// Why "rclone listremotes" did not produce a list.
//
// Worth telling apart because what the person has to do differs: type a
// password, upgrade rclone, or go and look at the path. The window used to
// read rclone's stderr for this itself, inside the same lambda that chose an
// icon size -- so the rule could not be checked without an encrypted config
// or an ancient rclone binary to hand, which is to say it was never checked.
enum class ListFailure {
  Failed,           // something else; the message is rclone's own
  PasswordRequired, // the configuration file is encrypted
  TooOld,           // this rclone predates "listremotes"
};

ListFailure ClassifyListRemotesFailure(const QString &standardError);

class RemoteRegistry : public QObject {
  Q_OBJECT

public:
  static RemoteRegistry &instance();

  const QList<Remote> &remotes() const { return mRemotes; }
  int count() const { return mRemotes.size(); }

  // The remote by that name, or nullptr. Accepts the trailing colon that
  // appears everywhere a remote is used as a path ("gdrive:"), because that
  // is how callers usually have it.
  const Remote *find(const QString &name) const;

  // Asks rclone again. The answer arrives through refreshed() or failed();
  // nothing blocks, because this can take a moment when the config file is
  // encrypted or a backend is slow to load.
  void refresh();

  // Replaces the list without asking rclone. For tests, and for a caller that
  // already has the output in its hand.
  void setRemotes(const QList<Remote> &remotes);

signals:
  void refreshed();

  // rclone could not be asked, or said something unusable. The message is
  // rclone's own, already trimmed.
  void failed(const QString &reason);

  // rclone wants the configuration password. Nothing here can ask for it --
  // that needs a window, or a variable set before starting -- so it is passed
  // on to whoever can.
  void passwordRequired();

  // rclone is old enough not to have "listremotes". Told apart from an
  // ordinary failure because the answer is different: upgrade, rather than
  // check the path. Reading rclone's stderr for this used to be the window's
  // job, which meant the answer existed only where there was a window.
  void tooOld();

private:
  explicit RemoteRegistry(QObject *parent = nullptr);

  QList<Remote> mRemotes;
  QProcess *mProcess = nullptr;
};
