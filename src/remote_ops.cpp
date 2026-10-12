#include "remote_ops.h"
#include "debug_log.h"
#include "job_registry.h"
#include "utils.h"

#include <QRegularExpression>

namespace {

QString trimSlashes(QString path) {
  while (path.startsWith(QLatin1Char('/'))) {
    path.remove(0, 1);
  }
  while (path.endsWith(QLatin1Char('/'))) {
    path.chop(1);
  }
  return path;
}

// One way of starting rclone for everything in this file: the remote-mode and
// default options every command from the remote tab has always carried, a
// description for the history, and the registry. The configuration file is
// added by RunningJob.
RunningJob *start(JobKind kind, QStringList command, const QString &info,
                  const QString &source, const QString &dest) {
  command << GetRemoteModeRcloneOptions()
          << GetDefaultOptionsList("defaultRcloneOptions");

  return JobRegistry::instance().start(kind, command,
                                       JobDescription{info, source, dest},
                                       QString(), JobKindToString(kind),
                                       QString());
}

} // namespace

namespace RemoteOps {

QString WhyNotAName(const QString &name) {
  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) {
    return QStringLiteral("a name is needed");
  }
  if (trimmed == QLatin1String(".") || trimmed == QLatin1String("..")) {
    return QStringLiteral("that is not a name, it is a direction");
  }
  if (trimmed.contains(QLatin1Char('/')) ||
      trimmed.contains(QLatin1Char('\\'))) {
    return QStringLiteral("a name cannot contain a slash");
  }
  return QString();
}

QString Join(const QString &parent, const QString &name) {
  const QString base = trimSlashes(parent);
  return base.isEmpty() ? name.trimmed()
                        : base + QLatin1Char('/') + name.trimmed();
}

QString Target(const QString &remote, const QString &path) {
  return remote + QLatin1Char(':') + trimSlashes(path);
}

RunningJob *MakeFolder(const QString &remote, const QString &parent,
                       const QString &name, QString *why) {
  const QString problem = WhyNotAName(name);
  if (!problem.isEmpty()) {
    if (why != nullptr) {
      *why = problem;
    }
    return nullptr;
  }

  const QString target = Target(remote, Join(parent, name));
  qCDebug(rbRemote) << "making folder" << target;

  return start(JobKind::Mkdir, QStringList{QStringLiteral("mkdir"), target},
               QStringLiteral("New folder: \"%1\"").arg(target), QString(),
               target);
}

QString ParentOf(const QString &path) {
  const QString clean = trimSlashes(path);
  const int slash = clean.lastIndexOf(QLatin1Char('/'));
  return slash < 0 ? QString() : clean.left(slash);
}

QString NameOf(const QString &path) {
  const QString clean = trimSlashes(path);
  return clean.mid(clean.lastIndexOf(QLatin1Char('/')) + 1);
}

RunningJob *Rename(const QString &remote, const QString &path,
                   const QString &newName, QString *why) {
  const auto refuse = [why](const QString &reason) {
    if (why != nullptr) {
      *why = reason;
    }
    return nullptr;
  };

  const QString from = trimSlashes(path);
  if (from.isEmpty()) {
    return refuse(QStringLiteral("the root of a remote has no name to change"));
  }

  const QString problem = WhyNotAName(newName);
  if (!problem.isEmpty()) {
    return refuse(problem);
  }
  if (newName.trimmed() == NameOf(from)) {
    // Not an error rclone would report: it would move a thing onto itself
    // and call that success. Said here so the caller is not told it worked.
    return refuse(QStringLiteral("that is already its name"));
  }

  const QString source = Target(remote, from);
  const QString dest = Target(remote, Join(ParentOf(from), newName));
  qCDebug(rbRemote) << "renaming" << source << "to" << dest;

  return start(JobKind::Rename,
               QStringList{QStringLiteral("moveto"), source, dest},
               QStringLiteral("Rename: \"%1\" to \"%2\"")
                   .arg(source, newName.trimmed()),
               source, dest);
}

RunningJob *Delete(const Item &item, QString *why) {
  const QString path = trimSlashes(item.path);
  if (path.isEmpty()) {
    if (why != nullptr) {
      *why = QStringLiteral(
          "that is the whole remote; deleting it is not done from here");
    }
    return nullptr;
  }

  const QString target = Target(item.remote, path);
  qCDebug(rbRemote) << "deleting" << (item.isFolder ? "folder" : "file")
                    << target;

  return start(JobKind::Delete,
               QStringList{item.isFolder ? QStringLiteral("purge")
                                         : QStringLiteral("deletefile"),
                           target},
               (item.isFolder ? QStringLiteral("Delete folder: \"%1\"")
                              : QStringLiteral("Delete file: \"%1\""))
                   .arg(target),
               target, QString());
}

