#include "extra_flags.h"
#include "rclone_flags.h"
#include "utils.h"

#include <QStringList>

namespace {

// Never, wherever this runs: they run a program, move rclone's own
// configuration, open its remote control, or put secrets where the API can
// read them back (a job's log is served by the API, and --dump writes
// request headers, tokens included, into it).
const QStringList kNever = {
    QStringLiteral("--config"),         QStringLiteral("--password-command"),
    QStringLiteral("--metadata-mapper"), QStringLiteral("--sftp-ssh"),
    QStringLiteral("--log-file"),       QStringLiteral("--cache-dir"),
    QStringLiteral("--temp-dir"),       QStringLiteral("--dump"),
    QStringLiteral("--dump-headers"),   QStringLiteral("--dump-bodies")};

// Only where the API is held to a list of folders: each of these names, or
// follows a link to, somewhere else on this machine.
const QStringList kOutsideTheLine = {
    QStringLiteral("--copy-links"),   QStringLiteral("--files-from"),
    QStringLiteral("--files-from-raw"), QStringLiteral("--include-from"),
    QStringLiteral("--exclude-from"), QStringLiteral("--filter-from"),
    QStringLiteral("--backup-dir"),   QStringLiteral("--compare-dest"),
    QStringLiteral("--copy-dest"),    QStringLiteral("--partial-suffix-dir")};

const RcloneFlag *findLong(const QList<RcloneFlag> &known,
                           const QString &name) {
  for (const RcloneFlag &flag : known) {
    if (flag.name == name) {
      return &flag;
    }
  }
  return nullptr;
}

const RcloneFlag *findShort(const QList<RcloneFlag> &known,
                            const QString &name) {
  for (const RcloneFlag &flag : known) {
    if (!flag.shortName.isEmpty() && flag.shortName == name) {
      return &flag;
    }
  }
  return nullptr;
}

QString refused(const QString &name) {
  return QStringLiteral(
             "\"%1\" is not a flag a task may be given through the API")
      .arg(name);
}

} // namespace

namespace ExtraFlags {

bool IsForbidden(const QString &name, bool restricted) {
  if (kNever.contains(name)) {
    return true;
  }
  // rclone's remote control: "--rc" itself and everything under it.
  if (name == QLatin1String("--rc") || name.startsWith(QLatin1String("--rc-"))) {
    return true;
  }
  // Every backend that can be told to run something says so the same way.
  if (name.endsWith(QLatin1String("-command"))) {
    return true;
  }
  return restricted && kOutsideTheLine.contains(name);
}

QString Check(const QString &extra, const QList<RcloneFlag> &known,
              bool restricted) {
  QStringList args;
  for (const QString &line : extra.split(QLatin1Char('\n'))) {
    args << SplitRcloneOptions(line);
  }

  // The flag still owed a value, when the last one takes one and did not
  // carry it after an "=".
  QString owed;

  for (const QString &arg : args) {
    if (!owed.isEmpty()) {
      // Taken as the value whatever it looks like: "--max-depth -1".
      owed.clear();
      continue;
    }

    if (!arg.startsWith(QLatin1Char('-')) || arg.size() < 2) {
      if (known.isEmpty()) {
        // Without the list there is no telling which flags take a value, so
        // a word on its own may well be one.
        continue;
      }
      return QStringLiteral("\"%1\" is not a flag, and no flag before it "
                            "takes a value")
          .arg(arg);
    }

    const int equals = arg.indexOf(QLatin1Char('='));
    const QString name = equals < 0 ? arg : arg.left(equals);

    if (name.startsWith(QLatin1String("--"))) {
      if (IsForbidden(name, restricted)) {
        return refused(name);
      }
      if (known.isEmpty()) {
        continue;
      }
      const RcloneFlag *flag = findLong(known, name);
      if (flag == nullptr) {
        return QStringLiteral("\"%1\" is not a flag this rclone has").arg(name);
      }
      if (flag->takesValue() && equals < 0) {
        owed = name;
      }
      continue;
    }

    // Short flags. "-vv" is "-v" twice, so each letter is looked up.
    if (known.isEmpty()) {
      // Nothing to look the letters up in. "-L" is the one short flag that
      // matters here, so it is known by heart.
      if (restricted && name.contains(QLatin1Char('L'))) {
        return refused(QStringLiteral("-L"));
      }
      continue;
    }
    for (int i = 1; i < name.size(); ++i) {
      const QString letter = QStringLiteral("-") + name.at(i);
      const RcloneFlag *flag = findShort(known, letter);
      if (flag == nullptr) {
        return QStringLiteral("\"%1\" is not a flag this rclone has")
            .arg(letter);
      }
      if (IsForbidden(flag->name, restricted)) {
        return refused(letter);
      }
      if (flag->takesValue() && equals < 0 && i == name.size() - 1) {
        owed = letter;
      }
    }
  }

  if (!owed.isEmpty()) {
    return QStringLiteral("\"%1\" needs a value").arg(owed);
  }
  return QString();
}

QHash<QString, QString> Backends(const QList<RcloneFlag> &known) {
  const auto prefixOf = [](const RcloneFlag &flag) {
    return flag.name.mid(2).section(QLatin1Char('-'), 0, 0);
  };

  QHash<QString, int> seen;
  for (const RcloneFlag &flag : known) {
    if (flag.group == QLatin1String("Backend")) {
      ++seen[prefixOf(flag)];
    }
  }

  QHash<QString, QString> backends;
  for (const RcloneFlag &flag : known) {
    if (flag.group != QLatin1String("Backend")) {
      continue;
    }
    const QString prefix = prefixOf(flag);
    backends.insert(flag.name, seen.value(prefix) > 1 ? prefix
                                                     : QStringLiteral("local"));
  }
  return backends;
}

QString FlagPrefixOfType(const QString &type) {
  // The ones whose name in the configuration is not how their flags begin.
  // From "rclone config providers", where Name and Prefix differ.
  if (type == QLatin1String("google cloud storage")) {
    return QStringLiteral("gcs");
  }
  if (type == QLatin1String("google photos")) {
    return QStringLiteral("gphotos");
  }
  if (type == QLatin1String("oracleobjectstorage")) {
    return QStringLiteral("oos");
  }
  return type;
}

bool WrapsAnother(const QString &type) {
  static const QStringList layers = {
      QStringLiteral("alias"),   QStringLiteral("archive"),
      QStringLiteral("cache"),   QStringLiteral("chunker"),
      QStringLiteral("combine"), QStringLiteral("compress"),
      QStringLiteral("crypt"),   QStringLiteral("hasher"),
      QStringLiteral("union")};
  return layers.contains(type);
}

} // namespace ExtraFlags
