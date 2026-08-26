#pragma once

// Orchestration (L1): reading one directory of a remote.
//
// Running "rclone lsjson" and feeding its output to the parser was written
// inside ItemModel::load(), interleaved with creating tree rows, a spinner
// timer, and a count of how many listings are in flight. So "what is in this
// directory" could only be asked by something building a tree -- which is why
// a Web UI cannot show a folder today, and why the listing has never been
// exercised by a test.
//
// The parsing has been core since S6 (lsjson_parser.h). This is the part that
// asks the question.
//
// See docs/LAYER-SPLIT.md block 4.

#include "lsjson_parser.h"

#include <QObject>
#include <QString>
#include <QVector>

class QProcess;

class DirectoryListing : public QObject {
  Q_OBJECT

public:
  // remote without a trailing colon; path relative to the remote's root, and
  // empty for the root itself.
  DirectoryListing(const QString &remote, const QString &path,
                   QObject *parent = nullptr);
  ~DirectoryListing() override;

  QString remote() const { return mRemote; }
  QString path() const { return mPath; }

  // "remote:path", the form rclone takes.
  QString target() const;

  // Starts reading. Called again while a listing is going, it does nothing:
  // the answer would be the same. Called again after one has ended, it reads
  // afresh, which is what a retry after a failure needs.
  void start();

  // Stops reading and stays quiet afterwards. Used when the folder is
  // collapsed or the tab closed while a listing is still going -- a listing
  // nobody is waiting for should not go on costing a process.
  void cancel();

  bool isRunning() const;

  // How many entries have been handed over so far.
  int count() const { return mCount; }

signals:
  // A batch, as it arrives. Emitted more than once for a large directory,
  // because a folder with a hundred thousand entries should start appearing
  // before it has all been read.
  void entries(const QVector<LsjsonEntry> &batch);

  // The listing is complete. Anything not handed over by now is not coming.
  void finished();

  // rclone could not read it. The listing is over either way; there is no
  // finished() after this.
  void failed(const QString &reason);

private:
  void deliver();

  QString mRemote;
  QString mPath;
  QProcess *mProcess = nullptr;
  LsjsonParser mParser;
  int mCount = 0;
  bool mCancelled = false;
};
