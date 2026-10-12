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

} // namespace ApiAuth
