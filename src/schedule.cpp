#include "schedule.h"
#include "qcron.h"

#include <QUuid>

namespace {

// The file put every free-text value through base64, so a name with a comma
// in it could survive a comma-separated line. The database does not need
// that, but the widget still reads and writes this form, so the encoding is
// kept exactly where it was.
QString encode(const QString &text) {
  return QString::fromUtf8(text.toUtf8().toBase64());
}

QString decode(const QString &text) {
  return QString::fromUtf8(QByteArray::fromBase64(text.toUtf8()));
}

QString valueOf(const QStringList &args, const QString &key) {
  const int at = args.indexOf(key);
  if (at == -1 || at + 1 >= args.size()) {
    return QString();
  }
  return args.at(at + 1);
}

bool boolOf(const QStringList &args, const QString &key, bool fallback) {
  const QString value = valueOf(args, key);
  if (value.isEmpty()) {
    return fallback;
  }
  return value == QStringLiteral("true");
}

QString boolText(bool value) {
  return value ? QStringLiteral("true") : QStringLiteral("false");
}

// Hours and minutes were stored as the two-character strings the spin boxes
// show ("00", "07"), and are read back as numbers here.
QString twoDigits(int value) {
  return QStringLiteral("%1").arg(value, 2, 10, QLatin1Char('0'));
}

} // namespace

QString NormalizeCron(const QString &cron) {
  QString out = cron.trimmed().toUpper();

  static const char *kDays[] = {"MON", "TUE", "WED", "THU",
                                "FRI", "SAT", "SUN"};
  for (int i = 0; i < 7; ++i) {
    out.replace(QLatin1String(kDays[i]), QString::number(i + 1));
  }

  static const char *kMonths[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                  "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  for (int i = 0; i < 12; ++i) {
    out.replace(QLatin1String(kMonths[i]), QString::number(i + 1));
  }

  // The sixth field is the year, which nothing here ever restricts.
  return out + QStringLiteral(" *");
}

bool Schedule::runsOn(Qt::DayOfWeek day) const {
  switch (day) {
  case Qt::Monday:
    return monday;
  case Qt::Tuesday:
    return tuesday;
  case Qt::Wednesday:
    return wednesday;
  case Qt::Thursday:
    return thursday;
  case Qt::Friday:
    return friday;
  case Qt::Saturday:
    return saturday;
  case Qt::Sunday:
    return sunday;
  }
  return false;
}

bool Schedule::isDue(const QDateTime &now) const {
  if (cronMode) {
    QString pattern = NormalizeCron(cron);
    QCron parser(pattern);
    return parser.isValid() && parser.match(now);
  }
  if (!dailyMode) {
    return false;
  }
  return runsOn(static_cast<Qt::DayOfWeek>(now.date().dayOfWeek())) &&
         now.time().hour() == hour && now.time().minute() == minute;
}

QDateTime Schedule::nextRun(const QDateTime &from) const {
  if (cronMode) {
    QString pattern = NormalizeCron(cron);
    QCron parser(pattern);
    if (!parser.isValid()) {
      return QDateTime();
    }
    return parser.next(from);
  }

  if (!dailyMode) {
    return QDateTime();
  }

  const QTime at(hour, minute);

  // Today counts only if the time has not gone by yet; a schedule set for
  // 07:00 and read at 09:00 is due tomorrow, not immediately.
  for (int ahead = 0; ahead <= 7; ++ahead) {
    const QDate date = from.date().addDays(ahead);
    if (!runsOn(static_cast<Qt::DayOfWeek>(date.dayOfWeek()))) {
      continue;
    }
    const QDateTime candidate(date, at);
    if (candidate > from) {
      return candidate;
    }
  }

  // No day ticked: it never comes due, which is a real answer and not an
  // error. Returning "tomorrow" would make a disabled schedule look armed.
  return QDateTime();
}

