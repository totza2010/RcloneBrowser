#include "job_options.h"
#include "task_json.h"

#include <QJsonObject>
#include <QTest>

// A task as another program reads and writes it: words rather than the
// numbers storage keeps. What is checked here is that the words go both ways
// and that a request which is wrong changes nothing.
//
// See docs/WEB-API-PLAN.md and VERIFY.md V-38.
class TestTaskJson : public QObject {
  Q_OBJECT

private:
  static JobOptions upload() {
    JobOptions task(false);
    task.jobType = JobOptions::Upload;
    task.operation = JobOptions::Copy;
    task.description = QStringLiteral("nightly");
    task.source = QStringLiteral("D:/films");
    task.dest = QStringLiteral("tgdrive:films");
    return task;
  }

  static QJsonObject one(const char *key, const QJsonValue &value) {
    QJsonObject object;
    object.insert(QLatin1String(key), value);
    return object;
  }

private slots:
  void aTaskIsWrittenInWords() {
    JobOptions task = upload();
    task.operation = JobOptions::Sync;
    task.syncTiming = JobOptions::After;
    task.compare = true;
    task.compareOption = JobOptions::Checksum;
    task.transfers = QStringLiteral("6");

    const QJsonObject json = TaskJson::Write(task);
    QCOMPARE(json.value(QStringLiteral("type")).toString(),
             QStringLiteral("upload"));
    QCOMPARE(json.value(QStringLiteral("operation")).toString(),
             QStringLiteral("sync"));
    QCOMPARE(json.value(QStringLiteral("syncTiming")).toString(),
             QStringLiteral("after"));
    QCOMPARE(json.value(QStringLiteral("compare")).toString(),
             QStringLiteral("checksum"));
    QCOMPARE(json.value(QStringLiteral("transfers")).toString(),
             QStringLiteral("6"));
    QCOMPARE(json.value(QStringLiteral("name")).toString(),
             QStringLiteral("nightly"));
    // What the first shape of this had, still there.
    QCOMPARE(json.value(QStringLiteral("isDownload")).toBool(), false);
  }

  // The point of the whole file: what is written can be read back, onto a
  // task that started as something else.
  void whatIsWrittenCanBeReadBack() {
    JobOptions from = upload();
    from.operation = JobOptions::Move;
    from.syncTiming = JobOptions::Before;
    from.compare = true;
    from.compareOption = JobOptions::SizeOnly;
    from.skipNewer = true;
    from.deleteExcluded = true;
    from.bandwidth = QStringLiteral("2M");
    from.maxDepth = 3;
    from.extra = QStringLiteral("--fast-list");
    from.excluded = QStringLiteral("*.tmp\n*.part");

    JobOptions to(true);
    QString why;
    QVERIFY2(TaskJson::Read(TaskJson::Write(from), &to, &why), qPrintable(why));

    QJsonObject a = TaskJson::Write(from);
    QJsonObject b = TaskJson::Write(to);
    a.remove(QStringLiteral("id")); // an id is not something Read writes
    b.remove(QStringLiteral("id"));
    QCOMPARE(b, a);
    QCOMPARE(to.jobType, JobOptions::Upload);
    QCOMPARE(to.operation, JobOptions::Move);
    QCOMPARE(to.compareOption, JobOptions::SizeOnly);
  }

  void whatIsNotMentionedIsLeftAlone() {
    JobOptions task = upload();
    task.transfers = QStringLiteral("6");
    QVERIFY(TaskJson::Read(one("bandwidth", QStringLiteral("1M")), &task,
                           nullptr));
    QCOMPARE(task.bandwidth, QStringLiteral("1M"));
    QCOMPARE(task.transfers, QStringLiteral("6"));
    QCOMPARE(task.source, QStringLiteral("D:/films"));
    QCOMPARE(task.operation, JobOptions::Copy);
  }

  // The flag the command is built from cannot disagree with the choice.
  void syncFollowsTheOperation() {
    JobOptions task = upload();
    QVERIFY(TaskJson::Read(one("operation", QStringLiteral("sync")), &task,
                           nullptr));
    QVERIFY(task.sync);
    QVERIFY(TaskJson::Read(one("operation", QStringLiteral("copy")), &task,
                           nullptr));
    QVERIFY(!task.sync);
  }

