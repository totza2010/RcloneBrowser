#pragma once

// A job, as it is said to anything outside this program: the API's /jobs and
// its event stream, and a webhook. One function, so a webhook says about a
// job exactly what /jobs says.

#include <QDateTime>
#include <QJsonObject>
#include <QString>

class RunningJob;

namespace JobJson {

// `streamIdleSeconds`: how long a stream may sit unplayed before it stops
// itself, 0 for never. Said with a stream; the API knows it, a webhook does
// not need it.
QJsonObject Write(const RunningJob &job, int streamIdleSeconds = 0);

// A moment, with its offset from UTC: "2026-10-19T10:52:00+07:00". Without
// one, "10:52" is a different instant to every reader in another time zone,
// and a program reading it has no window to look at to guess which. Empty
// for a moment that never was (invalid, or zero).
QString Stamp(const QDateTime &when);
QString Stamp(qint64 msSinceEpoch);

} // namespace JobJson
