#include "rclone_flags.h"

#include <QFile>
#include <QSet>
#include <QTest>

class TestRcloneFlags : public QObject {
  Q_OBJECT

private:
  static QList<RcloneFlag> fixture() {
    QFile file(QStringLiteral(RB_FIXTURES_DIR "/rclone_help_flags.txt"));
    if (!file.open(QIODevice::ReadOnly)) {
      return {};
    }
    return ParseRcloneHelpFlags(file.readAll());
  }

  static const RcloneFlag *find(const QList<RcloneFlag> &flags,
                                const QString &name) {
    for (const RcloneFlag &flag : flags) {
      if (flag.name == name) {
        return &flag;
      }
    }
    return nullptr;
  }

private slots:
  // Recorded from a real "rclone help flags" on a teldrive build, trimmed to
  // one slice of each shape the output takes.
  void parsesRealOutput() {
    const QList<RcloneFlag> flags = fixture();
    QVERIFY2(!flags.isEmpty(), "fixture missing or unreadable");

    // A boolean: no type, so nothing follows it on the command line.
    const RcloneFlag *checkFirst = find(flags, QStringLiteral("--check-first"));
    QVERIFY(checkFirst);
    QVERIFY(checkFirst->type.isEmpty());
    QVERIFY(!checkFirst->takesValue());
    QCOMPARE(checkFirst->insertion(), QStringLiteral("--check-first"));
    QCOMPARE(checkFirst->description,
             QStringLiteral("Do all the checks before starting transfers"));
    QCOMPARE(checkFirst->group, QStringLiteral("Copy"));
  }

  void readsShortFormWhenThereIsOne() {
    const QList<RcloneFlag> flags = fixture();

    const RcloneFlag *checksum = find(flags, QStringLiteral("--checksum"));
    QVERIFY(checksum);
    QCOMPARE(checksum->shortName, QStringLiteral("-c"));

    // Uppercase short forms exist too and must not be mistaken for anything
    // else.
    const RcloneFlag *ignoreTimes =
        find(flags, QStringLiteral("--ignore-times"));
    QVERIFY(ignoreTimes);
    QCOMPARE(ignoreTimes->shortName, QStringLiteral("-I"));

    // Most flags have none, and an empty capture must not become "-".
    const RcloneFlag *immutable = find(flags, QStringLiteral("--immutable"));
    QVERIFY(immutable);
    QVERIFY(immutable->shortName.isEmpty());
  }

  // Whether a flag takes a value decides whether picking it should leave an
  // "=" behind, and rclone marks it only by the presence of a type word.
  void readsTheTypeThatFollowsTheName_data() {
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("type");

    QTest::newRow("int") << "--teldrive-upload-concurrency"
                         << "int";
    QTest::newRow("string") << "--teldrive-api-host"
                            << "string";
    QTest::newRow("SizeSuffix") << "--teldrive-chunk-size"
                                << "SizeSuffix";
    QTest::newRow("stringArray") << "--compare-dest"
                                 << "stringArray";
    QTest::newRow("count") << "--verbose"
                           << "count";
    // An enumeration, which contains the character used elsewhere as a
    // separator and so is easy to split in the wrong place.
    QTest::newRow("alternatives") << "--cutoff-mode"
                                  << "HARD|SOFT|CAUTIOUS";
  }

  void readsTheTypeThatFollowsTheName() {
    QFETCH(QString, name);
    QFETCH(QString, type);

    // The list has to outlive the pointer into it.
    const QList<RcloneFlag> flags = fixture();
    const RcloneFlag *flag = find(flags, name);
    QVERIFY2(flag, qPrintable(name));
    QCOMPARE(flag->type, type);
    QVERIFY(flag->takesValue());
    QCOMPARE(flag->insertion(), name + QStringLiteral("="));
  }

  // The description of --cutoff-mode repeats the alternatives, so a parser
  // that searched for them rather than reading position would truncate it.
  void keepsTheWholeDescription() {
    const QList<RcloneFlag> flags = fixture();
    const RcloneFlag *flag = find(flags, QStringLiteral("--cutoff-mode"));
    QVERIFY(flag);
    QCOMPARE(flag->description,
             QStringLiteral("Mode to stop transfers when reaching the max "
                            "transfer limit HARD|SOFT|CAUTIOUS (default HARD)"));
  }

  // "rclone help flags" opens by describing itself. Its --group, --name and
  // -h belong to that command, not to copy or sync, and offering them would
  // send someone to a job that fails.
  void skipsTheHelpCommandsOwnFlags() {
    const QList<RcloneFlag> flags = fixture();
    QVERIFY(!find(flags, QStringLiteral("--group")));
    QVERIFY(!find(flags, QStringLiteral("--name")));
    QVERIFY(!find(flags, QStringLiteral("--help")));
  }

