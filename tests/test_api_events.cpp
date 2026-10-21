#include "api_server.h"
#include "app_settings.h"
#include "database.h"
#include "job_options.h"
#include "job_registry.h"
#include "list_of_job_options.h"
#include "running_job.h"
#include "utils.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

#include <memory>

// The event stream and the jobs that were already running when it opened.
// A test of its own because the order is the whole point: here a job starts
// before the API is even listening, as it does when the window runs a task
// the moment it opens (2026-10-21: a 1.17 TB upload sent the page no news
// of itself for as long as it ran -- the page saw it move every 30 s, when
// it read everything again, and looked stuck in between). In
// test_api_server an earlier test has always opened a stream first.
class TestApiEvents : public QObject {
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
  std::unique_ptr<QTemporaryDir> mFolder;

private slots:
  void initTestCase() {
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    mScratch.reset(new QTemporaryDir);
    QVERIFY(mScratch->isValid());
    qputenv("XDG_CONFIG_HOME", mScratch->path().toLocal8Bit());
    QVERIFY(QDir().mkpath(appDir()));
#endif
    mFolder.reset(new QTemporaryDir);
    QVERIFY(mFolder->isValid());
    QFile ini(iniPath());
    QVERIFY(ini.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ini.close();
    QVERIFY2(IsPortableMode(), "portable mode did not take effect");
    Database::setPath(QDir(appDir()).filePath(QStringLiteral("events.db")));
    QFile::remove(Database::path());
    QVERIFY(Database::connection().isOpen());
    SetRclone(QString::fromLatin1(RB_FAKE_RCLONE));
  }

  void cleanupTestCase() {
    const QString path = Database::path();
    Database::closeForThread();
    QFile::remove(path);
    QFile::remove(iniPath());
  }

  void aJobAlreadyRunningIsToldOfToo() {
    // Running before anybody listens.
    qputenv("FAKE_RCLONE_SLEEP_MS", "8000");
    auto *task = new JobOptions(false);
    task->description = QStringLiteral("started first");
    task->operation = JobOptions::Copy;
    task->jobType = JobOptions::Upload;
    task->source = mFolder->path();
    task->dest = QStringLiteral("fake:somewhere");
    task->isFolder = true;
    task->uniqueId = QUuid::createUuid();
    ListOfJobOptions::getInstance()->Persist(task);
    RunningJob *job = StartTask(task, QStringLiteral("window"), QUuid::createUuid().toString());
    QVERIFY(job != nullptr);
    QTRY_VERIFY(job->isRunning());

    // Then the API, and a page.
    QString error;
    QVERIFY2(ApiServer::instance().start(QStringLiteral("127.0.0.1"), 0, &error), qPrintable(error));
    QTcpSocket stream;
    stream.connectToHost(QHostAddress::LocalHost, ApiServer::instance().port());
    QVERIFY(stream.waitForConnected(5000));
    stream.write("GET /api/v1/events HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer " +
                 ApiServer::token().toUtf8() + "\r\n\r\n");
    stream.flush();
    QByteArray heard;
    QTRY_VERIFY_WITH_TIMEOUT((heard += stream.readAll(), heard.contains("event: hello")), 5000);

    // rclone's figures arrive, as they do once a second.
    emit job->statsUpdated(job->stats());
    QTRY_VERIFY2_WITH_TIMEOUT((heard += stream.readAll(),
                               heard.contains("event: job") &&
                                   heard.contains(job->requestId().toUtf8())),
                              "a job running before the stream opened was never told of", 3000);

    job->stop();
    QTRY_VERIFY_WITH_TIMEOUT(!job->isRunning(), 10000);
    ApiServer::instance().stop();
    ListOfJobOptions::getInstance()->Forget(task);
  }
};

QTEST_GUILESS_MAIN(TestApiEvents)
#include "test_api_events.moc"
