#include "job_log.h"
#include "utils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

// Same portable-mode redirection as test_task_store: the writer follows the
// configuration directory, so the marker has to go where this platform looks
// for it or the test would write into the real user configuration.
class TestJobLog : public QObject {
  Q_OBJECT

private:
  static QString configDir() {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return QCoreApplication::applicationDirPath();
#else
    return QDir(QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME")))
        .filePath("rclone-browser");
#endif
  }

  static QString iniPath() {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return QDir(configDir())
        .filePath(
            QFileInfo(QCoreApplication::applicationFilePath()).baseName() +
            ".ini");
#else
    return QDir(configDir()).filePath("rclone-browser.ini");
#endif
  }

  static QString readAll(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      return QString();
    }
    return QString::fromUtf8(file.readAll());
  }

  static void clearLogDir() {
    QDir dir(JobLogWriter::logDir());
    if (dir.exists()) {
      dir.removeRecursively();
    }
  }

private slots:
  void initTestCase() {
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    mScratch.reset(new QTemporaryDir);
    QVERIFY(mScratch->isValid());
    qputenv("XDG_CONFIG_HOME", mScratch->path().toLocal8Bit());
    QVERIFY(QDir().mkpath(configDir()));
#endif
    QFile ini(iniPath());
    QVERIFY(ini.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ini.close();
    QVERIFY(IsPortableMode());
  }

  void cleanup() { clearLogDir(); }

  void cleanupTestCase() {
    clearLogDir();
    QFile::remove(iniPath());
  }

  // A job that prints nothing should not litter the directory.
  void noFileUntilSomethingIsWritten() {
    JobLogWriter writer;
    writer.begin("copy", "abc123", {"rclone", "copy", "a", "b"});
    QVERIFY(!writer.isOpen());
    QVERIFY(writer.filePath().isEmpty());
  }

  void writesHeaderAndLines() {
    JobLogWriter writer;
    writer.begin("copy", "abc12345", {"rclone", "copy", "src", "dst"});
    writer.appendLine("first");
    writer.appendLine("second");
    writer.finish("finished");

    QVERIFY(!writer.filePath().isEmpty());
    const QString text = readAll(writer.filePath());

    QVERIFY(text.contains("# rclone-browser job log"));
    QVERIFY(text.contains("rclone copy src dst"));
    QVERIFY(text.contains("\nfirst\n"));
    QVERIFY(text.contains("\nsecond\n"));
    QVERIFY(text.contains("# finished:"));
    QVERIFY(text.contains("(finished)"));
  }

  // The header repeats the command line, which is exactly where a credential
  // would end up. The writer takes what it is given, so this pins the contract
  // that callers hand over already-redacted arguments.
  void headerRecordsWhateverItWasGiven() {
    JobLogWriter writer;
    writer.begin("mount", "id", RedactArgs({"rclone", "mount",
                                            "--rc-pass=hunter2", "r:", "R:"}));
    writer.appendLine("x");
    writer.finish("finished");

    const QString text = readAll(writer.filePath());
    QVERIFY(!text.contains("hunter2"));
    QVERIFY(text.contains("--rc-pass=***"));
  }

  void fileNameCarriesOperationAndId() {
    JobLogWriter writer;
    writer.begin("sync", "deadbeefcafe", {"rclone"});
    writer.appendLine("x");

    const QString name = QFileInfo(writer.filePath()).fileName();
    QVERIFY2(name.contains("-sync-"), qPrintable(name));
    // The id is shortened; the full one would make the name unwieldy.
    QVERIFY2(name.contains("deadbeef"), qPrintable(name));
    QVERIFY(name.endsWith(".log"));
  }

  // Two jobs starting in the same second must not write to one file.
  void concurrentJobsGetSeparateFiles() {
    JobLogWriter a;
    JobLogWriter b;
    a.begin("copy", "aaaaaaaa", {"rclone"});
    b.begin("copy", "aaaaaaaa", {"rclone"});
    a.appendLine("from a");
    b.appendLine("from b");
    a.finish("finished");
    b.finish("finished");

    QVERIFY(a.filePath() != b.filePath());
    QVERIFY(readAll(a.filePath()).contains("from a"));
    QVERIFY(!readAll(a.filePath()).contains("from b"));
  }

  void operationIsSanitisedForTheFileName_data() {
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");

    QTest::newRow("plain") << "copy" << "copy";
    QTest::newRow("path separators") << "a/b\\c" << "abc";
    QTest::newRow("traversal") << ".." << "";
    QTest::newRow("windows reserved") << "a:b*c?d" << "abcd";
    QTest::newRow("keeps dash and underscore") << "dry-run_1" << "dry-run_1";
    QTest::newRow("non ascii dropped") << "คัดลอก" << "";
  }

  void operationIsSanitisedForTheFileName() {
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QCOMPARE(JobLogWriter::sanitizeForFileName(input), expected);
  }

  // An operation that sanitises away must still produce a usable name.
  void emptyOperationFallsBackToJob() {
    JobLogWriter writer;
    writer.begin("..", "id", {"rclone"});
    writer.appendLine("x");
    QVERIFY2(QFileInfo(writer.filePath()).fileName().contains("-job"),
             qPrintable(writer.filePath()));
    // and it stayed inside the log directory
    QCOMPARE(QFileInfo(writer.filePath()).absolutePath(),
             QDir(JobLogWriter::logDir()).absolutePath());
  }

  void purgeRemovesOnlyOldLogs() {
    QDir dir(JobLogWriter::logDir());
    QVERIFY(dir.mkpath("."));

    const QString oldPath = dir.filePath("old.log");
    const QString newPath = dir.filePath("new.log");
    const QString other = dir.filePath("keep.txt");
    for (const QString &p : {oldPath, newPath, other}) {
      QFile f(p);
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write("x");
      f.close();
    }

    auto settings = GetSettings();
    settings->setValue("Settings/logRetentionDays", 7);
    settings->sync();

    // 30 days back, comfortably past the retention window.
    QFile f(oldPath);
    QVERIFY(f.open(QIODevice::ReadWrite));
    QVERIFY(f.setFileTime(QDateTime::currentDateTime().addDays(-30),
                          QFileDevice::FileModificationTime));
    f.close();

    QCOMPARE(JobLogWriter::purgeOldLogs(), 1);
    QVERIFY(!QFileInfo::exists(oldPath));
    QVERIFY(QFileInfo::exists(newPath));
    // Only *.log is ours to delete.
    QVERIFY(QFileInfo::exists(other));
  }

  void retentionOfZeroKeepsEverything() {
    QDir dir(JobLogWriter::logDir());
    QVERIFY(dir.mkpath("."));
    const QString oldPath = dir.filePath("ancient.log");
    QFile f(oldPath);
    QVERIFY(f.open(QIODevice::ReadWrite));
    f.write("x");
    QVERIFY(f.setFileTime(QDateTime::currentDateTime().addDays(-900),
                          QFileDevice::FileModificationTime));
    f.close();

    auto settings = GetSettings();
    settings->setValue("Settings/logRetentionDays", 0);
    settings->sync();

    QCOMPARE(JobLogWriter::purgeOldLogs(), 0);
    QVERIFY(QFileInfo::exists(oldPath));

    settings->setValue("Settings/logRetentionDays", 7);
    settings->sync();
  }

private:
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
  std::unique_ptr<QTemporaryDir> mScratch;
#endif
};

QTEST_MAIN(TestJobLog)
#include "test_job_log.moc"
