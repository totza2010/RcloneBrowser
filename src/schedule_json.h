#pragma once

// Orchestration (L1): a schedule, as something other than this program can
// read and write.
//
// A schedule is stored as the scheduler card has always written it: seven
// booleans for the days, two more that say which of "daily" and "cron" is
// meant and are supposed never to agree, the hour and the minute apart, and
// a number for how to run. That is right for storage, which must go on
// reading what is already saved, and wrong to hand to another program.
//
// So this is the schedule in words: "daily" on "mon".."fri" at "06:30", run
// "now" or by the "queue". It is what the API speaks and what the page's
// form is built on. See docs/WEB-API-PLAN.md.

#include <QDateTime>
#include <QJsonObject>
#include <QString>

struct Schedule;

namespace ScheduleJson {

// The whole schedule, with when it next comes due counted from `now`.
QJsonObject Write(const Schedule &schedule, const QDateTime &now);

// Puts what `json` says onto `schedule`, leaving alone whatever it does not
// mention -- so the same function fills in a new schedule and changes an
// existing one. False with *why when the result is not a schedule: a day
// that is not a day, a time that is not a time, a cron expression that
// cannot be read, or a daily schedule with no day in it.
//
// What it will not write: the id, and what the last run did.
bool Read(const QJsonObject &json, Schedule *schedule, QString *why);

} // namespace ScheduleJson
