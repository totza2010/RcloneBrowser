#include "directory_listing.h"
#include "utils.h"

#include <QSignalSpy>
#include <QTest>

// Reading one directory of a remote. This lived inside ItemModel::load(),
// interleaved with building tree rows and running a spinner, so it had never
// been tested and could not be asked for by anything that was not building a
// tree.
//
// These tests do not need a real remote: what is being pinned is the shape of
// the thing -- what it is asked, what it says when it cannot answer, and that
// a listing nobody is waiting for goes quiet.
//
// See docs/LAYER-SPLIT.md block 4 and VERIFY.md V-24.
class TestDirectoryListing : public QObject {
  Q_OBJECT

private slots:
  // The form rclone takes, and the one place a missing colon would send every
  // listing to the wrong place.
  void theTargetIsRemoteColonPath() {
    DirectoryListing root(QStringLiteral("tgdrive"), QString());
    QCOMPARE(root.target(), QStringLiteral("tgdrive:"));

    DirectoryListing deep(QStringLiteral("tgdrive"),
                          QStringLiteral("films/2026"));
    QCOMPARE(deep.target(), QStringLiteral("tgdrive:films/2026"));
  }

  void itKnowsWhatItWasAskedFor() {
    DirectoryListing listing(QStringLiteral("gdrive"), QStringLiteral("a/b"));
    QCOMPARE(listing.remote(), QStringLiteral("gdrive"));
    QCOMPARE(listing.path(), QStringLiteral("a/b"));
    QCOMPARE(listing.count(), 0);
    QVERIFY(!listing.isRunning());
  }

  // An rclone that is not there has to say so. A listing that fails quietly
  // shows an empty folder, which reads as "there is nothing here".
  void anRcloneThatIsNotThereReportsAFailure() {
    SetRclone(QStringLiteral("no-such-rclone-anywhere"));

    DirectoryListing listing(QStringLiteral("tgdrive"), QString());
    QSignalSpy failed(&listing, &DirectoryListing::failed);
    QSignalSpy finished(&listing, &DirectoryListing::finished);

    listing.start();

    // Failing to start is reported on the spot, before returning to the
    // event loop, so waiting first would wait for a second signal.
    if (failed.isEmpty()) {
      QVERIFY2(failed.wait(15000), "a missing rclone was never reported");
    }
    QCOMPARE(finished.count(), 0);
    QVERIFY2(!failed.first().at(0).toString().isEmpty(),
             "the failure was reported with nothing to say about it");
  }

  // Cancelling is not failing. Somebody who closed the tab does not want to
  // be told the listing they stopped did not finish.
  void cancellingSaysNothingAtAll() {
    SetRclone(QStringLiteral("no-such-rclone-anywhere"));

    DirectoryListing listing(QStringLiteral("tgdrive"), QString());
    listing.cancel(); // before it ever started: must not crash

    QSignalSpy failed(&listing, &DirectoryListing::failed);
    QSignalSpy finished(&listing, &DirectoryListing::finished);

    listing.start();
    listing.cancel();

    QTest::qWait(300);
    QCOMPARE(finished.count(), 0);
    QVERIFY2(failed.count() <= 1,
             "a cancelled listing kept reporting after it was stopped");
  }

  // A listing that ended can be started again -- which is what a retry after
  // a failure is. The guard against starting twice is about one that is
  // still going, not about one that is over.
  //
  // Written as the weaker claim on purpose: the first version of this test
  // asserted that two starts could never produce two failures, which the
  // design does not promise and should not.
  void aListingThatFailedCanBeTriedAgain() {
    SetRclone(QStringLiteral("no-such-rclone-anywhere"));

    DirectoryListing listing(QStringLiteral("tgdrive"), QString());
    QSignalSpy failed(&listing, &DirectoryListing::failed);

    listing.start();
    QCOMPARE(failed.count(), 1);
    QVERIFY2(!listing.isRunning(), "a listing that failed still calls itself running");

    listing.start();
    QCOMPARE(failed.count(), 2);
  }
};

QTEST_MAIN(TestDirectoryListing)
#include "test_directory_listing.moc"
