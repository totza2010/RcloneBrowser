#include "rclone_flags.h"
#include "utils.h"

#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>

QString RcloneFlag::insertion() const {
  return takesValue() ? name + QStringLiteral("=") : name;
}

// TEST: (V-14) เปิด Preferences -> ช่อง options -> พิมพ์ "--tel" ต้องขึ้นรายการ
// flag ของ teldrive จาก rclone ตัวที่ตั้งค่าไว้จริง ไม่ใช่รายการที่ฝังไว้
QList<RcloneFlag> ParseRcloneHelpFlags(const QByteArray &output) {
  QList<RcloneFlag> flags;

  // "Backend-only flags (these can be set in the config file also) (flag
  // group Backend):" -- the group name is in the last parenthesis, not the
  // first, so this anchors on the end of the line.
  static const QRegularExpression groupHeader(
      QStringLiteral(R"(\(flag group ([^)]+)\):\s*$)"));

  //   "  -c, --checksum            Check for changes with size & checksum"
  //   "      --compare-dest stringArray   Include additional server-side..."
  //   "      --check-first             Do all the checks before starting..."
  //
  // The type is optional and separated by a single space; the description
  // always starts after two or more. That run of spaces is the only thing
  // separating them, and it is what tells a boolean flag apart from one
  // taking a value, since neither is marked as such.
  //
  // The type is matched lazily rather than as a single word because a couple
  // of them contain spaces -- "--s3-use-accept-encoding-gzip Accept-Encoding:
  // gzip" and "--imagekit-only-signed Restrict unsigned image URLs". Requiring
  // one word dropped those two flags from the list entirely.
  static const QRegularExpression flagLine(
      QStringLiteral(R"(^ {2,}(?:(-[A-Za-z]), +)?(--[A-Za-z0-9][\w.-]*))"
                     R"((?: (\S(?:.*?\S)?))? {2,}(\S.*?)\s*$)"));

  QString group;
  // A flag can be listed under two groups -- --dry-run appears under both
  // Important and Config, --metadata-filter under both Filter and Metadata --
  // and it is the same flag either way. Thirteen of them would otherwise show
  // up twice in the list.
  QSet<QString> seen;

  const QStringList lines = QString::fromUtf8(output).split(QLatin1Char('\n'));
  for (const QString &line : lines) {
    const QRegularExpressionMatch header = groupHeader.match(line);
    if (header.hasMatch()) {
      group = header.captured(1);
      continue;
    }

    // Everything above the first header is "rclone help flags" describing
    // itself -- --group, --name and its own -h -- which are not flags any
    // rclone command would accept here.
    if (group.isEmpty()) {
      continue;
    }

    const QRegularExpressionMatch match = flagLine.match(line);
    if (!match.hasMatch()) {
      continue;
    }

    if (seen.contains(match.captured(2))) {
      continue;
    }
    seen.insert(match.captured(2));

    RcloneFlag flag;
    flag.shortName = match.captured(1);
    flag.name = match.captured(2);
    flag.type = match.captured(3);
    flag.description = match.captured(4);
    flag.group = group;
    flags.append(flag);
  }

  return flags;
}

// A backend that only one fork ships, and the repository that ships it.
// Add a row when another fork is worth recognising.
struct ForkMarker {
  QLatin1String flagPrefix;
  QLatin1String repo;
};

static const ForkMarker kForkMarkers[] = {
    {QLatin1String("--teldrive-"), QLatin1String("tgdrive/rclone")},
};

QString DetectRcloneRepo(const QList<RcloneFlag> &flags) {
  if (flags.isEmpty()) {
    return QString(); // not known yet, which is not the same as stock
  }

  for (const ForkMarker &marker : kForkMarkers) {
    for (const RcloneFlag &flag : flags) {
      if (flag.name.startsWith(marker.flagPrefix)) {
        return marker.repo;
      }
    }
  }

  // A build with no fork-only backend is upstream as far as anything here can
  // tell. Someone running a fork that adds nothing visible has to say so.
  return QStringLiteral("rclone/rclone");
}

RcloneFlagRegistry &RcloneFlagRegistry::instance() {
  static RcloneFlagRegistry registry;
  return registry;
}

RcloneFlagRegistry::RcloneFlagRegistry(QObject *parent) : QObject(parent) {}

void RcloneFlagRegistry::invalidate() {
  mFlags.clear();
  // An in-flight query is left alone; its answer is for the binary that was
  // configured when it started, and dropping it here would only leak.
}

void RcloneFlagRegistry::ensureLoaded() {
  if (mInFlight || isLoaded()) {
    return;
  }
  mInFlight = true;

  auto *process = new QProcess(this);
  process->setProcessChannelMode(QProcess::SeparateChannels);
  UseRclonePassword(process);

  QPointer<RcloneFlagRegistry> guard(this);
  QObject::connect(
      process,
      static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
          &QProcess::finished),
      this, [this, process, guard](int status, QProcess::ExitStatus) {
        process->deleteLater();
        if (!guard) {
          return;
        }
        mInFlight = false;

        if (status != 0) {
          return; // no completion rather than a wrong list
        }

        const QList<RcloneFlag> parsed =
            ParseRcloneHelpFlags(process->readAllStandardOutput());
        if (parsed.isEmpty()) {
          // An rclone too old for "help flags", or output in a shape this
          // does not know. Either way, guessing is worse than staying quiet.
          return;
        }

        mFlags = parsed;
        emit flagsChanged();
      });

  QObject::connect(process, &QProcess::errorOccurred, this,
                   [this, process, guard]() {
                     process->deleteLater();
                     if (guard) {
                       mInFlight = false;
                     }
                   });

  process->start(GetRclone(), QStringList() << "help"
                                            << "flags",
                 QIODevice::ReadOnly);
}