RunningJob *Size(const QString &remote, const QString &path,
                 const QStringList &filters, bool json) {
  const QString target = Target(remote, path);

  QStringList command{QStringLiteral("size"), target};
  command << filters;
  if (json) {
    command << QStringLiteral("--json");
  }

  return start(JobKind::Size, command,
               filters.isEmpty()
                   ? QStringLiteral("Size of \"%1\"").arg(target)
                   : QStringLiteral("Size of selected items in \"%1\"")
                         .arg(target),
               target, QString());
}

RunningJob *About(const QString &remote, bool json) {
  const QString target = Target(remote, QString());

  QStringList command{QStringLiteral("about"), target};
  if (json) {
    command << QStringLiteral("--json");
  }

  return start(JobKind::About, command,
               QStringLiteral("rclone about \"%1\"").arg(target), target,
               QString());
}

RunningJob *Tree(const QString &remote, const QString &path) {
  const QString target = Target(remote, path);

  return start(JobKind::Tree,
               QStringList{QStringLiteral("tree"), QStringLiteral("-d"),
                           target},
               QStringLiteral("rclone tree -d \"%1\"").arg(target), target,
               QString());
}

RunningJob *Link(const QString &remote, const QString &path, QString *why) {
  if (trimSlashes(path).isEmpty()) {
    if (why != nullptr) {
      *why = QStringLiteral(
          "that is the whole remote; sharing it is not done from here");
    }
    return nullptr;
  }

  const QString target = Target(remote, path);
  qCDebug(rbRemote) << "making a public link for" << target;

  return start(JobKind::Link, QStringList{QStringLiteral("link"), target},
               QStringLiteral("Public link for: \"%1\"").arg(target), target,
               QString());
}

RunningJob *Cleanup(const QString &remote) {
  const QString target = Target(remote, QString());
  qCDebug(rbRemote) << "cleaning up" << target;

  return start(JobKind::Cleanup,
               QStringList{QStringLiteral("cleanup"), target,
                           QStringLiteral("-vv")},
               QStringLiteral("rclone cleanup \"%1\"").arg(target), target,
               QString());
}

QStringList DedupeModes() {
  return {QStringLiteral("skip"),    QStringLiteral("first"),
          QStringLiteral("newest"),  QStringLiteral("oldest"),
          QStringLiteral("largest"), QStringLiteral("smallest"),
          QStringLiteral("rename")};
}

RunningJob *Dedupe(const QString &remote, const QString &path,
                   const QString &mode, bool dryRun, const QStringList &extra,
                   QString *why) {
  if (!DedupeModes().contains(mode)) {
    if (why != nullptr) {
      *why = QStringLiteral("the mode has to be one of: %1")
                 .arg(DedupeModes().join(QStringLiteral(", ")));
    }
    return nullptr;
  }

  const QString target = Target(remote, path);

  QStringList command{QStringLiteral("dedupe"),
                      QStringLiteral("--dedupe-mode"), mode};
  command << extra << target;
  if (dryRun) {
    command << QStringLiteral("--dry-run");
  }
  qCDebug(rbRemote) << "deduplicating" << target << "mode=" << mode
                    << "dryRun=" << dryRun;

  return start(JobKind::Dedupe, command,
               QStringLiteral("rclone %1").arg(command.join(QLatin1Char(' '))),
               target, target);
}

RunningJob *CopyBetween(bool move, const QString &sourceRemote,
                        const QString &sourcePath, const QString &destRemote,
                        const QString &destPath, const QStringList &args,
                        const QString &info) {
  const QString source = Target(sourceRemote, sourcePath);
  const QString dest = Target(destRemote, destPath);
  qCDebug(rbRemote) << (move ? "moving" : "copying") << source << "to" << dest;

  QStringList command{move ? QStringLiteral("move") : QStringLiteral("copy"),
                      source, dest};
  command << args;

  return start(JobKind::Transfer, command,
               info.isEmpty()
                   ? QStringLiteral("%1: \"%2\" to \"%3\"")
                         .arg(move ? QStringLiteral("Move")
                                   : QStringLiteral("Copy"),
                              source, dest)
                   : info,
               source, dest);
}

