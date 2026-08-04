#pragma once

// Core (L0): incremental parser for "rclone lsjson" output.
//
// Listing used to take two processes per directory, lsd for the directories
// and lsl for the files, each scraped with its own regular expression against
// a column layout meant for humans. lsjson answers both in one call and in a
// documented shape.
//
// The output is one large JSON array, and a directory with a hundred thousand
// entries should not have to arrive in full before anything appears. Entries
// are therefore pulled out as they complete, which means tracking bracket
// depth across chunk boundaries rather than handing the whole document to
// QJsonDocument.

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QVector>

struct LsjsonEntry {
  QString name;
  QString path;
  // -1 when the backend cannot report a size. teldrive returns entries
  // without one.
  qint64 size = -1;
  bool isDir = false;
  QDateTime modTime;

  // The display format the tree has always used, in local time, so the column
  // and its sorting behave exactly as they did with lsl.
  QString modifiedText() const;
};

class LsjsonParser {
public:
  // Appends a chunk of output and returns whatever entries it completed.
  QVector<LsjsonEntry> feed(const QByteArray &chunk);

  // True if any object failed to parse. The listing is still usable; this
  // reports that something was skipped.
  bool sawMalformedObject() const { return mMalformed; }

  // Objects skipped because they carried no usable name.
  int skippedCount() const { return mSkipped; }

  void reset();

private:
  QByteArray mBuffer;
  int mScanPos = 0;
  int mDepth = 0;
  int mObjectStart = -1;
  bool mInString = false;
  bool mEscaped = false;
  bool mMalformed = false;
  int mSkipped = 0;
};

// Exposed for testing: turns one lsjson object into an entry.
LsjsonEntry ParseLsjsonObject(const QByteArray &json, bool *ok = nullptr);
