#include "local_files.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

// How far the API may look on the machine it runs on. Uploading and
// downloading from the web page need the program to say what is on its own
// disk, and this is the line that says where that stops.
//
// See local_files.h and docs/WEB-API-PLAN.md.
class TestLocalFiles : public QObject {
  Q_OBJECT

private:
  QTemporaryDir mInside;  // a root the API is given
  QTemporaryDir mOutside; // somewhere it is not

  static void touch(const QString &path) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("x");
  }

  QString inside(const QString &relative = QString()) const {
    return relative.isEmpty() ? QFileInfo(mInside.path()).canonicalFilePath()
                              : QDir(mInside.path()).filePath(relative);
  }

private slots:
  void initTestCase() {
    QVERIFY(mInside.isValid() && mOutside.isValid());
    QVERIFY(QDir(mInside.path()).mkpath(QStringLiteral("films/2026")));
    touch(inside(QStringLiteral("films/a.mkv")));
    touch(QDir(mOutside.path()).filePath(QStringLiteral("secret.txt")));
  }

  void init() { qputenv("RB_LOCAL_ROOTS", mInside.path().toUtf8()); }
  void cleanup() { qunsetenv("RB_LOCAL_ROOTS"); }

  // With a list, the roots are the list and nothing else.
  void theRootsAreWhatWasListed() {
    QVERIFY(LocalFiles::IsRestricted());
    QCOMPARE(LocalFiles::Roots(), QStringList{inside()});

    // A root that does not exist is left out rather than offered: it cannot
    // contain anything, and a chooser would only show a folder that fails.
    qputenv("RB_LOCAL_ROOTS",
            (mInside.path() + QStringLiteral(";") + mInside.path() +
             QStringLiteral("/no-such-folder"))
                .toUtf8());
    QCOMPARE(LocalFiles::Roots(), QStringList{inside()});
  }

  // With none, the whole machine -- the program run on a desk, by the person
  // whose disk it is.
  void withNoListThereIsNoLine() {
    qunsetenv("RB_LOCAL_ROOTS");
    QVERIFY(!LocalFiles::IsRestricted());
    QVERIFY(!LocalFiles::Roots().isEmpty());
    QVERIFY(!LocalFiles::Allowed(mOutside.path()).isEmpty());
  }

  void whatIsInsideARootIsAllowed() {
    QVERIFY(!LocalFiles::Allowed(inside()).isEmpty());
    QVERIFY(!LocalFiles::Allowed(inside(QStringLiteral("films"))).isEmpty());
    QVERIFY(!LocalFiles::Allowed(inside(QStringLiteral("films/a.mkv"))).isEmpty());
  }

  // The one that matters. Whatever else works, a request must not be able to
  // name its way out of what it was given.
  void whatIsOutsideIsNot() {
    QVERIFY(LocalFiles::Allowed(mOutside.path()).isEmpty());
    QVERIFY(LocalFiles::Allowed(
                QDir(mOutside.path()).filePath(QStringLiteral("secret.txt")))
                .isEmpty());

    // Walking out with "..", from inside.
    QVERIFY(LocalFiles::Allowed(inside(QStringLiteral("films/../..")))
                .isEmpty());
    QVERIFY(LocalFiles::Allowed(
                inside(QStringLiteral("films/../../") +
                       QFileInfo(mOutside.path()).fileName()))
                .isEmpty());

    // A folder whose name merely begins like the root's is not inside it.
    QVERIFY(LocalFiles::Allowed(mInside.path() + QStringLiteral("-other"))
                .isEmpty());

    // And a path relative to nothing in particular, or no path at all.
    QVERIFY(LocalFiles::Allowed(QStringLiteral("films")).isEmpty());
    QVERIFY(LocalFiles::Allowed(QString()).isEmpty());
  }

  // A destination about to be made can be named, as long as the folder it
  // would be made in is allowed -- and not when that folder is not.
  void aPlaceThatDoesNotExistYetIsJudgedByItsFolder() {
    QVERIFY(!LocalFiles::Allowed(inside(QStringLiteral("films/new-folder")))
                 .isEmpty());
    QVERIFY(LocalFiles::Allowed(
                QDir(mOutside.path()).filePath(QStringLiteral("new-folder")))
                .isEmpty());
    // Two levels that do not exist: nothing real to judge it by.
    QVERIFY(LocalFiles::Allowed(inside(QStringLiteral("nope/deeper"))).isEmpty());
  }

  // A link inside a root that points outside it does not lead out.
  void aLinkOutOfARootDoesNotLeadOut() {
#if defined(Q_OS_WIN)
    QSKIP("making a real symbolic link needs a privilege tests do not have");
#else
    const QString link = inside(QStringLiteral("way-out"));
    QVERIFY(QFile::link(mOutside.path(), link));

    QVERIFY(LocalFiles::Allowed(link).isEmpty());
    QVERIFY(LocalFiles::Allowed(link + QStringLiteral("/secret.txt")).isEmpty());

    QList<LocalFiles::Entry> entries;
    QString why;
    QVERIFY(!LocalFiles::List(link, &entries, &why));
    QFile::remove(link);
#endif
  }

  void aFolderIsListedWithFoldersFirst() {
    QList<LocalFiles::Entry> entries;
    QString why;
    QVERIFY2(LocalFiles::List(inside(QStringLiteral("films")), &entries, &why),
             qPrintable(why));

    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0).name, QStringLiteral("2026"));
    QVERIFY(entries.at(0).isDir);
    QCOMPARE(entries.at(1).name, QStringLiteral("a.mkv"));
    QVERIFY(!entries.at(1).isDir);
    QCOMPARE(entries.at(1).size, Q_INT64_C(1));
  }

  // An empty path is where a chooser starts: the roots themselves.
  void anEmptyPathListsThePlacesToStartFrom() {
    QList<LocalFiles::Entry> entries;
    QString why;
    QVERIFY(LocalFiles::List(QString(), &entries, &why));
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().path, inside());
  }

  // Outside and not-there get the same answer. Which of the two it is, is
  // itself something about the disk.
  void outsideAndMissingAreNotToldApart() {
    QList<LocalFiles::Entry> entries;
    QString outside;
    QString missing;
    QVERIFY(!LocalFiles::List(mOutside.path(), &entries, &outside));
    QVERIFY(!LocalFiles::List(inside(QStringLiteral("nope/deeper")), &entries,
                              &missing));
    QCOMPARE(outside, missing);
    QVERIFY(entries.isEmpty());

    QString notAFolder;
    QVERIFY(!LocalFiles::List(inside(QStringLiteral("films/a.mkv")), &entries,
                              &notAFolder));
    QVERIFY(!notAFolder.isEmpty());
  }
};

QTEST_GUILESS_MAIN(TestLocalFiles)
#include "test_local_files.moc"
