#include "api_auth.h"
#include "utils.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

// ---- keys -------------------------------------------------------------------

QString ScopeName(Scope scope) {
  switch (scope) {
  case Scope::Read:
    return QStringLiteral("read");
  case Scope::Control:
    return QStringLiteral("control");
  case Scope::Write:
    return QStringLiteral("write");
  case Scope::Admin:
    return QStringLiteral("admin");
  default:
    return QStringLiteral("none");
  }
}

Scope ScopeNamed(const QString &name) {
  for (Scope scope :
       {Scope::Read, Scope::Control, Scope::Write, Scope::Admin}) {
    if (ScopeName(scope) == name) {
      return scope;
    }
  }
  return Scope::None;
}

Keys &Keys::instance() {
  static Keys keys;
  return keys;
}

void Keys::load() {
  if (mLoaded) {
    return;
  }
  mLoaded = true;
  mKeys.clear();
  mHashes.clear();

  const QJsonArray stored =
      QJsonDocument::fromJson(
          GetSettings()->value("Settings/apiKeys").toString().toUtf8())
          .array();
  for (const QJsonValue &value : stored) {
    const QJsonObject object = value.toObject();
    Key key;
    key.id = object.value(QStringLiteral("id")).toString();
    key.name = object.value(QStringLiteral("name")).toString();
    key.scope = ScopeNamed(object.value(QStringLiteral("scope")).toString());
    key.created = QDateTime::fromString(
        object.value(QStringLiteral("created")).toString(), Qt::ISODate);
    key.lastUsed = QDateTime::fromString(
        object.value(QStringLiteral("lastUsed")).toString(), Qt::ISODate);
    const QByteArray hash = QByteArray::fromBase64(
        object.value(QStringLiteral("hash")).toString().toLatin1());
    // One that cannot be read is dropped rather than half believed: a key
    // with no level must not come out as a key with every level.
    if (key.id.isEmpty() || hash.isEmpty() || key.scope == Scope::None) {
      continue;
    }
    mKeys.append(key);
    mHashes.insert(key.id, hash);
  }
}

void Keys::save() {
  QJsonArray stored;
  for (const Key &key : mKeys) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), key.id);
    object.insert(QStringLiteral("name"), key.name);
    object.insert(QStringLiteral("scope"), ScopeName(key.scope));
    object.insert(QStringLiteral("created"), key.created.toString(Qt::ISODate));
    object.insert(QStringLiteral("lastUsed"),
                  key.lastUsed.isValid() ? key.lastUsed.toString(Qt::ISODate)
                                         : QString());
    object.insert(QStringLiteral("hash"),
                  QString::fromLatin1(mHashes.value(key.id).toBase64()));
    stored.append(object);
  }
  GetSettings()->setValue(
      "Settings/apiKeys",
      QString::fromUtf8(QJsonDocument(stored).toJson(QJsonDocument::Compact)));
  GetSettings()->sync();
  mLastSaved = QDateTime::currentDateTime();
}

QList<Key> Keys::list() {
  load();
  return mKeys;
}

QString Keys::create(const QString &name, Scope scope, Key *made,
                     QString *why) {
  load();
  const auto refuse = [why](const QString &reason) {
    if (why != nullptr) {
      *why = reason;
    }
    return QString();
  };

  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty() || trimmed.size() > 40) {
    return refuse(QStringLiteral(
        "a key needs a name, of up to 40 characters, that says what it is for"));
  }
  for (const Key &key : mKeys) {
    if (key.name.compare(trimmed, Qt::CaseInsensitive) == 0) {
      // Names are what the history says started a run, so two keys with one
      // name would make that line mean either.
      return refuse(QStringLiteral("there is already a key called \"%1\"")
                        .arg(key.name));
    }
  }
  if (scope == Scope::None) {
    return refuse(QStringLiteral(
        "the level has to be one of read, control, write or admin"));
  }

  Key key;
  do {
    key.id = QString::fromLatin1(randomBytes(4).toHex());
  } while (mHashes.contains(key.id));
  key.name = trimmed;
  key.scope = scope;
  key.created = QDateTime::currentDateTime();

  const QByteArray secret = randomBytes(32).toBase64(
      QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
  mKeys.append(key);
  mHashes.insert(key.id, QCryptographicHash::hash(secret, QCryptographicHash::Sha256));
  save();

  if (made != nullptr) {
    *made = key;
  }
  return QStringLiteral("rbk_%1_%2").arg(key.id, QString::fromLatin1(secret));
}

bool Keys::remove(const QString &id) {
  load();
  for (int i = 0; i < mKeys.size(); ++i) {
    if (mKeys.at(i).id == id) {
      mKeys.removeAt(i);
      mHashes.remove(id);
      save();
      return true;
    }
  }
  return false;
}

bool Keys::accepts(const QString &presented, Key *key) {
  // "rbk_" + eight hex + "_" + the secret. The id says which key to compare
  // with, so a request costs one hash, not one per key.
  if (!presented.startsWith(QLatin1String("rbk_")) || presented.size() < 14 ||
      presented.at(12) != QLatin1Char('_')) {
    return false;
  }
  load();
  const QString id = presented.mid(4, 8);
  const auto found = mHashes.constFind(id);
  if (found == mHashes.constEnd()) {
    return false;
  }
  const QByteArray offered = QCryptographicHash::hash(
      presented.mid(13).toLatin1(), QCryptographicHash::Sha256);
  if (!sameBytes(offered, found.value())) {
    return false;
  }

  for (Key &stored : mKeys) {
    if (stored.id == id) {
      stored.lastUsed = QDateTime::currentDateTime();
      // Written down at most once a minute. "Last used" is for telling a
      // key nobody uses any more from one in use, and a write to the
      // settings on every request would be paying for precision nobody
      // reads.
      if (!mLastSaved.isValid() || mLastSaved.secsTo(stored.lastUsed) >= 60) {
        save();
      }
      if (key != nullptr) {
        *key = stored;
      }
      return true;
    }
  }
  return false;
}

void Keys::forgetAll() {
  mKeys.clear();
  mHashes.clear();
  mLoaded = true;
  save();
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
