#include "schedule_json.h"
#include "qcron.h"
#include "schedule.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QStringList>

namespace {

// Monday first, as Qt::DayOfWeek has it.
const QStringList kDays = {QStringLiteral("mon"), QStringLiteral("tue"),
                           QStringLiteral("wed"), QStringLiteral("thu"),
                           QStringLiteral("fri"), QStringLiteral("sat"),
                           QStringLiteral("sun")};

bool *dayOf(Schedule *schedule, int index) {
  switch (index) {
  case 0:
    return &schedule->monday;
  case 1:
    return &schedule->tuesday;
  case 2:
    return &schedule->wednesday;
  case 3:
    return &schedule->thursday;
  case 4:
    return &schedule->friday;
  case 5:
    return &schedule->saturday;
  default:
    return &schedule->sunday;
  }
}

bool fail(QString *why, const QString &reason) {
  if (why != nullptr) {
    *why = reason;
  }
  return false;
}

} // namespace

namespace ScheduleJson {

QJsonObject Write(const Schedule &schedule, const QDateTime &now) {
  QJsonObject json;
  json.insert(QStringLiteral("id"), schedule.id);
  json.insert(QStringLiteral("name"), schedule.name);
  json.insert(QStringLiteral("taskId"), schedule.taskId);
  json.insert(QStringLiteral("taskName"), schedule.taskName);
  json.insert(QStringLiteral("active"), schedule.active);

  json.insert(QStringLiteral("mode"), schedule.cronMode
                                          ? QStringLiteral("cron")
                                          : QStringLiteral("daily"));
  QJsonArray days;
  Schedule copy = schedule;
  for (int i = 0; i < kDays.size(); ++i) {
    if (*dayOf(&copy, i)) {
      days.append(kDays.at(i));
    }
  }
  json.insert(QStringLiteral("days"), days);
  json.insert(QStringLiteral("time"),
              QStringLiteral("%1:%2")
                  .arg(schedule.hour, 2, 10, QLatin1Char('0'))
                  .arg(schedule.minute, 2, 10, QLatin1Char('0')));
  json.insert(QStringLiteral("cron"), schedule.cron);

  json.insert(QStringLiteral("runAs"), schedule.executionMode == 1
                                           ? QStringLiteral("queue")
                                           : QStringLiteral("now"));
  // Kept for whoever read the first shape of this, which had only this.
  json.insert(QStringLiteral("executionMode"), schedule.executionMode);

  json.insert(QStringLiteral("lastRun"), schedule.lastRun);
  json.insert(QStringLiteral("lastFinished"), schedule.lastFinished);
  json.insert(QStringLiteral("lastStatus"), schedule.lastStatus);

  const QDateTime next = schedule.nextRun(now);
  json.insert(QStringLiteral("nextRun"),
              next.isValid() ? next.toString(Qt::ISODate) : QString());
  return json;
}

bool Read(const QJsonObject &json, Schedule *schedule, QString *why) {
  if (schedule == nullptr) {
    return fail(why, QStringLiteral("there is no schedule to write to"));
  }

  // On a copy, so a request that is wrong half way through changes nothing.
  Schedule next = *schedule;

  const auto text = [&json, why](const char *key, QString *into) {
    const QJsonValue value = json.value(QLatin1String(key));
    if (value.isUndefined()) {
      return true;
    }
    if (!value.isString()) {
      return fail(why, QStringLiteral("\"%1\" has to be text")
                           .arg(QLatin1String(key)));
    }
    *into = value.toString().trimmed();
    return true;
  };

  if (!text("name", &next.name) || !text("taskId", &next.taskId) ||
      !text("cron", &next.cron)) {
    return false;
  }

  if (json.contains(QStringLiteral("active"))) {
    const QJsonValue value = json.value(QStringLiteral("active"));
    if (!value.isBool()) {
      return fail(why, QStringLiteral("\"active\" has to be true or false"));
    }
    next.active = value.toBool();
  }

  if (json.contains(QStringLiteral("mode"))) {
    const QString asked = json.value(QStringLiteral("mode")).toString();
    if (asked != QLatin1String("daily") && asked != QLatin1String("cron")) {
      return fail(why, QStringLiteral("\"mode\" has to be daily or cron"));
    }
    // The two flags storage keeps are set together, so they cannot both be
    // on or both off -- which is a schedule that never comes due and does
    // not say why.
    next.cronMode = asked == QLatin1String("cron");
    next.dailyMode = !next.cronMode;
  }

  if (json.contains(QStringLiteral("days"))) {
    const QJsonValue value = json.value(QStringLiteral("days"));
    if (!value.isArray()) {
      return fail(why, QStringLiteral("\"days\" has to be a list, of mon tue "
                                      "wed thu fri sat sun"));
    }
    for (int i = 0; i < kDays.size(); ++i) {
      *dayOf(&next, i) = false;
    }
    for (const QJsonValue &day : value.toArray()) {
      const int at = kDays.indexOf(day.toString().toLower());
      if (at < 0) {
        return fail(why, QStringLiteral("\"days\" has to be a list, of mon "
                                        "tue wed thu fri sat sun"));
      }
      *dayOf(&next, at) = true;
    }
  }

  if (json.contains(QStringLiteral("time"))) {
    static const QRegularExpression clock(
        QStringLiteral("^([0-9]{1,2}):([0-9]{2})$"));
    const QRegularExpressionMatch match =
        clock.match(json.value(QStringLiteral("time")).toString());
    const int hour = match.captured(1).toInt();
    const int minute = match.captured(2).toInt();
    if (!match.hasMatch() || hour > 23 || minute > 59) {
      return fail(why, QStringLiteral(
                           "\"time\" has to be a time of day, such as 06:30"));
    }
    next.hour = hour;
    next.minute = minute;
  }

  if (json.contains(QStringLiteral("runAs"))) {
    const QString asked = json.value(QStringLiteral("runAs")).toString();
    if (asked != QLatin1String("now") && asked != QLatin1String("queue")) {
      return fail(why, QStringLiteral("\"runAs\" has to be now or queue"));
    }
    next.executionMode = asked == QLatin1String("queue") ? 1 : 0;
  }

  // What was asked for, taken together: it has to be something that can
  // come due. One that cannot is not an error to the clock -- it simply
  // never fires -- so this is the only place it would ever be said.
  if (next.cronMode) {
    // Named, because the parser takes what it reads by reference.
    QString pattern = NormalizeCron(next.cron);
    QCron parser(pattern);
    if (!parser.isValid()) {
      return fail(why,
                  QStringLiteral("\"cron\" is not an expression that can be "
                                 "read: five fields, such as 30 6 * * MON-FRI"));
    }
  } else {
    bool any = false;
    for (int i = 0; i < kDays.size(); ++i) {
      any = any || *dayOf(&next, i);
    }
    if (!any) {
      return fail(why, QStringLiteral(
                           "\"days\" is empty: a daily schedule needs at "
                           "least one day"));
    }
  }

  *schedule = next;
  return true;
}

} // namespace ScheduleJson
