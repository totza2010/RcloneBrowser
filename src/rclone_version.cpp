#include "rclone_version.h"

#include <QRegularExpression>
#include <QStringList>

namespace {

// "rclone v1.71.1-DEV" -> "1.71.1". Also copes with a bare "v1.71.1" and
// with a build that says something else entirely before the number.
QString numberFrom(const QString &line) {
  static const QRegularExpression version(
      QStringLiteral("v?(\\d+(?:\\.\\d+)*)"));
  const QRegularExpressionMatch match = version.match(line);
  return match.hasMatch() ? match.captured(1) : QString();
}

QString withoutBullet(QString line) {
  line = line.trimmed();
  if (line.startsWith(QLatin1String("- "))) {
    line.remove(0, 2);
  }
  return line.trimmed();
}

} // namespace

RcloneVersion ParseRcloneVersion(const QByteArray &output) {
  RcloneVersion version;
  version.raw = QString::fromUtf8(output).trimmed();

  const QStringList lines =
      version.raw.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  if (lines.isEmpty()) {
    return version;
  }

  version.number = numberFrom(lines.first());

  // Very old builds printed the version and nothing else, which is why these
  // are read by position rather than by name.
  if (lines.size() > 1) {
    version.osLine = withoutBullet(lines.at(1));
  }
  if (lines.size() > 2) {
    version.goLine = withoutBullet(lines.at(2));
  }

  return version;
}

bool RcloneVersion::atLeast(const QString &wanted) const {
  if (number.isEmpty()) {
    return false;
  }

  const QStringList mine = number.split(QLatin1Char('.'));
  const QStringList theirs = wanted.split(QLatin1Char('.'));

  // Compared field by field, not as text: "1.9" is text-greater than "1.10"
  // and that is the wrong answer. A missing field counts as zero, so 1.71
  // and 1.71.0 are the same version.
  const int fields = qMax(mine.size(), theirs.size());
  for (int i = 0; i < fields; ++i) {
    const int a = i < mine.size() ? mine.at(i).toInt() : 0;
    const int b = i < theirs.size() ? theirs.at(i).toInt() : 0;
    if (a != b) {
      return a > b;
    }
  }
  return true;
}
