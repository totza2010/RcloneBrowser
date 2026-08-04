#include "utils.h"

#include <QTest>

// RedactArgs guards every path that shows, copies or logs a command line.
// A regression here leaks credentials rather than merely breaking a feature,
// so the cases are spelled out rather than sampled.
class TestRedactArgs : public QObject {
  Q_OBJECT

private slots:
  void masksSecretValues_data() {
    QTest::addColumn<QString>("arg");
    QTest::addColumn<QString>("expected");

    QTest::newRow("rc-pass") << "--rc-pass=hunter2" << "--rc-pass=***";
    QTest::newRow("rc-user") << "--rc-user=abc123" << "--rc-user=***";
    QTest::newRow("drive token")
        << "--drive-token={\"access_token\":\"x\"}" << "--drive-token=***";
    QTest::newRow("sftp pass") << "--sftp-pass=abc" << "--sftp-pass=***";
    QTest::newRow("crypt password")
        << "--crypt-password=abc" << "--crypt-password=***";
    QTest::newRow("s3 secret access key")
        << "--s3-secret-access-key=abc" << "--s3-secret-access-key=***";
    QTest::newRow("client secret")
        << "--drive-client-secret=abc" << "--drive-client-secret=***";
  }

  void masksSecretValues() {
    QFETCH(QString, arg);
    QFETCH(QString, expected);
    QCOMPARE(RedactArgs({arg}).at(0), expected);
  }

  // Redacting too much would make the copied command line useless for
  // troubleshooting, which is the whole point of that button.
  void leavesOrdinaryArgumentsAlone_data() {
    QTest::addColumn<QString>("arg");

    QTest::newRow("subcommand") << "copy";
    QTest::newRow("remote") << "tgdrive_main_01:some/path";
    QTest::newRow("config flag") << "--config";
    QTest::newRow("config path") << "C:/Users/x/rclone.conf";
    QTest::newRow("transfers") << "--transfers=8";
    QTest::newRow("rc addr") << "--rc-addr=localhost:5572";
    QTest::newRow("bare rc") << "--rc";
    QTest::newRow("windows path with pass in it")
        << "C:/Users/passenger/data";
    QTest::newRow("checksum flag") << "--checksum";
  }

  void leavesOrdinaryArgumentsAlone() {
    QFETCH(QString, arg);
    QCOMPARE(RedactArgs({arg}).at(0), arg);
  }

  void preservesOrderAndCount() {
    const QStringList in{"mount", "remote:", "R:", "--rc",
                         "--rc-addr=localhost:5555", "--rc-user=abc",
                         "--rc-pass=def"};
    const QStringList out = RedactArgs(in);

    QCOMPARE(out.size(), in.size());
    QCOMPARE(out.at(0), QStringLiteral("mount"));
    QCOMPARE(out.at(1), QStringLiteral("remote:"));
    QCOMPARE(out.at(4), QStringLiteral("--rc-addr=localhost:5555"));
    QCOMPARE(out.at(5), QStringLiteral("--rc-user=***"));
    QCOMPARE(out.at(6), QStringLiteral("--rc-pass=***"));
  }

  void handlesEmptyInput() { QVERIFY(RedactArgs({}).isEmpty()); }

  // The value is matched by the prefix up to '=', so a secret containing '='
  // must not survive in the tail.
  void masksValuesContainingEquals() {
    QCOMPARE(RedactArgs({"--rc-pass=a=b=c"}).at(0),
             QStringLiteral("--rc-pass=***"));
  }

  // At -vv rclone prints the values it read out of the environment, so the
  // remote-control password appears in the job output verbatim.
  void redactsCredentialsEchoedInOutput() {
    const QString line =
        R"(2026/08/04 22:41:35 DEBUG : Setting --rc-pass "sw8Kd2" from environment variable RCLONE_RC_PASS="sw8Kd2")";
    const QString out = RedactOutputLine(line, "abc123", "sw8Kd2");

    QVERIFY(!out.contains(QStringLiteral("sw8Kd2")));
    QVERIFY(out.contains(QStringLiteral("***")));
    // The rest of the line has to survive or the log becomes unreadable.
    QVERIFY(out.contains(QStringLiteral("RCLONE_RC_PASS")));
    QVERIFY(out.contains(QStringLiteral("DEBUG")));
  }

  void redactsUserAndPasswordOnTheSameLine() {
    const QString out =
        RedactOutputLine(R"(Setting rc_user="abc123" rc_pass="sw8Kd2")",
                         "abc123", "sw8Kd2");
    QVERIFY(!out.contains(QStringLiteral("abc123")));
    QVERIFY(!out.contains(QStringLiteral("sw8Kd2")));
  }

  // A short user name can occur inside the password; replacing the shorter
  // one first would leave a fragment of the longer behind.
  void redactsOverlappingCredentials() {
    const QString out = RedactOutputLine("user=ab pass=xxabxx", "ab", "xxabxx");
    QVERIFY(!out.contains(QStringLiteral("xxabxx")));
  }

  void leavesOutputAloneWithoutCredentials() {
    const QString line = "2026/08/04 NOTICE: Serving remote control on "
                         "http://127.0.0.1:5555/";
    QCOMPARE(RedactOutputLine(line, QString(), QString()), line);
  }
};

QTEST_MAIN(TestRedactArgs)
#include "test_redact_args.moc"
