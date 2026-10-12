#include "remote_ops.h"

#include <QTest>

// What is done to a remote's files, as the core understands it. The commands
// themselves are exercised through the API in test_api_server, against a
// stand-in rclone; this is the part that needs no process at all.
//
// See docs/WEB-API-PLAN.md.
class TestRemoteOps : public QObject {
  Q_OBJECT

private slots:
  // One name, not a path. The window never checked, because whoever typed it
  // was looking at the folder they were in; a request over HTTP is not, and
  // "../../somewhere" as a folder name is how something ends up made, or
  // later deleted, in a place nobody was looking at.
  void aNameIsOneNameAndNotAWayOfGettingSomewhereElse() {
    QVERIFY(RemoteOps::WhyNotAName(QStringLiteral("films")).isEmpty());
    QVERIFY(RemoteOps::WhyNotAName(QStringLiteral("My Films 2026")).isEmpty());
    QVERIFY(RemoteOps::WhyNotAName(QStringLiteral(".hidden")).isEmpty());
    QVERIFY(RemoteOps::WhyNotAName(QStringLiteral("a..b")).isEmpty());

    for (const char *bad : {"", "   ", ".", "..", "a/b", "../x", "a\\b", "/x"}) {
      QVERIFY2(!RemoteOps::WhyNotAName(QString::fromLatin1(bad)).isEmpty(), bad);
    }
  }

  void joiningPutsANameInsideAFolder() {
    QCOMPARE(RemoteOps::Join(QString(), QStringLiteral("films")),
             QStringLiteral("films"));
    QCOMPARE(RemoteOps::Join(QStringLiteral("a/b"), QStringLiteral("c")),
             QStringLiteral("a/b/c"));

    // However the folder was spelled, there is one slash between them.
    QCOMPARE(RemoteOps::Join(QStringLiteral("/a/b/"), QStringLiteral("c")),
             QStringLiteral("a/b/c"));
    QCOMPARE(RemoteOps::Join(QStringLiteral("/"), QStringLiteral("c")),
             QStringLiteral("c"));
  }

  void theTargetIsRemoteColonPath() {
    QCOMPARE(RemoteOps::Target(QStringLiteral("gdrive"), QString()),
             QStringLiteral("gdrive:"));
    QCOMPARE(RemoteOps::Target(QStringLiteral("gdrive"),
                               QStringLiteral("/films/2026/")),
             QStringLiteral("gdrive:films/2026"));
  }

  void aPathIsAFolderAndAName() {
    QCOMPARE(RemoteOps::ParentOf(QStringLiteral("a/b/c")), QStringLiteral("a/b"));
    QCOMPARE(RemoteOps::NameOf(QStringLiteral("a/b/c")), QStringLiteral("c"));

    // At the top of a remote there is a name and no folder above it.
    QCOMPARE(RemoteOps::ParentOf(QStringLiteral("films")), QString());
    QCOMPARE(RemoteOps::NameOf(QStringLiteral("films")), QStringLiteral("films"));

    // Slashes at the ends change nothing.
    QCOMPARE(RemoteOps::ParentOf(QStringLiteral("/a/b/")), QStringLiteral("a"));
    QCOMPARE(RemoteOps::NameOf(QStringLiteral("/a/b/")), QStringLiteral("b"));
  }

  // Renaming takes a name, not a path: where a thing is does not change.
  // Everything refused here is refused before rclone is started.
  void renamingIsRefusedWhenThereIsNothingToDo() {
    QString why;

    // The root of a remote has no name.
    QVERIFY(RemoteOps::Rename(QStringLiteral("gdrive"), QString(),
                              QStringLiteral("x"), &why) == nullptr);
    QVERIFY(!why.isEmpty());

    // The name it already has. rclone would move it onto itself and report
    // success, and the caller would be told something had happened.
    why.clear();
    QVERIFY(RemoteOps::Rename(QStringLiteral("gdrive"),
                              QStringLiteral("films/2026"),
                              QStringLiteral("2026"), &why) == nullptr);
    QVERIFY(!why.isEmpty());

    // A "name" that would put it somewhere else.
    why.clear();
    QVERIFY(RemoteOps::Rename(QStringLiteral("gdrive"),
                              QStringLiteral("films/2026"),
                              QStringLiteral("../elsewhere"), &why) == nullptr);
    QVERIFY(!why.isEmpty());
  }

  // The whole remote is never what "delete" means from here, however the
  // empty path is spelled.
  void deletingRefusesTheRootOfARemote() {
    for (const char *path : {"", "/", "//", "   "}) {
      RemoteOps::Item item;
      item.remote = QStringLiteral("gdrive");
      item.path = QString::fromLatin1(path).trimmed();
      item.isFolder = true;

      QString why;
      QVERIFY2(RemoteOps::Delete(item, &why) == nullptr, path);
      QVERIFY(!why.isEmpty());
    }
  }

