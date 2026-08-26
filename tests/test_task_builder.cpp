#include "database.h"
#include "job_options.h"
#include "list_of_job_options.h"
#include "task_builder.h"
#include "utils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

// What makes a task a task. These rules lived inside TransferDialog, written
// out more than once, each copy welded to a message box and a call to focus a
// widget -- so an HTTP request creating a task could not be held to the same
// rules without building a dialog first.
//
// See docs/LAYER-SPLIT.md block 3 and VERIFY.md V-24.
class TestTaskBuilder : public QObject {
  Q_OBJECT

private:
  using Problem = TaskBuilder::Problem;

  static JobOptions download() {
    JobOptions task(true);
    task.jobType = JobOptions::JobType::Download;
    task.description = QStringLiteral("nightly");
    task.source = QStringLiteral("tgdrive:films");
    task.dest = QStringLiteral("D:/films");
    return task;
  }

  static JobOptions upload() {
    JobOptions task(false);
    task.jobType = JobOptions::JobType::Upload;
    task.description = QStringLiteral("nightly");
    task.source = QStringLiteral("D:/films");
    task.dest = QStringLiteral("tgdrive:films");
    return task;
  }

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
  int mCounter = 0;

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
  }

  void cleanupTestCase() {
    Database::closeForThread();
    QFile::remove(iniPath());
  }

  // A database of its own per test, so that saving really is saving and one
  // test cannot see another's tasks.
  void init() {
    ++mCounter;
    Database::closeForThread();
    Database::setPath(
        QDir(appDir()).filePath(QStringLiteral("tb%1.db").arg(mCounter)));
    QFile::remove(Database::path());
    QVERIFY2(Database::connection().isOpen(),
             qPrintable(Database::lastError()));
  }

  void cleanup() {
    const QString path = Database::path();
    Database::closeForThread();
    QFile::remove(path);
  }

  void aCompleteTaskHasNothingWrongWithIt() {
    QCOMPARE(TaskBuilder::Check(download()), Problem::None);
    QCOMPARE(TaskBuilder::Check(upload()), Problem::None);
  }

  void aTaskWithNoNameIsRefusedFirst() {
    JobOptions task = download();
    task.description.clear();
    task.dest.clear(); // two things wrong; the name is the one reported

    QCOMPARE(TaskBuilder::Check(task), Problem::NoName);
    QVERIFY(!TaskBuilder::Explain(Problem::NoName).isEmpty());
  }

  void whitespaceIsNotAName() {
    JobOptions task = upload();
    task.description = QStringLiteral("   ");
    QCOMPARE(TaskBuilder::Check(task), Problem::NoName);
  }

  // Which side is required depends on the direction, and getting this
  // backwards would refuse every valid task in one direction while letting
  // every broken one through in the other.
  void eachDirectionRequiresItsOwnSide() {
    JobOptions noDest = download();
    noDest.dest.clear();
    QCOMPARE(TaskBuilder::Check(noDest), Problem::NoDestination);

    JobOptions noSource = upload();
    noSource.source.clear();
    QCOMPARE(TaskBuilder::Check(noSource), Problem::NoSource);

    // And the far side is never the one complained about: it comes from the
    // remote being browsed, so it is not the user's to leave blank.
    JobOptions downloadNoSource = download();
    downloadNoSource.source.clear();
    QCOMPARE(TaskBuilder::Check(downloadNoSource), Problem::None);

    JobOptions uploadNoDest = upload();
    uploadNoDest.dest.clear();
    QCOMPARE(TaskBuilder::Check(uploadNoDest), Problem::None);
  }

  void everyProblemHasSomethingToSay() {
    for (Problem problem : {Problem::NoName, Problem::NoSource,
                            Problem::NoDestination}) {
      QVERIFY(!TaskBuilder::Explain(problem).isEmpty());
    }
    QVERIFY(TaskBuilder::Explain(Problem::None).isEmpty());
  }

  // The far side is the remote, whichever way the transfer goes.
  void theRemoteIsTheFarSide() {
    QCOMPARE(TaskBuilder::RemoteOf(download()), QStringLiteral("tgdrive"));
    QCOMPARE(TaskBuilder::RemoteOf(upload()), QStringLiteral("tgdrive"));
  }

  void aLocalPathNamesNoRemote() {
    JobOptions task = download();
    task.source = QStringLiteral("/mnt/elsewhere");
    QVERIFY(TaskBuilder::RemoteOf(task).isEmpty());
  }

  // A drive letter is the one local path that looks exactly like a remote.
  // Without this a task copied to a local disk would be named after "D".
  void aWindowsDriveLetterIsNotARemote() {
#if defined(Q_OS_WIN)
    JobOptions task = download();
    for (const char *local : {"D:/films", "D:\\films", "c:/films"}) {
      task.source = QString::fromLatin1(local);
      QVERIFY2(TaskBuilder::RemoteOf(task).isEmpty(), local);
    }

    // A real remote whose name happens to be one letter is still a remote
    // everywhere a drive letter is not possible -- but on Windows the two
    // cannot be told apart, and reading it as a drive is the safer of the
    // two wrong answers: it costs a name, not a wrong destination.
    task.source = QStringLiteral("tgdrive:films");
    QCOMPARE(TaskBuilder::RemoteOf(task), QStringLiteral("tgdrive"));
#else
    QSKIP("drive letters are a Windows problem");
#endif
  }

  // The shape the dialog has always produced. Pinned because a task named
  // differently is a task the user cannot find again.
  void theAutomaticNameKeepsItsShape() {
    const QDateTime when(QDate(2026, 8, 26), QTime(15, 13, 1));
    QCOMPARE(TaskBuilder::AutoName(QStringLiteral("tgdrive"), when),
             QStringLiteral("_tmp_26Aug2026_151301_tgdrive"));
    QCOMPARE(TaskBuilder::AutoName(download(), when),
             QStringLiteral("_tmp_26Aug2026_151301_tgdrive"));
  }

  void anAutomaticNameIsAValidName() {
    JobOptions task = upload();
    task.description = TaskBuilder::AutoName(task, QDateTime::currentDateTime());
    QCOMPARE(TaskBuilder::Check(task), Problem::None);
  }

  // The whole path in one call, which is what an API request will use: named
  // if it needs one, checked, and stored.
  void creatingATaskNamesChecksAndSavesIt() {
    auto *task = new JobOptions(false);
    task->jobType = JobOptions::JobType::Upload;
    task->description = QStringLiteral("evening upload");
    task->source = QStringLiteral("D:/films");
    task->dest = QStringLiteral("tgdrive:films");

    Problem problem = Problem::NoName;
    const QString id =
        TaskBuilder::Create(task, QDateTime::currentDateTime(), &problem);

    QVERIFY2(!id.isEmpty(), "a complete task was refused");
    QCOMPARE(problem, Problem::None);

    // Stored, not merely accepted.
    JobOptions *back = ListOfJobOptions::getInstance()->find(id);
    QVERIFY2(back != nullptr, "the task was not saved");
    QCOMPARE(back->description, QStringLiteral("evening upload"));
  }

  void aTaskWithNoNameIsGivenOneRatherThanRefused() {
    auto *task = new JobOptions(false);
    task->jobType = JobOptions::JobType::Upload;
    task->source = QStringLiteral("D:/films");
    task->dest = QStringLiteral("tgdrive:films");

    const QDateTime when(QDate(2026, 8, 26), QTime(15, 13, 1));
    Problem problem = Problem::NoName;
    const QString id = TaskBuilder::Create(task, when, &problem);

    QCOMPARE(problem, Problem::None);
    QVERIFY(!id.isEmpty());
    QCOMPARE(task->description, QStringLiteral("_tmp_26Aug2026_151301_tgdrive"));
  }

  // The failure this exists to prevent: an unchecked task stored anyway,
  // sitting in the list looking ordinary until the night it is due to run.
  void anIncompleteTaskIsNotSaved() {
    auto *task = new JobOptions(false);
    task->jobType = JobOptions::JobType::Upload;
    task->description = QStringLiteral("broken");
    task->source.clear(); // an upload with nothing to send
    task->dest = QStringLiteral("tgdrive:films");

    // Counted rather than expecting an empty list: ListOfJobOptions is a
    // singleton and keeps what earlier tests in this process put in it, so
    // "did this one get in" is the question, not "is the list empty".
    const int before = ListOfJobOptions::getInstance()->getTasks().size();

    Problem problem = Problem::None;
    const QString id =
        TaskBuilder::Create(task, QDateTime::currentDateTime(), &problem);

    QVERIFY2(id.isEmpty(), "an incomplete task was accepted");
    QCOMPARE(problem, Problem::NoSource);
    QCOMPARE(ListOfJobOptions::getInstance()->getTasks().size(), before);
    QVERIFY2(ListOfJobOptions::getInstance()->findByName(
                 QStringLiteral("broken")) == nullptr,
             "an incomplete task reached the list");

    delete task;
  }

  void creatingWithNothingToCreateIsRefusedRatherThanCrashing() {
    Problem problem = Problem::None;
    QVERIFY(TaskBuilder::Create(nullptr, QDateTime::currentDateTime(),
                                &problem)
                .isEmpty());
  }

  // A caller that does not care why is allowed not to ask.
  void theReasonIsOptional() {
    auto *task = new JobOptions(false);
    task->jobType = JobOptions::JobType::Upload;
    task->description = QStringLiteral("no reason wanted");
    task->source = QStringLiteral("D:/films");
    task->dest = QStringLiteral("tgdrive:films");

    QVERIFY(!TaskBuilder::Create(task, QDateTime::currentDateTime()).isEmpty());
  }
};

// GUILESS rather than APPLESS: JobOptions reaches for the settings, and
// without an application instance that is a crash before the first check.
QTEST_GUILESS_MAIN(TestTaskBuilder)
#include "test_task_builder.moc"