QStringList Schedule::toArgs() const {
  QStringList args;
  args << QStringLiteral("mSchedulerId") << id;
  args << QStringLiteral("mSchedulerName") << encode(name);
  args << QStringLiteral("mTaskId") << taskId;
  args << QStringLiteral("mTaskName") << encode(taskName);
  args << QStringLiteral("mLastRun") << encode(lastRun);
  args << QStringLiteral("mRequestId") << requestId;
  args << QStringLiteral("mLastRunFinished") << encode(lastFinished);
  args << QStringLiteral("mLastRunStatus") << encode(lastStatus);
  args << QStringLiteral("mSchedulerStatus")
       << (active ? QStringLiteral("activated") : QStringLiteral("paused"));

  args << QStringLiteral("mDailyState") << boolText(dailyMode);
  args << QStringLiteral("mDailyMon") << boolText(monday);
  args << QStringLiteral("mDailyTue") << boolText(tuesday);
  args << QStringLiteral("mDailyWed") << boolText(wednesday);
  args << QStringLiteral("mDailyThu") << boolText(thursday);
  args << QStringLiteral("mDailyFri") << boolText(friday);
  args << QStringLiteral("mDailySat") << boolText(saturday);
  args << QStringLiteral("mDailySun") << boolText(sunday);
  args << QStringLiteral("mDailyHour") << twoDigits(hour);
  args << QStringLiteral("mDailyMinute") << twoDigits(minute);

  args << QStringLiteral("mCronState") << boolText(cronMode);
  args << QStringLiteral("mCron") << encode(cron);
  args << QStringLiteral("mExecutionMode") << QString::number(executionMode);

  return args;
}

Schedule Schedule::fromArgs(const QStringList &args) {
  Schedule schedule;

  schedule.id = valueOf(args, QStringLiteral("mSchedulerId"));
  if (schedule.id.isEmpty()) {
    schedule.id = QUuid::createUuid().toString();
  }
  schedule.name = decode(valueOf(args, QStringLiteral("mSchedulerName")));
  schedule.taskId = valueOf(args, QStringLiteral("mTaskId"));
  schedule.taskName = decode(valueOf(args, QStringLiteral("mTaskName")));
  schedule.requestId = valueOf(args, QStringLiteral("mRequestId"));

  schedule.active = valueOf(args, QStringLiteral("mSchedulerStatus")) ==
                    QStringLiteral("activated");

  schedule.dailyMode = boolOf(args, QStringLiteral("mDailyState"), true);
  schedule.monday = boolOf(args, QStringLiteral("mDailyMon"), true);
  schedule.tuesday = boolOf(args, QStringLiteral("mDailyTue"), true);
  schedule.wednesday = boolOf(args, QStringLiteral("mDailyWed"), true);
  schedule.thursday = boolOf(args, QStringLiteral("mDailyThu"), true);
  schedule.friday = boolOf(args, QStringLiteral("mDailyFri"), true);
  schedule.saturday = boolOf(args, QStringLiteral("mDailySat"), true);
  schedule.sunday = boolOf(args, QStringLiteral("mDailySun"), true);
  schedule.hour = valueOf(args, QStringLiteral("mDailyHour")).toInt();
  schedule.minute = valueOf(args, QStringLiteral("mDailyMinute")).toInt();

  schedule.cronMode = boolOf(args, QStringLiteral("mCronState"), false);
  const QString cron = decode(valueOf(args, QStringLiteral("mCron")));
  if (!cron.isEmpty()) {
    schedule.cron = cron;
  }
  schedule.executionMode =
      valueOf(args, QStringLiteral("mExecutionMode")).toInt();

  const QString lastRun = decode(valueOf(args, QStringLiteral("mLastRun")));
  if (!lastRun.isEmpty()) {
    schedule.lastRun = lastRun;
  }
  schedule.lastFinished =
      decode(valueOf(args, QStringLiteral("mLastRunFinished")));
  schedule.lastStatus = decode(valueOf(args, QStringLiteral("mLastRunStatus")));

  return schedule;
}
