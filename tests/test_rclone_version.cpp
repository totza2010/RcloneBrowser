#include "rclone_version.h"

#include <QTest>

// Reading "rclone version" used to be string surgery inside a MainWindow
// lambda, so it could not be tested and could not be asked for without a
// window. It has to cope with what is actually installed, which on a
// developer's machine is usually a build from source with a -DEV suffix, and
// on an old system may be a single line.
//
// See docs/LAYER-SPLIT.md block 2 and docs/RCLONE-MANAGER.md.
class TestRcloneVersion : public QObject {
  Q_OBJECT

private slots:
  void readsWhatRcloneActuallyPrints() {
    const RcloneVersion v = ParseRcloneVersion(
        "rclone v1.71.1\n"
        "- os/version: Microsoft Windows 11 Pro 24H2 (64 bit)\n"
        "- os/kernel: 10.0.26200.1234 (x86_64)\n"
        "- os/type: windows\n"
        "- go/version: go1.24.1\n");

    QVERIFY(v.isValid());
    QCOMPARE(v.number, QStringLiteral("1.71.1"));
    QCOMPARE(v.osLine,
             QStringLiteral("os/version: Microsoft Windows 11 Pro 24H2 (64 bit)"));
    QCOMPARE(v.goLine, QStringLiteral("os/kernel: 10.0.26200.1234 (x86_64)"));
    QVERIFY(v.raw.contains(QStringLiteral("go1.24.1")));
  }

  // A build from source, which is what a fork such as tgdrive usually is.
  // The suffix has to go or the number cannot be compared.
  void aBuildFromSourceIsStillAVersion() {
    const RcloneVersion dev = ParseRcloneVersion("rclone v1.71.1-DEV\n"
                                                 "- os/type: windows\n");
    QCOMPARE(dev.number, QStringLiteral("1.71.1"));
    QVERIFY(dev.atLeast(QStringLiteral("1.50")));

    const RcloneVersion beta =
        ParseRcloneVersion("rclone v1.72.0-beta.1234.abcdef\n");
    QCOMPARE(beta.number, QStringLiteral("1.72.0"));
    QVERIFY(beta.atLeast(QStringLiteral("1.71")));
  }

  void aVeryOldOneLineVersionStillReads() {
    const RcloneVersion v = ParseRcloneVersion("rclone v1.36\n");
    QCOMPARE(v.number, QStringLiteral("1.36"));
    QVERIFY(v.osLine.isEmpty());
    QVERIFY(!v.atLeast(QStringLiteral("1.50")));
  }

  // The reason this is compared field by field rather than as text.
  void tenIsNewerThanNine() {
    const RcloneVersion v = ParseRcloneVersion("rclone v1.10.0\n");
    QVERIFY2(v.atLeast(QStringLiteral("1.9")),
             "1.10 was treated as older than 1.9");
    QVERIFY(!v.atLeast(QStringLiteral("1.11")));
  }

  void aMissingFieldCountsAsZero() {
    const RcloneVersion v = ParseRcloneVersion("rclone v1.71\n");
    QVERIFY(v.atLeast(QStringLiteral("1.71.0")));
    QVERIFY(!v.atLeast(QStringLiteral("1.71.1")));
    QVERIFY(v.atLeast(QStringLiteral("1.71")));
  }

  // Nothing readable must not become "new enough": that is how a feature
  // gets offered on a build that cannot do it.
  void nothingReadableIsNeverNewEnough() {
    for (const char *nonsense : {"", "\n", "command not found", "rclone"}) {
      const RcloneVersion v = ParseRcloneVersion(nonsense);
      QVERIFY2(!v.isValid(), nonsense);
      QVERIFY2(!v.atLeast(QStringLiteral("1.0")), nonsense);
    }
  }
};

QTEST_APPLESS_MAIN(TestRcloneVersion)
#include "test_rclone_version.moc"
