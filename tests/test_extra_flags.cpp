#include "extra_flags.h"
#include "rclone_flags.h"

#include <QTest>

// Which rclone flags a task may be given through the API. Two rules: a flag
// has to be one this rclone has, and some are refused whatever rclone thinks
// of them.
//
// See docs/WEB-API-PLAN.md and VERIFY.md V-39.
class TestExtraFlags : public QObject {
  Q_OBJECT

private:
  static RcloneFlag flag(const char *name, const char *type = "",
                         const char *shortName = "") {
    RcloneFlag f;
    f.name = QLatin1String(name);
    f.type = QLatin1String(type);
    f.shortName = QLatin1String(shortName);
    return f;
  }

  // A little of what "rclone help flags" says.
  static QList<RcloneFlag> known() {
    return {flag("--fast-list"),
            flag("--ignore-case"),
            flag("--include", "stringArray"),
            flag("--max-depth", "int"),
            flag("--verbose", "count", "-v"),
            flag("--copy-links", "", "-L"),
            flag("--config", "string"),
            flag("--password-command", "SpaceSepList"),
            flag("--files-from", "stringArray"),
            flag("--rc")};
  }

private slots:
  void whatRcloneHasIsAccepted_data() {
    QTest::addColumn<QString>("extra");
    QTest::newRow("nothing") << "";
    QTest::newRow("one") << "--fast-list";
    QTest::newRow("value after a space") << "--include \"*.jpg\" --ignore-case";
    QTest::newRow("value after an equals") << "--max-depth=3";
    QTest::newRow("a value that looks like a flag") << "--max-depth -1";
    QTest::newRow("one to a line") << "--fast-list\n--max-depth 2";
    QTest::newRow("short") << "-v";
    QTest::newRow("short, twice") << "-vv";
  }

  void whatRcloneHasIsAccepted() {
    QFETCH(QString, extra);
    QCOMPARE(ExtraFlags::Check(extra, known(), false), QString());
  }

  void whatItDoesNotHaveIsRefusedByName_data() {
    QTest::addColumn<QString>("extra");
    QTest::addColumn<QString>("named");
    QTest::newRow("misspelt") << "--fast_list" << "--fast_list";
    QTest::newRow("after a good one") << "--fast-list --nonsense" << "--nonsense";
    QTest::newRow("a word on its own") << "hello" << "hello";
    QTest::newRow("a second value") << "--max-depth 1 2" << "2";
    QTest::newRow("a value left off") << "--fast-list --max-depth" << "--max-depth";
    QTest::newRow("short") << "-x" << "-x";
  }

  void whatItDoesNotHaveIsRefusedByName() {
    QFETCH(QString, extra);
    QFETCH(QString, named);
    const QString why = ExtraFlags::Check(extra, known(), false);
    QVERIFY2(why.contains(named), qPrintable(why));
  }

  // Refused although rclone has them: these are not options for a transfer.
  void whatRunsAProgramIsRefusedWhateverRcloneThinks_data() {
    QTest::addColumn<QString>("extra");
    QTest::newRow("config") << "--config /tmp/other.conf";
    QTest::newRow("config with equals") << "--config=/tmp/other.conf";
    QTest::newRow("password command") << "--password-command \"sh -c id\"";
    QTest::newRow("remote control") << "--rc";
    QTest::newRow("remote control option") << "--rc-addr=:5572";
    QTest::newRow("a backend's command") << "--sftp-key-file-command x";
    QTest::newRow("behind a good one") << "--fast-list\n--config x";
  }

  void whatRunsAProgramIsRefusedWhateverRcloneThinks() {
    QFETCH(QString, extra);
    // With the list and without it: this half does not depend on rclone
    // having answered.
    QVERIFY(!ExtraFlags::Check(extra, known(), false).isEmpty());
    QVERIFY(!ExtraFlags::Check(extra, {}, false).isEmpty());
  }

  // Where the API is held to a list of folders, a flag that reads somewhere
  // else on the machine would be the way round it.
  void whatReachesElsewhereIsRefusedOnlyWhereThereIsALine() {
    for (const char *extra : {"--copy-links", "-L", "--files-from /etc/list"}) {
      QVERIFY2(ExtraFlags::Check(QLatin1String(extra), known(), false).isEmpty(),
               extra);
      QVERIFY2(!ExtraFlags::Check(QLatin1String(extra), known(), true).isEmpty(),
               extra);
      QVERIFY2(!ExtraFlags::Check(QLatin1String(extra), {}, true).isEmpty(),
               extra);
    }
  }

  // rclone puts every backend's flags in one group; the name is all that
  // says whose they are.
  void aFlagIsForTheKindOfRemoteItsNameBeginsWith() {
    const auto backend = [](const char *name) {
      RcloneFlag f = flag(name, "string");
      f.group = QStringLiteral("Backend");
      return f;
    };
    const QList<RcloneFlag> list = {
        backend("--drive-chunk-size"), backend("--drive-description"),
        backend("--teldrive-chunk-size"), backend("--teldrive-description"),
        backend("--local-encoding"), backend("--local-description"),
        // The local disk's, which carry no prefix.
        backend("--copy-links"), backend("--one-file-system"),
        flag("--fast-list")};

    const QHash<QString, QString> backends = ExtraFlags::Backends(list);
    QCOMPARE(backends.value(QStringLiteral("--drive-chunk-size")),
             QStringLiteral("drive"));
    QCOMPARE(backends.value(QStringLiteral("--teldrive-description")),
             QStringLiteral("teldrive"));
    QCOMPARE(backends.value(QStringLiteral("--copy-links")),
             QStringLiteral("local"));
    QCOMPARE(backends.value(QStringLiteral("--one-file-system")),
             QStringLiteral("local"));
    // For any kind: not in the answer.
    QVERIFY(!backends.contains(QStringLiteral("--fast-list")));

    QCOMPARE(ExtraFlags::FlagPrefixOfType(QStringLiteral("drive")),
             QStringLiteral("drive"));
    QCOMPARE(ExtraFlags::FlagPrefixOfType(QStringLiteral("google photos")),
             QStringLiteral("gphotos"));
    QVERIFY(ExtraFlags::WrapsAnother(QStringLiteral("crypt")));
    QVERIFY(!ExtraFlags::WrapsAnother(QStringLiteral("teldrive")));
  }

  // rclone could not be asked: nothing is known to be wrong, so only what is
  // forbidden is.
  void withoutTheListOnlyTheForbiddenIsRefused() {
    QCOMPARE(ExtraFlags::Check(QStringLiteral("--anything --at all"), {}, false),
             QString());
  }
};

QTEST_APPLESS_MAIN(TestExtraFlags)
#include "test_extra_flags.moc"
