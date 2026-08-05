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

  // The "Remaining" field on the card reads etaText(). It showed
  // "447h42m7s" on a slow upload, which is a number nobody can parse at a
  // glance and which changed every second.
  void etaTextIsRounded() {
    const JobStats stats = JobStats::fromCoreStats(
        R"({"bytes": 1, "totalBytes": 100, "eta": 1611727})");
    QCOMPARE(stats.etaText(), QStringLiteral("18d 15h"));
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

  // An estimate is not known to the second. Reporting one that way made the
  // figure jitter every poll without telling the reader anything.
  void formatEta_data() {
    QTest::addColumn<qint64>("seconds");
    QTest::addColumn<QString>("expected");

    QTest::newRow("seconds") << Q_INT64_C(45) << "45s";
    QTest::newRow("under a minute") << Q_INT64_C(59) << "59s";
    QTest::newRow("exactly a minute") << Q_INT64_C(60) << "1m 0s";
    QTest::newRow("minutes") << Q_INT64_C(303) << "5m 3s";
    QTest::newRow("hours drop seconds") << Q_INT64_C(7530) << "2h 5m";
    QTest::newRow("days drop minutes") << Q_INT64_C(273600) << "3d 4h";
    QTest::newRow("unknown") << Q_INT64_C(-1) << "-";
  }

  void formatEta() {
    QFETCH(qint64, seconds);
    QFETCH(QString, expected);
    QCOMPARE(FormatEta(seconds), expected);
  }

  // The phase decides whether the card shows a percentage at all. Getting it
  // wrong is what made the bar sit at 0% and then jump once rclone finished
  // counting.
  void phaseIsStartingBeforeAnythingIsCounted() {
    const JobStats stats = JobStats::fromCoreStats(R"({"bytes": 0})");
    QCOMPARE(stats.phase(), JobPhase::Starting);
    QCOMPARE(stats.phaseText(), QStringLiteral("Starting"));
  }

  void phaseIsScanningWhileListing() {
    const JobStats stats = JobStats::fromCoreStats(
        R"({"bytes": 0, "totalBytes": 0, "listed": 1204, "checks": 12})");
    QCOMPARE(stats.phase(), JobPhase::Scanning);
    QVERIFY(stats.phaseText().startsWith(QStringLiteral("Scanning")));
    QVERIFY(stats.phaseText().contains(QStringLiteral("1")));
  }

  // Older rclone reports no "listed" field. The word alone still beats a
  // percentage with nothing behind it.
  void phaseIsScanningWithoutAListedCount() {
    const JobStats stats =
        JobStats::fromCoreStats(R"({"bytes": 0, "totalChecks": 40})");
    QCOMPARE(stats.phase(), JobPhase::Scanning);
    QCOMPARE(stats.phaseText(), QStringLiteral("Scanning"));
  }

  void phaseIsTransferringOnceThereIsATotal() {
    const JobStats stats = JobStats::fromCoreStats(
        R"({"bytes": 500, "totalBytes": 1000, "transfers": 1,
            "totalTransfers": 4})");
    QCOMPARE(stats.phase(), JobPhase::Transferring);
    // Nothing in words: the bar is already saying it.
    QVERIFY(stats.phaseText().isEmpty());
  }

  // Everything counted has moved but rclone is still up, setting modification
  // times and closing backends. Without this the card reads 100% and looks
  // hung.
  void phaseIsFinishingAfterTheLastByte() {
    const JobStats stats = JobStats::fromCoreStats(
        R"({"bytes": 1000, "totalBytes": 1000, "transfers": 4,
            "totalTransfers": 4})");
    QCOMPARE(stats.phase(), JobPhase::Finishing);
    QCOMPARE(stats.phaseText(), QStringLiteral("Finishing"));
  }

  // rclone can report more bytes than it estimated; that must not read as
  // still transferring after the count is met.
  void phaseIsFinishingWhenBytesOvershoot() {
    const JobStats stats = JobStats::fromCoreStats(
        R"({"bytes": 1200, "totalBytes": 1000, "transfers": 2,
            "totalTransfers": 2})");
    QCOMPARE(stats.phase(), JobPhase::Finishing);
  }

  // A narrow bar drops whole figures from the end rather than eliding through
  // the middle of one. The percentage has to survive every time.
  void progressPartsAreOrderedSoTheEndCanBeDropped() {
    const JobStats stats = JobStats::fromCoreStats(
        R"({"bytes": 524288, "totalBytes": 1048576, "speed": 1048576,
            "eta": 303})");
    const QStringList parts = stats.progressParts();
    QCOMPARE(parts.size(), 4);
    QCOMPARE(parts.at(0), QStringLiteral("50%"));
    QVERIFY(parts.last().endsWith(QStringLiteral("left")));
    QCOMPARE(JoinProgressParts(parts), stats.progressText());
  }

  void progressTextCarriesEveryKnownFigure() {
    const JobStats stats = JobStats::fromCoreStats(
        R"({"bytes": 524288, "totalBytes": 1048576, "speed": 1048576,
            "eta": 303})");
    const QString text = stats.progressText();
    QVERIFY2(text.contains(QStringLiteral("50%")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("512.0 KiB")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("1.00 MiB/s")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("5m 3s left")), qPrintable(text));
  }

  // A speed of zero and a null eta are what rclone reports before it knows,
  // not measurements. Printing them as "0 B/s" and "-" reads as a stalled
  // transfer.
  void progressTextLeavesOutWhatIsNotKnownYet() {
    const JobStats stats = JobStats::fromCoreStats(
        R"({"bytes": 0, "totalBytes": 1000, "speed": 0, "eta": null})");
    const QString text = stats.progressText();
    QVERIFY2(!text.contains(QStringLiteral("/s")), qPrintable(text));
    QVERIFY2(!text.contains(QStringLiteral("left")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("0%")), qPrintable(text));
  }

  void transferItemTextShowsPercentageWhenSized() {
    const JobStats stats = JobStats::fromCoreStats(
        R"({"bytes": 1, "transferring": [{"name": "a.bin", "bytes": 524288,
            "size": 1048576, "percentage": 50, "speed": 1048576, "eta": 45}]})");
    QCOMPARE(stats.transferring.size(), 1);
    const QString text = stats.transferring.at(0).progressText();
    QVERIFY2(text.contains(QStringLiteral("50%")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("512.0 KiB / 1.00 MiB")),
             qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("45s left")), qPrintable(text));
  }

  // teldrive and other backends that cannot size a file up front. rclone still
  // sends percentage 0, and the old card drew a 0% bar reading "0% of unknown
  // size" for the whole upload.
  void transferItemTextOmitsInventedFiguresWhenUnsized() {
    const JobStats stats = JobStats::fromCoreStats(
        R"({"bytes": 1, "transferring": [{"name": "a.bin", "bytes": 524288,
            "percentage": 0, "speed": 1048576}]})");
    QCOMPARE(stats.transferring.size(), 1);
    const JobTransferItem &item = stats.transferring.at(0);
    QCOMPARE(item.size, Q_INT64_C(-1));

    const QString text = item.progressText();
    QVERIFY2(!text.contains(QLatin1Char('%')), qPrintable(text));
    QVERIFY2(!text.contains(QStringLiteral("left")), qPrintable(text));
    QVERIFY2(!text.contains(QStringLiteral("unknown")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("512.0 KiB")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("1.00 MiB/s")), qPrintable(text));
  }
};

QTEST_MAIN(TestJobStats)
#include "test_job_stats.moc"
