#pragma once

// Orchestration (L1): reading the start of one file of a remote, so it can be
// looked at -- a picture, a PDF, a page of text -- without downloading it
// somewhere first.
//
// A question like DirectoryListing, not a job: nothing is changed, so there
// is no history row, and whoever asked can go away and take rclone with them.

#include <QByteArray>
#include <QObject>
#include <QString>

class QProcess;

class FileReader : public QObject {
  Q_OBJECT

public:
  // remote without a trailing colon; path relative to the remote's root.
  // At most `limit` bytes are read: rclone is told to stop there
  // ("cat --count"), so a large file costs no more than its first part.
  FileReader(const QString &remote, const QString &path, qint64 limit,
             QObject *parent = nullptr);
  ~FileReader() override;

  void start();
  // Stops reading and stays quiet afterwards.
  void cancel();

  // How a file of this name can be shown, by its name alone:
  // "image" | "pdf" | "text" | "media" | "" (not something to show here).
  // Never "html": a page from a remote, shown inside this program's own,
  // could act as this program's page. HTML and SVG are text here -- an
  // SVG only as a picture, where its scripts do not run.
  static QString ViewOf(const QString &name);
  // The type to show it as, for a view ViewOf() gave: "image/png",
  // "application/pdf", "text/plain; charset=utf-8".
  static QString TypeOf(const QString &name);

signals:
  // All of it, or its first `limit` bytes when `truncated`.
  void finished(const QByteArray &data, bool truncated);
  void failed(const QString &reason);

private:
  const QString mRemote;
  const QString mPath;
  const qint64 mLimit;
  QProcess *mProcess = nullptr;
  QByteArray mData;
  bool mCancelled = false;
};