  void compareNoneIsTheBoxUnticked() {
    JobOptions task = upload();
    task.compare = true;
    QVERIFY(TaskJson::Read(one("compare", QStringLiteral("none")), &task,
                           nullptr));
    QVERIFY(!task.compare);
    QCOMPARE(TaskJson::Write(task).value(QStringLiteral("compare")).toString(),
             QStringLiteral("none"));
  }

  void aNumberWillDoWhereTextIsKept() {
    JobOptions task = upload();
    QVERIFY(TaskJson::Read(one("transfers", 12), &task, nullptr));
    QCOMPARE(task.transfers, QStringLiteral("12"));
  }

  void aWordThatIsNotAChoiceIsRefusedAndSaysWhich_data() {
    QTest::addColumn<QString>("key");
    QTest::addColumn<QJsonValue>("value");
    QTest::newRow("operation") << "operation" << QJsonValue("mount");
    QTest::newRow("operation number") << "operation" << QJsonValue(3);
    QTest::newRow("type") << "type" << QJsonValue("sideways");
    QTest::newRow("timing") << "syncTiming" << QJsonValue("never");
    QTest::newRow("compare") << "compare" << QJsonValue("vibes");
    QTest::newRow("depth") << "maxDepth" << QJsonValue(-1);
    QTest::newRow("depth text") << "maxDepth" << QJsonValue("deep");
    QTest::newRow("flag") << "skipNewer" << QJsonValue("yes");
    QTest::newRow("text") << "extra" << QJsonValue(true);
    QTest::newRow("not a number") << "transfers" << QJsonValue("many");
    QTest::newRow("not whole") << "checkers" << QJsonValue("1.5");
    QTest::newRow("not a size") << "minSize" << QJsonValue("big");
    QTest::newRow("not a time") << "maxAge" << QJsonValue("3 days ago!");
    QTest::newRow("not a rate") << "bandwidth" << QJsonValue("fast; rm -rf");
  }

  void aWordThatIsNotAChoiceIsRefusedAndSaysWhich() {
    QFETCH(QString, key);
    QFETCH(QJsonValue, value);

    JobOptions task = upload();
    const QJsonObject before = TaskJson::Write(task);

    // With something right next to it, to see that nothing is half written.
    QJsonObject asked;
    asked.insert(QStringLiteral("name"), QStringLiteral("changed"));
    asked.insert(key, value);

    QString why;
    QVERIFY(!TaskJson::Read(asked, &task, &why));
    QVERIFY2(why.contains(key), qPrintable(why));
    QCOMPARE(TaskJson::Write(task), before);
  }

  // What rclone would take is taken, the less obvious shapes included.
  void whatRcloneWouldTakeIsTaken() {
    JobOptions task = upload();
    QJsonObject asked;
    asked.insert(QStringLiteral("bandwidth"),
                 QStringLiteral("08:00,512k 18:00,off"));
    asked.insert(QStringLiteral("minSize"), QStringLiteral("1.5Gi"));
    asked.insert(QStringLiteral("minAge"), QStringLiteral("1h30m"));
    asked.insert(QStringLiteral("maxAge"), QStringLiteral("2026-01-02"));
    asked.insert(QStringLiteral("transfers"), QStringLiteral(" 8 "));
    asked.insert(QStringLiteral("checkers"), QString());
    QString why;
    QVERIFY2(TaskJson::Read(asked, &task, &why), qPrintable(why));
    QCOMPARE(task.transfers, QStringLiteral("8"));
    QCOMPARE(task.checkers, QString());
  }

  void aNewTransferStartsWhereTheDialogDoes() {
    const QJsonObject up = TaskJson::Defaults(false);
    QCOMPARE(up.value(QStringLiteral("type")).toString(),
             QStringLiteral("upload"));
    QCOMPARE(up.value(QStringLiteral("operation")).toString(),
             QStringLiteral("copy"));
    QCOMPARE(up.value(QStringLiteral("transfers")).toString(),
             QStringLiteral("4"));
    QCOMPARE(up.value(QStringLiteral("checkers")).toString(),
             QStringLiteral("8"));
    QVERIFY(!up.contains(QStringLiteral("id")));
    QCOMPARE(TaskJson::Defaults(true).value(QStringLiteral("type")).toString(),
             QStringLiteral("download"));
  }
};

// GUILESS rather than APPLESS: JobOptions reaches for the settings.
QTEST_GUILESS_MAIN(TestTaskJson)
#include "test_task_json.moc"
