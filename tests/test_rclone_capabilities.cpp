#include "rclone_capabilities.h"

#include <QFile>
#include <QTest>

class TestRcloneCapabilities : public QObject {
  Q_OBJECT

private slots:
  // The real output of "rclone backend features teldrive:". teldrive is the
  // reason this registry exists: it reports no hashes and no duplicate files,
  // so Check can only compare sizes and Dedupe can never do anything.
  void teldriveFixture() {
    QFile file(QStringLiteral(RB_FIXTURES_DIR "/teldrive_features.json"));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));

    const RcloneCapabilities caps =
        RcloneCapabilities::fromBackendFeatures(file.readAll());

    QVERIFY(caps.known);

    QVERIFY(caps.about);
    QVERIFY(caps.publicLink);
    QVERIFY(caps.copy);
    QVERIFY(caps.move);
    QVERIFY(caps.purge);

    QVERIFY(!caps.duplicateFiles);
    QVERIFY(!caps.cleanUp);
    QVERIFY(!caps.listR);
    QVERIFY(!caps.command);
    QVERIFY(!caps.putStream);

    QVERIFY(caps.hashes.isEmpty());
    QVERIFY(!caps.canHash());

    QCOMPARE(caps.precisionNs, Q_INT64_C(1000000000));
  }

  // A backend that does hash. Guards against the parser reading Hashes from
  // the wrong place in the document.
  void hashesArrayIsRead() {
    const QByteArray json = R"({
      "Name": "gd", "Hashes": ["md5", "sha1"], "Precision": 1000000,
      "Features": { "About": true, "DuplicateFiles": true, "CleanUp": true }
    })";

    const RcloneCapabilities caps =
        RcloneCapabilities::fromBackendFeatures(json);

    QVERIFY(caps.known);
    QVERIFY(caps.canHash());
    QCOMPARE(caps.hashes.size(), 2);
    QCOMPARE(caps.hashes.at(0), QStringLiteral("md5"));
    QVERIFY(caps.duplicateFiles);
    QVERIFY(caps.cleanUp);
    QCOMPARE(caps.precisionNs, Q_INT64_C(1000000));
  }

  // Custom backends do not always report the full feature set. A missing key
  // must read as "not supported" without failing the whole parse.
  void missingKeysDefaultToFalse() {
    const RcloneCapabilities caps =
        RcloneCapabilities::fromBackendFeatures(R"({"Features": {}})");

    QVERIFY(caps.known);
    QVERIFY(!caps.about);
    QVERIFY(!caps.publicLink);
    QVERIFY(!caps.duplicateFiles);
    QVERIFY(caps.hashes.isEmpty());
  }

  // known must stay false on anything unparseable, because RemoteWidget only
  // disables actions once known is true. Getting this wrong would grey out
  // working buttons whenever the probe misbehaves.
  void malformedInputStaysUnknown_data() {
    QTest::addColumn<QByteArray>("json");
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("truncated") << QByteArray(R"({"Features": {"About": tr)");
    QTest::newRow("not json") << QByteArray("Failed to create file system");
    QTest::newRow("array") << QByteArray("[1, 2, 3]");
    QTest::newRow("bare string") << QByteArray("\"nope\"");
  }

  void malformedInputStaysUnknown() {
    QFETCH(QByteArray, json);
    const RcloneCapabilities caps =
        RcloneCapabilities::fromBackendFeatures(json);
    QVERIFY(!caps.known);
  }

  // A default-constructed value is what callers see before the query lands.
  void defaultIsUnknownAndPermissive() {
    const RcloneCapabilities caps;
    QVERIFY(!caps.known);
    QVERIFY(!caps.canHash());
  }
};

QTEST_MAIN(TestRcloneCapabilities)
#include "test_rclone_capabilities.moc"
