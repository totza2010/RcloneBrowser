#include "job_stats.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>

#include <algorithm>
#include <QStringList>

QString FormatBytes(qint64 bytes) {
  if (bytes < 0) {
    return QStringLiteral("-");
  }
  static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
  double value = static_cast<double>(bytes);
  int unit = 0;
  while (value >= 1024.0 && unit < 5) {
    value /= 1024.0;
    ++unit;
  }
  // Whole bytes read oddly with a decimal point.
  const int decimals = (unit == 0) ? 0 : (value < 10.0 ? 2 : 1);
  return QStringLiteral("%1 %2")
      .arg(value, 0, 'f', decimals)
      .arg(QLatin1String(units[unit]));
}

QString FormatSeconds(qint64 seconds) {
  if (seconds < 0) {
    return QStringLiteral("-");
  }
  const qint64 h = seconds / 3600;
  const qint64 m = (seconds % 3600) / 60;
  const qint64 s = seconds % 60;
  if (h > 0) {
    return QStringLiteral("%1h%2m%3s").arg(h).arg(m).arg(s);
  }
  if (m > 0) {
    return QStringLiteral("%1m%2s").arg(m).arg(s);
  }
  return QStringLiteral("%1s").arg(s);
}

QString FormatEta(qint64 seconds) {
  if (seconds < 0) {
    return QStringLiteral("-");
  }
  if (seconds < 60) {
    return QStringLiteral("%1s").arg(seconds);
  }
  if (seconds < 3600) {
    return QStringLiteral("%1m %2s").arg(seconds / 60).arg(seconds % 60);
  }
  if (seconds < 86400) {
    return QStringLiteral("%1h %2m")
        .arg(seconds / 3600)
        .arg((seconds % 3600) / 60);
  }
  return QStringLiteral("%1d %2h")
      .arg(seconds / 86400)
      .arg((seconds % 86400) / 3600);
}

QString JoinProgressParts(const QStringList &parts) {
  // Wide enough to read as a break rather than as punctuation inside a figure.
  return parts.join(QStringLiteral("  ·  "));
}

QString JobTransferItem::speedText() const {
  return FormatBytes(static_cast<qint64>(speed)) + QStringLiteral("/s");
}

QString JobTransferItem::etaText() const { return FormatEta(etaSeconds); }

QStringList JobTransferItem::progressParts() const {
  QStringList parts;

  if (size >= 0) {
    parts << QStringLiteral("%1%").arg(percentage);
    parts << FormatBytes(bytes) + QStringLiteral(" / ") + FormatBytes(size);
  } else {
    // No size to divide by, so percentage and ETA would both be made up.
    // The byte count is the only figure that is actually known.
    parts << FormatBytes(bytes);
  }

  if (speed > 0) {
    parts << speedText();
  }
  if (size >= 0 && etaSeconds >= 0) {
    parts << FormatEta(etaSeconds) + QStringLiteral(" left");
  }

  return parts;
}

QString JobTransferItem::progressText() const {
  return JoinProgressParts(progressParts());
}

int JobStats::percent() const {
  if (totalBytes <= 0) {
    return 0;
  }
  const qint64 pct = bytes * 100 / totalBytes;
  return static_cast<int>(std::clamp<qint64>(pct, 0, 100));
}

QString JobStats::sizeText() const {
  return FormatBytes(bytes) + QStringLiteral(" / ") + FormatBytes(totalBytes);
}

QString JobStats::speedText() const {
  return FormatBytes(static_cast<qint64>(speed)) + QStringLiteral("/s");
}

QString JobStats::etaText() const { return FormatEta(etaSeconds); }

QString JobStats::checksText() const {
  return QStringLiteral("%1 / %2").arg(checks).arg(totalChecks);
}

QString JobStats::transfersText() const {
  // Out of every file there is, once that is known: rclone's own total is
  // the files begun so far -- "0 / 2" for a folder of 328.
  return QStringLiteral("%1 / %2")
      .arg(transfers)
      .arg(sourceFiles >= 0 ? sourceFiles : totalTransfers);
}

QString JobStats::elapsedText() const {
  return FormatSeconds(static_cast<qint64>(elapsedSeconds));
}

JobPhase JobStats::phase() const {
  if (totalBytes > 0) {
    // Everything counted has moved, but the process is still up: rclone is
    // setting modification times, removing what the sync deleted and closing
    // the backends. The card would otherwise sit at 100% looking stuck.
    if (bytes >= totalBytes &&
        (totalTransfers == 0 || transfers >= totalTransfers)) {
      return JobPhase::Finishing;
    }
    return JobPhase::Transferring;
  }

  // No total yet. rclone is still walking the source, or the job has nothing
  // to do at all.
  if (listed > 0 || checks > 0 || totalChecks > 0 || transfers > 0) {
    return JobPhase::Scanning;
  }
  return JobPhase::Starting;
}

