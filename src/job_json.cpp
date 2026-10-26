#include "job_json.h"
#include "running_job.h"

#include <QJsonArray>

namespace JobJson {

QString Stamp(const QDateTime &when) {
  if (!when.isValid()) {
    return QString();
  }
  const QDateTime local = when.toLocalTime();
  // To the millisecond: the history is paged by "before this moment", and
  // runs begun within one second would otherwise fall between two pages.
  return local.toOffsetFromUtc(local.offsetFromUtc())
      .toString(Qt::ISODateWithMs);
}

QString Stamp(qint64 msSinceEpoch) {
  return msSinceEpoch > 0 ? Stamp(QDateTime::fromMSecsSinceEpoch(msSinceEpoch))
                          : QString();
}

QJsonObject Write(const RunningJob &job, int streamIdleSeconds) {
  QJsonObject object;
  object.insert(QStringLiteral("requestId"), job.requestId());
  object.insert(QStringLiteral("taskId"), job.taskId());
  object.insert(QStringLiteral("kind"), JobKindToString(job.kind()));
  object.insert(QStringLiteral("startedBy"), job.transferMode());
  // Who put it in the queue, when it came from there: a key's name, the
  // window, a schedule, a watched folder.
  object.insert(QStringLiteral("askedBy"), job.askedBy());
  object.insert(QStringLiteral("info"), job.description().info);
  object.insert(QStringLiteral("source"), job.description().source);
  object.insert(QStringLiteral("dest"), job.description().dest);
  object.insert(QStringLiteral("running"), job.isRunning());
  object.insert(QStringLiteral("dryRun"), job.isDryRun());
  object.insert(QStringLiteral("state"), job.finalStatus());
  object.insert(QStringLiteral("startedAt"), Stamp(job.startedAt()));

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

  // The script a mount runs once it is up, and what it has said: the core
  // runs it, this only reports it.
  if (!job.mountScript().isEmpty()) {
    const RunningJob::ScriptReport &report = job.scriptReport();
    QJsonObject script;
    script.insert(QStringLiteral("path"), job.mountScript());
    script.insert(QStringLiteral("started"), report.started);
    script.insert(QStringLiteral("running"), report.running);
    script.insert(QStringLiteral("exitCode"),
                  report.exitCode >= 0 ? QJsonValue(report.exitCode)
                                       : QJsonValue(QJsonValue::Null));
    if (!report.error.isEmpty()) {
      script.insert(QStringLiteral("error"), report.error);
    }
    script.insert(QStringLiteral("output"), QJsonArray::fromStringList(report.lines));
    object.insert(QStringLiteral("script"), script);
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
    figures.insert(QStringLiteral("averageSpeed"), stats.averageSpeed);
    if (stats.sourceFiles >= 0) {
      figures.insert(QStringLiteral("sourceFiles"), double(stats.sourceFiles));
    }
    // What the page shows as "x of y files": the same rule as the window's.
    figures.insert(QStringLiteral("filesToTransfer"), double(stats.filesToTransfer()));
    figures.insert(QStringLiteral("totalTransfers"),
                   double(stats.totalTransfers));
    figures.insert(QStringLiteral("checks"), double(stats.checks));
    figures.insert(QStringLiteral("totalChecks"), double(stats.totalChecks));
    figures.insert(QStringLiteral("deletes"), double(stats.deletes));
    figures.insert(QStringLiteral("renames"), double(stats.renames));
    // What it is doing, in the words the window's card uses.
    const JobPhase phase = stats.phase();
    figures.insert(QStringLiteral("phase"),
                   phase == JobPhase::Starting   ? QStringLiteral("starting")
                   : phase == JobPhase::Scanning ? QStringLiteral("scanning")
                   : phase == JobPhase::Finishing ? QStringLiteral("finishing")
                                                  : QStringLiteral("transferring"));
    // Each file on its way, as the window draws a bar for each. A size of
    // -1 is a backend that cannot say one up front: no percent, no ETA.
    QJsonArray files;
    for (const JobTransferItem &item : stats.transferring) {
      QJsonObject file;
      file.insert(QStringLiteral("name"), item.name);
      file.insert(QStringLiteral("bytes"), double(item.bytes));
      file.insert(QStringLiteral("size"), double(item.size));
      file.insert(QStringLiteral("percent"), item.size >= 0 ? item.percentage : -1);
      file.insert(QStringLiteral("speed"), item.speed);
      file.insert(QStringLiteral("etaSeconds"), double(item.etaSeconds));
      files.append(file);
    }
    figures.insert(QStringLiteral("transferring"), files);
    object.insert(QStringLiteral("stats"), figures);
  }
  return object;
}

} // namespace JobJson
