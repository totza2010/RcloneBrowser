#include "lsjson_parser.h"

#include <QJsonDocument>
#include <QJsonObject>

QString LsjsonEntry::modifiedText() const {
  if (!modTime.isValid()) {
    return QString();
  }
  return modTime.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

LsjsonEntry ParseLsjsonObject(const QByteArray &json, bool *ok) {
  LsjsonEntry entry;
  if (ok) {
    *ok = false;
  }

  QJsonParseError error{};
  const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject()) {
    return entry;
  }

  const QJsonObject object = doc.object();

  entry.name = object.value("Name").toString();
  entry.path = object.value("Path").toString();
  if (entry.name.isEmpty()) {
    // Nothing to show in the tree and nothing to key on.
    return entry;
  }

  entry.isDir = object.value("IsDir").toBool(false);

  // Directories have no meaningful size and backends that cannot report one
  // omit the field; -1 keeps that distinct from an empty file.
  if (object.contains(QLatin1String("Size"))) {
    entry.size = object.value("Size").toVariant().toLongLong();
  }

  const QString modTime = object.value("ModTime").toString();
  if (!modTime.isEmpty()) {
    entry.modTime = QDateTime::fromString(modTime, Qt::ISODateWithMs);
    if (!entry.modTime.isValid()) {
      // Some backends leave off the fractional seconds.
      entry.modTime = QDateTime::fromString(modTime, Qt::ISODate);
    }
  }

  if (ok) {
    *ok = true;
  }
  return entry;
}

void LsjsonParser::reset() {
  mBuffer.clear();
  mScanPos = 0;
  mDepth = 0;
  mObjectStart = -1;
  mInString = false;
  mEscaped = false;
  mMalformed = false;
  mSkipped = 0;
}

QVector<LsjsonEntry> LsjsonParser::feed(const QByteArray &chunk) {
  QVector<LsjsonEntry> entries;
  mBuffer += chunk;

  int consumed = 0;

  for (int i = mScanPos; i < mBuffer.size(); ++i) {
    const char c = mBuffer.at(i);

    // String state has to survive across chunks, or a name containing a brace
    // would be read as structure.
    if (mEscaped) {
      mEscaped = false;
      continue;
    }
    if (mInString) {
      if (c == '\\') {
        mEscaped = true;
      } else if (c == '"') {
        mInString = false;
      }
      continue;
    }
    if (c == '"') {
      mInString = true;
      continue;
    }

    if (c == '{') {
      if (mDepth == 0) {
        mObjectStart = i;
      }
      ++mDepth;
    } else if (c == '}') {
      --mDepth;
      if (mDepth == 0 && mObjectStart >= 0) {
        bool ok = false;
        const LsjsonEntry entry = ParseLsjsonObject(
            mBuffer.mid(mObjectStart, i - mObjectStart + 1), &ok);
        if (ok) {
          entries.append(entry);
        } else {
          // A single unreadable entry should not lose the rest of a
          // directory, so it is counted and stepped over.
          mMalformed = true;
          ++mSkipped;
        }
        mObjectStart = -1;
        consumed = i + 1;
      } else if (mDepth < 0) {
        // Structurally broken input; resynchronise rather than reading
        // negative depth forever.
        mDepth = 0;
        mMalformed = true;
        consumed = i + 1;
      }
    }
  }

  // Drop only what has been turned into entries. A partial object at the end
  // of the chunk stays for the next one.
  if (consumed > 0) {
    mBuffer.remove(0, consumed);
    if (mObjectStart > 0) {
      mObjectStart -= consumed;
    }
  }
  mScanPos = mBuffer.size();

  return entries;
}
