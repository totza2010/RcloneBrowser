#include "job_stats.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

QString JobTransferItem::speedText() const {
  return FormatBytes(static_cast<qint64>(speed)) + QStringLiteral("/s");
}

QString JobTransferItem::etaText() const { return FormatSeconds(etaSeconds); }

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

QString JobStats::etaText() const { return FormatSeconds(etaSeconds); }

QString JobStats::checksText() const {
  return QStringLiteral("%1 / %2").arg(checks).arg(totalChecks);
}

QString JobStats::transfersText() const {
  return QStringLiteral("%1 / %2").arg(transfers).arg(totalTransfers);
}

QString JobStats::elapsedText() const {
  return FormatSeconds(static_cast<qint64>(elapsedSeconds));
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
  stats.elapsedSeconds = root.value("elapsedTime").toDouble();
  stats.fatalError = root.value("fatalError").toBool(false);
  stats.retryError = root.value("retryError").toBool(false);

  // eta is null until rclone can estimate one, and stays null for backends
  // that cannot report a total size.
  const QJsonValue eta = root.value("eta");
  stats.etaSeconds = eta.isNull() ? -1 : eta.toVariant().toLongLong();

  const QJsonArray transferring = root.value("transferring").toArray();
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