QString JobStats::phaseText() const {
  switch (phase()) {
  case JobPhase::Starting:
    return QStringLiteral("Starting");
  case JobPhase::Scanning:
    // "listed" is only reported by newer rclone; without it the word alone
    // still beats a percentage that has nothing behind it.
    if (listed > 0) {
      return QStringLiteral("Scanning — %1 listed")
          .arg(QLocale::system().toString(listed));
    }
    return QStringLiteral("Scanning");
  case JobPhase::Finishing:
    return QStringLiteral("Finishing");
  case JobPhase::Transferring:
    break;
  }
  return QString();
}

QStringList JobStats::progressParts() const {
  QStringList parts;

  parts << QStringLiteral("%1%").arg(percent());
  parts << sizeText();

  if (speed > 0) {
    parts << speedText();
  }
  if (etaSeconds >= 0) {
    parts << FormatEta(etaSeconds) + QStringLiteral(" left");
  }

  return parts;
}

QString JobStats::progressText() const {
  return JoinProgressParts(progressParts());
}

JobStats JobStats::fromCoreStats(const QByteArray &json) {
  JobStats stats;

  QJsonParseError error{};
  const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject()) {
    return stats; // valid stays false
  }

  const QJsonObject root = doc.object();

  // An error response ({"error": "...", "status": 500}) is not stats.
  if (root.contains(QLatin1String("error")) &&
      !root.contains(QLatin1String("bytes"))) {
    return stats;
  }

  stats.bytes = root.value("bytes").toVariant().toLongLong();
  stats.totalBytes = root.value("totalBytes").toVariant().toLongLong();
  stats.checks = root.value("checks").toVariant().toLongLong();
  stats.totalChecks = root.value("totalChecks").toVariant().toLongLong();
  stats.transfers = root.value("transfers").toVariant().toLongLong();
  stats.totalTransfers = root.value("totalTransfers").toVariant().toLongLong();
  stats.deletes = root.value("deletes").toVariant().toLongLong();
  stats.renames = root.value("renames").toVariant().toLongLong();
  stats.errors = root.value("errors").toVariant().toLongLong();
  stats.listed = root.value("listed").toVariant().toLongLong();
  stats.speed = root.value("speed").toDouble();
  stats.averageSpeed = stats.speed;
  stats.elapsedSeconds = root.value("elapsedTime").toDouble();
  stats.fatalError = root.value("fatalError").toBool(false);
  stats.retryError = root.value("retryError").toBool(false);

  // eta is null until rclone can estimate one, and stays null for backends
  // that cannot report a total size.
  const QJsonValue eta = root.value("eta");
  stats.etaSeconds = eta.isNull() ? -1 : eta.toVariant().toLongLong();

  const QJsonArray transferring = root.value("transferring").toArray();
  double now = 0.0;
  for (const QJsonValue &value : transferring) {
    const QJsonObject item = value.toObject();
    JobTransferItem entry;
    entry.name = item.value("name").toString();
    if (entry.name.isEmpty()) {
      continue;
    }
    entry.bytes = item.value("bytes").toVariant().toLongLong();
    // Backends that cannot size a file up front omit this or send 0.
    entry.size = item.contains(QLatin1String("size"))
                     ? item.value("size").toVariant().toLongLong()
                     : -1;
    entry.percentage = static_cast<int>(std::clamp<qint64>(
        item.value("percentage").toVariant().toLongLong(), 0, 100));
    entry.speed = item.value("speed").toDouble();
    const QJsonValue itemEta = item.value("eta");
    entry.etaSeconds = itemEta.isNull() || itemEta.isUndefined()
                           ? -1
                           : itemEta.toVariant().toLongLong();
    stats.transferring.append(entry);
    now += entry.speed;
  }

  // The speed shown is what the files in flight are going at now, added up:
  // the same figures shown beside each file, so the total is never less than
  // its parts. (Each file's speedAvg was tried, and lagged far behind a file
  // just begun -- the total read 63 KB/s beside files at 343.) rclone's
  // "speed" is an average for the whole job, which a resumed upload throws
  // well off: a real 1.17 TB upload to teldrive showed 16.4 MB/s beside two
  // files at 29.1 and 13.2, and a time left of 19 hours for 7.5 (2026-10-21).
  // Between files nothing is in flight, and rclone's figures stand.
  if (now > 0) {
    stats.speed = now;
    if (stats.totalBytes > stats.bytes) {
      stats.etaSeconds = static_cast<qint64>((stats.totalBytes - stats.bytes) / now);
    }
  }

  stats.valid = true;
  return stats;
}

bool IsRcPollingNoise(const QString &line) {
  // 'DEBUG : rc: "core/stats": with parameters map[]'
  // 'DEBUG : rc: "core/stats": reply map[bytes:0 ...]'
  // Only the endpoints the job card polls are filtered; anything else the
  // remote control does stays visible.
  return line.contains(QLatin1String("rc: \"core/stats\"")) ||
         line.contains(QLatin1String("rc: \"core/quit\""));
}

