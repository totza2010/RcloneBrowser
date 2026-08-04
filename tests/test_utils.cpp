#include "utils.h"

#include <QProcess>
#include <QSet>
#include <QTest>

class TestUtils : public QObject {
  Q_OBJECT

private slots:
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