RunningJob *CopyInto(bool move, const Item &what, const QString &destRemote,
                     const QString &destFolder, bool dryRun, QString *why) {
  const auto refuse = [why](const QString &reason) {
    if (why != nullptr) {
      *why = reason;
    }
    return nullptr;
  };

  const QString from = trimSlashes(what.path);
  const QString into = trimSlashes(destFolder);
  const bool sameRemote = destRemote == what.remote;

  if (from.isEmpty()) {
    return refuse(QStringLiteral(
        "that is the whole remote; choose something inside it"));
  }

  if (sameRemote) {
    if (into == ParentOf(from)) {
      return refuse(QStringLiteral("it is already in that folder"));
    }
    // A folder cannot go inside itself. rclone would start copying it into
    // its own copy, and for a move there is no outcome that makes sense.
    if (what.isFolder &&
        (into == from || into.startsWith(from + QLatin1Char('/')))) {
      return refuse(QStringLiteral("a folder cannot be put inside itself"));
    }
  }

  // rclone copies the *contents* of a folder into the destination, so a
  // folder is given a place of its own name there. A file needs none: it is
  // copied into the destination as it is.
  const QString destPath = what.isFolder ? Join(into, NameOf(from)) : into;

  QStringList args;
  if (dryRun) {
    args << QStringLiteral("--dry-run");
  }
  return CopyBetween(move, what.remote, from, destRemote, destPath, args);
}

RunningJob *Check(const QString &remote, const QString &path,
                  const QString &otherRemote, const QString &otherPath,
                  const CheckOptions &options, QString *why) {
  if (remote == otherRemote && trimSlashes(path) == trimSlashes(otherPath)) {
    if (why != nullptr) {
      *why = QStringLiteral("that is the same folder; choose another to "
                            "compare it with");
    }
    return nullptr;
  }

  const QString source = Target(remote, path);
  const QString dest = Target(otherRemote, otherPath);

  QStringList command{QStringLiteral("check")};
  if (options.oneWay) {
    command << QStringLiteral("--one-way");
  }
  if (options.sizeOnly) {
    command << QStringLiteral("--size-only");
  }
  if (options.download) {
    command << QStringLiteral("--download");
  }
  command << source << dest;
  qCDebug(rbRemote) << "checking" << source << "against" << dest;

  return start(JobKind::Check, command,
               QStringLiteral("rclone %1").arg(command.join(QLatin1Char(' '))),
               source, dest);
}

CheckResult ReadCheckOutput(const QStringList &lines) {
  static const QRegularExpression differ(
      QStringLiteral("(\\d+) differences? found"));
  static const QRegularExpression match(
      QStringLiteral("(\\d+) matching files?"));

  CheckResult result;
  for (const QString &line : lines) {
    const QRegularExpressionMatch d = differ.match(line);
    if (d.hasMatch()) {
      result.differences = d.captured(1).toInt();
    }
    const QRegularExpressionMatch m = match.match(line);
    if (m.hasMatch()) {
      result.matching = m.captured(1).toInt();
    }
  }
  return result;
}

RunningJob *ListForExport(const QString &remote, const QString &path,
                          const ExportOptions &options) {
  const QString target = Target(remote, path);

  QStringList command{QStringLiteral("lsl")};
  if (options.maxDepth > 0) {
    command << QStringLiteral("--max-depth")
            << QString::number(options.maxDepth);
  }
  command << target;

  return start(JobKind::Export, command,
               QStringLiteral("Export \"%1\"").arg(target), target,
               QString());
}

bool ParseLslLine(const QString &line, qint64 *size, QString *modified,
                  QString *name) {
  static const QRegularExpression pattern(QStringLiteral(
      "^\\s*(\\d+) "
      "(\\d\\d\\d\\d-\\d\\d-\\d\\d \\d\\d:\\d\\d:\\d\\d)\\.\\d+ (.+)$"));
  const QRegularExpressionMatch match = pattern.match(line);
  if (!match.hasMatch()) {
    return false;
  }
  if (size != nullptr) {
    *size = match.captured(1).toLongLong();
  }
  if (modified != nullptr) {
    *modified = match.captured(2);
  }
  if (name != nullptr) {
    *name = match.captured(3);
  }
  return true;
}

QString ExportRow(const QString &name, const QString &modified, qint64 size,
                  bool namesOnly) {
  // A quote inside a name is doubled, which is what a quoted field needs to
  // survive being read back. The window wrote the name as it stood, so a
  // file with a quote in its name broke the row it was on.
  QString quoted = name;
  quoted.replace(QLatin1Char('"'), QStringLiteral("\"\""));

  if (namesOnly) {
    return QStringLiteral("\"%1\"").arg(quoted);
  }
  return QStringLiteral("\"%1\",\"%2\",%3").arg(quoted, modified).arg(size);
}

} // namespace RemoteOps
