#include "remote_registry.h"

#include <QSignalSpy>
#include <QTest>

// The remote list was parsed inside the lambda that also chose an icon size,
// with a rule that threw away any line that did not split into exactly two
// pieces. These tests pin the parsing on its own, where it can be looked at.
// See docs/API.md S5.
class TestRemoteRegistry : public QObject {
  Q_OBJECT

private slots:
  // Real output from "rclone listremotes --long".
  void readsNameAndType() {
    const QList<Remote> remotes = ParseListRemotes(
        "gdrive: drive\n"
        "tgdrive_main_01: teldrive\n"
        "local_disk: local\n"
        "encrypted: crypt\n");

    QCOMPARE(remotes.size(), 4);
    QCOMPARE(remotes[0].name, QStringLiteral("gdrive"));
    QCOMPARE(remotes[0].type, QStringLiteral("drive"));
    QCOMPARE(remotes[1].name, QStringLiteral("tgdrive_main_01"));
    QCOMPARE(remotes[1].type, QStringLiteral("teldrive"));
    QCOMPARE(remotes[3].type, QStringLiteral("crypt"));
  }

  void blankLinesAndPaddingAreIgnored() {
    const QList<Remote> remotes = ParseListRemotes("\n"
                                                   "  gdrive:   drive  \n"
                                                   "\n"
                                                   "\r\n"
                                                   "local: local\n");
    QCOMPARE(remotes.size(), 2);
    QCOMPARE(remotes[0].name, QStringLiteral("gdrive"));
    QCOMPARE(remotes[0].type, QStringLiteral("drive"));
  }

  // A type with a space in it -- "Google Cloud Storage" is one -- used to make
  // the line split into three pieces and be dropped, taking the remote off
  // the screen entirely.
  void aTypeWithASpaceIsStillARemote() {
    const QList<Remote> remotes =
        ParseListRemotes("bucket: google cloud storage\n");
    QCOMPARE(remotes.size(), 1);
    QCOMPARE(remotes[0].name, QStringLiteral("bucket"));
    QCOMPARE(remotes[0].type, QStringLiteral("google cloud storage"));
  }

  void aLineThatIsNotARemoteIsSkipped() {
    const QList<Remote> remotes = ParseListRemotes(
        "gdrive: drive\n"
        "something rclone printed that is not a remote\n"
        ": no name\n"
        "no type:\n"
        "local: local\n");

    // Better to lose a line nobody can read than to show a remote with the
    // wrong type: the type decides the icon and which operations are offered.
    QCOMPARE(remotes.size(), 2);
    QCOMPARE(remotes[0].name, QStringLiteral("gdrive"));
    QCOMPARE(remotes[1].name, QStringLiteral("local"));
  }

  void noRemotesIsNotAnError() {
    QVERIFY(ParseListRemotes("").isEmpty());
    QVERIFY(ParseListRemotes("\n\n").isEmpty());
  }

  void findAcceptsTheTrailingColonPathsUse() {
    RemoteRegistry &registry = RemoteRegistry::instance();
    QSignalSpy refreshed(&registry, &RemoteRegistry::refreshed);

    registry.setRemotes(ParseListRemotes("gdrive: drive\nlocal: local\n"));
    QCOMPARE(refreshed.count(), 1);
    QCOMPARE(registry.count(), 2);

    // Everywhere a remote is used as a path it carries a colon, and that is
    // usually how a caller has the name in hand.
    QVERIFY(registry.find(QStringLiteral("gdrive")) != nullptr);
    QVERIFY(registry.find(QStringLiteral("gdrive:")) != nullptr);
    QCOMPARE(registry.find(QStringLiteral("gdrive:"))->type,
             QStringLiteral("drive"));

    QVERIFY(registry.find(QStringLiteral("missing")) == nullptr);
    QVERIFY(registry.find(QString()) == nullptr);
  }
};

QTEST_MAIN(TestRemoteRegistry)
#include "test_remote_registry.moc"
