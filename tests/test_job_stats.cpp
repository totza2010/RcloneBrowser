#include "job_stats.h"

#include <QFile>
#include <QTest>

class TestJobStats : public QObject {
  Q_OBJECT

private slots:
  // Recorded from a real "rclone copy" with three files in flight. These are
  // the figures the job card used to scrape out of the printed output.
  void liveTransferFixture() {
    QFile file(QStringLiteral(RB_FIXTURES_DIR "/core_stats_active.json"));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));

    const JobStats stats = JobStats::fromCoreStats(file.readAll());

    QVERIFY(stats.valid);
    QCOMPARE(stats.bytes, Q_INT64_C(63721472));
    QCOMPARE(stats.totalBytes, Q_INT64_C(377487360));
    QCOMPARE(stats.totalTransfers, Q_INT64_C(3));
    QCOMPARE(stats.listed, Q_INT64_C(6));
    QCOMPARE(stats.errors, Q_INT64_C(0));
    QCOMPARE(stats.etaSeconds, Q_INT64_C(24));
    QVERIFY(!stats.fatalError);
    QVERIFY(!stats.retryError);
    QCOMPARE(stats.percent(), 16);

    QCOMPARE(stats.transferring.size(), 3);
    const JobTransferItem &first = stats.transferring.at(0);
    QCOMPARE(first.name, QStringLiteral("file1.bin"));
    QCOMPARE(first.bytes, Q_INT64_C(21688320));
    QCOMPARE(first.size, Q_INT64_C(62914560));
    QCOMPARE(first.percentage, 34);
    QCOMPARE(first.etaSeconds, Q_INT64_C(9));
    QVERIFY(first.speed > 0.0);
  }

  // The bug the old parser kept reintroducing: rclone can report more bytes
  // transferred than it first estimated, and the progress label went past
  // 100%.
  void percentNeverExceedsHundred() {
    const JobStats stats = JobStats::fromCoreStats(
        R"({"bytes": 2000, "totalBytes": 1000})");
    QVERIFY(stats.valid);
    QCOMPARE(stats.percent(), 100);
  }

  // Backends that cannot size the job up front report zero, which must not
  // divide.
  void percentIsZeroWithoutATotal() {
    const JobStats stats =
        JobStats::fromCoreStats(R"({"bytes": 500, "totalBytes": 0})");
    QVERIFY(stats.valid);
    QCOMPARE(stats.percent(), 0);
  }

  // eta is null until rclone can estimate one, and stays null on backends
  // with no total. It must not read as "0 seconds remaining".
  void nullEtaBecomesUnknown() {
    const JobStats stats =
        JobStats::fromCoreStats(R"({"bytes": 1, "eta": null})");
    QVERIFY(stats.valid);
    QCOMPARE(stats.etaSeconds, Q_INT64_C(-1));
    QCOMPARE(stats.etaText(), QStringLiteral("-"));
  }

  void emptyStatsAreStillValid() {
    const JobStats stats = JobStats::fromCoreStats(R"({"bytes": 0})");
    QVERIFY(stats.valid);
    QCOMPARE(stats.percent(), 0);
    QVERIFY(stats.transferring.isEmpty());
  }

  void malformedInputIsNotValid_data() {
    QTest::addColumn<QByteArray>("json");
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("truncated") << QByteArray(R"({"bytes": 12)");
    QTest::newRow("html error page") << QByteArray("<html>401</html>");
    QTest::newRow("array") << QByteArray("[]");
    QTest::newRow("rc error")
        << QByteArray(R"({"error": "unauthenticated", "status": 401})");
  }

  void malformedInputIsNotValid() {
    QFETCH(QByteArray, json);
    QVERIFY(!JobStats::fromCoreStats(json).valid);
  }

  // Entries without a name cannot be keyed in the progress list, so they are
  // dropped rather than creating an unnamed row.
  void transferItemsWithoutNameAreSkipped() {
    const JobStats stats = JobStats::fromCoreStats(R"({
      "bytes": 1,
      "transferring": [{"bytes": 5}, {"name": "ok.bin", "bytes": 5}]
    })");
    QCOMPARE(stats.transferring.size(), 1);
    QCOMPARE(stats.transferring.at(0).name, QStringLiteral("ok.bin"));
  }

  void transferItemWithoutSizeIsUnknown() {
    const JobStats stats = JobStats::fromCoreStats(
        R"({"bytes": 1, "transferring": [{"name": "a", "bytes": 5}]})");
    QCOMPARE(stats.transferring.size(), 1);
    QCOMPARE(stats.transferring.at(0).size, Q_INT64_C(-1));
    QCOMPARE(stats.transferring.at(0).etaSeconds, Q_INT64_C(-1));
  }

  void parsesServingPort_data() {
    QTest::addColumn<QString>("line");
    QTest::addColumn<quint16>("expected");

    QTest::newRow("real notice")
        << "2026/08/03 22:39:45 NOTICE: Serving remote control on "
           "http://127.0.0.1:5555/"
        << quint16(5555);
    QTest::newRow("localhost")
        << "Serving remote control on http://localhost:41234/"
        << quint16(41234);
    QTest::newRow("no trailing slash")
        << "Serving remote control on http://127.0.0.1:8080" << quint16(8080);
    QTest::newRow("https")
        << "Serving remote control on https://127.0.0.1:9000/"
        << quint16(9000);

    QTest::newRow("unrelated line")
        << "2026/08/03 NOTICE: teldrive root '': --vfs-cache-mode writes"
        << quint16(0);
    QTest::newRow("empty") << QString() << quint16(0);
    QTest::newRow("port out of range")
        << "Serving remote control on http://127.0.0.1:99999/" << quint16(0);
    QTest::newRow("no port")
        << "Serving remote control on http://127.0.0.1/" << quint16(0);
  }

  void parsesServingPort() {
    QFETCH(QString, line);
    QFETCH(quint16, expected);
    QCOMPARE(ParseRcServingPort(line), expected);
  }

  // The job card polls once a second and rclone logs both the request and the
  // full reply at -vv, so without this filter our own polling would be most
  // of the log the user is trying to read.
  void filtersOwnPollingNoise_data() {
    QTest::addColumn<QString>("line");
    QTest::addColumn<bool>("isNoise");

    QTest::newRow("stats request")
        << R"(2026/08/04 22:46:14 DEBUG : rc: "core/stats": with parameters map[])"
        << true;
    QTest::newRow("stats reply")
        << R"(2026/08/04 22:46:14 DEBUG : rc: "core/stats": reply map[bytes:0 checks:0]: <nil>)"
        << true;
    QTest::newRow("quit request")
        << R"(DEBUG : rc: "core/quit": with parameters map[])" << true;

    QTest::newRow("real transfer log")
        << "2026/08/04 22:46:24 DEBUG : Vengeance.mkv: Starting multipart "
           "upload"
        << false;
    QTest::newRow("serving notice")
        << "2026/08/04 22:46:14 NOTICE: Serving remote control on "
           "http://127.0.0.1:2541/"
        << false;
    QTest::newRow("another rc endpoint")
        << R"(DEBUG : rc: "core/version": with parameters map[])" << false;
    QTest::newRow("error")
        << "2026/08/04 ERROR : Vengeance.mkv: Failed to copy" << false;
  }

  void filtersOwnPollingNoise() {
    QFETCH(QString, line);
    QFETCH(bool, isNoise);
    QCOMPARE(IsRcPollingNoise(line), isNoise);
  }

  void formatBytes_data() {
    QTest::addColumn<qint64>("bytes");
    QTest::addColumn<QString>("expected");

    QTest::newRow("zero") << Q_INT64_C(0) << "0 B";
    QTest::newRow("bytes") << Q_INT64_C(512) << "512 B";
    QTest::newRow("kib") << Q_INT64_C(2048) << "2.00 KiB";
    QTest::newRow("mib") << Q_INT64_C(62914560) << "60.0 MiB";
    QTest::newRow("gib") << Q_INT64_C(5368709120) << "5.00 GiB";
    QTest::newRow("unknown") << Q_INT64_C(-1) << "-";
  }

  void formatBytes() {
    QFETCH(qint64, bytes);
    QFETCH(QString, expected);
    QCOMPARE(FormatBytes(bytes), expected);
  }

  void formatSeconds_data() {
    QTest::addColumn<qint64>("seconds");
    QTest::addColumn<QString>("expected");

    QTest::newRow("seconds") << Q_INT64_C(9) << "9s";
    QTest::newRow("minutes") << Q_INT64_C(83) << "1m23s";
    QTest::newRow("hours") << Q_INT64_C(3725) << "1h2m5s";
    QTest::newRow("unknown") << Q_INT64_C(-1) << "-";
  }

  void formatSeconds() {
    QFETCH(qint64, seconds);
    QFETCH(QString, expected);
    QCOMPARE(FormatSeconds(seconds), expected);
  }
};

QTEST_MAIN(TestJobStats)
#include "test_job_stats.moc"
