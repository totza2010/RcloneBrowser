#include "schedule.h"
#include "schedule_json.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>
#include <QTimeZone>

// A schedule as another program reads and writes it: words rather than the
// nine booleans storage keeps.
//
// See docs/WEB-API-PLAN.md and VERIFY.md V-41.
class TestScheduleJson : public QObject {
  Q_OBJECT

private:
  static Schedule weekdays() {
    Schedule s;
    s.id = QStringLiteral("{one}");
    s.name = QStringLiteral("nightly");
    s.taskId = QStringLiteral("{task}");
    s.saturday = false;
    s.sunday = false;
    s.hour = 6;
    s.minute = 30;
    return s;
  }

  static QJsonObject one(const char *key, const QJsonValue &value) {
    QJsonObject object;
    object.insert(QLatin1String(key), value);
    return object;
  }

  static const QDateTime &aMonday() {
    static const QDateTime at(QDate(2026, 10, 12), QTime(9, 0));
    return at;
  }

private slots:
  void aScheduleIsWrittenInWords() {
    const QJsonObject json = ScheduleJson::Write(weekdays(), aMonday());
    QCOMPARE(json.value(QStringLiteral("mode")).toString(),
             QStringLiteral("daily"));
    QCOMPARE(json.value(QStringLiteral("time")).toString(),
             QStringLiteral("06:30"));
    QCOMPARE(json.value(QStringLiteral("days")).toArray().size(), 5);
    QCOMPARE(json.value(QStringLiteral("days")).toArray().at(0).toString(),
             QStringLiteral("mon"));
    QCOMPARE(json.value(QStringLiteral("runAs")).toString(),
             QStringLiteral("now"));
    // 09:00 on a Monday: 06:30 has gone, so it is tomorrow's.
    QCOMPARE(json.value(QStringLiteral("nextRun")).toString(),
             QStringLiteral("2026-10-13T06:30:00"));
  }

  void whatIsWrittenCanBeReadBack() {
    Schedule from = weekdays();
    from.executionMode = 1;
    from.active = true;

    Schedule to;
    QString why;
    QVERIFY2(ScheduleJson::Read(ScheduleJson::Write(from, aMonday()), &to, &why),
             qPrintable(why));
    QJsonObject a = ScheduleJson::Write(from, aMonday());
    QJsonObject b = ScheduleJson::Write(to, aMonday());
    a.remove(QStringLiteral("id")); // not something Read writes
    b.remove(QStringLiteral("id"));
    QCOMPARE(b, a);
  }

  void whatIsNotMentionedIsLeftAlone() {
    Schedule s = weekdays();
    QVERIFY(ScheduleJson::Read(one("time", QStringLiteral("18:05")), &s,
                               nullptr));
    QCOMPARE(s.hour, 18);
    QCOMPARE(s.minute, 5);
    QCOMPARE(s.name, QStringLiteral("nightly"));
    QVERIFY(s.monday && !s.sunday);
  }

  // The two flags storage keeps cannot be made to agree.
  void aScheduleIsDailyOrCronAndNeverBoth() {
    Schedule s = weekdays();
    QJsonObject asked = one("mode", QStringLiteral("cron"));
    asked.insert(QStringLiteral("cron"), QStringLiteral("30 6,18 * * MON-FRI"));
    QString why;
    QVERIFY2(ScheduleJson::Read(asked, &s, &why), qPrintable(why));
    QVERIFY(s.cronMode && !s.dailyMode);
    QVERIFY(s.nextRun(aMonday()).isValid());

    QVERIFY(ScheduleJson::Read(one("mode", QStringLiteral("daily")), &s,
                               nullptr));
    QVERIFY(!s.cronMode && s.dailyMode);
  }

  void whatCouldNeverComeDueIsRefusedAndSaysWhich_data() {
    QTest::addColumn<QString>("key");
    QTest::addColumn<QJsonValue>("value");
    QTest::newRow("no days") << "days" << QJsonValue(QJsonArray());
    QTest::newRow("not a day") << "days"
                               << QJsonValue(QJsonArray{QStringLiteral("someday")});
    QTest::newRow("days as text") << "days" << QJsonValue("mon");
    QTest::newRow("no such hour") << "time" << QJsonValue("24:00");
    QTest::newRow("no such minute") << "time" << QJsonValue("06:60");
    QTest::newRow("not a time") << "time" << QJsonValue("soon");
    QTest::newRow("mode") << "mode" << QJsonValue("weekly");
    QTest::newRow("runAs") << "runAs" << QJsonValue("later");
    QTest::newRow("active") << "active" << QJsonValue("yes");
    QTest::newRow("name") << "name" << QJsonValue(7);
  }

  void whatCouldNeverComeDueIsRefusedAndSaysWhich() {
    QFETCH(QString, key);
    QFETCH(QJsonValue, value);

    Schedule s = weekdays();
    const QJsonObject before = ScheduleJson::Write(s, aMonday());

    QJsonObject asked;
    asked.insert(QStringLiteral("name"), QStringLiteral("changed"));
    asked.insert(key, value);

    QString why;
    QVERIFY(!ScheduleJson::Read(asked, &s, &why));
    QVERIFY2(why.contains(key), qPrintable(why));
    // And nothing was half written.
    QCOMPARE(ScheduleJson::Write(s, aMonday()), before);
  }

  // One way of writing a moment, on every system: Qt's own "t" says
  // "GMT+7" on Windows and "+07" on Linux.
  void aMomentIsWrittenTheSameWayEverywhere() {
    const QDate day(2026, 10, 18);
    const QTime at(23, 54, 1);
    QCOMPARE(ScheduleStamp(QDateTime(day, at, QTimeZone::fromSecondsAheadOfUtc(7 * 3600))),
             QStringLiteral("Sun, 18/Oct/2026 23:54:01 GMT+7"));
    QCOMPARE(ScheduleStamp(QDateTime(day, at, QTimeZone::fromSecondsAheadOfUtc(19800))),
             QStringLiteral("Sun, 18/Oct/2026 23:54:01 GMT+5:30"));
    QCOMPARE(ScheduleStamp(QDateTime(day, at, QTimeZone::fromSecondsAheadOfUtc(-4 * 3600))),
             QStringLiteral("Sun, 18/Oct/2026 23:54:01 GMT-4"));
    QCOMPARE(ScheduleStamp(QDateTime(day, at, QTimeZone::UTC)),
             QStringLiteral("Sun, 18/Oct/2026 23:54:01 GMT"));
  }

  void aCronExpressionThatCannotBeReadIsRefused() {
    Schedule s = weekdays();
    QJsonObject asked = one("mode", QStringLiteral("cron"));
    asked.insert(QStringLiteral("cron"), QStringLiteral("whenever"));
    QString why;
    QVERIFY(!ScheduleJson::Read(asked, &s, &why));
    QVERIFY2(why.contains(QStringLiteral("cron")), qPrintable(why));
    QVERIFY(!s.cronMode);

    // But one that is only kept, while the schedule is daily, is not judged.
    QVERIFY(ScheduleJson::Read(one("cron", QStringLiteral("whenever")), &s,
                               nullptr));
  }
};

QTEST_APPLESS_MAIN(TestScheduleJson)
#include "test_schedule_json.moc"
