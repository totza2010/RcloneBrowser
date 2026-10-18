#include "api_auth.h"
#include "utils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

// Who is allowed in. This is the code where being wrong is worst, and none of
// it is about HTTP -- so it is tested here, without a socket in sight.
//
// See docs/WEB-UI.md.
class TestApiAuth : public QObject {
  Q_OBJECT

private:
  static QString appDir() {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return QCoreApplication::applicationDirPath();
#else
    return QDir(QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME")))
        .filePath("rclone-browser");
#endif
  }

  static QString iniPath() {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return QDir(appDir()).filePath(
        QFileInfo(QCoreApplication::applicationFilePath()).baseName() + ".ini");
#else
    return QDir(appDir()).filePath("rclone-browser.ini");
#endif
  }

  std::unique_ptr<QTemporaryDir> mScratch;

private slots:
  void initTestCase() {
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    mScratch.reset(new QTemporaryDir);
    QVERIFY(mScratch->isValid());
    qputenv("XDG_CONFIG_HOME", mScratch->path().toLocal8Bit());
    QVERIFY(QDir().mkpath(appDir()));
#endif
    QFile ini(iniPath());
    QVERIFY(ini.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ini.close();
    QVERIFY2(IsPortableMode(), "portable mode did not take effect");
  }

  void cleanupTestCase() { QFile::remove(iniPath()); }

  // ---- keys -----------------------------------------------------------------

  void aKeyOpensOnlyAsItself() {
    ApiAuth::Keys::instance().forgetAll();
    ApiAuth::Key made;
    QString why;
    const QString key = ApiAuth::Keys::instance().create(
        QStringLiteral("sonarr"), ApiAuth::Scope::Control, &made, &why);
    QVERIFY2(!key.isEmpty(), qPrintable(why));
    QVERIFY(key.startsWith(QStringLiteral("rbk_") + made.id + QLatin1Char('_')));
    QVERIFY(key.size() > 50);

    ApiAuth::Key found;
    QVERIFY(ApiAuth::Keys::instance().accepts(key, &found));
    QCOMPARE(found.name, QStringLiteral("sonarr"));
    QCOMPARE(found.scope, ApiAuth::Scope::Control);
    QVERIFY(found.lastUsed.isValid());

    // One character different, the right id with nothing after it, an id
    // that is not one: none of them.
    QString wrong = key;
    wrong[wrong.size() - 1] = wrong.at(wrong.size() - 1) == QLatin1Char('A')
                                  ? QLatin1Char('B')
                                  : QLatin1Char('A');
    QVERIFY(!ApiAuth::Keys::instance().accepts(wrong, nullptr));
    QVERIFY(!ApiAuth::Keys::instance().accepts(key.left(13), nullptr));
    QVERIFY(!ApiAuth::Keys::instance().accepts(
        QStringLiteral("rbk_00000000_") + key.mid(13), nullptr));
    QVERIFY(!ApiAuth::Keys::instance().accepts(QString(), nullptr));

    // The secret is not what is kept.
    const QString stored =
        GetSettings()->value("Settings/apiKeys").toString();
    QVERIFY(stored.contains(QStringLiteral("sonarr")));
    QVERIFY(!stored.contains(key.mid(13)));

    // Taken back, it opens nothing.
    QVERIFY(ApiAuth::Keys::instance().remove(made.id));
    QVERIFY(!ApiAuth::Keys::instance().accepts(key, nullptr));
    QVERIFY(!ApiAuth::Keys::instance().remove(made.id));
  }

  void aKeyNeedsANameOfItsOwnAndALevel() {
    ApiAuth::Keys::instance().forgetAll();
    QString why;
    QVERIFY(ApiAuth::Keys::instance()
                .create(QStringLiteral("  "), ApiAuth::Scope::Read, nullptr, &why)
                .isEmpty());
    QVERIFY(ApiAuth::Keys::instance()
                .create(QStringLiteral("x"), ApiAuth::Scope::None, nullptr, &why)
                .isEmpty());
    QVERIFY(!ApiAuth::Keys::instance()
                 .create(QStringLiteral("home"), ApiAuth::Scope::Read, nullptr, &why)
                 .isEmpty());
    // The history says which key started a run, so one name is one key.
    QVERIFY(ApiAuth::Keys::instance()
                .create(QStringLiteral("HOME"), ApiAuth::Scope::Write, nullptr, &why)
                .isEmpty());
    QVERIFY2(why.contains(QStringLiteral("already")), qPrintable(why));
    QCOMPARE(ApiAuth::ScopeNamed(QStringLiteral("write")), ApiAuth::Scope::Write);
    QCOMPARE(ApiAuth::ScopeNamed(QStringLiteral("root")), ApiAuth::Scope::None);
    ApiAuth::Keys::instance().forgetAll();
  }

  void init() {
    ApiAuth::SetPassword(QString());
    ApiAuth::Attempts::instance().forget();
    ApiAuth::Sessions::instance().closeAll();
  }

  // The password itself must not be recoverable from what is stored. This is
  // the whole point of hashing it, and the one thing worth checking first.
  void whatIsStoredIsNotThePassword() {
    const QString stored = ApiAuth::HashPassword(QStringLiteral("hunter2hunter2"));

    QVERIFY(!stored.contains(QStringLiteral("hunter2")));
    QVERIFY(stored.startsWith(QStringLiteral("pbkdf2-sha256$")));

    // And it says which algorithm and how many rounds made it, so raising the
    // cost later does not lock out everybody hashed with the old one.
    QCOMPARE(stored.split(QLatin1Char('$')).size(), 4);
  }

  // Two people who choose the same password must not have the same stored
  // value: one cracked hash would otherwise be every account with that
  // password, here and everywhere else the same list is used.
  void thesamePasswordHashesDifferentlyEachTime() {
    const QString first = ApiAuth::HashPassword(QStringLiteral("the same one"));
    const QString second = ApiAuth::HashPassword(QStringLiteral("the same one"));

    QVERIFY(first != second);
    QVERIFY(ApiAuth::PasswordMatches(QStringLiteral("the same one"), first));
    QVERIFY(ApiAuth::PasswordMatches(QStringLiteral("the same one"), second));
  }

  void aPasswordMatchesOnlyItself() {
    const QString stored = ApiAuth::HashPassword(QStringLiteral("correct horse"));

    QVERIFY(ApiAuth::PasswordMatches(QStringLiteral("correct horse"), stored));
    QVERIFY(!ApiAuth::PasswordMatches(QStringLiteral("correct hors"), stored));
    QVERIFY(!ApiAuth::PasswordMatches(QStringLiteral("Correct horse"), stored));
    QVERIFY(!ApiAuth::PasswordMatches(QStringLiteral(""), stored));
  }

  // The one that matters most: with nothing stored, nothing gets in. An empty
  // stored value compared against an empty password must not read as a match,
  // which is exactly how a system ends up open to whoever asks first.
  void nothingStoredLetsNobodyIn() {
    QVERIFY(!ApiAuth::HasPassword());
    QVERIFY(!ApiAuth::PasswordMatches(QString(), QString()));
    QVERIFY(!ApiAuth::PasswordMatches(QStringLiteral("anything"), QString()));
  }

  // A stored value we cannot read is a refusal, not an acceptance. Half a
  // settings file, an edit by hand, a downgrade -- all of them arrive here.
  void aStoredValueThatMakesNoSenseIsRefused() {
    for (const char *nonsense :
         {"", "plaintext", "pbkdf2-sha256$", "pbkdf2-sha256$0$c2FsdA==$aGFzaA==",
          "pbkdf2-sha256$notanumber$c2FsdA==$aGFzaA==", "md5$1$a$b"}) {
      QVERIFY2(!ApiAuth::PasswordMatches(QStringLiteral("anything"),
                                         QString::fromLatin1(nonsense)),
               nonsense);
    }
  }

  void settingAPasswordIsRememberedAndCanBeCleared() {
    ApiAuth::SetPassword(QStringLiteral("a good long one"));
    QVERIFY(ApiAuth::HasPassword());
    QVERIFY(ApiAuth::PasswordMatches(QStringLiteral("a good long one"),
                                     ApiAuth::StoredPassword()));

    ApiAuth::SetPassword(QString());
    QVERIFY(!ApiAuth::HasPassword());
  }

  // Changing the password must throw out whoever was already signed in with
  // the old one, or the change protects against nobody.
  void changingThePasswordSignsEverybodyOut() {
    const QString session = ApiAuth::Sessions::instance().open();
    QVERIFY(ApiAuth::Sessions::instance().accepts(session));

    ApiAuth::SetPassword(QStringLiteral("a new long one"));
    QVERIFY2(!ApiAuth::Sessions::instance().accepts(session),
             "an old session outlived the password change");
  }

  void onlyASessionThatWasOpenedIsAccepted() {
    const QString session = ApiAuth::Sessions::instance().open();

    QVERIFY(ApiAuth::Sessions::instance().accepts(session));
    QVERIFY(!ApiAuth::Sessions::instance().accepts(QString()));
    QVERIFY(!ApiAuth::Sessions::instance().accepts(QStringLiteral("made up")));
    QVERIFY(!ApiAuth::Sessions::instance().accepts(session + QLatin1Char('x')));

    ApiAuth::Sessions::instance().close(session);
    QVERIFY(!ApiAuth::Sessions::instance().accepts(session));
  }

  void twoSessionsAreNotTheSameSession() {
    const QString first = ApiAuth::Sessions::instance().open();
    const QString second = ApiAuth::Sessions::instance().open();

    QVERIFY(first != second);
    QCOMPARE(ApiAuth::Sessions::instance().count(), 2);

    // Signing one out leaves the other alone -- two browsers, one person.
    ApiAuth::Sessions::instance().close(first);
    QVERIFY(!ApiAuth::Sessions::instance().accepts(first));
    QVERIFY(ApiAuth::Sessions::instance().accepts(second));
  }

  // A password somebody can guess at a thousand times a second is a password
  // of about twelve bits, whatever the user typed.
  void guessingIsStoppedAfterAFewTries() {
    ApiAuth::Attempts &attempts = ApiAuth::Attempts::instance();
    QVERIFY(!attempts.isLockedOut());

    for (int i = 0; i < 4; ++i) {
      attempts.failed();
    }
    QVERIFY2(!attempts.isLockedOut(), "locked out too early to be usable");

    attempts.failed();
    QVERIFY2(attempts.isLockedOut(), "guessing was never stopped");
    QVERIFY(attempts.secondsRemaining() > 0);

    // And getting it right afterwards clears it: somebody who mistyped five
    // times is not shut out for the evening once they succeed.
    attempts.succeeded();
    QVERIFY(!attempts.isLockedOut());
    QCOMPARE(attempts.secondsRemaining(), 0);
  }
};

QTEST_MAIN(TestApiAuth)
#include "test_api_auth.moc"
