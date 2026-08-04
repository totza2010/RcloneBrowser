#include "job_options.h"
#include "list_of_job_options.h"
#include "utils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTest>

// The task file is a QDataStream of JobOptions written field by field, with
// no field names. Reordering or inserting a field silently shifts everything
// after it, and the failure shows up as a saved task that loads with the
// wrong settings rather than as an error. That makes it the highest-risk part
// of the layer split, and the reason for a golden file.
//
// The store reads and writes next to the executable in portable mode, which
// is what lets this run against a fixture instead of the user's real tasks.
class TestTaskStore : public QObject {
  Q_OBJECT

private:
  // Portable mode keys off "<executable base name>.ini" sitting next to the
  // executable, so writing one here redirects the store into the build tree.
  static QString appDir() {
    return QCoreApplication::applicationDirPath();
  }
  static QString iniPath() {
    return QDir(appDir()).filePath(
        QFileInfo(QCoreApplication::applicationFilePath()).baseName() + ".ini");
  }
  static QString taskFilePath() {
    return QDir(appDir()).filePath("tasks.bin");
  }

  // QDataStream writes QString as UTF-16 big-endian. Decoding the file with
  // QString::fromUtf16 would read it in host order and silently produce
  // mojibake, which made an earlier version of the credential check pass
  // without looking at anything. Build the needle in the file's own encoding
  // and search the bytes instead.
  static bool fileContains(const QString &path, const QString &needle) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      return false;
    }
    const QByteArray raw = file.readAll();
    file.close();

    QByteArray encoded;
    encoded.reserve(needle.size() * 2);
    for (const QChar &c : needle) {
      encoded.append(static_cast<char>(c.unicode() >> 8));
      encoded.append(static_cast<char>(c.unicode() & 0xff));
    }
    return raw.contains(encoded);
  }

private slots:
  void initTestCase() {
    QFile ini(iniPath());
    QVERIFY2(ini.open(QIODevice::WriteOnly | QIODevice::Truncate),
             qPrintable(ini.errorString()));
    ini.close();
    QVERIFY2(IsPortableMode(),
             "portable mode did not take effect; the store would read the "
             "real user task file");

    // Start from the golden file rather than whatever a previous run left.
    QFile::remove(taskFilePath());
    QVERIFY(QFile::copy(QStringLiteral(RB_FIXTURES_DIR "/tasks_v8.bin"),
                        taskFilePath()));
  }

  void cleanupTestCase() {
    QFile::remove(iniPath());
    QFile::remove(taskFilePath());
  }

  // Every field of the golden file, so a shift of even one position fails
  // here instead of in front of a user.
  void loadsGoldenFile() {
    const QList<JobOptions *> &tasks =
        ListOfJobOptions::getInstance()->getTasks();
    QCOMPARE(tasks.size(), 2);

    const JobOptions *copy = tasks.at(0);
    QCOMPARE(copy->description, QStringLiteral("nightly photos"));
    QCOMPARE(copy->operation, JobOptions::Copy);
    QCOMPARE(copy->jobType, JobOptions::Upload);
    QCOMPARE(copy->source, QStringLiteral("C:/src/photos"));
    QCOMPARE(copy->dest, QStringLiteral("remote:backup/photos"));
    QCOMPARE(copy->isFolder, true);
    QCOMPARE(copy->transfers, QStringLiteral("4"));
    QCOMPARE(copy->checkers, QStringLiteral("8"));
    QCOMPARE(copy->bandwidth, QStringLiteral("1M"));
    QCOMPARE(copy->connectTimeout, QStringLiteral("60s"));
    QCOMPARE(copy->idleTimeout, QStringLiteral("300s"));
    QCOMPARE(copy->retries, QStringLiteral("3"));
    QCOMPARE(copy->lowLevelRetries, QStringLiteral("10"));
    QCOMPARE(copy->skipNewer, true);
    QCOMPARE(copy->skipExisting, false);
    QCOMPARE(copy->deleteExcluded, false);
    QCOMPARE(copy->excluded, QStringLiteral("*.tmp"));
    QCOMPARE(copy->extra, QStringLiteral("--fast-list"));
    QCOMPARE(copy->remoteType, QStringLiteral("teldrive"));
    QCOMPARE(copy->remoteMode, QStringLiteral("main"));
    QCOMPARE(copy->uniqueId.toString(QUuid::WithoutBraces),
             QStringLiteral("11111111-1111-1111-1111-111111111111"));

    const JobOptions *mount = tasks.at(1);
    QCOMPARE(mount->description, QStringLiteral("mount media"));
    QCOMPARE(mount->operation, JobOptions::Mount);
    QCOMPARE(mount->source, QStringLiteral("remote:media"));
    QCOMPARE(mount->dest, QStringLiteral("R:"));
    QCOMPARE(mount->mountReadOnly, true);
    QCOMPARE(mount->mountCacheLevel, JobOptions::Writes);
    QCOMPARE(mount->mountAutoStart, true);
    QCOMPARE(mount->mountRcPort, QStringLiteral("5572"));
    QCOMPARE(mount->mountScript, QStringLiteral("/opt/scripts/after-mount.sh"));
    QCOMPARE(mount->uniqueId.toString(QUuid::WithoutBraces),
             QStringLiteral("22222222-2222-2222-2222-222222222222"));
  }

  // A saved mount task must not carry a remote-control login. The credentials
  // are generated per run and passed through the environment; anything found
  // here would mean they had leaked back into persisted state.
  void goldenFileHoldsNoCredentials() {
    // Prove the search works before trusting what it fails to find.
    QVERIFY(fileContains(taskFilePath(), QStringLiteral("nightly photos")));

    QVERIFY(!fileContains(taskFilePath(), QStringLiteral("rc-user")));
    QVERIFY(!fileContains(taskFilePath(), QStringLiteral("rc-pass")));
    QVERIFY(!fileContains(taskFilePath(), QStringLiteral("RCLONE_RC_")));
  }

  // Adding a task and writing it out has to leave the existing ones readable.
  void persistKeepsEarlierTasksReadable() {
    ListOfJobOptions *store = ListOfJobOptions::getInstance();

    auto *added = new JobOptions(false);
    added->description = "added by test";
    added->operation = JobOptions::Sync;
    added->source = "remote:a";
    added->dest = "remote:b";
    added->transfers = "2";
    QVERIFY(store->Persist(added));

    QCOMPARE(store->getTasks().size(), 3);

    // Re-read the file itself: the in-memory list would pass even if writing
    // produced something unreadable.
    QVERIFY(fileContains(taskFilePath(), QStringLiteral("nightly photos")));
    QVERIFY(fileContains(taskFilePath(), QStringLiteral("mount media")));
    QVERIFY(fileContains(taskFilePath(), QStringLiteral("added by test")));
  }

  void forgetRemovesOnlyThatTask() {
    ListOfJobOptions *store = ListOfJobOptions::getInstance();
    const int before = store->getTasks().size();

    JobOptions *victim = nullptr;
    for (JobOptions *jo : store->getTasks()) {
      if (jo->description == "added by test") {
        victim = jo;
      }
    }
    QVERIFY(victim != nullptr);
    QVERIFY(store->Forget(victim));
    QCOMPARE(store->getTasks().size(), before - 1);

    QVERIFY(!fileContains(taskFilePath(), QStringLiteral("added by test")));
    QVERIFY(fileContains(taskFilePath(), QStringLiteral("nightly photos")));
  }
};

QTEST_MAIN(TestTaskStore)
#include "test_task_store.moc"