  void recordsTheGroupEachFlagCameFrom() {
    const QList<RcloneFlag> flags = fixture();

    const RcloneFlag *dryRun = find(flags, QStringLiteral("--dry-run"));
    QVERIFY(dryRun);
    QCOMPARE(dryRun->group, QStringLiteral("Important"));

    // The header is "Backend-only flags (these can be set in the config file
    // also) (flag group Backend):" -- two parentheses, and the group is in
    // the second.
    const RcloneFlag *chunk =
        find(flags, QStringLiteral("--teldrive-chunk-size"));
    QVERIFY(chunk);
    QCOMPARE(chunk->group, QStringLiteral("Backend"));
  }

  // The whole point of asking the binary: a teldrive build reports flags a
  // mainline one does not have.
  void findsBackendFlagsOfThisBuild() {
    const QList<RcloneFlag> flags = fixture();
    int teldrive = 0;
    for (const RcloneFlag &flag : flags) {
      if (flag.name.startsWith(QStringLiteral("--teldrive-"))) {
        ++teldrive;
      }
    }
    QCOMPARE(teldrive, 3);
  }

  // Two of rclone's 1078 flags have a type name containing spaces. A parser
  // that took the type as one word dropped both lines whole -- the flag went
  // missing rather than coming out wrong, which is harder to notice.
  void readsTypesContainingSpaces() {
    const QList<RcloneFlag> flags = fixture();
    const RcloneFlag *gzip =
        find(flags, QStringLiteral("--s3-use-accept-encoding-gzip"));
    QVERIFY(gzip);
    QCOMPARE(gzip->type, QStringLiteral("Accept-Encoding: gzip"));
    QCOMPARE(gzip->description,
             QStringLiteral(
                 "Whether to send Accept-Encoding: gzip header (default unset)"));
  }

  // rclone lists thirteen flags under two groups each. They are the same flag
  // both times and should appear once.
  void listsEachFlagOnce() {
    const QList<RcloneFlag> flags = fixture();

    int dryRun = 0;
    QSet<QString> names;
    for (const RcloneFlag &flag : flags) {
      names.insert(flag.name);
      if (flag.name == QStringLiteral("--dry-run")) {
        ++dryRun;
      }
    }

    QCOMPARE(names.size(), flags.size());
    QCOMPARE(dryRun, 1);
    // The first listing wins, so the group is the one rclone showed first.
    QCOMPARE(find(flags, QStringLiteral("--dry-run"))->group,
             QStringLiteral("Important"));
  }

  // The update check needs to know which repository to watch, and nothing in
  // the binary says. Both forks report the Go module path
  // "github.com/rclone/rclone" -- tgdrive never renamed it -- and both print
  // the same "rclone v1.73.1" with upstream's own numbering. The backends are
  // the only difference that reaches the outside.
  void detectsTheForkFromItsBackends() {
    QCOMPARE(DetectRcloneRepo(fixture()), QStringLiteral("tgdrive/rclone"));
  }

  void treatsABuildWithoutForkBackendsAsUpstream() {
    const QList<RcloneFlag> flags = ParseRcloneHelpFlags(
        "Flags for anything which can copy a file (flag group Copy):\n"
        "      --check-first     Do all the checks before starting\n"
        "Backend-only flags (flag group Backend):\n"
        "      --drive-chunk-size SizeSuffix   Upload chunk size\n");
    QCOMPARE(flags.size(), 2);
    QCOMPARE(DetectRcloneRepo(flags), QStringLiteral("rclone/rclone"));
  }

  // A failed query must not read as "stock rclone", which would send the
  // update check to the wrong repository without saying so.
  void detectsNothingWhenTheQueryFailed() {
    QVERIFY(DetectRcloneRepo({}).isEmpty());
    QVERIFY(DetectRcloneRepo(ParseRcloneHelpFlags("some error\n")).isEmpty());
  }

  void malformedInputYieldsNothing_data() {
    QTest::addColumn<QByteArray>("output");
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("error") << QByteArray("unknown command \"flags\"\n");
    // An rclone old enough to have no flag groups. Better to offer no
    // completion than a list built out of whatever these lines are.
    QTest::newRow("no group headers")
        << QByteArray("Flags:\n      --checkers int   Number of checkers\n");
  }

  void malformedInputYieldsNothing() {
    QFETCH(QByteArray, output);
    QVERIFY(ParseRcloneHelpFlags(output).isEmpty());
  }

  // Windows line endings, since the binary may be a build from anywhere.
  void handlesCarriageReturns() {
    const QByteArray output =
        "Flags for x (flag group Copy):\r\n"
        "      --check-first     Do all the checks before starting\r\n";
    const QList<RcloneFlag> flags = ParseRcloneHelpFlags(output);
    QCOMPARE(flags.size(), 1);
    QCOMPARE(flags.at(0).name, QStringLiteral("--check-first"));
    QCOMPARE(flags.at(0).description,
             QStringLiteral("Do all the checks before starting"));
  }
};

QTEST_MAIN(TestRcloneFlags)
#include "test_rclone_flags.moc"
