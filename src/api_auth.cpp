#include "api_auth.h"
#include "utils.h"

#include <QCryptographicHash>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QSettings>

namespace {

// The cost of turning a password into a hash. Chosen so that one attempt
// takes long enough to be useless in bulk and short enough that nobody
// notices signing in. Written into the stored value rather than assumed, so
// this can be raised later without locking anybody out.
const int kIterations = 120000;

const int kKeyBytes = 32;
const int kSaltBytes = 16;

QByteArray randomBytes(int count) {
  QByteArray bytes(count, Qt::Uninitialized);
  QRandomGenerator::system()->generate(bytes.begin(), bytes.end());
  return bytes;
}

// Compared without stopping at the first difference. Bailing out early tells
// whoever is guessing how much of their guess was right, which turns a search
// of every possible hash into a search of one byte at a time.
bool sameBytes(const QByteArray &a, const QByteArray &b) {
  if (a.size() != b.size()) {
    return false;
  }
  quint8 difference = 0;
  for (int i = 0; i < a.size(); ++i) {
    difference |= quint8(a.at(i)) ^ quint8(b.at(i));
  }
  return difference == 0;
}

QByteArray derive(const QString &password, const QByteArray &salt,
                  int iterations) {
  return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256,
                                            password.toUtf8(), salt,
                                            iterations, kKeyBytes);
}

} // namespace

namespace ApiAuth {

QString HashPassword(const QString &password) {
  const QByteArray salt = randomBytes(kSaltBytes);
  const QByteArray key = derive(password, salt, kIterations);

  return QStringLiteral("pbkdf2-sha256$%1$%2$%3")
      .arg(kIterations)
      .arg(QString::fromLatin1(salt.toBase64()))
      .arg(QString::fromLatin1(key.toBase64()));
}

bool PasswordMatches(const QString &password, const QString &stored) {
  // Both checked, and not merged into one condition: an empty stored value
  // must be a refusal, never a match against an empty password.
  if (stored.isEmpty() || password.isEmpty()) {
    return false;
  }

  const QStringList parts = stored.split(QLatin1Char('$'));
  if (parts.size() != 4 || parts.at(0) != QLatin1String("pbkdf2-sha256")) {
    return false; // a stored value we cannot read is not one we can accept
  }

  bool readable = false;
  const int iterations = parts.at(1).toInt(&readable);
  if (!readable || iterations <= 0) {
    return false;
  }

  const QByteArray salt = QByteArray::fromBase64(parts.at(2).toLatin1());
  const QByteArray expected = QByteArray::fromBase64(parts.at(3).toLatin1());
  if (salt.isEmpty() || expected.isEmpty()) {
    return false;
  }

  return sameBytes(derive(password, salt, iterations), expected);
}

QString StoredPassword() {
  return GetSettings()->value("Settings/apiPasswordHash").toString();
}

bool HasPassword() { return !StoredPassword().isEmpty(); }

void SetPassword(const QString &password) {
  if (password.isEmpty()) {
    GetSettings()->remove(QStringLiteral("Settings/apiPasswordHash"));
  } else {
    GetSettings()->setValue("Settings/apiPasswordHash", HashPassword(password));
  }
  GetSettings()->sync();

  // Whoever was signed in with the old password is signed out. A password
  // change that leaves the old sessions alive changes nothing for whoever
  // the change was aimed at.
  Sessions::instance().closeAll();
}

QString UserName() {
  return GetSettings()
      ->value("Settings/apiUser", QStringLiteral("admin"))
      .toString();
}

// ---- sessions -------------------------------------------------------------

Sessions &Sessions::instance() {
  static Sessions sessions;
  return sessions;
}

int Sessions::idleMinutes() {
  const int stored =
      GetSettings()->value("Settings/apiSessionMinutes", 720).toInt();
  return stored > 0 ? stored : 720;
}

QString Sessions::open() {
  const QString session =
      QString::fromLatin1(randomBytes(32).toHex());
  mSessions.insert(session, QDateTime::currentDateTime());
  return session;
}

bool Sessions::accepts(const QString &session) {
  if (session.isEmpty()) {
    return false;
  }

  const auto found = mSessions.find(session);
  if (found == mSessions.end()) {
    return false;
  }

  if (found.value().secsTo(QDateTime::currentDateTime()) >
      qint64(idleMinutes()) * 60) {
    mSessions.erase(found);
    return false;
  }

  found.value() = QDateTime::currentDateTime();
  return true;
}

void Sessions::close(const QString &session) { mSessions.remove(session); }

void Sessions::closeAll() { mSessions.clear(); }

// ---- guessing -------------------------------------------------------------

Attempts &Attempts::instance() {
  static Attempts attempts;
  return attempts;
}

bool Attempts::isLockedOut() const {
  return mLockedUntil.isValid() &&
         mLockedUntil > QDateTime::currentDateTime();
}

int Attempts::secondsRemaining() const {
  if (!isLockedOut()) {
    return 0;
  }
  return int(QDateTime::currentDateTime().secsTo(mLockedUntil)) + 1;
}

void Attempts::failed() {
  mFailures += 1;
  if (mFailures < 5) {
    return;
  }

  // A minute after the fifth, doubling, up to a quarter of an hour. Long
  // enough that guessing is hopeless; short enough that somebody who simply
  // mistyped is not shut out for the evening.
  const int steps = qMin(mFailures - 5, 4);
  const int seconds = qMin(60 * (1 << steps), 900);
  mLockedUntil = QDateTime::currentDateTime().addSecs(seconds);
}

void Attempts::succeeded() {
  mFailures = 0;
  mLockedUntil = QDateTime();
}

void Attempts::forget() { succeeded(); }

} // namespace ApiAuth
