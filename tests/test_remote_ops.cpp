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
