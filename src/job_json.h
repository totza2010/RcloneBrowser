#pragma once

// A job, as it is said to anything outside this program: the API's /jobs and
// its event stream, and a webhook. One function, so a webhook says about a
// job exactly what /jobs says.

#include <QJsonObject>

class RunningJob;

namespace JobJson {

// `streamIdleSeconds`: how long a stream may sit unplayed before it stops
// itself, 0 for never. Said with a stream; the API knows it, a webhook does
// not need it.
QJsonObject Write(const RunningJob &job, int streamIdleSeconds = 0);

} // namespace JobJson
