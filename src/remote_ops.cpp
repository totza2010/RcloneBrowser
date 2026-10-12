#include "remote_ops.h"
#include "debug_log.h"
#include "job_registry.h"
#include "utils.h"

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

} // namespace RemoteOps
