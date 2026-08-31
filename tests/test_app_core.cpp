#include "app_core.h"
#include "database.h"
#include "job_queue.h"
#include "scheduler_store.h"
#include "utils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

// The application, assembled by something that is not a window.
//
// Every part of this program could already answer for itself without a
// window; nothing put them together except MainWindow's constructor. These
// tests are about the order in which AppCore does it, because the order is
// where it went wrong: it starts a queue that was left running, and starting
// a job needs to know where rclone is.
//
// See docs/LAYER-SPLIT.md and VERIFY.md V-24 item 6.4.
class TestAppCore : public QObject {
  Q_OBJECT

private:
  static QString appDir() {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return QCoreApplication::applicationDirPath();
#else
    return QDir(QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME")))
        .filePath("rclone-browser");
#endif
  }

  static QString iniPath() {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return QDir(appDir()).filePath(
        QFileInfo(QCoreApplication::applicationFilePath()).baseName() + ".ini");
#else
    return QDir(appDir()).filePath("rclone-browser.ini");
#endif
  }

  std::unique_ptr<QTemporaryDir> mScratch;

private slots:
  void initTestCase() {
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    mScratch.reset(new QTemporaryDir);
    QVERIFY(mScratch->isValid());
    qputenv("XDG_CONFIG_HOME", mScratch->path().toLocal8Bit());
    QVERIFY(QDir().mkpath(appDir()));
#endif
    QFile ini(iniPath());
    QVERIFY(ini.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ini.close();
    QVERIFY2(IsPortableMode(), "portable mode did not take effect");

    Database::setPath(QDir(appDir()).filePath(QStringLiteral("core.db")));
    QFile::remove(Database::path());
    QVERIFY2(Database::connection().isOpen(),
             qPrintable(Database::lastError()));

    // Nothing must run: the queue switched off means start() reads it and
    // stops there.
    GetSettings()->setValue(QStringLiteral("Settings/queueStatus"), "false");
    GetSettings()->setValue(QStringLiteral("Settings/rclone"),
                            QStringLiteral("C:/somewhere/rclone.exe"));
    GetSettings()->sync();
  }

  void cleanupTestCase() {
    const QString path = Database::path();
    Database::closeForThread();
    QFile::remove(path);
    QFile::remove(iniPath());
  }

  // The one that went wrong. start() reads the stored queue, and a queue left
  // running starts its first job straight away -- so where rclone is has to
  // be known before that, not afterwards in a window's constructor. Without
  // it rclone was started as "" , failed in milliseconds, and the entry was
  // consumed: the queue simply did not come back, and the only sign was a
  // line in the job log reading: running "" with 25 arguments.
  void itKnowsWhereRcloneIsBeforeAnythingCanRun() {
    // Not checked for emptiness first: in portable mode GetRclone() turns a
    // relative path into one under the application directory, so "unset"
    // does not read back as empty. What matters is where it points after.
    SetRclone(QStringLiteral("C:/wrong/place/rclone.exe"));

    AppCore::instance().start();

    QCOMPARE(GetRclone(), QStringLiteral("C:/somewhere/rclone.exe"));
  }

  // Reading the stored state is start()'s job, and it has to have happened
  // by the time start() returns -- a window built afterwards draws what is
  // already there rather than loading it again.
  void itHasReadTheStoredStateByTheTimeItReturns() {
    QVERIFY(AppCore::instance().isStarted());

    // Loaded, not merely constructed: both of these are empty here, but they
    // have been read, which is what lets a window draw from them.
    QCOMPARE(JobQueue::instance().count(), 0);
    QCOMPARE(SchedulerStore::instance().count(), 0);

    // The queue drives itself from here, whoever is watching. This was
    // switched on by the window, so a queue left running only resumed if
    // somebody opened one.
    QVERIFY2(JobQueue::instance().drivesItself(),
             "the queue was left waiting for a window to drive it");
  }

  // Two clocks would each start the schedule that came due, which is two
  // copies of one transfer writing the same destination.
  void startingTwiceDoesNothingTheSecondTime() {
    QVERIFY(AppCore::instance().isStarted());
    AppCore::instance().start(); // must be a no-op
    QVERIFY(AppCore::instance().isStarted());
  }

  void runningAScheduleThatDoesNotExistIsRefusedWithAReason() {
    QString reason;
    const QString id =
        AppCore::instance().runNow(QStringLiteral("{no-such-schedule}"), &reason);

    QVERIFY(id.isEmpty());
    QVERIFY2(!reason.isEmpty(),
             "refused without saying why, which is what the button shows");
  }

  // Looking now must not throw or hang when there is nothing to look at.
  void checkingWithNoSchedulesIsHarmless() {
    QSignalSpy ticked(&AppCore::instance(), &AppCore::ticked);
    AppCore::instance().checkSchedules();
    QCOMPARE(ticked.count(), 1);
  }
};

QTEST_GUILESS_MAIN(TestAppCore)
#include "test_app_core.moc"
