#pragma once

// Core (L0): what a given rclone backend can actually do -- see
// docs/ARCHITECTURE.md. Must not depend on QtWidgets.
//
// Backends differ widely in what they support, and custom ones diverge more
// than the mainline set. teldrive, for example, reports no hashes at all and
// no duplicate-file support, so "check" and "dedupe" can never succeed on it.
// Querying once and disabling the affected actions is friendlier than letting
// the user press a button and read a raw rclone error.

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>

class RcloneCapabilities {
public:
  bool about = false;
  bool cleanUp = false;
  bool command = false;
  bool copy = false;
  bool duplicateFiles = false;
  bool listR = false;
  bool move = false;
  bool publicLink = false;
  bool purge = false;
  bool putStream = false;

  // Empty when the backend cannot hash at all. "rclone check" then has nothing
  // to compare beyond size, and "cryptcheck" cannot work.
  QStringList hashes;

  // Modification-time resolution in nanoseconds, as reported by the backend.
  // Fed to --modify-window when syncing against a more precise backend.
  qint64 precisionNs = 0;

  // False until a query has come back. Everything stays permissive while
  // unknown, so a slow or failing query never hides working buttons.
  bool known = false;

  bool canHash() const { return !hashes.isEmpty(); }

  static RcloneCapabilities fromBackendFeatures(const QByteArray &json);
};

// Queries "rclone backend features <remote>:" once per remote and caches the
// result for the lifetime of the process. The query is asynchronous; callers
// get a signal when it lands and should keep their UI permissive until then.
class RcloneCapabilityRegistry : public QObject {
  Q_OBJECT

public:
  static RcloneCapabilityRegistry &instance();

  // Returns the cached entry, starting a query if this remote has not been
  // seen yet. The returned value has known == false until the query lands.
  RcloneCapabilities get(const QString &remote);

  void invalidate(const QString &remote);

signals:
  void capabilitiesChanged(const QString &remote,
                           const RcloneCapabilities &caps);

private:
  explicit RcloneCapabilityRegistry(QObject *parent = nullptr);

  void query(const QString &remote);

  QHash<QString, RcloneCapabilities> mCache;
  QSet<QString> mInFlight;
};
