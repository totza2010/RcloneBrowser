#include "local_files.h"
#include "utils.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>

namespace {

// The list as somebody wrote it: RB_LOCAL_ROOTS wins, then the settings.
// Separated by semicolons or new lines -- not colons, which a Windows path
// has one of.
QStringList configured() {
  QString raw = qEnvironmentVariable("RB_LOCAL_ROOTS");
  if (raw.trimmed().isEmpty()) {
    raw = GetSettings()->value("Settings/apiLocalRoots").toString();
  }

  QStringList roots;
  for (QString part : raw.split(QRegularExpression(QStringLiteral("[;\\n]")),
                                Qt::SkipEmptyParts)) {
    part = part.trimmed();
    if (part.isEmpty()) {
      continue;
    }
    // Resolved once here, so every later comparison is between two paths in
    // the same form. A root that does not exist is left out: it cannot
    // contain anything, and keeping it would only make a chooser show a
    // folder that fails to open.
    const QString resolved = QFileInfo(part).canonicalFilePath();
    if (!resolved.isEmpty() && !roots.contains(resolved)) {
      roots << resolved;
    }
  }
  return roots;
}

bool inside(const QString &path, const QString &root) {
  if (path == root) {
    return true;
  }
  const QString base =
      root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
#if defined(Q_OS_WIN)
  return path.startsWith(base, Qt::CaseInsensitive);
#else
  return path.startsWith(base);
#endif
}

} // namespace

namespace LocalFiles {

bool IsRestricted() { return !configured().isEmpty(); }

QStringList Roots() {
  const QStringList listed = configured();
  if (!listed.isEmpty()) {
    return listed;
  }

  QStringList roots;
#if defined(Q_OS_WIN)
  for (const QFileInfo &drive : QDir::drives()) {
    roots << drive.absoluteFilePath();
  }
#else
  roots << QStringLiteral("/");
#endif
  return roots;
}

QString Allowed(const QString &path) {
  if (path.trimmed().isEmpty()) {
    return QString();
  }

  const QFileInfo info(path);
  if (!info.isAbsolute()) {
    // Relative to what? There is no folder a request is "in".
    return QString();
  }

  // What exists is resolved whole. What does not is resolved as far as its
  // folder, and its own name put back -- so "media/new-folder" can be named
  // before it is made, and "media/link-out/new" still cannot escape.
  QString resolved = info.canonicalFilePath();
  if (resolved.isEmpty()) {
    const QString parent = QFileInfo(info.absolutePath()).canonicalFilePath();
    if (parent.isEmpty()) {
      return QString();
    }
    const QString name = info.fileName();
    if (name.isEmpty() || name == QLatin1String("..") ||
        name == QLatin1String(".")) {
      return QString();
    }
    resolved = QDir(parent).filePath(name);
  }

  if (!IsRestricted()) {
    return resolved;
  }
  for (const QString &root : configured()) {
    if (inside(resolved, root)) {
      return resolved;
    }
  }
  return QString();
}

bool List(const QString &path, QList<Entry> *entries, QString *why) {
  const auto fail = [why](const QString &reason) {
    if (why != nullptr) {
      *why = reason;
    }
    return false;
  };

  if (path.trimmed().isEmpty()) {
    for (const QString &root : Roots()) {
      Entry entry;
      entry.name = root;
      entry.path = root;
      entry.isDir = true;
      entries->append(entry);
    }
    return true;
  }

  const QString allowed = Allowed(path);
  if (allowed.isEmpty()) {
    // The same answer whether it is outside the roots or not there at all:
    // which of the two it is, is itself something about the disk.
    return fail(QStringLiteral("that folder is not one this can look inside"));
  }

  const QFileInfo info(allowed);
  if (!info.isDir()) {
    return fail(QStringLiteral("that is not a folder"));
  }

  QDir dir(allowed);
  if (!dir.isReadable()) {
    return fail(QStringLiteral("that folder cannot be read"));
  }

  const QFileInfoList listed =
      dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                        QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
  for (const QFileInfo &item : listed) {
    Entry entry;
    entry.name = item.fileName();
    entry.path = item.absoluteFilePath();
    entry.isDir = item.isDir();
    entry.size = item.isDir() ? 0 : item.size();
    entry.modified = item.lastModified();
    entries->append(entry);
  }
  return true;
}

} // namespace LocalFiles