  // Putting a thing somewhere: refused when there is nowhere for it to go
  // that makes sense, before rclone is asked to try.
  void copyingIsRefusedWhenItCannotMakeSense() {
    const auto item = [](const char *path, bool folder) {
      RemoteOps::Item it;
      it.remote = QStringLiteral("gdrive");
      it.path = QString::fromLatin1(path);
      it.isFolder = folder;
      return it;
    };
    QString why;

    // The whole remote is not a thing to be copied into part of itself.
    QVERIFY(RemoteOps::CopyInto(false, item("", true), QStringLiteral("gdrive"),
                                QStringLiteral("backup"), false, &why) == nullptr);

    // Where it already is.
    QVERIFY(RemoteOps::CopyInto(true, item("films/a.mkv", false),
                                QStringLiteral("gdrive"),
                                QStringLiteral("films"), false, &why) == nullptr);

    // A folder into itself, and into something inside itself.
    QVERIFY(RemoteOps::CopyInto(true, item("films", true),
                                QStringLiteral("gdrive"),
                                QStringLiteral("films"), false, &why) == nullptr);
    QVERIFY(RemoteOps::CopyInto(false, item("films", true),
                                QStringLiteral("gdrive"),
                                QStringLiteral("films/2026/old"), false,
                                &why) == nullptr);
    QVERIFY(!why.isEmpty());
  }

  // A line of "rclone lsl", taken apart -- and a line that is not one, left
  // out, which is how the notices rclone prints among them stay out of the
  // exported file.
  void aLineOfAListingIsTakenApart() {
    qint64 size = -1;
    QString modified;
    QString name;

    QVERIFY(RemoteOps::ParseLslLine(
        QStringLiteral("   300000 2026-10-12 14:16:41.398206300 films/a b.mkv"),
        &size, &modified, &name));
    QCOMPARE(size, Q_INT64_C(300000));
    QCOMPARE(modified, QStringLiteral("2026-10-12 14:16:41"));
    QCOMPARE(name, QStringLiteral("films/a b.mkv"));

    QVERIFY(!RemoteOps::ParseLslLine(
        QStringLiteral("2026/10/12 12:00:00 NOTICE: something else"), &size,
        &modified, &name));
    QVERIFY(!RemoteOps::ParseLslLine(QString(), nullptr, nullptr, nullptr));
  }

  // The exported row, the same wherever it is written. A quote in a name is
  // doubled; written as it stood, it ended the field early and broke the row.
  void anExportedRowSurvivesAQuoteInAName() {
    QCOMPARE(RemoteOps::ExportRow(QStringLiteral("a.mkv"),
                                  QStringLiteral("2026-10-12 14:16:41"), 300000,
                                  false),
             QStringLiteral("\"a.mkv\",\"2026-10-12 14:16:41\",300000"));
    QCOMPARE(RemoteOps::ExportRow(QStringLiteral("a.mkv"), QString(), 0, true),
             QStringLiteral("\"a.mkv\""));
    QCOMPARE(RemoteOps::ExportRow(QStringLiteral("b \"quoted\".txt"), QString(),
                                  0, true),
             QStringLiteral("\"b \"\"quoted\"\".txt\""));
  }

  // rclone ends a check with an error when the two sides differ. Whether it
  // got as far as counting is what tells a difference from a failure.
  void aDifferenceIsToldFromAFailure() {
    const RemoteOps::CheckResult same = RemoteOps::ReadCheckOutput(
        {QStringLiteral("NOTICE: x: 0 differences found"),
         QStringLiteral("NOTICE: x: 3 matching files")});
    QVERIFY(same.completed());
    QVERIFY(same.matched());
    QCOMPARE(same.matching, 3);

    const RemoteOps::CheckResult differs = RemoteOps::ReadCheckOutput(
        {QStringLiteral("ERROR : zzz.txt: file not in x"),
         QStringLiteral("NOTICE: x: 1 differences found"),
         QStringLiteral("NOTICE: Failed to check: 1 differences found")});
    QVERIFY(differs.completed());
    QVERIFY(!differs.matched());
    QCOMPARE(differs.differences, 1);

    const RemoteOps::CheckResult failed = RemoteOps::ReadCheckOutput(
        {QStringLiteral("CRITICAL: Failed to create file system")});
    QVERIFY(!failed.completed());
  }

  // A name that will not do starts nothing, and says why. No job, no process,
  // no row in the history for something that was never attempted.
  void aNameThatWillNotDoStartsNothing() {
    QString why;
    QVERIFY(RemoteOps::MakeFolder(QStringLiteral("gdrive"), QString(),
                                  QStringLiteral("../escape"), &why) == nullptr);
    QVERIFY(!why.isEmpty());
  }
};

QTEST_GUILESS_MAIN(TestRemoteOps)
#include "test_remote_ops.moc"
