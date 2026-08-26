#include "utils.h"

#include <QProcess>
#include <QSet>
#include <QTest>

class TestUtils : public QObject {
  Q_OBJECT

private slots:
  // The rule for reading a line of rclone options. It was written out nine
  // times -- the check, dedupe, mount and folder dialogs, the task options,
  // the script runner, the window twice, and here -- in two spellings that
  // had already drifted apart. Nine copies of a quoting rule is nine chances
  // for the same typed line to mean different things depending on which box
  // it went into. See docs/LAYER-SPLIT.md block 5.
  void splitsOnSpacesOutsideQuotes() {
    QCOMPARE(SplitRcloneOptions(QStringLiteral("--fast-list --transfers 4")),
             (QStringList{"--fast-list", "--transfers", "4"}));
  }

  // The whole point of the quotes: a path with a space in it is one argument.
  void aQuotedRunStaysOneArgument() {
    QCOMPARE(SplitRcloneOptions(QStringLiteral("--exclude \"My Films/**\"")),
             (QStringList{"--exclude", "My Films/**"}));

    QCOMPARE(
        SplitRcloneOptions(
            QStringLiteral("--exclude \"a b\" --include \"c d\" --fast-list")),
        (QStringList{"--exclude", "a b", "--include", "c d", "--fast-list"}));
  }

  // Quotes are removed, not passed on: rclone is given the argument, not the
  // way it was typed.
  void theQuotesThemselvesDoNotSurvive() {
    for (const QString &arg :
         SplitRcloneOptions(QStringLiteral("--exclude \"x y\""))) {
      QVERIFY2(!arg.contains(QLatin1Char('"')), qPrintable(arg));
    }
  }

  void nothingTypedIsNoArguments() {
    QVERIFY(SplitRcloneOptions(QString()).isEmpty());
    QVERIFY(SplitRcloneOptions(QStringLiteral("   ")).isEmpty());
    QVERIFY(SplitRcloneOptions(QStringLiteral("\t \n ")).isEmpty());
  }

  // Runs of spaces are how a line looks after somebody edits it, and they
  // must not become empty arguments -- rclone reads an empty argument as a
  // path, not as nothing.
  void extraSpacesDoNotBecomeEmptyArguments() {
    const QStringList args =
        SplitRcloneOptions(QStringLiteral("  --fast-list    --checkers  6  "));
    QCOMPARE(args, (QStringList{"--fast-list", "--checkers", "6"}));
    for (const QString &arg : args) {
      QVERIFY(!arg.isEmpty());
    }
  }

  void oneArgumentOnItsOwnIsStillAnArgument() {
    QCOMPARE(SplitRcloneOptions(QStringLiteral("--fast-list")),
             (QStringList{"--fast-list"}));
    QCOMPARE(SplitRcloneOptions(QStringLiteral("\"C:/Program Files/x.exe\"")),
             (QStringList{"C:/Program Files/x.exe"}));
  }


  void credentialHasRequestedLength_data() {
    QTest::addColumn<int>("length");
    QTest::newRow("rc user") << 10;
    QTest::newRow("rc pass") << 22;
    QTest::newRow("one") << 1;
    QTest::newRow("zero") << 0;
  }

  void credentialHasRequestedLength() {
    QFETCH(int, length);
    QCOMPARE(GenerateRcCredential(length).length(), length);
  }

  // The value ends up in an environment variable and in a script argument, so
  // it must stay to characters that need no quoting anywhere.
  void credentialIsAlphanumeric() {
    const QString value = GenerateRcCredential(500);
    for (const QChar &c : value) {
      QVERIFY2(c.isLetterOrNumber() && c.unicode() < 128,
               qPrintable(QStringLiteral("unexpected character: %1").arg(c)));
    }
  }

  void credentialsDiffer() {
    QSet<QString> seen;
    for (int i = 0; i < 50; ++i) {
      seen.insert(GenerateRcCredential(22));
    }
    // 22 characters from a 62 character alphabet; a repeat here means the
    // generator is not random at all.
    QCOMPARE(seen.size(), 50);
  }

  // Covers every character of the alphabet over enough samples. bounded() is
  // uniform; the previous "generate() % length" skewed towards the start.
  void credentialUsesWholeAlphabet() {
    const QString value = GenerateRcCredential(20000);
    QSet<QChar> seen;
    for (const QChar &c : value) {
      seen.insert(c);
    }
    QCOMPARE(seen.size(), 62);
  }

  void rcCredentialsGoIntoTheEnvironment() {
    QProcess process;
    UseRcCredentials(&process, "someuser", "somepass");

    const QProcessEnvironment env = process.processEnvironment();
    QCOMPARE(env.value("RCLONE_RC_USER"), QStringLiteral("someuser"));
    QCOMPARE(env.value("RCLONE_RC_PASS"), QStringLiteral("somepass"));
    // The rest of the environment has to survive, or rclone loses PATH,
    // HOME and the config password.
    QVERIFY(env.keys().size() > 2);
  }

  void emptyRcCredentialsAreNotSet() {
    QProcess process;
    UseRcCredentials(&process, QString(), QString());
    QVERIFY(process.processEnvironment().isEmpty());

    UseRcCredentials(&process, "user", QString());
    QVERIFY(process.processEnvironment().isEmpty());
  }

  void compareVersion_data() {
    QTest::addColumn<QString>("a");
    QTest::addColumn<QString>("b");
    QTest::addColumn<unsigned int>("expected"); // 0 equal, 1 a>b, 2 b>a

    QTest::newRow("equal") << "1.72.1" << "1.72.1" << 0u;
    QTest::newRow("patch greater") << "1.72.2" << "1.72.1" << 1u;
    QTest::newRow("patch lesser") << "1.72.1" << "1.72.2" << 2u;
    QTest::newRow("minor greater") << "1.73.0" << "1.72.9" << 1u;
    QTest::newRow("major greater") << "2.0.0" << "1.99.99" << 1u;
    QTest::newRow("shorter equals padded") << "1.72" << "1.72.0" << 0u;
    QTest::newRow("shorter is lesser") << "1.72" << "1.72.1" << 2u;
    QTest::newRow("double digit minor") << "1.9.0" << "1.10.0" << 2u;
  }

  void compareVersion() {
    QFETCH(QString, a);
    QFETCH(QString, b);
    QFETCH(unsigned int, expected);
    QCOMPARE(::compareVersion(a.toStdString(), b.toStdString()), expected);
  }
};

QTEST_MAIN(TestUtils)
#include "test_utils.moc"
