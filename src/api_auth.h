#pragma once

// Core (L0): who is allowed to ask the API anything.
//
// There were two ways in and both were the same secret: the token the daemon
// prints. That is right for a script and wrong for a person -- it cannot be
// remembered, cannot be chosen, and cannot be changed without restarting the
// daemon and re-telling every client.
//
// So: an administrator password, and sessions handed out to whoever proves
// they know it. The token stays exactly as it was, for curl and for anything
// that runs unattended.
//
// Kept apart from the server because none of it is about HTTP: hashing a
// password, telling whether one matches, and deciding when a session has gone
// stale can all be tested without opening a socket, and this is the code where
// being wrong is worst. See docs/WEB-UI.md.

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>

namespace ApiAuth {

// How a stored password looks: "pbkdf2-sha256$<iterations>$<salt>$<hash>",
// the two parts base64. Written down in the settings, so the format carries
// its own algorithm and cost -- raising the cost later must not lock out
// everybody whose password was hashed with the old one.
QString HashPassword(const QString &password);

// Whether the password matches what was stored. False for an empty stored
// value, always: "no password set" must never mean "any password will do".
bool PasswordMatches(const QString &password, const QString &stored);

// Whether an administrator password has been set at all.
bool HasPassword();

QString StoredPassword();
void SetPassword(const QString &password); // empty clears it
QString UserName();                        // "admin" unless changed

// The people currently signed in. In memory only: a daemon that restarts has
// forgotten everyone, which is the behaviour to want -- a session that
// outlives the process it belongs to is a session nobody can revoke.
class Sessions {
public:
  static Sessions &instance();

  // A new session for whoever just proved they know the password.
  QString open();

  // Whether this is a live session, and if it is, it is kept alive: somebody
  // still using the page has not gone away. Expired ones are dropped here,
  // which is why there is no timer.
  bool accepts(const QString &session);

  void close(const QString &session);
  void closeAll(); // when the password changes, everyone signs in again

  int count() const { return mSessions.size(); }

  // How long a session lives without being used.
  static int idleMinutes();

private:
  QHash<QString, QDateTime> mSessions;
};

// How many failed attempts in a row before this stops answering, and for how
// long. A password somebody can guess at a thousand times a second is a
// password of about twelve bits, whatever the user typed.
class Attempts {
public:
  static Attempts &instance();

  bool isLockedOut() const;
  int secondsRemaining() const;

  void failed();
  void succeeded();

  void forget(); // for tests

private:
  int mFailures = 0;
  QDateTime mLockedUntil;
};

// ---- keys -------------------------------------------------------------------
//
// The token can do everything, which is right for the person who owns the
// machine and wrong to hand to another program: giving it away is giving
// away the house key, and taking it back means changing it for everybody.
//
// A key has a name, says how much it may do, and can be taken back on its
// own. The levels go up in steps, each including the one before:
//
//   read     look at everything, follow events, read logs
//   control  and run, queue, stop, switch the queue and schedules on and off
//   write    and make, change and delete tasks and schedules, and change
//            files on remotes
//   admin    and stop the program, change the password, manage keys --
//            what the token and a signed-in administrator can do
//
// Which level each route needs is in ApiRoutes, beside the route.
enum class Scope { None = 0, Read = 1, Control = 2, Write = 3, Admin = 4 };

QString ScopeName(Scope scope);
// None for a word that is not one of the four.
Scope ScopeNamed(const QString &name);

struct Key {
  QString id;   // eight hex characters, also the start of the key itself
  QString name; // what it is for: "sonarr", "home assistant"
  Scope scope = Scope::Read;
  QDateTime created;
  QDateTime lastUsed; // invalid until it has been
};

// The keys there are. Kept in the settings as their names, levels and a
// hash of the secret -- never the secret itself, which is shown once, when
// the key is made, and then exists only wherever it was pasted.
//
// The secret is 32 random bytes, so it is hashed with a plain SHA-256 rather
// than the slow derivation a password gets: slowness defends a guessable
// secret, and this one is not guessable, while every request has to check
// it.
class Keys {
public:
  static Keys &instance();

  QList<Key> list();

  // Makes a key. Returns the key as it is to be used --
  // "rbk_<id>_<secret>" -- or an empty string with *why when the name is
  // empty, too long or taken, or the level is not one.
  QString create(const QString &name, Scope scope, Key *made, QString *why);

  bool remove(const QString &id);

  // The key this is, or false. A key that is used is noted as used.
  bool accepts(const QString &presented, Key *key);

  void forgetAll(); // for tests

private:
  void load();
  void save();

  bool mLoaded = false;
  QList<Key> mKeys;
  QHash<QString, QByteArray> mHashes; // id -> hash of the secret
  QDateTime mLastSaved;
};

} // namespace ApiAuth
