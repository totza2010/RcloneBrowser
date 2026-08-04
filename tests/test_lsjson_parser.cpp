#include "lsjson_parser.h"

#include <QTest>

class TestLsjsonParser : public QObject {
  Q_OBJECT

private:
  // Reads a whole document in one go, for the cases where chunking is not
  // what is under test.
  static QVector<LsjsonEntry> parseAll(const QByteArray &json) {
    LsjsonParser parser;
    return parser.feed(json);
  }

private slots:
  // Shape taken from real "rclone lsjson" output.
  void parsesFilesAndDirectories() {
    const QByteArray json = R"([
{"Path":"docs","Name":"docs","Size":-1,"MimeType":"inode/directory","ModTime":"2026-08-03T21:24:12.9642623+07:00","IsDir":true},
{"Path":"a.txt","Name":"a.txt","Size":5610,"MimeType":"text/plain; charset=utf-8","ModTime":"2026-08-04T21:12:37.6469519+07:00","IsDir":false}
])";

    const QVector<LsjsonEntry> entries = parseAll(json);
    QCOMPARE(entries.size(), 2);

    QCOMPARE(entries.at(0).name, QStringLiteral("docs"));
    QVERIFY(entries.at(0).isDir);

    QCOMPARE(entries.at(1).name, QStringLiteral("a.txt"));
    QVERIFY(!entries.at(1).isDir);
    QCOMPARE(entries.at(1).size, Q_INT64_C(5610));
    QVERIFY(entries.at(1).modTime.isValid());
  }

  // The tree has always shown "yyyy-MM-dd HH:mm:ss" in local time, and sorts
  // on that string, so the format has to survive the move off lsl.
  void formatsModifiedLikeTheOldListing() {
    LsjsonEntry entry;
    entry.name = "x";
    entry.modTime =
        QDateTime::fromString(QStringLiteral("2026-08-04T21:12:37.6469519+07:00"),
                              Qt::ISODateWithMs);
    QVERIFY(entry.modTime.isValid());

    const QString text = entry.modifiedText();
    QCOMPARE(text.size(), 19);
    QCOMPARE(text.at(4), QChar('-'));
    QCOMPARE(text.at(10), QChar(' '));
    QCOMPARE(text.at(13), QChar(':'));

    // Same instant, expressed in another zone, must render identically.
    LsjsonEntry other;
    other.modTime = QDateTime::fromString(
        QStringLiteral("2026-08-04T14:12:37.6469519+00:00"), Qt::ISODateWithMs);
    QCOMPARE(other.modifiedText(), text);
  }

  void handlesModTimeWithoutFractionalSeconds() {
    bool ok = false;
    const LsjsonEntry entry = ParseLsjsonObject(
        R"({"Name":"a","ModTime":"2026-08-04T21:12:37+07:00","IsDir":false})",
        &ok);
    QVERIFY(ok);
    QVERIFY(entry.modTime.isValid());
  }

  // teldrive omits sizes on some entries. -1 has to stay distinct from an
  // empty file, or the tree would claim a file is 0 bytes.
  void missingSizeIsUnknownNotZero() {
    bool ok = false;
    const LsjsonEntry entry =
        ParseLsjsonObject(R"({"Name":"a","IsDir":false})", &ok);
    QVERIFY(ok);
    QCOMPARE(entry.size, Q_INT64_C(-1));

    const LsjsonEntry empty =
        ParseLsjsonObject(R"({"Name":"b","Size":0,"IsDir":false})", &ok);
    QCOMPARE(empty.size, Q_INT64_C(0));
  }

  void entryWithoutNameIsSkipped() {
    LsjsonParser parser;
    const QVector<LsjsonEntry> entries =
        parser.feed(R"([{"Size":1},{"Name":"ok","Size":2}])");
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.at(0).name, QStringLiteral("ok"));
    QCOMPARE(parser.skippedCount(), 1);
  }

  // The reason for streaming: output arrives in pieces of whatever size the
  // pipe hands over, including splits inside a name or a number.
  void survivesArbitraryChunkBoundaries() {
    const QByteArray json = R"([
{"Path":"one","Name":"one","Size":1,"ModTime":"2026-08-04T21:12:37+07:00","IsDir":false},
{"Path":"two","Name":"two","Size":2,"ModTime":"2026-08-04T21:12:38+07:00","IsDir":false},
{"Path":"three","Name":"three","Size":3,"ModTime":"2026-08-04T21:12:39+07:00","IsDir":true}
])";

    for (int chunkSize = 1; chunkSize <= 17; ++chunkSize) {
      LsjsonParser parser;
      QVector<LsjsonEntry> all;
      for (int i = 0; i < json.size(); i += chunkSize) {
        all += parser.feed(json.mid(i, chunkSize));
      }
      QCOMPARE(all.size(), 3);
      QCOMPARE(all.at(0).name, QStringLiteral("one"));
      QCOMPARE(all.at(1).name, QStringLiteral("two"));
      QCOMPARE(all.at(2).name, QStringLiteral("three"));
      QVERIFY(all.at(2).isDir);
      QVERIFY(!parser.sawMalformedObject());
    }
  }

  // A brace inside a file name would end the object early if string state
  // were not tracked.
  void bracesInsideNamesDoNotEndTheObject() {
    const QVector<LsjsonEntry> entries = parseAll(
        R"([{"Name":"we{ird}.mkv","Size":1,"IsDir":false},{"Name":"after","Size":2,"IsDir":false}])");
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0).name, QStringLiteral("we{ird}.mkv"));
    QCOMPARE(entries.at(1).name, QStringLiteral("after"));
  }

  // Written with ordinary escapes rather than a raw string: moc mis-lexes a
  // backslash-escaped quote inside R"(...)" and then reports "No relevant
  // classes found" for the entire file, leaving the test to fail at link time
  // with missing metaObject symbols.
  void escapedQuotesInNamesAreHandled() {
    const QVector<LsjsonEntry> entries = parseAll(
        "[{\"Name\":\"quote\\\"brace{.mkv\",\"Size\":1,\"IsDir\":false}]");
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.at(0).name, QStringLiteral("quote\"brace{.mkv"));
  }

  void escapedBackslashBeforeQuoteIsNotAnEscape() {
    // The stored name ends in a single backslash, so the quote that follows
    // really does close the string.
    const QVector<LsjsonEntry> entries = parseAll(
        "[{\"Name\":\"a\\\\\",\"Size\":1,\"IsDir\":false},"
        "{\"Name\":\"b\",\"Size\":2,\"IsDir\":false}]");
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0).name, QStringLiteral("a\\"));
    QCOMPARE(entries.at(1).name, QStringLiteral("b"));
  }

  void nonAsciiNamesSurvive() {
    const QVector<LsjsonEntry> entries = parseAll(
        QString(R"([{"Name":"บุรีรัมย์.mkv","Size":1,"IsDir":false}])").toUtf8());
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.at(0).name, QStringLiteral("บุรีรัมย์.mkv"));
  }

  void hugeSizesKeepPrecision() {
    bool ok = false;
    const LsjsonEntry entry = ParseLsjsonObject(
        R"({"Name":"big.mkv","Size":12141189673,"IsDir":false})", &ok);
    QVERIFY(ok);
    QCOMPARE(entry.size, Q_INT64_C(12141189673));
  }

  void emptyListingYieldsNothing() {
    LsjsonParser parser;
    QVERIFY(parser.feed("[]").isEmpty());
    QVERIFY(!parser.sawMalformedObject());
  }

  // rclone writes an error to stdout in some failure modes; the listing
  // should come back empty rather than inventing entries.
  void nonJsonInputYieldsNothing() {
    LsjsonParser parser;
    QVERIFY(parser.feed("Failed to create file system: not found\n").isEmpty());
  }

  // One damaged object must not cost the rest of the directory.
  void malformedObjectIsSkippedAndFlagged() {
    LsjsonParser parser;
    const QVector<LsjsonEntry> entries = parser.feed(
        R"([{"Name":"good1","Size":1},{"Name":,"Size":2},{"Name":"good2","Size":3}])");
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0).name, QStringLiteral("good1"));
    QCOMPARE(entries.at(1).name, QStringLiteral("good2"));
    QVERIFY(parser.sawMalformedObject());
  }

  // These two feed deliberately truncated output, which leaves an odd number
  // of quotes in the literal. moc does not recognise raw strings and counts
  // quotes across the file, so an unbalanced one convinces it that everything
  // after is inside a string; it then reports "No relevant classes found" and
  // the test fails at link time with missing metaObject symbols. Ordinary
  // escaped literals keep moc in step.
  void truncatedOutputYieldsOnlyCompleteEntries() {
    LsjsonParser parser;
    const QVector<LsjsonEntry> entries = parser.feed(
        "[{\"Name\":\"done\",\"Size\":1},{\"Name\":\"cut off\",\"Si");
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.at(0).name, QStringLiteral("done"));
  }

  void resetClearsCarriedState() {
    LsjsonParser parser;
    parser.feed("[{\"Name\":\"partial\",\"Si");
    parser.reset();
    const QVector<LsjsonEntry> entries =
        parser.feed(R"([{"Name":"fresh","Size":1}])");
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.at(0).name, QStringLiteral("fresh"));
  }
};

QTEST_MAIN(TestLsjsonParser)
#include "test_lsjson_parser.moc"