quint16 ParseRcServingPort(const QString &line) {
  // "2026/08/03 22:39:45 NOTICE: Serving remote control on http://127.0.0.1:5555/"
  static const QRegularExpression rx(
      R"(Serving remote control on\s+https?://[^\s:/]+:(\d{1,5})/?)");
  const QRegularExpressionMatch match = rx.match(line);
  if (!match.hasMatch()) {
    return 0;
  }
  bool ok = false;
  const uint port = match.captured(1).toUInt(&ok);
  if (!ok || port == 0 || port > 65535) {
    return 0;
  }
  return static_cast<quint16>(port);
}

qint64 ParseRcloneSize(const QString &text) {
  static const QRegularExpression pattern(
      QStringLiteral("^\\s*([0-9]+(?:\\.[0-9]+)?)\\s*(B|KiB|MiB|GiB|TiB|PiB|EiB)\\s*$"));
  const QRegularExpressionMatch match = pattern.match(text);
  if (!match.hasMatch()) {
    return -1;
  }

  static const QStringList units = {
      QStringLiteral("B"),   QStringLiteral("KiB"), QStringLiteral("MiB"),
      QStringLiteral("GiB"), QStringLiteral("TiB"), QStringLiteral("PiB"),
      QStringLiteral("EiB")};
  double value = match.captured(1).toDouble();
  for (int i = units.indexOf(match.captured(2)); i > 0; --i) {
    value *= 1024.0;
  }
  return qint64(value + 0.5);
}

bool ApplyFinalTotalsLine(const QString &line, JobStats *stats) {
  if (stats == nullptr) {
    return false;
  }
  const QString text = line.trimmed();

  const auto after = [&text](const char *label) {
    return text.mid(int(qstrlen(label))).trimmed();
  };

  if (text.startsWith(QLatin1String("Transferred:"))) {
    // Two lines share this label: one counts bytes, the other counts files.
    // The part before the first comma is "a / b" either way.
    const QString pair = after("Transferred:").section(QLatin1Char(','), 0, 0);
    const QString left = pair.section(QLatin1Char('/'), 0, 0).trimmed();
    const QString right = pair.section(QLatin1Char('/'), 1, 1).trimmed();

    const qint64 sent = ParseRcloneSize(left);
    if (sent >= 0) {
      stats->bytes = sent;
      const qint64 total = ParseRcloneSize(right);
      if (total >= 0) {
        stats->totalBytes = total;
      }
      return true;
    }

    bool leftOk = false;
    bool rightOk = false;
    const qint64 done = left.toLongLong(&leftOk);
    const qint64 all = right.toLongLong(&rightOk);
    if (leftOk && rightOk) {
      stats->transfers = done;
      stats->totalTransfers = all;
      return true;
    }
    return false;
  }

  if (text.startsWith(QLatin1String("Checks:"))) {
    const QString pair = after("Checks:").section(QLatin1Char(','), 0, 0);
    bool leftOk = false;
    bool rightOk = false;
    const qint64 done =
        pair.section(QLatin1Char('/'), 0, 0).trimmed().toLongLong(&leftOk);
    const qint64 all =
        pair.section(QLatin1Char('/'), 1, 1).trimmed().toLongLong(&rightOk);
    if (leftOk && rightOk) {
      stats->checks = done;
      stats->totalChecks = all;
      return true;
    }
    return false;
  }

  if (text.startsWith(QLatin1String("Errors:"))) {
    bool ok = false;
    const qint64 count =
        after("Errors:").section(QLatin1Char(' '), 0, 0).toLongLong(&ok);
    if (ok) {
      stats->errors = count;
      return true;
    }
  }
  return false;
}

QStringList FinalTotalsFlags(const QStringList &args) {
  bool statsLevel = false;
  bool statsInterval = false;
  bool verbose = false;

  for (const QString &arg : args) {
    if (arg.startsWith(QLatin1String("--stats-log-level"))) {
      statsLevel = true;
    } else if (arg == QLatin1String("--stats") ||
               arg.startsWith(QLatin1String("--stats="))) {
      statsInterval = true;
    } else if (arg == QLatin1String("-v") || arg == QLatin1String("-vv") ||
               arg == QLatin1String("--verbose") ||
               arg.startsWith(QLatin1String("--log-level"))) {
      verbose = true;
    }
  }

  QStringList flags;
  if (!statsLevel) {
    flags << QStringLiteral("--stats-log-level") << QStringLiteral("NOTICE");
  }
  // Only when the task said nothing about it either way. Somebody who asked
  // for verbose output wants the report every minute; somebody who set an
  // interval chose one.
  if (!statsInterval && !verbose && !statsLevel) {
    flags << QStringLiteral("--stats") << QStringLiteral("8760h");
  }
  return flags;
}
