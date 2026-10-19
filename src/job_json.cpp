#include "job_json.h"
#include "running_job.h"

namespace JobJson {

QJsonObject Write(const RunningJob &job, int streamIdleSeconds) {
  QJsonObject object;
  object.insert(QStringLiteral("requestId"), job.requestId());
  object.insert(QStringLiteral("taskId"), job.taskId());
  object.insert(QStringLiteral("kind"), JobKindToString(job.kind()));
  object.insert(QStringLiteral("startedBy"), job.transferMode());
  object.insert(QStringLiteral("info"), job.description().info);
  object.insert(QStringLiteral("source"), job.description().source);
  object.insert(QStringLiteral("dest"), job.description().dest);
  object.insert(QStringLiteral("running"), job.isRunning());
  object.insert(QStringLiteral("dryRun"), job.isDryRun());
  object.insert(QStringLiteral("state"), job.finalStatus());
  object.insert(QStringLiteral("startedAt"),
                job.startedAt().toString(Qt::ISODate));

  // SECURITY: the command carries backend tokens and the remote-control
  // password. displayCommand() is the redacted form; the raw arguments must
  // never leave this process (docs/ARCHITECTURE.md section 5).
  object.insert(QStringLiteral("command"),
                job.displayCommand().join(QLatin1Char(' ')));

  if (!job.bandwidth().isEmpty()) {
    object.insert(QStringLiteral("bandwidth"), job.bandwidth());
  }

  // Where a stream can be opened: a path of this same server. The host is
  // not here: this program does not know by what name whoever is asking
  // reached it, and they do.
  if (job.kind() == JobKind::Stream && job.streamPort() != 0) {
    QJsonObject stream;
    stream.insert(QStringLiteral("path"), job.streamPath());
    stream.insert(QStringLiteral("stopsWhenIdleFor"), streamIdleSeconds);
    object.insert(QStringLiteral("stream"), stream);
  }

  // Where a serve listens. The host is not here for the same reason as a
  // stream's: whoever asks knows by what name they reached this machine.
  if (job.kind() == JobKind::Serve && job.serveAddress().port != 0) {
    const RunningJob::ServeAddress &address = job.serveAddress();
    QJsonObject serve;
    serve.insert(QStringLiteral("protocol"), address.protocol);
    serve.insert(QStringLiteral("port"), int(address.port));
    serve.insert(QStringLiteral("everyone"), address.everyone);
    serve.insert(QStringLiteral("readOnly"), address.readOnly);
    serve.insert(QStringLiteral("user"), address.user);
    object.insert(QStringLiteral("serve"), serve);
  }

  const JobStats &stats = job.stats();
  if (stats.valid) {
    QJsonObject figures;
    figures.insert(QStringLiteral("bytes"), double(stats.bytes));
    figures.insert(QStringLiteral("totalBytes"), double(stats.totalBytes));
    figures.insert(QStringLiteral("transfers"), double(stats.transfers));
    figures.insert(QStringLiteral("errors"), double(stats.errors));
    figures.insert(QStringLiteral("elapsedSeconds"), stats.elapsedSeconds);
    figures.insert(QStringLiteral("speed"), stats.speed);
    figures.insert(QStringLiteral("etaSeconds"), double(stats.etaSeconds));
    figures.insert(QStringLiteral("percent"), stats.percent());
    figures.insert(QStringLiteral("totalTransfers"),
                   double(stats.totalTransfers));
    object.insert(QStringLiteral("stats"), figures);
  }
  return object;
}

} // namespace JobJson
