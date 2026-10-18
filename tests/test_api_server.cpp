#include "api_auth.h"
#include "api_routes.h"
#include "rclone_config.h"
#include "app_core.h"
#include "api_server.h"
#include "database.h"
#include "job_options.h"
#include "job_queue.h"
#include "job_registry.h"
#include "list_of_job_options.h"
#include "remote_registry.h"
#include "run_history.h"
#include "scheduler_store.h"
#include "schedule.h"
#include "utils.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QHostAddress>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTcpServer>
#include <QUrl>
#include <QUuid>

#include <memory>

// The HTTP API, over a real socket on a real port.
//
// Written this way on purpose: the thing worth checking is not that the
// routing function returns 200, it is that something speaking HTTP from
// outside the process gets an answer -- and, more importantly, that it does
// not get one without a token.
//
// See docs/API.md sections 11 and 13.
class TestApiServer : public QObject {
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

  // Waits for one complete response, pumping events by hand.
  //
  // waitForReadyRead() on the client will not do: the server is in this same
  // thread, and waiting on the client socket never delivers the server's
  // newConnection -- so the request is never read and the wait times out
  // with nothing to show for it.
  QByteArray collect(QTcpSocket *socket, bool headersOnly = false) {
    QByteArray answer;
    QDeadlineTimer deadline(5000);

    while (!deadline.hasExpired()) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
      answer += socket->readAll();

      const int headerEnd = answer.indexOf("\r\n\r\n");
      if (headerEnd < 0) {
        continue;
      }
      if (headersOnly) {
        // A HEAD answer says how long the body would have been and then
        // sends none of it. Waiting for that body is waiting for nothing.
        break;
      }
      const int at = answer.indexOf("Content-Length: ");
      const int declared =
          at < 0 ? 0
                 : QByteArray(answer.mid(at + 16)).split('\r').first().toInt();
      if (answer.size() >= headerEnd + 4 + declared) {
        break;
      }
    }
    return answer;
  }

  // Sends exactly these bytes. For a request ask() cannot express -- here, a
  // header without the "Bearer " the scheme calls for.
  QByteArray askRaw(const QByteArray &request, bool headersOnly = false) {
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, ApiServer::instance().port());

    QDeadlineTimer deadline(5000);
    while (socket.state() != QAbstractSocket::ConnectedState &&
           !deadline.hasExpired()) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    if (socket.state() != QAbstractSocket::ConnectedState) {
      return QByteArray();
    }

    socket.write(request);
    socket.flush();
    return collect(&socket, headersOnly);
  }

  // One request, one answer. Returns the whole response including headers.
  QByteArray ask(const QByteArray &method, const QByteArray &target,
                 const QByteArray &token) {
    QByteArray request = method + " " + target + " HTTP/1.1\r\n";
    request += "Host: localhost\r\n";
    if (!token.isEmpty()) {
      request += "Authorization: Bearer " + token + "\r\n";
    }
    request += "\r\n";
    return askRaw(request, method == "HEAD");
  }

  QByteArray send(const QByteArray &method, const QByteArray &target,
                  const QByteArray &token, const QByteArray &body) {
    QByteArray request = method + " " + target + " HTTP/1.1\r\n";
    request += "Host: localhost\r\n";
    if (!token.isEmpty()) {
      request += "Authorization: Bearer " + token + "\r\n";
    }
    request += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    request += "\r\n";
    request += body;
    return askRaw(request);
  }

  // A saved task to command. Its source and destination are never reached:
  // nothing in this file lets the queue drive itself, so an entry sits where
  // it was put.
  JobOptions *makeTask(const QString &name) {
    auto *task = new JobOptions(false);
    task->description = name;
    task->operation = JobOptions::Copy;
    task->jobType = JobOptions::Upload;
    task->source = QStringLiteral("C:/nowhere");
    task->dest = QStringLiteral("nowhere:");
    task->isFolder = true;
    task->uniqueId = QUuid::createUuid();
    ListOfJobOptions::getInstance()->Persist(task);
    return task;
  }

  // Opens a connection and leaves it open. Everything else in this file is
  // one request and one answer; the event stream is neither.
  bool openConnection(QTcpSocket *socket) {
    socket->connectToHost(QHostAddress::LocalHost, ApiServer::instance().port());
    QDeadlineTimer deadline(5000);
    while (socket->state() != QAbstractSocket::ConnectedState &&
           !deadline.hasExpired()) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return socket->state() == QAbstractSocket::ConnectedState;
  }

  // Runs the event loop until `wanted` has arrived down the socket, adding
  // whatever turns up to `seen`. Returns false if it never comes.
  bool waitFor(QTcpSocket *socket, QByteArray *seen, const QByteArray &wanted,
               int ms = 5000) {
    QDeadlineTimer deadline(ms);
    while (!deadline.hasExpired()) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
      *seen += socket->readAll();
      if (seen->contains(wanted)) {
        return true;
      }
    }
    return false;
  }

  static int statusOf(const QByteArray &response) {
    const QList<QByteArray> parts = response.left(64).split(' ');
    return parts.size() > 1 ? parts.at(1).toInt() : 0;
  }

  static QJsonDocument bodyOf(const QByteArray &response) {
    const int at = response.indexOf("\r\n\r\n");
    return at < 0 ? QJsonDocument()
                  : QJsonDocument::fromJson(response.mid(at + 4));
  }

  static QByteArray taskBody(const QString &name, const QString &source,
                             const QString &dest) {
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("upload"));
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("source"), source);
    object.insert(QStringLiteral("dest"), dest);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
  }

  // A folder of this machine the API has been given, with one file in it.
  // Returns its resolved path.
  QString givenFolder(QTemporaryDir *dir) {
    QDir(dir->path()).mkpath(QStringLiteral("films"));
    QFile file(QDir(dir->path()).filePath(QStringLiteral("films/a.mkv")));
    if (file.open(QIODevice::WriteOnly)) {
      file.write("x");
    }
    qputenv("RB_LOCAL_ROOTS", dir->path().toUtf8());
    return QFileInfo(dir->path()).canonicalFilePath();
  }

  std::unique_ptr<QTemporaryDir> mScratch;
  QByteArray mToken;

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

    Database::setPath(QDir(appDir()).filePath(QStringLiteral("api.db")));
    QFile::remove(Database::path());
    QVERIFY(Database::connection().isOpen());

    // Port 0: the operating system picks a free one, so the test cannot fail
    // because something else on the machine happens to hold a fixed port.
    QString error;
    QVERIFY2(ApiServer::instance().start(QStringLiteral("127.0.0.1"), 0,
                                         &error),
             qPrintable(error));
    QVERIFY(ApiServer::instance().isListening());
    QVERIFY(ApiServer::instance().port() != 0);

    mToken = ApiServer::token().toUtf8();
    QVERIFY2(!mToken.isEmpty(), "starting the API left it with no token");
  }

  void cleanupTestCase() {
    // Everything these tests sent, looked up in the list the API's
    // documentation is made from. Anything not there is a route added to
    // the code and not to the list -- the documentation would not show it --
    // apart from the requests below, which are nonsense on purpose.
    static const QStringList nonsense = {
        QStringLiteral("GET /api/v1/nothing"),
    };
    QStringList missing;
    for (const QString &request : ApiRoutes::Unlisted()) {
      if (!nonsense.contains(request)) {
        missing << request;
      }
    }
    QVERIFY2(missing.isEmpty(),
             qPrintable(QStringLiteral("not in ApiRoutes: ") +
                        missing.join(QStringLiteral(", "))));

    JobQueue::instance().clear();
    JobQueue::instance().pause();
    ApiServer::instance().stop();
    const QString path = Database::path();
    Database::closeForThread();
    QFile::remove(path);
    QFile::remove(iniPath());
  }

  // The one that matters most. Everything else here could work perfectly and
  // this API would still be a way for anything on the machine to read every
  // remote path and start transfers.
  void nothingIsAnsweredWithoutAToken() {
    for (const char *target :
         {"/api/v1/system", "/api/v1/tasks", "/api/v1/jobs", "/api/v1/queue",
          "/api/v1/remotes", "/api/v1/schedules", "/api/v1/history"}) {
      const QByteArray reply = ask("GET", target, QByteArray());
      QCOMPARE(statusOf(reply), 401);
      QVERIFY2(!reply.contains("\"name\""), target);
    }
  }

  void awrongTokenIsNoBetterThanNone() {
    QCOMPARE(statusOf(ask("GET", "/api/v1/system", "not-the-token")), 401);
    QCOMPARE(statusOf(ask("GET", "/api/v1/system", mToken + "x")), 401);

    // The prefix has to be there too: a bare token in the header is not the
    // scheme this speaks.
    QCOMPARE(statusOf(askRaw("GET /api/v1/system HTTP/1.1\r\nAuthorization: " +
                             mToken + "\r\n\r\n")),
             401);
  }

  // The exception, and the reason for it: a client has to be able to tell
  // "the server is there and my token is wrong" from "there is no server".
  // It says nothing else.
  void pingAnswersWithoutATokenAndTellsNothing() {
    const QByteArray reply = ask("GET", "/api/v1/ping", QByteArray());
    QCOMPARE(statusOf(reply), 200);

    const QJsonObject object = bodyOf(reply).object();
    QCOMPARE(object.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(object.size(), 1);
  }

  void withTheTokenItAnswers() {
    const QByteArray reply = ask("GET", "/api/v1/system", mToken);
    QCOMPARE(statusOf(reply), 200);

    const QJsonObject object = bodyOf(reply).object();
    QVERIFY(object.contains(QStringLiteral("queued")));
    QVERIFY(object.contains(QStringLiteral("queueRunning")));
    QVERIFY(object.contains(QStringLiteral("coreStarted")));
  }

  void theListsAreListsEvenWhenEmpty() {
    for (const char *target :
         {"/api/v1/tasks", "/api/v1/jobs", "/api/v1/queue", "/api/v1/remotes",
          "/api/v1/schedules", "/api/v1/history"}) {
      const QByteArray reply = ask("GET", target, mToken);
      QCOMPARE(statusOf(reply), 200);
      QVERIFY2(bodyOf(reply).isArray(), target);
    }
  }

  void somethingThatIsNotThereIsFourOhFour() {
    QCOMPARE(statusOf(ask("GET", "/api/v1/nothing", mToken)), 404);
    QCOMPARE(statusOf(ask("GET", "/api/v1/tasks/{no-such-task}", mToken)), 404);
  }

  // ---- writing tasks ---------------------------------------------------------

  // A task made through the API is the one the window would have saved: it
  // is in the list, it has an id, and it reads back with every field.
  void aTaskCanBeMadeChangedAndTakenAway() {
    QVERIFY(useFakeRclone(1));
    QTemporaryDir given;
    const QString root = givenFolder(&given);
    const int before = ListOfJobOptions::getInstance()->getTasks().size();

    QJsonObject asked =
        QJsonDocument::fromJson(
            taskBody(QStringLiteral("made by the page"), root + "/films",
                     QStringLiteral("fake:backup")))
            .object();
    asked.insert(QStringLiteral("operation"), QStringLiteral("sync"));
    asked.insert(QStringLiteral("transfers"), 2);
    asked.insert(QStringLiteral("excluded"), QStringLiteral("*.tmp"));

    const QByteArray made =
        send("POST", "/api/v1/tasks", mToken,
             QJsonDocument(asked).toJson(QJsonDocument::Compact));
    QCOMPARE(statusOf(made), 201);
    const QJsonObject task = bodyOf(made).object();
    const QByteArray id = task.value(QStringLiteral("id")).toString().toUtf8();
    QVERIFY(!id.isEmpty());
    QCOMPARE(task.value(QStringLiteral("operation")).toString(),
             QStringLiteral("sync"));
    QCOMPARE(task.value(QStringLiteral("transfers")).toString(),
             QStringLiteral("2"));
    // Read off the disk, since nobody said.
    QCOMPARE(task.value(QStringLiteral("isFolder")).toBool(), true);
    QCOMPARE(ListOfJobOptions::getInstance()->getTasks().size(), before + 1);

    const QByteArray target = "/api/v1/tasks/" + QUrl::toPercentEncoding(id);
    QCOMPARE(bodyOf(ask("GET", target, mToken))
                 .object().value(QStringLiteral("excluded")).toString(),
             QStringLiteral("*.tmp"));

    // Changing says only what changes.
    const QByteArray changed =
        send("PUT", target, mToken, "{\"bandwidth\":\"1M\",\"name\":\"renamed\"}");
    QCOMPARE(statusOf(changed), 200);
    const QJsonObject after = bodyOf(changed).object();
    QCOMPARE(after.value(QStringLiteral("bandwidth")).toString(),
             QStringLiteral("1M"));
    QCOMPARE(after.value(QStringLiteral("name")).toString(),
             QStringLiteral("renamed"));
    QCOMPARE(after.value(QStringLiteral("operation")).toString(),
             QStringLiteral("sync"));
    QCOMPARE(ListOfJobOptions::getInstance()->getTasks().size(), before + 1);

    // A change that is wrong changes nothing.
    QCOMPARE(statusOf(send("PUT", target, mToken,
                           "{\"name\":\"half\",\"operation\":\"mount\"}")),
             400);
    QCOMPARE(bodyOf(ask("GET", target, mToken))
                 .object().value(QStringLiteral("name")).toString(),
             QStringLiteral("renamed"));
    // Nor may a task be edited down to nothing.
    QCOMPARE(statusOf(send("PUT", target, mToken, "{\"source\":\"\"}")), 400);

    QCOMPARE(statusOf(ask("DELETE", target, mToken)), 200);
    QCOMPARE(statusOf(ask("GET", target, mToken)), 404);
    QCOMPARE(statusOf(ask("DELETE", target, mToken)), 404);
    QCOMPARE(ListOfJobOptions::getInstance()->getTasks().size(), before);
    qunsetenv("RB_LOCAL_ROOTS");
  }

  // A task nobody named gets the name the window would have given it.
  void aTaskNobodyNamedIsNamed() {
    QVERIFY(useFakeRclone(1));
    QTemporaryDir given;
    const QString root = givenFolder(&given);
    const QByteArray made =
        send("POST", "/api/v1/tasks", mToken,
             taskBody(QString(), root + "/films", QStringLiteral("fake:b")));
    QCOMPARE(statusOf(made), 201);
    const QJsonObject task = bodyOf(made).object();
    QVERIFY(task.value(QStringLiteral("name")).toString().startsWith(
        QStringLiteral("_tmp_")));
    QCOMPARE(statusOf(ask("DELETE",
                          "/api/v1/tasks/" +
                              QUrl::toPercentEncoding(
                                  task.value(QStringLiteral("id")).toString()),
                          mToken)),
             200);
    qunsetenv("RB_LOCAL_ROOTS");
  }

  // A saved task is a transfer waiting to happen, so it is held to the line
  // a transfer started now is held to. Otherwise saving one and running it
  // would be the way round.
  void aTaskIsHeldToTheSameFoldersAsATransfer() {
    QVERIFY(useFakeRclone(1));
    QTemporaryDir given;
    QTemporaryDir other;
    const QString root = givenFolder(&given);
    const int before = ListOfJobOptions::getInstance()->getTasks().size();

    const QByteArray outside =
        send("POST", "/api/v1/tasks", mToken,
             taskBody(QStringLiteral("out"), other.path(),
                      QStringLiteral("fake:b")));
    QCOMPARE(statusOf(outside), 403);
    QCOMPARE(bodyOf(outside).object().value(QStringLiteral("code")).toString(),
             QStringLiteral("outside_roots"));

    // Nor by making one inside and moving it out.
    const QByteArray made =
        send("POST", "/api/v1/tasks", mToken,
             taskBody(QStringLiteral("in"), root + "/films",
                      QStringLiteral("fake:b")));
    QCOMPARE(statusOf(made), 201);
    const QByteArray target =
        "/api/v1/tasks/" +
        QUrl::toPercentEncoding(
            bodyOf(made).object().value(QStringLiteral("id")).toString());
    QJsonObject move;
    move.insert(QStringLiteral("source"), other.path());
    QCOMPARE(statusOf(send("PUT", target, mToken,
                           QJsonDocument(move).toJson(QJsonDocument::Compact))),
             403);
    QCOMPARE(bodyOf(ask("GET", target, mToken))
                 .object().value(QStringLiteral("source")).toString(),
             root + "/films");

    QCOMPARE(statusOf(ask("DELETE", target, mToken)), 200);
    QCOMPARE(ListOfJobOptions::getInstance()->getTasks().size(), before);
    qunsetenv("RB_LOCAL_ROOTS");
  }

  void aTaskThatCannotBeOneIsRefusedAndSaysWhy() {
    const int before = ListOfJobOptions::getInstance()->getTasks().size();

    // Nothing said at all.
    QCOMPARE(statusOf(ask("POST", "/api/v1/tasks", mToken)), 400);
    // An upload of nothing.
    const QByteArray empty =
        send("POST", "/api/v1/tasks", mToken,
             taskBody(QStringLiteral("x"), QString(), QStringLiteral("fake:b")));
    QCOMPARE(statusOf(empty), 400);
    QCOMPARE(bodyOf(empty).object().value(QStringLiteral("code")).toString(),
             QStringLiteral("incomplete"));
    // A choice that is not one.
    const QByteArray bad = send("POST", "/api/v1/tasks", mToken,
                                "{\"type\":\"upload\",\"compare\":\"vibes\"}");
    QCOMPARE(statusOf(bad), 400);
    QVERIFY(bodyOf(bad).object().value(QStringLiteral("error")).toString()
                .contains(QStringLiteral("compare")));

    QCOMPARE(statusOf(send("PUT", "/api/v1/tasks/{no-such-task}", mToken, "{}")),
             404);
    QCOMPARE(ListOfJobOptions::getInstance()->getTasks().size(), before);

    // And none of it without a secret.
    QCOMPARE(statusOf(send("POST", "/api/v1/tasks", QByteArray(), "{}")), 401);
    QCOMPARE(statusOf(send("PUT", "/api/v1/tasks/x", QByteArray(), "{}")), 401);
    QCOMPARE(statusOf(ask("DELETE", "/api/v1/tasks/x", QByteArray())), 401);
  }

  // A path field is somewhere anything can be typed. What is saved has to
  // be about things that are there.
  void aTaskIsNotSavedWithEndsThatAreNotThere() {
    QVERIFY(useFakeRclone(1));
    QTemporaryDir given;
    const QString root = givenFolder(&given);
    const int before = ListOfJobOptions::getInstance()->getTasks().size();

    for (const char *dest : {"nonsense:", "no colon at all", ":x", "fa ke:x"}) {
      const QByteArray reply =
          send("POST", "/api/v1/tasks", mToken,
               taskBody(QStringLiteral("x"), root + "/films",
                        QString::fromLatin1(dest)));
      QVERIFY2(statusOf(reply) == 400, dest);
      QCOMPARE(bodyOf(reply).object().value(QStringLiteral("code")).toString(),
               QStringLiteral("no_such_remote"));
    }

    const QByteArray missing =
        send("POST", "/api/v1/tasks", mToken,
             taskBody(QStringLiteral("x"), root + "/films/no-such-file",
                      QStringLiteral("fake:b")));
    QCOMPARE(statusOf(missing), 400);
    QCOMPARE(bodyOf(missing).object().value(QStringLiteral("code")).toString(),
             QStringLiteral("no_such_path"));
    QCOMPARE(ListOfJobOptions::getInstance()->getTasks().size(), before);

    // A task whose remote has since gone can still be renamed -- the end
    // that was not touched is not judged again -- but not pointed at
    // another remote that is not there.
    JobOptions *old = makeTask(QStringLiteral("older than its remote"));
    old->source = root + "/films";
    const QByteArray target =
        "/api/v1/tasks/" + QUrl::toPercentEncoding(old->uniqueId.toString());
    QCOMPARE(statusOf(send("PUT", target, mToken, "{\"name\":\"renamed\"}")),
             200);
    QCOMPARE(statusOf(send("PUT", target, mToken, "{\"dest\":\"gone:too\"}")),
             400);
    QCOMPARE(statusOf(send("PUT", target, mToken, "{\"dest\":\"fake:b\"}")),
             200);
    QCOMPARE(statusOf(ask("DELETE", target, mToken)), 200);

    // What a form asks while a path is being typed.
    const QByteArray there =
        ask("GET",
            QByteArray("/api/v1/local/stat?path=") +
                QUrl::toPercentEncoding(root + "/films"),
            mToken);
    QCOMPARE(statusOf(there), 200);
    QCOMPARE(bodyOf(there).object().value(QStringLiteral("exists")).toBool(),
             true);
    QCOMPARE(bodyOf(there).object().value(QStringLiteral("isDir")).toBool(),
             true);
    QCOMPARE(bodyOf(ask("GET",
                        QByteArray("/api/v1/local/stat?path=") +
                            QUrl::toPercentEncoding(root + "/films/nope"),
                        mToken))
                 .object().value(QStringLiteral("exists")).toBool(),
             false);
    QTemporaryDir other;
    QCOMPARE(statusOf(ask("GET",
                          QByteArray("/api/v1/local/stat?path=") +
                              QUrl::toPercentEncoding(other.path()),
                          mToken)),
             403);
    qunsetenv("RB_LOCAL_ROOTS");
  }

  // A task's own flags are a command line, so not every flag may be in it.
  void aTaskMayNotCarryAFlagThatRunsAProgram() {
    QVERIFY(useFakeRclone(1));
    QTemporaryDir given;
    const QString root = givenFolder(&given);
    const int before = ListOfJobOptions::getInstance()->getTasks().size();

    const auto withExtra = [&](const QString &extra) {
      QJsonObject asked =
          QJsonDocument::fromJson(taskBody(QStringLiteral("x"), root + "/films",
                                           QStringLiteral("fake:b")))
              .object();
      asked.insert(QStringLiteral("extra"), extra);
      return QJsonDocument(asked).toJson(QJsonDocument::Compact);
    };

    for (const char *extra :
         {"--config /tmp/x.conf", "--password-command \"sh -c id\"",
          "--rc-addr=:5572", "--fast-list\n--log-file /tmp/x",
          // and, because this API is held to a folder here:
          "--copy-links", "--files-from /etc/passwd"}) {
      const QByteArray reply = send("POST", "/api/v1/tasks", mToken,
                                    withExtra(QString::fromLatin1(extra)));
      QVERIFY2(statusOf(reply) == 400, extra);
      QCOMPARE(bodyOf(reply).object().value(QStringLiteral("code")).toString(),
               QStringLiteral("bad_flag"));
      // Nor is its command shown, as though it could be run.
      QVERIFY2(statusOf(send("POST", "/api/v1/tasks/preview", mToken,
                             withExtra(QString::fromLatin1(extra)))) == 400,
               extra);
    }
    QCOMPARE(ListOfJobOptions::getInstance()->getTasks().size(), before);

    // Nor by making a task without one and adding it after.
    const QByteArray made = send("POST", "/api/v1/tasks", mToken,
                                 withExtra(QStringLiteral("--fast-list")));
    QCOMPARE(statusOf(made), 201);
    const QByteArray target =
        "/api/v1/tasks/" +
        QUrl::toPercentEncoding(
            bodyOf(made).object().value(QStringLiteral("id")).toString());
    QCOMPARE(statusOf(send("PUT", target, mToken,
                           "{\"extra\":\"--config /tmp/x.conf\"}")),
             400);
    QCOMPARE(bodyOf(ask("GET", target, mToken))
                 .object().value(QStringLiteral("extra")).toString(),
             QStringLiteral("--fast-list"));
    QCOMPARE(statusOf(ask("DELETE", target, mToken)), 200);

    // What a form chooses from. The stand-in rclone has no list to give, and
    // that is said rather than passed off as "there are no flags".
    const QByteArray list = ask("GET", "/api/v1/rclone/flags", mToken);
    QCOMPARE(statusOf(list), 200);
    QVERIFY(bodyOf(list).object().value(QStringLiteral("flags")).isArray());
    QCOMPARE(bodyOf(list).object().value(QStringLiteral("loaded")).toBool(),
             false);
    QCOMPARE(statusOf(ask("GET", "/api/v1/rclone/flags", QByteArray())), 401);
    qunsetenv("RB_LOCAL_ROOTS");
  }

  // ---- writing schedules -----------------------------------------------------

  // A task put on a schedule, changed, switched and taken off again.
  void aTaskCanBePutOnAScheduleAndTakenOff() {
    JobOptions *task = makeTask(QStringLiteral("to be scheduled"));
    const QString taskId = task->uniqueId.toString();
    const int before = SchedulerStore::instance().count();

    QJsonObject asked;
    asked.insert(QStringLiteral("taskId"), taskId);
    asked.insert(QStringLiteral("days"),
                 QJsonArray{QStringLiteral("mon"), QStringLiteral("fri")});
    asked.insert(QStringLiteral("time"), QStringLiteral("06:30"));
    asked.insert(QStringLiteral("runAs"), QStringLiteral("queue"));
    asked.insert(QStringLiteral("active"), true);

    const QByteArray made =
        send("POST", "/api/v1/schedules", mToken,
             QJsonDocument(asked).toJson(QJsonDocument::Compact));
    QCOMPARE(statusOf(made), 201);
    const QJsonObject schedule = bodyOf(made).object();
    const QString id = schedule.value(QStringLiteral("id")).toString();
    QVERIFY(!id.isEmpty());
    // Named after its task when nobody named it.
    QCOMPARE(schedule.value(QStringLiteral("name")).toString(),
             QStringLiteral("to be scheduled"));
    QCOMPARE(schedule.value(QStringLiteral("taskName")).toString(),
             QStringLiteral("to be scheduled"));
    QCOMPARE(schedule.value(QStringLiteral("days")).toArray().size(), 2);
    QVERIFY(!schedule.value(QStringLiteral("nextRun")).toString().isEmpty());
    QCOMPARE(SchedulerStore::instance().count(), before + 1);

    // It is the store's, which is what the clock reads.
    const Schedule *stored = SchedulerStore::instance().find(id);
    QVERIFY(stored != nullptr);
    QCOMPARE(stored->executionMode, 1);
    QVERIFY(stored->monday && stored->friday && !stored->tuesday);
    QVERIFY(stored->active);

    const QByteArray target =
        "/api/v1/schedules/" + QUrl::toPercentEncoding(id);

    // Changing says only what changes.
    const QByteArray changed =
        send("PUT", target, mToken, "{\"time\":\"18:05\",\"name\":\"evening\"}");
    QCOMPARE(statusOf(changed), 200);
    QCOMPARE(bodyOf(changed).object().value(QStringLiteral("time")).toString(),
             QStringLiteral("18:05"));
    QCOMPARE(bodyOf(changed).object().value(QStringLiteral("runAs")).toString(),
             QStringLiteral("queue"));
    QCOMPARE(SchedulerStore::instance().find(id)->name,
             QStringLiteral("evening"));

    // A change that is wrong changes nothing.
    QCOMPARE(statusOf(send("PUT", target, mToken,
                           "{\"name\":\"half\",\"time\":\"25:00\"}")),
             400);
    QCOMPARE(statusOf(send("PUT", target, mToken, "{\"days\":[]}")), 400);
    QCOMPARE(SchedulerStore::instance().find(id)->name,
             QStringLiteral("evening"));

    // Its own switch.
    QCOMPARE(statusOf(ask("POST", target + "/disable", mToken)), 200);
    QVERIFY(!SchedulerStore::instance().find(id)->active);
    QCOMPARE(statusOf(ask("POST", target + "/enable", mToken)), 200);
    QVERIFY(SchedulerStore::instance().find(id)->active);

    // The task cannot be taken from under it.
    const QByteArray taskTarget =
        "/api/v1/tasks/" + QUrl::toPercentEncoding(taskId);
    const QByteArray held = ask("DELETE", taskTarget, mToken);
    QCOMPARE(statusOf(held), 409);
    QCOMPARE(bodyOf(held).object().value(QStringLiteral("code")).toString(),
             QStringLiteral("scheduled"));

    QCOMPARE(statusOf(ask("DELETE", target, mToken)), 200);
    QCOMPARE(statusOf(ask("DELETE", target, mToken)), 404);
    QCOMPARE(SchedulerStore::instance().count(), before);
    // And now it can.
    QCOMPARE(statusOf(ask("DELETE", taskTarget, mToken)), 200);
  }

  // A schedule's run taken out of the queue before it ran is recorded on the
  // schedule, wherever it was taken out from -- here, through the API.
  void aScheduleHearsItsRunWasTakenOutOfTheQueue() {
    JobQueue::instance().pause();
    JobOptions *task = makeTask(QStringLiteral("queued by a schedule"));
    QJsonObject asked;
    asked.insert(QStringLiteral("taskId"), task->uniqueId.toString());
    asked.insert(QStringLiteral("runAs"), QStringLiteral("queue"));
    asked.insert(QStringLiteral("active"), false);
    const QString id =
        bodyOf(send("POST", "/api/v1/schedules", mToken,
                    QJsonDocument(asked).toJson(QJsonDocument::Compact)))
            .object().value(QStringLiteral("id")).toString();
    const QByteArray target = "/api/v1/schedules/" + QUrl::toPercentEncoding(id);

    const QByteArray fired = ask("POST", target + "/run", mToken);
    QCOMPARE(statusOf(fired), 202);
    const QString requestId =
        bodyOf(fired).object().value(QStringLiteral("requestId")).toString();
    QCOMPARE(SchedulerStore::instance().find(id)->requestId, requestId);
    // And said, so the queue's entry can be told for this schedule's.
    bool said = false;
    for (const QJsonValue &value : bodyOf(ask("GET", "/api/v1/schedules", mToken)).array()) {
      said = said || value.toObject().value(QStringLiteral("requestId")).toString() == requestId;
    }
    QVERIFY(said);

    QCOMPARE(statusOf(ask("DELETE",
                          "/api/v1/queue/" + QUrl::toPercentEncoding(requestId),
                          mToken)),
             204);
    QCOMPARE(SchedulerStore::instance().find(id)->lastStatus,
             QStringLiteral("removed from the queue"));

    QCOMPARE(statusOf(ask("DELETE", target, mToken)), 200);
    ListOfJobOptions::getInstance()->Forget(task);
  }

  // How a schedule's run went is written down by the core, with no window to
  // do it: started, held, and how it ended.
  void aScheduleRecordsHowItsRunWent() {
    QVERIFY(useFakeRclone(1));
    qputenv("FAKE_RCLONE_SLEEP_MS", "1500");
    JobOptions *task = makeTask(QStringLiteral("run by a schedule"));
    QJsonObject asked;
    asked.insert(QStringLiteral("taskId"), task->uniqueId.toString());
    asked.insert(QStringLiteral("runAs"), QStringLiteral("now"));
    asked.insert(QStringLiteral("active"), false);
    const QString id =
        bodyOf(send("POST", "/api/v1/schedules", mToken,
                    QJsonDocument(asked).toJson(QJsonDocument::Compact)))
            .object().value(QStringLiteral("id")).toString();
    const QByteArray target = "/api/v1/schedules/" + QUrl::toPercentEncoding(id);
    const auto stored = [&id]() { return *SchedulerStore::instance().find(id); };

    QCOMPARE(statusOf(ask("POST", target + "/run", mToken)), 202);
    QCOMPARE(stored().lastStatus, QStringLiteral("running"));
    QVERIFY(stored().lastRun.contains(QString::number(QDate::currentDate().year())));
    QVERIFY(stored().lastFinished.isEmpty());

    // Asked again while it runs: held, and the reason written down.
    QCOMPARE(statusOf(ask("POST", target + "/run", mToken)), 409);
    QCOMPARE(stored().lastStatus, QStringLiteral("its last run has not finished"));

    // And when it ends, how.
    QTRY_COMPARE_WITH_TIMEOUT(stored().lastStatus, QStringLiteral("finished"), 10000);
    QVERIFY(!stored().lastFinished.isEmpty());

    // The page reads the same.
    for (const QJsonValue &value : bodyOf(ask("GET", "/api/v1/schedules", mToken)).array()) {
      if (value.toObject().value(QStringLiteral("id")).toString() == id) {
        QCOMPARE(value.toObject().value(QStringLiteral("lastStatus")).toString(),
                 QStringLiteral("finished"));
      }
    }

    qputenv("FAKE_RCLONE_SLEEP_MS", "0");
    QCOMPARE(statusOf(ask("DELETE", target, mToken)), 200);
    ListOfJobOptions::getInstance()->Forget(task);
  }

  // After a restart a schedule is told what is true of its run: still in
  // the queue if its entry is, "unknown" if it said running.
  void aScheduleIsToldTheTruthAboutItsRunAfterAStart() {
    JobQueue::instance().pause();
    JobOptions *task = makeTask(QStringLiteral("across a restart"));

    Schedule waiting;
    waiting.id = QUuid::createUuid().toString();
    waiting.name = QStringLiteral("still waiting");
    waiting.taskId = task->uniqueId.toString();
    waiting.requestId = JobQueue::instance().enqueue(waiting.taskId);
    waiting.lastStatus = QStringLiteral("in the queue");
    SchedulerStore::instance().add(waiting);

    Schedule gone = waiting;
    gone.id = QUuid::createUuid().toString();
    gone.name = QStringLiteral("was running");
    gone.requestId = QUuid::createUuid().toString();
    gone.lastStatus = QStringLiteral("running");
    SchedulerStore::instance().add(gone);

    Schedule lost = gone;
    lost.id = QUuid::createUuid().toString();
    lost.name = QStringLiteral("queued, and the entry is gone");
    lost.lastStatus = QStringLiteral("in the queue");
    SchedulerStore::instance().add(lost);

    AppCore::instance().reconcileSchedules();

    QCOMPARE(SchedulerStore::instance().find(waiting.id)->lastStatus,
             QStringLiteral("in the queue"));
    QCOMPARE(SchedulerStore::instance().find(gone.id)->lastStatus,
             QStringLiteral("unknown"));
    QCOMPARE(SchedulerStore::instance().find(lost.id)->lastStatus,
             QStringLiteral("unknown"));

    for (const QString &id : {waiting.id, gone.id, lost.id}) {
      SchedulerStore::instance().remove(id);
    }
    JobQueue::instance().remove(waiting.requestId);
    ListOfJobOptions::getInstance()->Forget(task);
  }

  void aScheduleThatCouldNotRunIsRefused() {
    JobOptions *task = makeTask(QStringLiteral("for refusals"));
    const QString taskId = task->uniqueId.toString();
    const int before = SchedulerStore::instance().count();

    // About no task, or one that is not there.
    QCOMPARE(statusOf(send("POST", "/api/v1/schedules", mToken, "{}")), 400);
    const QByteArray nothing =
        send("POST", "/api/v1/schedules", mToken, "{\"taskId\":\"{nope}\"}");
    QCOMPARE(statusOf(nothing), 400);
    QCOMPARE(bodyOf(nothing).object().value(QStringLiteral("code")).toString(),
             QStringLiteral("no_such_task"));

    // A cron expression nobody could read.
    QJsonObject cron;
    cron.insert(QStringLiteral("taskId"), taskId);
    cron.insert(QStringLiteral("mode"), QStringLiteral("cron"));
    cron.insert(QStringLiteral("cron"), QStringLiteral("whenever"));
    const QByteArray bad =
        send("POST", "/api/v1/schedules", mToken,
             QJsonDocument(cron).toJson(QJsonDocument::Compact));
    QCOMPARE(statusOf(bad), 400);
    QVERIFY(bodyOf(bad).object().value(QStringLiteral("error")).toString()
                .contains(QStringLiteral("cron")));

    // A mount is not something to start every morning.
    JobOptions *mount = makeTask(QStringLiteral("a mount"));
    mount->operation = JobOptions::Mount;
    QJsonObject onMount;
    onMount.insert(QStringLiteral("taskId"), mount->uniqueId.toString());
    QCOMPARE(statusOf(send("POST", "/api/v1/schedules", mToken,
                           QJsonDocument(onMount).toJson(QJsonDocument::Compact))),
             400);

    QCOMPARE(statusOf(send("PUT", "/api/v1/schedules/{nope}", mToken, "{}")),
             404);
    QCOMPARE(statusOf(ask("POST", "/api/v1/schedules/{nope}/enable", mToken)),
             404);
    QCOMPARE(SchedulerStore::instance().count(), before);

    // And none of it without a secret.
    QCOMPARE(statusOf(send("POST", "/api/v1/schedules", QByteArray(), "{}")),
             401);
    QCOMPARE(statusOf(ask("DELETE", "/api/v1/schedules/x", QByteArray())), 401);
    QCOMPARE(statusOf(ask("POST", "/api/v1/scheduler/pause", QByteArray())),
             401);

    ListOfJobOptions::getInstance()->Forget(task);
    ListOfJobOptions::getInstance()->Forget(mount);
  }

  // The whole scheduler has a switch too, and it is the store's.
  void theSchedulerCanBeSwitchedOffAndOn() {
    const bool was = SchedulerStore::instance().isRunning();

    QCOMPARE(statusOf(ask("POST", "/api/v1/scheduler/pause", mToken)), 200);
    QVERIFY(!SchedulerStore::instance().isRunning());
    QCOMPARE(bodyOf(ask("GET", "/api/v1/system", mToken))
                 .object().value(QStringLiteral("schedulerRunning")).toBool(),
             false);

    const QByteArray on = ask("POST", "/api/v1/scheduler/start", mToken);
    QCOMPARE(statusOf(on), 200);
    QCOMPARE(bodyOf(on).object().value(QStringLiteral("running")).toBool(), true);
    QVERIFY(SchedulerStore::instance().isRunning());

    if (!was) {
      SchedulerStore::instance().pause();
    }
  }

  // What the window shows on its "rclone cmd" tab: the command, and nothing
  // saved by asking.
  void theCommandATaskWouldRunCanBeSeenFirst() {
    const int before = ListOfJobOptions::getInstance()->getTasks().size();

    QJsonObject asked =
        QJsonDocument::fromJson(taskBody(QString(), QStringLiteral("C:/films"),
                                         QStringLiteral("fake:backup")))
            .object();
    asked.insert(QStringLiteral("operation"), QStringLiteral("sync"));
    asked.insert(QStringLiteral("bandwidth"), QStringLiteral("2M"));
    asked.insert(QStringLiteral("dryRun"), true);

    const QByteArray reply =
        send("POST", "/api/v1/tasks/preview", mToken,
             QJsonDocument(asked).toJson(QJsonDocument::Compact));
    QCOMPARE(statusOf(reply), 200);
    const QString command =
        bodyOf(reply).object().value(QStringLiteral("command")).toString();
    QVERIFY2(command.contains(QStringLiteral("sync")), qPrintable(command));
    QVERIFY2(command.contains(QStringLiteral("fake:backup")),
             qPrintable(command));
    QVERIFY2(command.contains(QStringLiteral("2M")), qPrintable(command));
    QVERIFY2(command.contains(QStringLiteral("--dry-run")),
             qPrintable(command));
    QCOMPARE(ListOfJobOptions::getInstance()->getTasks().size(), before);

    const QByteArray defaults =
        ask("GET", "/api/v1/tasks/defaults?type=download", mToken);
    QCOMPARE(statusOf(defaults), 200);
    QCOMPARE(bodyOf(defaults).object().value(QStringLiteral("type")).toString(),
             QStringLiteral("download"));
  }

  // Commands are behind the same door as everything else. Worth its own
  // test rather than trusting the shared check: this is the half where an
  // unauthenticated caller could start transfers, not merely read about
  // them.
  void commandsNeedTheTokenToo() {
    QCOMPARE(statusOf(ask("POST", "/api/v1/queue/start", QByteArray())), 401);
    QCOMPARE(statusOf(ask("POST", "/api/v1/tasks/x/run", QByteArray())), 401);
    QCOMPARE(statusOf(ask("DELETE", "/api/v1/queue/x", QByteArray())), 401);

    // And it was refused, not done: the queue must be exactly as it was.
    QVERIFY2(!JobQueue::instance().isRunning(),
             "an unauthenticated request started the queue");
  }

  void theQueueCanBeStartedAndPaused() {
    QByteArray reply = ask("POST", "/api/v1/queue/start", mToken);
    QCOMPARE(statusOf(reply), 200);
    QCOMPARE(bodyOf(reply).object().value(QStringLiteral("running")).toBool(),
             true);
    QVERIFY(JobQueue::instance().isRunning());

    reply = ask("POST", "/api/v1/queue/pause", mToken);
    QCOMPARE(statusOf(reply), 200);
    QCOMPARE(bodyOf(reply).object().value(QStringLiteral("running")).toBool(),
             false);
    QVERIFY(!JobQueue::instance().isRunning());
  }

  // The one that goes all the way through: a task is queued over HTTP, shows
  // up in what the API reports, and can be taken back out again.
  void aTaskCanBeQueuedAndTakenBackOut() {
    JobOptions *task = makeTask(QStringLiteral("queued over http"));
    const QByteArray id = task->uniqueId.toString().toUtf8();

    const QByteArray reply =
        send("POST", "/api/v1/tasks/" + id + "/enqueue", mToken, QByteArray());
    QCOMPARE(statusOf(reply), 202);

    const QString requestId =
        bodyOf(reply).object().value(QStringLiteral("requestId")).toString();
    QVERIFY2(!requestId.isEmpty(), "queuing gave back no request id");
    QCOMPARE(JobQueue::instance().count(), 1);

    // Reported the same way to whoever asks next, rather than only to the
    // caller that put it there.
    const QJsonArray queue = bodyOf(ask("GET", "/api/v1/queue", mToken)).array();
    QCOMPARE(queue.size(), 1);
    QCOMPARE(queue.at(0).toObject().value(QStringLiteral("requestId")).toString(),
             requestId);

    QCOMPARE(statusOf(ask("DELETE", "/api/v1/queue/" + requestId.toUtf8(),
                          mToken)),
             204);
    QCOMPARE(JobQueue::instance().count(), 0);

    ListOfJobOptions::getInstance()->Forget(task);
  }

  // The line can be put in another order and emptied, as from the window.
  void theQueueCanBeReorderedAndCleared() {
    JobOptions *a = makeTask(QStringLiteral("first in"));
    JobOptions *b = makeTask(QStringLiteral("second in"));
    JobOptions *c = makeTask(QStringLiteral("third in"));
    QStringList ids;
    for (JobOptions *task : {a, b, c}) {
      const QByteArray reply = send(
          "POST", "/api/v1/tasks/" + task->uniqueId.toString().toUtf8() + "/enqueue",
          mToken, QByteArray());
      QCOMPARE(statusOf(reply), 202);
      ids << bodyOf(reply).object().value(QStringLiteral("requestId")).toString();
    }
    const auto order = []() {
      QStringList now;
      for (const QueueEntry &entry : JobQueue::instance().entries()) {
        now << entry.requestId;
      }
      return now;
    };
    const auto move = [this](const QString &id, const QByteArray &to) {
      return statusOf(send("POST",
                           "/api/v1/queue/" + QUrl::toPercentEncoding(id) + "/move",
                           mToken, "{\"to\":" + to + "}"));
    };

    QCOMPARE(move(ids.at(2), "\"top\""), 200);
    QCOMPARE(order(), (QStringList{ids.at(2), ids.at(0), ids.at(1)}));
    QCOMPARE(move(ids.at(2), "\"down\""), 200);
    QCOMPARE(order(), (QStringList{ids.at(0), ids.at(2), ids.at(1)}));
    QCOMPARE(move(ids.at(0), "2"), 200);
    QCOMPARE(order(), (QStringList{ids.at(2), ids.at(1), ids.at(0)}));
    // Already at the top: done, not an error.
    QCOMPARE(move(ids.at(2), "\"up\""), 409);
    QCOMPARE(move(ids.at(2), "\"top\""), 200);
    QCOMPARE(move(ids.at(2), "7"), 409);
    QCOMPARE(move(ids.at(2), "\"sideways\""), 400);
    QCOMPARE(move(QStringLiteral("{nobody}"), "\"top\""), 404);
    QCOMPARE(order(), (QStringList{ids.at(2), ids.at(1), ids.at(0)}));

    // Emptied, and the tasks are still tasks.
    const QByteArray cleared = ask("POST", "/api/v1/queue/clear", mToken);
    QCOMPARE(statusOf(cleared), 200);
    QCOMPARE(bodyOf(cleared).object().value(QStringLiteral("removed")).toInt(), 3);
    QCOMPARE(JobQueue::instance().count(), 0);
    QVERIFY(ListOfJobOptions::getInstance()->find(a->uniqueId) != nullptr);

    QCOMPARE(statusOf(ask("POST", "/api/v1/queue/clear", QByteArray())), 401);

    for (JobOptions *task : {a, b, c}) {
      ListOfJobOptions::getInstance()->Forget(task);
    }
  }

  // ---- keys -------------------------------------------------------------------

  // A key for another program may do as much as it was given and no more,
  // and the history says which key started a run.
  void aKeyMayDoAsMuchAsItWasGiven() {
    QVERIFY(useFakeRclone(1));
    qputenv("FAKE_RCLONE_SLEEP_MS", "0");
    ApiAuth::Keys::instance().forgetAll();

    const auto makeKey = [this](const char *name, const char *scope) {
      const QByteArray reply = send(
          "POST", "/api/v1/keys", mToken,
          QByteArray("{\"name\":\"") + name + "\",\"scope\":\"" + scope + "\"}");
      return bodyOf(reply).object();
    };
    const QJsonObject readOne = makeKey("dashboard", "read");
    const QJsonObject controlOne = makeKey("home assistant", "control");
    const QJsonObject writeOne = makeKey("sonarr", "write");
    const QByteArray reader = readOne.value(QStringLiteral("key")).toString().toUtf8();
    const QByteArray controller = controlOne.value(QStringLiteral("key")).toString().toUtf8();
    const QByteArray writer = writeOne.value(QStringLiteral("key")).toString().toUtf8();
    QVERIFY(reader.startsWith("rbk_"));

    JobOptions *task = makeTask(QStringLiteral("for keys"));
    const QByteArray taskPath =
        "/api/v1/tasks/" + QUrl::toPercentEncoding(task->uniqueId.toString());

    // read: looks, and that is all.
    QCOMPARE(statusOf(ask("GET", "/api/v1/tasks", reader)), 200);
    const QByteArray refused = ask("POST", taskPath + "/enqueue", reader);
    QCOMPARE(statusOf(refused), 403);
    QCOMPARE(bodyOf(refused).object().value(QStringLiteral("needs")).toString(),
             QStringLiteral("control"));
    QCOMPARE(statusOf(ask("GET", "/api/v1/keys", reader)), 403);
    // What is not described is not handed to a key.
    QCOMPARE(statusOf(ask("GET", "/api/v1/nothing", reader)), 403);

    // control: sets things going and stops them, changes nothing.
    const QByteArray queued = ask("POST", taskPath + "/enqueue", controller);
    QCOMPARE(statusOf(queued), 202);
    QCOMPARE(statusOf(ask("DELETE",
                          "/api/v1/queue/" +
                              bodyOf(queued).object().value(QStringLiteral("requestId"))
                                  .toString().toUtf8(),
                          controller)),
             204);
    const QByteArray run = ask("POST", taskPath + "/run", controller);
    QCOMPARE(statusOf(run), 202);
    RunningJob *job = JobRegistry::instance().find(
        bodyOf(run).object().value(QStringLiteral("requestId")).toString());
    QVERIFY(job != nullptr);
    QCOMPARE(job->transferMode(), QStringLiteral("api:home assistant"));
    job->stop();
    QCOMPARE(statusOf(send("PUT", taskPath, controller, "{\"name\":\"x\"}")), 403);

    // write: changes tasks and schedules, but is not the owner.
    QCOMPARE(statusOf(send("PUT", taskPath, writer, "{\"name\":\"renamed by a key\"}")),
             200);
    QCOMPARE(statusOf(ask("POST", "/api/v1/shutdown", writer)), 403);
    QCOMPARE(statusOf(send("POST", "/api/v1/keys", writer,
                           "{\"name\":\"another\",\"scope\":\"admin\"}")),
             403);
    QCOMPARE(statusOf(send("POST", "/api/v1/password", writer,
                           "{\"current\":\"\",\"new\":\"12345678\"}")),
             403);

    // The list says who they are and when they were used, never the key.
    const QByteArray listed = ask("GET", "/api/v1/keys", mToken);
    QCOMPARE(statusOf(listed), 200);
    QCOMPARE(bodyOf(listed).array().size(), 3);
    QVERIFY(!listed.contains(reader.mid(13)));
    for (const QJsonValue &value : bodyOf(listed).array()) {
      QVERIFY(!value.toObject().contains(QStringLiteral("key")));
      if (value.toObject().value(QStringLiteral("name")).toString() ==
          QLatin1String("dashboard")) {
        QVERIFY(!value.toObject().value(QStringLiteral("lastUsed")).toString().isEmpty());
      }
    }

    // A name is one key; a level is one of four.
    QCOMPARE(statusOf(send("POST", "/api/v1/keys", mToken,
                           "{\"name\":\"Sonarr\",\"scope\":\"read\"}")),
             400);
    QCOMPARE(statusOf(send("POST", "/api/v1/keys", mToken,
                           "{\"name\":\"x\",\"scope\":\"root\"}")),
             400);

    // Taken back, it opens nothing -- from the very next request.
    QCOMPARE(statusOf(ask("DELETE",
                          "/api/v1/keys/" +
                              readOne.value(QStringLiteral("id")).toString().toUtf8(),
                          mToken)),
             200);
    QCOMPARE(statusOf(ask("GET", "/api/v1/tasks", reader)), 401);
    QCOMPARE(statusOf(ask("DELETE", "/api/v1/keys/nothing", mToken)), 404);

    ListOfJobOptions::getInstance()->Forget(task);
    ApiAuth::Keys::instance().forgetAll();
  }

  // ---- how a remote is set up ----------------------------------------------

  void aRemotesSettingsAreShownWithTheSecretsLeftOut() {
    QVERIFY(useFakeRclone(1));

    // Asked for first thing, before anything has read the backends -- the
    // moment a host the backend marks sensitive once went out because only
    // its name was there to judge it by. The answer waits for the backends.
    QVERIFY(!RcloneConfigRegistry::instance().isLoaded());
    const QByteArray first = ask("GET", "/api/v1/remotes/fake/config", mToken);
    QCOMPARE(statusOf(first), 200);
    QCOMPARE(bodyOf(first).object().value(QStringLiteral("described")).toBool(), true);
    QVERIFY2(!first.contains("SECRET"), first.constData());
    QVERIFY(RcloneConfigRegistry::instance().isLoaded());

    const QByteArray kinds = ask("GET", "/api/v1/providers", mToken);
    QCOMPARE(statusOf(kinds), 200);
    QCOMPARE(bodyOf(kinds).object().value(QStringLiteral("providers")).toArray()
                 .at(0).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("fake"));
    const QByteArray kind = ask("GET", "/api/v1/providers/fake", mToken);
    QCOMPARE(statusOf(kind), 200);
    QCOMPARE(bodyOf(kind).object().value(QStringLiteral("options")).toArray().size(), 4);
    QCOMPARE(statusOf(ask("GET", "/api/v1/providers/nothing", mToken)), 404);

    const QByteArray reply = ask("GET", "/api/v1/remotes/fake/config", mToken);
    QCOMPARE(statusOf(reply), 200);
    QVERIFY2(!reply.contains("SECRET") && !reply.contains("OBSCURED"),
             reply.constData());
    const QJsonObject config = bodyOf(reply).object();
    QCOMPARE(config.value(QStringLiteral("type")).toString(), QStringLiteral("fake"));
    QCOMPARE(config.value(QStringLiteral("described")).toBool(), true);
    QStringList shown;
    for (const QJsonValue &value : config.value(QStringLiteral("settings")).toArray()) {
      const QJsonObject s = value.toObject();
      shown << s.value(QStringLiteral("name")).toString() +
                   (s.value(QStringLiteral("secret")).toBool()
                        ? QStringLiteral("=*")
                        : QLatin1Char('=') + s.value(QStringLiteral("value")).toString());
    }
    QCOMPARE(shown, (QStringList{QStringLiteral("access_token=*"),
                                 QStringLiteral("chunk_size=1Gi"),
                                 QStringLiteral("pass=*"),
                                 QStringLiteral("page_size=10"),
                                 QStringLiteral("client_secret=*"),
                                 QStringLiteral("region=home")}));

    QCOMPARE(statusOf(ask("GET", "/api/v1/remotes/nothing/config", mToken)), 404);
    qputenv("FAKE_RCLONE_FAIL", "1");
    QCOMPARE(statusOf(ask("GET", "/api/v1/remotes/fake/config", mToken)), 502);
    qunsetenv("FAKE_RCLONE_FAIL");

    // The owner's business, even with the secrets out.
    ApiAuth::Keys::instance().forgetAll();
    const QByteArray made = send("POST", "/api/v1/keys", mToken,
                                 "{\"name\":\"looker\",\"scope\":\"write\"}");
    const QByteArray key = bodyOf(made).object().value(QStringLiteral("key")).toString().toUtf8();
    QCOMPARE(statusOf(ask("GET", "/api/v1/remotes/fake/config", key)), 403);
    QCOMPARE(statusOf(ask("GET", "/api/v1/providers", key)), 200);
    ApiAuth::Keys::instance().forgetAll();
  }

  // A remote renamed: made again under the new name with exactly what it
  // had, the old one deleted, and the tasks on it following.
  void aRemoteCanBeRenamedAndItsTasksFollow() {
    QVERIFY(useFakeRclone(1));
    QTemporaryDir scratch;
    const QString record = scratch.filePath(QStringLiteral("said.txt"));
    qputenv("FAKE_RCLONE_RECORD", record.toLocal8Bit());

    JobOptions *task = makeTask(QStringLiteral("on the fake"));
    task->dest = QStringLiteral("fake:backup/films");
    ListOfJobOptions::getInstance()->Persist(task);

    const QByteArray reply = send("PUT", "/api/v1/remotes/fake/config", mToken,
                                  "{\"name\":\"renamed\"}");
    QCOMPARE(statusOf(reply), 200);
    QCOMPARE(bodyOf(reply).object().value(QStringLiteral("tasks")).toArray()
                 .at(0).toString(),
             QStringLiteral("on the fake"));
    QCOMPARE(task->dest, QStringLiteral("renamed:backup/films"));

    QFile said(record);
    QVERIFY(said.open(QIODevice::ReadOnly));
    const QList<QByteArray> lines = said.readAll().split('\n');
    const QList<QByteArray> create = lines.at(0).split('\x1f');
    QCOMPARE(create.at(0), QByteArray("create"));
    QCOMPARE(create.at(1), QByteArray("renamed"));
    QCOMPARE(create.at(2), QByteArray("fake"));
    // Everything it had, the password as it is stored, and not obscured
    // a second time.
    QVERIFY2(create.contains("access_token=SECRET-TOKEN"), QByteArray(lines.at(0)).replace('', ' ').constData());
    QVERIFY2(create.contains("pass=OBSCURED-PASS"), QByteArray(lines.at(0)).replace('', ' ').constData());
    QVERIFY2(create.contains("--no-obscure"), QByteArray(lines.at(0)).replace('', ' ').constData());
    QVERIFY2(create.contains("--non-interactive"), QByteArray(lines.at(0)).replace('', ' ').constData());
    QCOMPARE(lines.at(1), QByteArray("delete\x1f" "fake"));
    // The secrets went to rclone and nowhere else.
    QVERIFY(!reply.contains("SECRET"));

    // Refused: no name, a bad one, one taken, a setting not offered yet.
    QCOMPARE(statusOf(send("PUT", "/api/v1/remotes/fake/config", mToken, "{\"name\":\"\"}")), 400);
    QCOMPARE(statusOf(send("PUT", "/api/v1/remotes/fake/config", mToken, "{\"name\":\"a:b\"}")), 400);
    // Its own name is no change at all. (A name another remote has is
    // refused: see aRemoteCanBeMadeAndChanged.)
    QCOMPARE(statusOf(send("PUT", "/api/v1/remotes/fake/config", mToken, "{\"name\":\"fake\"}")), 200);
    QCOMPARE(statusOf(send("PUT", "/api/v1/remotes/fake/config", mToken,
                           "{\"name\":\"x\",\"chunk_size\":\"1Gi\"}")), 400);
    QCOMPARE(statusOf(send("PUT", "/api/v1/remotes/nothing/config", mToken,
                           "{\"name\":\"x\"}")), 404);

    qunsetenv("FAKE_RCLONE_RECORD");
    ListOfJobOptions::getInstance()->Forget(task);
  }

  // A remote made, and its settings changed, through rclone and nothing else.
  void aRemoteCanBeMadeAndChanged() {
    QVERIFY(useFakeRclone(1));
    QTemporaryDir scratch;
    const QString record = scratch.filePath(QStringLiteral("said.txt"));
    qputenv("FAKE_RCLONE_RECORD", record.toLocal8Bit());
    const auto said = [&record]() {
      QFile file(record);
      file.open(QIODevice::ReadOnly);
      return file.readAll().split('\n');
    };

    const QByteArray made = send(
        "POST", "/api/v1/remotes", mToken,
        "{\"name\":\"fresh\",\"type\":\"fake\",\"settings\":{\"access_token\":\"T\","
        "\"chunk_size\":\"2Gi\",\"pass\":\"typed\"}}");
    QCOMPARE(statusOf(made), 201);
    QList<QByteArray> create = said().at(0).split('\x1f');
    QCOMPARE(create.mid(0, 3), (QList<QByteArray>{"create", "fresh", "fake"}));
    QVERIFY(create.contains("pass=typed"));
    // Obscured by rclone, as typed.
    QVERIFY(create.contains("--obscure"));
    QVERIFY(create.contains("--non-interactive"));

    // What the backend has, and nothing else; what it requires.
    QCOMPARE(statusOf(send("POST", "/api/v1/remotes", mToken,
                           "{\"name\":\"x\",\"type\":\"fake\",\"settings\":{\"access_token\":\"T\","
                           "\"--config\":\"/etc/x\"}}")),
             400);
    QCOMPARE(statusOf(send("POST", "/api/v1/remotes", mToken,
                           "{\"name\":\"x\",\"type\":\"fake\",\"settings\":{\"chunk_size\":\"1Gi\"}}")),
             400); // access_token is required
    QCOMPARE(statusOf(send("POST", "/api/v1/remotes", mToken,
                           "{\"name\":\"x\",\"type\":\"nothing\",\"settings\":{}}")),
             400);
    QCOMPARE(statusOf(send("POST", "/api/v1/remotes", mToken,
                           "{\"name\":\"fake\",\"type\":\"fake\",\"settings\":{\"access_token\":\"T\"}}")),
             409); // taken
    QCOMPARE(statusOf(send("POST", "/api/v1/remotes", mToken,
                           "{\"name\":\"x\",\"type\":\"fake\",\"color\":\"red\"}")),
             400);

    // Changed: only what is given, through rclone config update.
    const QByteArray changed = send("PUT", "/api/v1/remotes/fake/config", mToken,
                                    "{\"settings\":{\"page_size\":\"50\"}}");
    QCOMPARE(statusOf(changed), 200);
    const QList<QByteArray> update = said().last().isEmpty()
                                         ? said().at(said().size() - 2).split('\x1f')
                                         : said().last().split('\x1f');
    QCOMPARE(update.mid(0, 3), (QList<QByteArray>{"update", "fake", "page_size=50"}));
    QCOMPARE(statusOf(send("PUT", "/api/v1/remotes/fake/config", mToken,
                           "{\"settings\":{\"nonsense\":\"1\"}}")),
             400);
    QCOMPARE(statusOf(send("PUT", "/api/v1/remotes/nothing/config", mToken,
                           "{\"settings\":{\"page_size\":\"1\"}}")),
             404);

    // Not without being the owner.
    QCOMPARE(statusOf(send("POST", "/api/v1/remotes", QByteArray(), "{}")), 401);
    qunsetenv("FAKE_RCLONE_RECORD");
  }

  void aRemoteIsNotDeletedFromUnderItsTasks() {
    QVERIFY(useFakeRclone(1));
    JobOptions *task = makeTask(QStringLiteral("needs the fake"));
    task->dest = QStringLiteral("fake:backup");

    const QByteArray held = ask("DELETE", "/api/v1/remotes/fake", mToken);
    QCOMPARE(statusOf(held), 409);
    QCOMPARE(bodyOf(held).object().value(QStringLiteral("code")).toString(),
             QStringLiteral("used_by_tasks"));
    QCOMPARE(bodyOf(held).object().value(QStringLiteral("tasks")).toArray().at(0).toString(),
             QStringLiteral("needs the fake"));

    QCOMPARE(statusOf(ask("DELETE", "/api/v1/remotes/fake?force=1", mToken)), 200);
    // The task is left as it was: deleting a remote is not deleting tasks.
    QCOMPARE(task->dest, QStringLiteral("fake:backup"));

    // Not while a job is using it.
    qputenv("FAKE_RCLONE_SLEEP_MS", "3000");
    const QByteArray started = send("POST", "/api/v1/remotes/fake/stream", mToken,
                                    "{\"path\":\"films/a.mkv\"}");
    QCOMPARE(statusOf(started), 202);
    QCOMPARE(statusOf(ask("DELETE", "/api/v1/remotes/fake?force=1", mToken)), 409);
    QCOMPARE(statusOf(send("PUT", "/api/v1/remotes/fake/config", mToken,
                           "{\"name\":\"y\"}")), 409);
    JobRegistry::instance().find(bodyOf(started).object()
                                     .value(QStringLiteral("requestId")).toString())->stop();
    qputenv("FAKE_RCLONE_SLEEP_MS", "0");

    QCOMPARE(statusOf(ask("DELETE", "/api/v1/remotes/nothing", mToken)), 404);
    // The owner's business.
    QCOMPARE(statusOf(ask("DELETE", "/api/v1/remotes/fake", QByteArray())), 401);
    ListOfJobOptions::getInstance()->Forget(task);
  }

  // The API describes itself, from the same list every request is checked
  // against, and a tool can read the description before it has a key.
  void theApiDescribesItself() {
    const QByteArray reply = ask("GET", "/api/v1/openapi.json", QByteArray());
    QCOMPARE(statusOf(reply), 200);
    const QJsonObject spec = bodyOf(reply).object();
    QCOMPARE(spec.value(QStringLiteral("openapi")).toString(),
             QStringLiteral("3.1.0"));
    const QJsonObject paths = spec.value(QStringLiteral("paths")).toObject();

    // Every route is in it, and no route is in it twice.
    int operations = 0;
    for (const QString &path : paths.keys()) {
      operations += paths.value(path).toObject().size();
    }
    QCOMPARE(operations, int(ApiRoutes::All().size()));

    const QJsonObject make = paths.value(QStringLiteral("/api/v1/tasks"))
                                 .toObject()
                                 .value(QStringLiteral("post"))
                                 .toObject();
    QVERIFY(make.contains(QStringLiteral("requestBody")));
    QVERIFY(make.value(QStringLiteral("responses")).toObject()
                .contains(QStringLiteral("201")));

    // What needs no key says so; everything else needs one.
    QVERIFY(paths.value(QStringLiteral("/api/v1/ping")).toObject()
                .value(QStringLiteral("get")).toObject()
                .contains(QStringLiteral("security")));
    QVERIFY(!make.contains(QStringLiteral("security")));
    QVERIFY(!spec.value(QStringLiteral("security")).toArray().isEmpty());

    // A path's placeholders are parameters.
    const QJsonArray parameters =
        paths.value(QStringLiteral("/api/v1/tasks/{id}")).toObject()
            .value(QStringLiteral("put")).toObject()
            .value(QStringLiteral("parameters")).toArray();
    QCOMPARE(parameters.at(0).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("id"));

    // Nothing of this installation is in it.
    QVERIFY(!reply.contains(mToken));

    // A literal segment is matched before a placeholder in the same place.
    const ApiRoutes::Route *route =
        ApiRoutes::Match(QStringLiteral("GET"), QStringLiteral("/api/v1/tasks/defaults"));
    QVERIFY(route != nullptr);
    QCOMPARE(route->path, QStringLiteral("/api/v1/tasks/defaults"));
    QVERIFY(ApiRoutes::Match(QStringLiteral("HEAD"), QStringLiteral("/api/v1/ping")));
    QVERIFY(!ApiRoutes::Match(QStringLiteral("GET"), QStringLiteral("/api/v1/tasks/")));

    // And the check at the end of this file is a check: a request on no
    // route is noted, one on a route is not.
    ask("GET", "/api/v1/nothing", mToken);
    ask("GET", "/api/v1/system", mToken);
    QVERIFY(ApiRoutes::Unlisted().contains(QStringLiteral("GET /api/v1/nothing")));
    QVERIFY(!ApiRoutes::Unlisted().contains(QStringLiteral("GET /api/v1/system")));
  }

  // A dry run asked for in the query, because that is how somebody testing
  // by hand will ask for it.
  void aDryRunIsCarriedThrough() {
    JobOptions *task = makeTask(QStringLiteral("dry"));
    const QByteArray id = task->uniqueId.toString().toUtf8();

    QCOMPARE(statusOf(ask("POST", "/api/v1/tasks/" + id + "/enqueue?dryRun=1",
                          mToken)),
             202);
    QCOMPARE(JobQueue::instance().count(), 1);
    QVERIFY(JobQueue::instance().entries().first().dryRun);

    JobQueue::instance().clear();

    // And in the body, which is how a program will send it. Two spellings of
    // one option, because a thing that is only askable one way is a thing
    // half the callers get wrong.
    QCOMPARE(statusOf(send("POST", "/api/v1/tasks/" + id + "/enqueue", mToken,
                           "{\"dryRun\":true}")),
             202);
    QVERIFY(JobQueue::instance().entries().first().dryRun);

    JobQueue::instance().clear();
    ListOfJobOptions::getInstance()->Forget(task);
  }

  // Commanding something that is not there is a 404 with a reason, not a
  // silence and not a 500. Each of these is a thing a dashboard will ask for
  // after the user deleted it in another window.
  void commandingSomethingThatIsGoneSaysSo() {
    for (const char *target :
         {"/api/v1/tasks/{no-such-task}/run",
          "/api/v1/tasks/{no-such-task}/enqueue",
          "/api/v1/schedules/{no-such-schedule}/run",
          "/api/v1/jobs/{no-such-job}/stop"}) {
      const QByteArray reply = ask("POST", target, mToken);
      QCOMPARE(statusOf(reply), 404);
      QVERIFY2(bodyOf(reply).object().contains(QStringLiteral("error")),
               target);
    }

    QCOMPARE(statusOf(ask("DELETE", "/api/v1/queue/{nothing}", mToken)), 404);
  }

  // A log asked for by a name nothing knows, and a name that is a job but
  // wrote nothing, are different answers to different questions.
  void aLogThatIsNotThereSaysWhichKindOfNotThere() {
    const QByteArray reply = ask("GET", "/api/v1/jobs/{nobody}/log", mToken);
    QCOMPARE(statusOf(reply), 404);
    QVERIFY(bodyOf(reply)
                .object()
                .value(QStringLiteral("error"))
                .toString()
                .contains(QStringLiteral("no such job")));
  }

  // The stream is behind the same door. It is the one endpoint that would
  // hand out everything that happens from now on, so an unauthenticated
  // caller getting it would be worse than any single read.
  void theEventStreamNeedsATokenLikeEverythingElse() {
    QCOMPARE(statusOf(ask("GET", "/api/v1/events", QByteArray())), 401);
  }

  // The reason SSE exists here at all: a dashboard that polls /jobs once a
  // second has news that is always up to a second old and pays for it
  // whether anything happened or not. This says something the moment it
  // happens.
  void theEventStreamSaysHelloAndThenWhatHappens() {
    QTcpSocket stream;
    QVERIFY(openConnection(&stream));
    stream.write("GET /api/v1/events HTTP/1.1\r\nHost: localhost\r\n"
                 "Authorization: Bearer " + mToken + "\r\n\r\n");
    stream.flush();

    QByteArray seen;
    QVERIFY2(waitFor(&stream, &seen, "event: hello"),
             "the stream said nothing when it opened");
    QVERIFY2(seen.contains("text/event-stream"), seen.left(200).constData());

    // Nothing said how long the body is, because it is not over.
    QVERIFY(!seen.contains("Content-Length"));

    // Now make something happen, through the API like anybody else would.
    QCOMPARE(statusOf(ask("POST", "/api/v1/queue/start", mToken)), 200);
    QVERIFY2(waitFor(&stream, &seen, "event: queue"),
             "starting the queue was not announced");

    ask("POST", "/api/v1/queue/pause", mToken);
    stream.disconnectFromHost();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }

  // The Web UI's own files come without a token on purpose: a login page
  // that needs a login is a page nobody can reach. What matters is that they
  // are only markup and script -- the test below checks that the same
  // tokenless caller still gets nothing about this installation.
  void theWebUiIsServedWithoutATokenAndSaysNothing() {
    const QByteArray page = ask("GET", "/", QByteArray());
    QCOMPARE(statusOf(page), 200);
    QVERIFY2(page.contains("<title>Rclone Browser"),
             "the web UI was not compiled into the binary");
    QVERIFY(page.contains("text/html"));

    QCOMPARE(statusOf(ask("GET", "/app.js", QByteArray())), 200);
    QVERIFY(ask("GET", "/app.css", QByteArray()).contains("text/css"));

    // A remote's picture is asked for by the name of its kind, which is the
    // one place a name from outside goes into a file name. The pictures
    // belong to the window and are not in this test's build, so the answer
    // for a real one is "none here" -- never an error, and never a file.
    QCOMPARE(statusOf(ask("GET", "/icons/remote/drive.png", QByteArray())), 404);
    for (const char *path :
         {"/icons/remote/../../webui/app.js.png", "/icons/remote/..%2Fx.png",
          "/icons/remote/Drive.png", "/icons/remote/.png",
          "/icons/remote/a/b.png"}) {
      QVERIFY2(statusOf(ask("GET", path, QByteArray())) == 404, path);
    }

    // And nothing of this installation came with it -- above all not the
    // token, which a page that asks for one must not already know.
    QVERIFY2(!page.contains(mToken), "the page was served with the token in it");
    QCOMPARE(statusOf(ask("GET", "/api/v1/tasks", QByteArray())), 401);
  }

  // ---- signing in ---------------------------------------------------------

  // The sign-in page has to know which form to draw before anybody has
  // signed in, so this one endpoint answers without a secret. What it says
  // is the shape of the lock, never anything behind it.
  void theSignInPageCanAskWhatKindOfLockThisIs() {
    ApiAuth::SetPassword(QString());

    const QByteArray reply = ask("GET", "/api/v1/auth", QByteArray());
    QCOMPARE(statusOf(reply), 200);

    const QJsonObject object = bodyOf(reply).object();
    QCOMPARE(object.value(QStringLiteral("passwordSet")).toBool(), false);
    QCOMPARE(object.value(QStringLiteral("user")).toString(),
             QStringLiteral("admin"));

    // Nothing of the installation, and above all not the token.
    QVERIFY(!reply.contains(mToken));
  }

  void signingInWithNoPasswordSetIsRefusedRatherThanAllowed() {
    ApiAuth::SetPassword(QString());
    ApiAuth::Attempts::instance().forget();

    const QByteArray reply =
        send("POST", "/api/v1/login", QByteArray(),
             "{\"user\":\"admin\",\"password\":\"\"}");
    QCOMPARE(statusOf(reply), 409);
  }

  // The whole point of the password: a session that works everywhere the
  // token does, for somebody who never sees the token at all.
  void thePasswordBuysASessionThatWorksLikeTheToken() {
    ApiAuth::SetPassword(QString());
    ApiAuth::Attempts::instance().forget();

    // Setting the first password takes the token, because there is no old
    // password to prove anything with.
    const QByteArray set =
        send("POST", "/api/v1/password", mToken,
             "{\"current\":\"\",\"new\":\"a good long one\"}");
    QCOMPARE(statusOf(set), 200);

    const QByteArray wrong =
        send("POST", "/api/v1/login", QByteArray(),
             "{\"user\":\"admin\",\"password\":\"not it\"}");
    QCOMPARE(statusOf(wrong), 401);

    const QByteArray right =
        send("POST", "/api/v1/login", QByteArray(),
             "{\"user\":\"admin\",\"password\":\"a good long one\"}");
    QCOMPARE(statusOf(right), 200);

    const QByteArray session =
        bodyOf(right).object().value(QStringLiteral("session")).toString().toUtf8();
    QVERIFY(!session.isEmpty());
    QVERIFY2(session != mToken, "the session handed back was the token itself");

    // It opens the same doors...
    QCOMPARE(statusOf(ask("GET", "/api/v1/system", session)), 200);

    // ...and signing out shuts them again.
    QCOMPARE(statusOf(ask("POST", "/api/v1/logout", session)), 204);
    QCOMPARE(statusOf(ask("GET", "/api/v1/system", session)), 401);

    // The token was never affected by any of it.
    QCOMPARE(statusOf(ask("GET", "/api/v1/system", mToken)), 200);

    ApiAuth::SetPassword(QString());
    ApiAuth::Attempts::instance().forget();
  }

  // Changing it needs the old one even from a page that is already signed
  // in: a browser left open on an unattended machine must not be a way to
  // take the password over.
  void changingThePasswordNeedsTheOldOne() {
    ApiAuth::SetPassword(QStringLiteral("the first one"));
    ApiAuth::Attempts::instance().forget();

    const QByteArray refused =
        send("POST", "/api/v1/password", mToken,
             "{\"current\":\"guessing\",\"new\":\"the second one\"}");
    QCOMPARE(statusOf(refused), 403);

    const QByteArray tooShort =
        send("POST", "/api/v1/password", mToken,
             "{\"current\":\"the first one\",\"new\":\"short\"}");
    QCOMPARE(statusOf(tooShort), 400);

    QVERIFY(ApiAuth::PasswordMatches(QStringLiteral("the first one"),
                                     ApiAuth::StoredPassword()));

    ApiAuth::SetPassword(QString());
    ApiAuth::Attempts::instance().forget();
  }

  // ---- looking inside a remote --------------------------------------------

  void listingARemoteNeedsASecretAndARealRemote() {
    QCOMPARE(statusOf(ask("GET", "/api/v1/remotes/anything/ls", QByteArray())),
             401);
    QCOMPARE(statusOf(ask("GET", "/api/v1/remotes/no-such-remote/ls", mToken)),
             404);
  }

  // Points rclone at the stand-in and waits for the remote list, so the
  // listing tests below have a remote to ask about.
  bool useFakeRclone(int entries) {
    qputenv("FAKE_RCLONE_ENTRIES", QByteArray::number(entries));
    SetRclone(QString::fromLatin1(RB_FAKE_RCLONE));

    QSignalSpy refreshed(&RemoteRegistry::instance(),
                         &RemoteRegistry::refreshed);
    RemoteRegistry::instance().refresh();
    if (refreshed.isEmpty() && !refreshed.wait(10000)) {
      return false;
    }
    return RemoteRegistry::instance().find(QStringLiteral("fake")) != nullptr;
  }

  // The listing, end to end: a request over a socket, a process run, its
  // output parsed, and an answer written when it is done. Every other route
  // answers from something already in memory; this is the only one that has
  // to wait, which is why it is the only one that can fail to answer at all.
  void aFolderIsListedThroughTheApi() {
    QVERIFY2(useFakeRclone(6), "the stand-in rclone did not list a remote");

    const QByteArray reply = ask("GET", "/api/v1/remotes/fake/ls", mToken);
    QCOMPARE(statusOf(reply), 200);

    const QJsonObject object = bodyOf(reply).object();
    QCOMPARE(object.value(QStringLiteral("remote")).toString(),
             QStringLiteral("fake"));
    QCOMPARE(object.value(QStringLiteral("truncated")).toBool(), false);

    const QJsonArray entries = object.value(QStringLiteral("entries")).toArray();
    QCOMPARE(entries.size(), 6);

    const QJsonObject first = entries.at(0).toObject();
    QCOMPARE(first.value(QStringLiteral("name")).toString(),
             QStringLiteral("item-0"));
    QCOMPARE(first.value(QStringLiteral("isDir")).toBool(), true);
    QVERIFY(!first.value(QStringLiteral("modTime")).toString().isEmpty());
  }

  // The path an entry comes back with has to be the one to ask for next.
  // rclone gives it relative to the folder it listed, so an entry two levels
  // down came back as a bare name -- and the next click asked for that name
  // at the root of the remote, which is a different folder or, as the first
  // person to try it found, no folder at all.
  void anEntryCarriesThePathToAskForNext() {
    QVERIFY(useFakeRclone(3));

    const QByteArray reply =
        ask("GET", "/api/v1/remotes/fake/ls?path=films%2F2012", mToken);
    QCOMPARE(statusOf(reply), 200);

    const QJsonObject object = bodyOf(reply).object();
    QCOMPARE(object.value(QStringLiteral("path")).toString(),
             QStringLiteral("films/2012"));

    const QJsonObject first =
        object.value(QStringLiteral("entries")).toArray().at(0).toObject();
    QCOMPARE(first.value(QStringLiteral("name")).toString(),
             QStringLiteral("item-0"));
    QCOMPARE(first.value(QStringLiteral("path")).toString(),
             QStringLiteral("films/2012/item-0"));
  }

  // One folder, however it is spelled. A trailing slash used to make the
  // joined path "films//2012", which no backend recognises.
  void aPathIsTheSameFolderHoweverItIsSpelled() {
    QVERIFY(useFakeRclone(2));

    for (const char *spelling : {"films", "/films", "films/", "/films/"}) {
      const QByteArray reply =
          ask("GET", QByteArray("/api/v1/remotes/fake/ls?path=") + spelling,
              mToken);
      QCOMPARE(statusOf(reply), 200);

      const QJsonObject object = bodyOf(reply).object();
      QCOMPARE(object.value(QStringLiteral("path")).toString(),
               QStringLiteral("films"));
      QCOMPARE(object.value(QStringLiteral("entries")).toArray().at(0)
                   .toObject().value(QStringLiteral("path")).toString(),
               QStringLiteral("films/item-0"));
    }
  }

  // When a remote was last uploaded to, from the history. The window shows
  // it under each remote; the page has to be able to say the same, and from
  // the same row.
  void aRemoteSaysWhenItWasLastUploadedTo() {
    QVERIFY(useFakeRclone(1));

    JobRunRecord run;
    run.requestId = QStringLiteral("{last-upload-test}");
    run.taskName = QStringLiteral("nightly");
    run.kind = QStringLiteral("transfer");
    run.transferMode = QStringLiteral("task");
    run.source = QStringLiteral("C:/films");
    run.dest = QStringLiteral("fake:films");
    run.startedAt = QDateTime::currentMSecsSinceEpoch() - 90000;
    run.finishedAt = run.startedAt + 61500;
    run.state = QStringLiteral("finished");
    QVERIFY(RunHistory::recordFinished(run));

    const QByteArray reply = ask("GET", "/api/v1/remotes", mToken);
    QCOMPARE(statusOf(reply), 200);

    QJsonObject remote;
    for (const QJsonValue &value : bodyOf(reply).array()) {
      if (value.toObject().value(QStringLiteral("name")).toString() ==
          QLatin1String("fake")) {
        remote = value.toObject();
      }
    }
    QVERIFY2(!remote.isEmpty(), "the remote was not listed");

    const QJsonObject up = remote.value(QStringLiteral("lastUpload")).toObject();
    QCOMPARE(up.value(QStringLiteral("requestId")).toString(), run.requestId);
    QCOMPARE(up.value(QStringLiteral("state")).toString(),
             QStringLiteral("finished"));
    QCOMPARE(up.value(QStringLiteral("durationSeconds")).toDouble(), 61.5);

    // The moment carries its offset from UTC. Without one, the same string
    // means a different instant to a reader in another time zone.
    const QString started = up.value(QStringLiteral("startedAt")).toString();
    QVERIFY2(QDateTime::fromString(started, Qt::ISODate).toMSecsSinceEpoch() /
                     1000 ==
                 run.startedAt / 1000,
             qPrintable(started));
    QVERIFY2(started.contains(QLatin1Char('+')) ||
                 started.endsWith(QLatin1Char('Z')) ||
                 started.lastIndexOf(QLatin1Char('-')) > 10,
             qPrintable(started));

    // Never downloaded from: null, not an empty object and not somebody
    // else's run.
    QVERIFY(remote.value(QStringLiteral("lastDownload")).isNull());
  }

  // ---- doing things to a remote's files ------------------------------------

  // Making a folder, from outside the window. The answer comes when rclone
  // has finished, so whoever asked knows the folder is there and not merely
  // that somebody has been asked to make it.
  void aFolderCanBeMadeThroughTheApi() {
    QVERIFY(useFakeRclone(1));
    qunsetenv("FAKE_RCLONE_FAIL");

    const QByteArray reply =
        send("POST", "/api/v1/remotes/fake/mkdir", mToken,
             "{\"path\":\"films\",\"name\":\"2026\"}");
    QCOMPARE(statusOf(reply), 200);

    const QJsonObject object = bodyOf(reply).object();
    QCOMPARE(object.value(QStringLiteral("path")).toString(),
             QStringLiteral("films/2026"));

    // And it went through the registry like any other job, so it is in the
    // history -- which the button in the window never managed.
    const QString request = object.value(QStringLiteral("requestId")).toString();
    QVERIFY(!request.isEmpty());
    const JobRunRecord row = RunHistory::find(request);
    QCOMPARE(row.kind, QStringLiteral("mkdir"));
    QCOMPARE(row.state, QStringLiteral("finished"));
    QCOMPARE(row.dest, QStringLiteral("fake:films/2026"));
  }

  // When rclone cannot, the caller is told what rclone said. "It failed" is
  // the one thing they already know.
  void aFolderThatCannotBeMadeSaysWhyInRclonesWords() {
    QVERIFY(useFakeRclone(1));
    qputenv("FAKE_RCLONE_FAIL", "1");

    const QByteArray reply =
        send("POST", "/api/v1/remotes/fake/mkdir", mToken,
             "{\"path\":\"\",\"name\":\"nope\"}");
    qunsetenv("FAKE_RCLONE_FAIL");

    QCOMPARE(statusOf(reply), 502);
    const QJsonObject object = bodyOf(reply).object();
    QCOMPARE(object.value(QStringLiteral("code")).toString(),
             QStringLiteral("rclone_failed"));
    QVERIFY2(object.value(QStringLiteral("error")).toString().contains(
                 QStringLiteral("directory not found")),
             qPrintable(object.value(QStringLiteral("error")).toString()));
  }

  // A name is one name. Refused before anything is started, so there is no
  // job and no row for something that was never attempted.
  void aFolderNameThatReachesElsewhereIsRefused() {
    QVERIFY(useFakeRclone(1));
    const int before = RunHistory::count();

    for (const char *body :
         {"{\"path\":\"films\",\"name\":\"../../etc\"}",
          "{\"path\":\"films\",\"name\":\"a/b\"}",
          "{\"path\":\"films\",\"name\":\"\"}",
          "{\"path\":\"films\"}"}) {
      const QByteArray reply =
          send("POST", "/api/v1/remotes/fake/mkdir", mToken, body);
      QCOMPARE(statusOf(reply), 400);
      QCOMPARE(bodyOf(reply).object().value(QStringLiteral("code")).toString(),
               QStringLiteral("bad_name"));
    }
    QCOMPARE(RunHistory::count(), before);
  }

  // Renaming, from outside the window. The thing stays where it is and gets
  // a new name; the answer says where it now is, so the caller does not have
  // to work that out.
  void aThingCanBeRenamedThroughTheApi() {
    QVERIFY(useFakeRclone(1));
    qunsetenv("FAKE_RCLONE_FAIL");

    const QByteArray reply =
        send("POST", "/api/v1/remotes/fake/rename", mToken,
             "{\"path\":\"films/2026/old.mkv\",\"name\":\"new.mkv\"}");
    QCOMPARE(statusOf(reply), 200);

    const QJsonObject object = bodyOf(reply).object();
    QCOMPARE(object.value(QStringLiteral("from")).toString(),
             QStringLiteral("films/2026/old.mkv"));
    QCOMPARE(object.value(QStringLiteral("path")).toString(),
             QStringLiteral("films/2026/new.mkv"));

    const JobRunRecord row = RunHistory::find(
        object.value(QStringLiteral("requestId")).toString());
    QCOMPARE(row.kind, QStringLiteral("rename"));
    QCOMPARE(row.source, QStringLiteral("fake:films/2026/old.mkv"));
    QCOMPARE(row.dest, QStringLiteral("fake:films/2026/new.mkv"));
  }

  void renamingIsRefusedBeforeAnythingRuns() {
    QVERIFY(useFakeRclone(1));
    const int before = RunHistory::count();

    for (const char *body :
         {"{\"path\":\"\",\"name\":\"x\"}",                  // the root
          "{\"path\":\"films/a\",\"name\":\"a\"}",           // same name
          "{\"path\":\"films/a\",\"name\":\"../b\"}",        // elsewhere
          "{\"path\":\"films/a\",\"name\":\"\"}"}) {         // nothing
      QCOMPARE(statusOf(send("POST", "/api/v1/remotes/fake/rename", mToken,
                             body)),
               400);
    }
    QCOMPARE(RunHistory::count(), before);
  }

  void aRenameThatFailsSaysWhy() {
    QVERIFY(useFakeRclone(1));
    qputenv("FAKE_RCLONE_FAIL", "1");
    const QByteArray reply =
        send("POST", "/api/v1/remotes/fake/rename", mToken,
             "{\"path\":\"films/a\",\"name\":\"b\"}");
    qunsetenv("FAKE_RCLONE_FAIL");

    QCOMPARE(statusOf(reply), 502);
    QVERIFY(!bodyOf(reply).object().value(QStringLiteral("error"))
                 .toString().isEmpty());
  }

  // Deleting, from outside the window. What ran is in the history, which is
  // the first place anybody looks when something is gone.
  void aFileAndAFolderAreDeletedByDifferentCommands() {
    QVERIFY(useFakeRclone(1));
    qunsetenv("FAKE_RCLONE_FAIL");

    const QByteArray file =
        send("POST", "/api/v1/remotes/fake/delete", mToken,
             "{\"path\":\"films/old.mkv\"}");
    QCOMPARE(statusOf(file), 200);
    QCOMPARE(bodyOf(file).object().value(QStringLiteral("isDir")).toBool(),
             false);

    const QByteArray folder =
        send("POST", "/api/v1/remotes/fake/delete", mToken,
             "{\"path\":\"films/2019\",\"isDir\":true}");
    QCOMPARE(statusOf(folder), 200);

    const JobRunRecord fileRow = RunHistory::find(
        bodyOf(file).object().value(QStringLiteral("requestId")).toString());
    const JobRunRecord folderRow = RunHistory::find(
        bodyOf(folder).object().value(QStringLiteral("requestId")).toString());
    QCOMPARE(fileRow.kind, QStringLiteral("delete"));
    QCOMPARE(fileRow.source, QStringLiteral("fake:films/old.mkv"));

    // Said nothing about being a folder: treated as a file, and described so.
    QVERIFY2(fileRow.info.startsWith(QStringLiteral("Delete file")),
             qPrintable(fileRow.info));
    QVERIFY2(folderRow.info.startsWith(QStringLiteral("Delete folder")),
             qPrintable(folderRow.info));
  }

  // The one that must never work. An empty path is the whole remote, and a
  // field left blank is not a request to delete everything.
  void theWholeRemoteCannotBeDeletedByLeavingThePathEmpty() {
    QVERIFY(useFakeRclone(1));
    const int before = RunHistory::count();

    for (const char *body :
         {"{\"path\":\"\",\"isDir\":true}", "{\"isDir\":true}",
          "{\"path\":\"/\",\"isDir\":true}", "{\"path\":\"///\"}", "{}"}) {
      const QByteArray reply =
          send("POST", "/api/v1/remotes/fake/delete", mToken, body);
      QCOMPARE(statusOf(reply), 400);
    }
    QCOMPARE(RunHistory::count(), before);
  }

  void aDeleteThatFailsSaysWhy() {
    QVERIFY(useFakeRclone(1));
    qputenv("FAKE_RCLONE_FAIL", "1");
    const QByteArray reply =
        send("POST", "/api/v1/remotes/fake/delete", mToken,
             "{\"path\":\"films/a\",\"isDir\":true}");
    qunsetenv("FAKE_RCLONE_FAIL");
    QCOMPARE(statusOf(reply), 502);
  }

  // ---- asking rclone about a remote ----------------------------------------

  // Numbers, not a sentence about them: another program asking how big a
  // folder is wants a count of bytes.
  void theSizeOfAFolderComesBackAsNumbers() {
    QVERIFY(useFakeRclone(1));
    qunsetenv("FAKE_RCLONE_FAIL");

    const QByteArray reply =
        ask("GET", "/api/v1/remotes/fake/size?path=films", mToken);
    QCOMPARE(statusOf(reply), 200);

    const QJsonObject object = bodyOf(reply).object();
    QCOMPARE(object.value(QStringLiteral("count")).toInt(), 3);
    QCOMPARE(object.value(QStringLiteral("bytes")).toInt(), 1536);
    QCOMPARE(object.value(QStringLiteral("path")).toString(),
             QStringLiteral("films"));

    // It ran as a job, so it is in the history like any other.
    QCOMPARE(RunHistory::find(object.value(QStringLiteral("requestId")).toString())
                 .kind,
             QStringLiteral("size"));
  }

  // rclone prints the quota over several lines and may say something else
  // first. The figures are still found.
  void theQuotaIsReadOutOfWhateverRcloneSaidAroundIt() {
    QVERIFY(useFakeRclone(1));
    const QByteArray reply = ask("GET", "/api/v1/remotes/fake/about", mToken);
    QCOMPARE(statusOf(reply), 200);

    const QJsonObject object = bodyOf(reply).object();
    QCOMPARE(object.value(QStringLiteral("total")).toInt(), 1000);
    QCOMPARE(object.value(QStringLiteral("used")).toInt(), 400);
    QCOMPARE(object.value(QStringLiteral("free")).toInt(), 600);
  }

  // Indentation is all that says which folder is inside which. A job's
  // output used to be trimmed at both ends, which flattened every tree.
  void aTreeKeepsItsShape() {
    QVERIFY(useFakeRclone(1));
    const QByteArray reply =
        ask("GET", "/api/v1/remotes/fake/tree?path=", mToken);
    QCOMPARE(statusOf(reply), 200);

    const QString tree =
        bodyOf(reply).object().value(QStringLiteral("output")).toString();
    QVERIFY2(tree.contains(QStringLiteral("|   `-- 2026")), qPrintable(tree));
    QVERIFY2(tree.contains(QStringLiteral("\n    `-- live")),
             "the indentation of the last branch was lost");
  }

  // A question rclone cannot answer comes back with what rclone said.
  void aQuestionThatFailsSaysWhy() {
    QVERIFY(useFakeRclone(1));
    qputenv("FAKE_RCLONE_FAIL", "1");
    const QByteArray reply = ask("GET", "/api/v1/remotes/fake/about", mToken);
    qunsetenv("FAKE_RCLONE_FAIL");

    QCOMPARE(statusOf(reply), 502);
    QVERIFY(bodyOf(reply).object().value(QStringLiteral("error")).toString()
                .contains(QStringLiteral("didn't find section")));
  }

  void askingAboutARemoteNeedsASecretAndARealRemote() {
    QVERIFY(useFakeRclone(1));
    for (const char *question : {"size", "about", "tree"}) {
      const QByteArray target =
          QByteArray("/api/v1/remotes/fake/") + question;
      QCOMPARE(statusOf(ask("GET", target, QByteArray())), 401);
      QCOMPARE(statusOf(ask("GET", QByteArray("/api/v1/remotes/nope/") + question,
                            mToken)),
               404);
    }
  }

  // ---- link, cleanup, dedupe ------------------------------------------------

  // The address is what was asked for, so it comes back as a field of its
  // own and not buried in rclone's output for the caller to dig out.
  void aPublicLinkComesBackAsAnAddress() {
    QVERIFY(useFakeRclone(1));
    qunsetenv("FAKE_RCLONE_FAIL");

    const QByteArray reply =
        send("POST", "/api/v1/remotes/fake/link", mToken,
             "{\"path\":\"films/a.mkv\"}");
    QCOMPARE(statusOf(reply), 200);
    QCOMPARE(bodyOf(reply).object().value(QStringLiteral("url")).toString(),
             QStringLiteral("https://example.test/share/abc123"));
  }

  // Sharing changes who can reach something. A path left blank is not a
  // request to share the whole remote.
  void theWholeRemoteCannotBeSharedByLeavingThePathEmpty() {
    QVERIFY(useFakeRclone(1));
    for (const char *body : {"{\"path\":\"\"}", "{}", "{\"path\":\"/\"}"}) {
      QCOMPARE(statusOf(send("POST", "/api/v1/remotes/fake/link", mToken, body)),
               400);
    }
  }

  // A mode rclone does not know is refused here. Passed on, rclone falls
  // back to asking at a terminal nobody is sitting at.
  void dedupeTakesOnlyAModeItKnows() {
    QVERIFY(useFakeRclone(1));
    for (const char *body :
         {"{\"path\":\"films\"}", "{\"path\":\"films\",\"mode\":\"\"}",
          "{\"path\":\"films\",\"mode\":\"interactive\"}",
          "{\"path\":\"films\",\"mode\":\"newest --max-delete 0\"}"}) {
      const QByteArray reply =
          send("POST", "/api/v1/remotes/fake/dedupe", mToken, body);
      QCOMPARE(statusOf(reply), 400);
      QCOMPARE(bodyOf(reply).object().value(QStringLiteral("code")).toString(),
               QStringLiteral("bad_mode"));
    }
  }

  // A dry run says what it would do, and the history records that it was
  // one -- so nobody reading it later takes a rehearsal for the real thing.
  void aDedupeDryRunIsRecordedAsARehearsal() {
    QVERIFY(useFakeRclone(1));
    const QByteArray reply =
        send("POST", "/api/v1/remotes/fake/dedupe", mToken,
             "{\"path\":\"films\",\"mode\":\"newest\",\"dryRun\":true}");
    QCOMPARE(statusOf(reply), 200);

    const QJsonObject object = bodyOf(reply).object();
    QVERIFY(object.value(QStringLiteral("output")).toString().contains(
        QStringLiteral("Skipped delete as --dry-run is set")));

    const JobRunRecord row = RunHistory::find(
        object.value(QStringLiteral("requestId")).toString());
    QCOMPARE(row.kind, QStringLiteral("dedupe"));
    QVERIFY(row.dryRun);

    // And the real one is not.
    const QByteArray real =
        send("POST", "/api/v1/remotes/fake/dedupe", mToken,
             "{\"path\":\"films\",\"mode\":\"newest\"}");
    QCOMPARE(statusOf(real), 200);
    QVERIFY(!RunHistory::find(bodyOf(real).object()
                                  .value(QStringLiteral("requestId"))
                                  .toString())
                 .dryRun);
  }

  void cleanupRunsAndABackendThatCannotSaysSo() {
    QVERIFY(useFakeRclone(1));
    QCOMPARE(statusOf(send("POST", "/api/v1/remotes/fake/cleanup", mToken, "{}")),
             200);

    qputenv("FAKE_RCLONE_FAIL", "1");
    const QByteArray reply =
        send("POST", "/api/v1/remotes/fake/cleanup", mToken, "{}");
    qunsetenv("FAKE_RCLONE_FAIL");
    QCOMPARE(statusOf(reply), 502);
    QVERIFY(bodyOf(reply).object().value(QStringLiteral("error")).toString()
                .contains(QStringLiteral("does not support")));
  }

  // ---- copying and moving ---------------------------------------------------

  // A copy is answered when it starts, not when it ends: it takes as long as
  // the files are large. It is a transfer like any other from then on.
  void aCopyIsStartedAndReportedAsAJob() {
    QVERIFY(useFakeRclone(1));
    qputenv("FAKE_RCLONE_SLEEP_MS", "0");

    const QByteArray reply =
        send("POST", "/api/v1/remotes/fake/copy", mToken,
             "{\"path\":\"films/2026\",\"isDir\":true,\"to\":\"backup\"}");
    QCOMPARE(statusOf(reply), 202);

    const QJsonObject object = bodyOf(reply).object();
    QCOMPARE(object.value(QStringLiteral("source")).toString(),
             QStringLiteral("fake:films/2026"));
    // A folder arrives under its own name inside the destination. Without
    // that rclone would spill its contents straight into "backup".
    QCOMPARE(object.value(QStringLiteral("dest")).toString(),
             QStringLiteral("fake:backup/2026"));

    const QString request = object.value(QStringLiteral("requestId")).toString();
    RunningJob *job = JobRegistry::instance().find(request);
    QVERIFY(job != nullptr);
    QCOMPARE(JobKindToString(job->kind()), QStringLiteral("transfer"));

    // A file goes into the folder as it is.
    const QByteArray file =
        send("POST", "/api/v1/remotes/fake/move", mToken,
             "{\"path\":\"films/a.mkv\",\"to\":\"backup\"}");
    QCOMPARE(statusOf(file), 202);
    QCOMPARE(bodyOf(file).object().value(QStringLiteral("dest")).toString(),
             QStringLiteral("fake:backup"));
  }

  void aCopyThatCannotMakeSenseIsRefused() {
    QVERIFY(useFakeRclone(1));

    for (const char *body :
         {"{\"path\":\"\",\"isDir\":true,\"to\":\"backup\"}",
          "{\"path\":\"films\",\"isDir\":true,\"to\":\"films/inside\"}",
          "{\"path\":\"films/a.mkv\",\"to\":\"films\"}"}) {
      QCOMPARE(statusOf(send("POST", "/api/v1/remotes/fake/copy", mToken, body)),
               400);
    }

    // And to a remote that is not there.
    QCOMPARE(statusOf(send(
                 "POST", "/api/v1/remotes/fake/copy", mToken,
                 "{\"path\":\"a\",\"to\":\"b\",\"toRemote\":\"nowhere\"}")),
             404);
  }

  // ---- comparing, and exporting ---------------------------------------------

  // Two folders that differ is an answer. rclone reports it by ending in an
  // error, and passing that on as a failure would tell the caller the check
  // could not be done when it was done and found something.
  void aCheckThatFindsDifferencesIsAnAnswerNotAFailure() {
    QVERIFY(useFakeRclone(1));
    qunsetenv("FAKE_RCLONE_FAIL");

    const QByteArray same = ask(
        "GET", "/api/v1/remotes/fake/check?path=a&to=b", mToken);
    QCOMPARE(statusOf(same), 200);
    QCOMPARE(bodyOf(same).object().value(QStringLiteral("matched")).toBool(),
             true);
    QCOMPARE(bodyOf(same).object().value(QStringLiteral("matching")).toInt(), 3);

    qputenv("FAKE_RCLONE_DIFFER", "1");
    const QByteArray differs = ask(
        "GET", "/api/v1/remotes/fake/check?path=a&to=b", mToken);
    qunsetenv("FAKE_RCLONE_DIFFER");
    QCOMPARE(statusOf(differs), 200);

    const QJsonObject object = bodyOf(differs).object();
    QCOMPARE(object.value(QStringLiteral("matched")).toBool(), false);
    QCOMPARE(object.value(QStringLiteral("differences")).toInt(), 1);
    QVERIFY(object.value(QStringLiteral("output")).toString().contains(
        QStringLiteral("zzz.txt")));

    // And a check that could not be made at all is still a failure.
    qputenv("FAKE_RCLONE_FAIL", "1");
    const QByteArray failed = ask(
        "GET", "/api/v1/remotes/fake/check?path=a&to=b", mToken);
    qunsetenv("FAKE_RCLONE_FAIL");
    QCOMPARE(statusOf(failed), 502);
  }

  void aFolderIsNotCheckedAgainstItself() {
    QVERIFY(useFakeRclone(1));
    QCOMPARE(statusOf(ask("GET", "/api/v1/remotes/fake/check?path=a&to=a",
                          mToken)),
             400);
    QCOMPARE(statusOf(ask("GET",
                          "/api/v1/remotes/fake/check?path=a&to=b&toRemote=none",
                          mToken)),
             404);
  }

  // The exported listing comes back as a file: the rows and nothing else,
  // with whatever rclone said in between left out.
  void anExportComesBackAsAFile() {
    QVERIFY(useFakeRclone(1));

    const QByteArray reply =
        ask("GET", "/api/v1/remotes/fake/export?path=films", mToken);
    QCOMPARE(statusOf(reply), 200);
    QVERIFY(reply.contains("text/csv"));
    QVERIFY(reply.contains("attachment; filename=\"export.csv\""));

    const QByteArray body = reply.mid(reply.indexOf("\r\n\r\n") + 4);
    const QList<QByteArray> rows = body.trimmed().split('\n');
    QCOMPARE(rows.size(), 3);
    QCOMPARE(rows.at(0),
             QByteArray("\"films/a.mkv\",\"2026-10-12 14:16:41\",300000"));
    QVERIFY2(!body.contains("NOTICE"), "a notice ended up in the exported file");
    QVERIFY(rows.at(1).startsWith("\"b \"\"quoted\"\".txt\""));

    // Names only.
    const QByteArray names =
        ask("GET", "/api/v1/remotes/fake/export?path=films&format=txt", mToken);
    QCOMPARE(statusOf(names), 200);
    QVERIFY(names.contains("export.txt"));
    QVERIFY(names.endsWith("\"empty\"\n"));
  }

  // ---- mounting ---------------------------------------------------------------

  // A mount started from outside the window. It is a job like any other from
  // then on, and stopping that job is what unmounts it.
  void aMountIsStartedAsAJobAndStoppedLikeOne() {
    QVERIFY(useFakeRclone(1));
    qputenv("FAKE_RCLONE_SLEEP_MS", "3000"); // stays up for a moment

    const QByteArray reply =
        send("POST", "/api/v1/remotes/fake/mount", mToken,
             "{\"path\":\"films\",\"mountPoint\":\"X:\",\"readOnly\":true,"
             "\"cache\":\"writes\"}");
    qputenv("FAKE_RCLONE_SLEEP_MS", "0");
    QCOMPARE(statusOf(reply), 202);

    const QJsonObject object = bodyOf(reply).object();
    QCOMPARE(object.value(QStringLiteral("source")).toString(),
             QStringLiteral("fake:films"));
    QCOMPARE(object.value(QStringLiteral("mountPoint")).toString(),
             QStringLiteral("X:"));

    const QString request = object.value(QStringLiteral("requestId")).toString();
    RunningJob *job = JobRegistry::instance().find(request);
    QVERIFY(job != nullptr);
    QCOMPARE(JobKindToString(job->kind()), QStringLiteral("mount"));

    // What was asked for is in the command, and the login for its remote
    // control is not: that goes through the environment.
    const QString command = job->displayCommand().join(QLatin1Char(' '));
    QVERIFY2(command.contains(QStringLiteral("--read-only")), qPrintable(command));
    QVERIFY(command.contains(QStringLiteral("--vfs-cache-mode writes")));
    QVERIFY(command.contains(QStringLiteral("--rc-addr localhost:")));
    QVERIFY2(!command.contains(QStringLiteral("--rc-pass")) &&
                 !command.contains(QStringLiteral("--rc-user")),
             "the mount's remote-control login is among its arguments");

    // Taken down the way any job is stopped. (The stand-in has no remote
    // control to ask politely, so this only checks the request is accepted.)
    QCOMPARE(statusOf(ask("POST",
                          QByteArray("/api/v1/jobs/") +
                              QUrl::toPercentEncoding(request) + "/stop",
                          mToken)),
             202);
    job->stop();
  }

  // A stream is an address a player can open with nothing but the address,
  // so the address carries a secret -- and the secret is nowhere else. It
  // is an address of this same server: there is no second port to open.
  void aFileIsStreamedAtAnAddressOnlyItsAskerKnows() {
    QVERIFY(useFakeRclone(1));
    qputenv("FAKE_RCLONE_SLEEP_MS", "5000");

    const QByteArray reply =
        send("POST", "/api/v1/remotes/fake/stream", mToken,
             "{\"path\":\"films/Film [1080p] a.mkv\"}");
    qputenv("FAKE_RCLONE_SLEEP_MS", "0");
    QCOMPARE(statusOf(reply), 202);
    const QJsonObject object = bodyOf(reply).object();
    // Nothing about a port: that is between this program and rclone.
    QVERIFY(!object.contains(QStringLiteral("port")));

    const QString path = object.value(QStringLiteral("path")).toString();
    QVERIFY2(path.startsWith(QStringLiteral("/stream/")), qPrintable(path));
    QVERIFY2(path.endsWith(QStringLiteral("/Film%20%5B1080p%5D%20a.mkv")),
             qPrintable(path));
    const QString secret = path.section(QLatin1Char('/'), 2, 2);
    QCOMPARE(secret.size(), 32);

    const QString request = object.value(QStringLiteral("requestId")).toString();
    RunningJob *job = JobRegistry::instance().find(request);
    QVERIFY(job != nullptr);
    QCOMPARE(JobKindToString(job->kind()), QStringLiteral("stream"));

    // rclone listens on this machine alone. It serves the file out of its
    // folder with the rest of the folder filtered away, the name's own
    // brackets not left to mean something to the filter.
    const QString command = job->displayCommand().join(QLatin1Char(' '));
    QVERIFY2(command.contains(QStringLiteral("serve http fake:films")),
             qPrintable(command));
    QVERIFY2(command.contains(QStringLiteral("--addr 127.0.0.1:")),
             qPrintable(command));
    QVERIFY2(command.contains(QStringLiteral("\\[1080p\\]")),
             qPrintable(command));
    // And the secret is not in what is shown of the command.
    QVERIFY2(!command.contains(secret), qPrintable(command));

    // The job can give the address again, to whoever may see jobs.
    bool found = false;
    for (const QJsonValue &value :
         bodyOf(ask("GET", "/api/v1/jobs", mToken)).array()) {
      const QJsonObject one = value.toObject();
      if (one.value(QStringLiteral("requestId")).toString() == request) {
        found = true;
        QCOMPARE(one.value(QStringLiteral("stream")).toObject()
                     .value(QStringLiteral("path")).toString(),
                 path);
        QVERIFY(!one.value(QStringLiteral("command")).toString().contains(secret));
      }
    }
    QVERIFY(found);

    // Standing in for rclone: something on the port it was given, which
    // says what it was asked and answers as rclone would to a seek.
    QTcpServer rclone;
    QVERIFY(rclone.listen(QHostAddress::LocalHost, job->streamPort()));
    QByteArray asked;
    QObject::connect(&rclone, &QTcpServer::newConnection, &rclone, [&]() {
      QTcpSocket *peer = rclone.nextPendingConnection();
      QObject::connect(peer, &QTcpSocket::readyRead, peer, [&asked, peer]() {
        asked += peer->readAll();
        if (!asked.contains("\r\n\r\n")) {
          return;
        }
        peer->write("HTTP/1.1 206 Partial Content\r\n"
                    "Content-Type: video/x-matroska\r\n"
                    "Content-Range: bytes 2-5/10\r\n"
                    "Content-Length: 4\r\n"
                    "Connection: close\r\n\r\nFILM");
        peer->disconnectFromHost();
      });
    });

    // Asked for with no token at all, as a player would, and with what a
    // browser would have sent besides.
    const QByteArray played = askRaw(
        "GET " + path.toUtf8() + " HTTP/1.1\r\nHost: localhost\r\n"
        "Range: bytes=2-5\r\nAuthorization: Bearer " + mToken +
        "\r\nCookie: rb_session=abc\r\n\r\n");
    QCOMPARE(statusOf(played), 206);
    QVERIFY2(played.endsWith("FILM"), played.constData());
    QVERIFY(played.contains("Content-Range: bytes 2-5/10"));

    // Passed on as it came: the same path, and the seek.
    QVERIFY2(asked.startsWith("GET " + path.toUtf8() + " HTTP/1.1\r\n"),
             asked.constData());
    QVERIFY(asked.toLower().contains("range: bytes=2-5"));
    // And this API's own secrets were not passed on with it.
    QVERIFY2(!asked.contains(mToken) && !asked.toLower().contains("cookie"),
             "rclone was handed the API's credentials");

    // A player that leaves in the middle, which is what every seek is and
    // what closing a player is. rclone is still sending when it goes. This
    // took the whole program down once, so what is checked is that there
    // is still a program afterwards.
    {
      QTcpServer endless;
      QVERIFY(endless.listen(QHostAddress::LocalHost, 0));
      rclone.close();
      // The same stream, with a different rclone behind it: one that never
      // stops sending.
      job->setStreamAddress(endless.serverPort(), job->streamPath());
      QObject::connect(&endless, &QTcpServer::newConnection, &endless, [&]() {
        QTcpSocket *peer = endless.nextPendingConnection();
        peer->write("HTTP/1.1 200 OK\r\nContent-Length: 1000000000\r\n"
                    "Connection: close\r\n\r\n");
        auto *more = new QTimer(peer);
        QObject::connect(more, &QTimer::timeout, peer, [peer]() {
          peer->write(QByteArray(256 * 1024, 'x'));
        });
        more->start(5);
      });

      for (int round = 0; round < 3; ++round) {
        QTcpSocket player;
        QVERIFY(openConnection(&player));
        player.write("GET " + path.toUtf8() + " HTTP/1.1\r\nHost: x\r\n\r\n");
        QByteArray seen;
        QVERIFY(waitFor(&player, &seen, "xxxxxxxx"));
        // Gone, without a word, with megabytes still on their way.
        player.abort();
        QTest::qWait(150);
      }
      QCOMPARE(statusOf(ask("GET", "/api/v1/ping", QByteArray())), 200);
    }

    // Another secret, or none, is no stream at all.
    QCOMPARE(statusOf(ask("GET",
                          "/stream/00000000000000000000000000000000/a.mkv",
                          QByteArray())),
             404);
    QCOMPARE(statusOf(ask("GET", "/stream/", QByteArray())), 404);
    QCOMPARE(statusOf(ask("GET", "/stream//" + secret.toUtf8(), QByteArray())),
             404);
    // Read, never written.
    QCOMPARE(statusOf(ask("DELETE", path.toUtf8(), QByteArray())), 405);

    // Stopped, the same address is nothing.
    rclone.close();
    job->stop();
    QCOMPARE(statusOf(ask("GET", path.toUtf8(), QByteArray())), 404);

    // A folder is served whole, and any number at once.
    qputenv("FAKE_RCLONE_SLEEP_MS", "3000");
    const QByteArray folder =
        send("POST", "/api/v1/remotes/fake/stream", mToken,
             "{\"path\":\"films\",\"isDir\":true}");
    qputenv("FAKE_RCLONE_SLEEP_MS", "0");
    QCOMPARE(statusOf(folder), 202);
    const QJsonObject whole = bodyOf(folder).object();
    QVERIFY(whole.value(QStringLiteral("path")).toString().endsWith(
        QLatin1Char('/')));
    RunningJob *served = JobRegistry::instance().find(
        whole.value(QStringLiteral("requestId")).toString());
    QVERIFY(served != nullptr);
    QVERIFY(!served->displayCommand().contains(QStringLiteral("--include")));

    // Nothing is listening where that one's rclone should be, which is what
    // a player sees in the moment before rclone is up: told so, not hung.
    QCOMPARE(statusOf(ask("GET",
                          whole.value(QStringLiteral("path")).toString().toUtf8(),
                          QByteArray())),
             502);
    served->stop();

    // Not without a secret, and not of nothing.
    QCOMPARE(statusOf(send("POST", "/api/v1/remotes/fake/stream", QByteArray(),
                           "{\"path\":\"films/a.mkv\"}")),
             401);
    QCOMPARE(statusOf(send("POST", "/api/v1/remotes/fake/stream", mToken,
                           "{\"path\":\"\"}")),
             409);
  }

  // A stream nobody plays stops itself; one somebody is playing does not,
  // however long it goes.
  void aStreamNobodyPlaysStopsItself() {
    QVERIFY(useFakeRclone(1));
    qputenv("FAKE_RCLONE_SLEEP_MS", "20000"); // would stay up a long time
    ApiServer::setStreamIdleLimitMs(400);

    const auto start = [this](const QByteArray &path) {
      const QByteArray reply = send("POST", "/api/v1/remotes/fake/stream",
                                    mToken, "{\"path\":\"" + path + "\"}");
      return bodyOf(reply).object();
    };

    // Opened by nobody: gone after the limit.
    const QJsonObject left = start("films/left.mkv");
    QCOMPARE(left.value(QStringLiteral("stopsWhenIdleFor")).toInt(), 0); // 400 ms
    RunningJob *leftJob = JobRegistry::instance().find(
        left.value(QStringLiteral("requestId")).toString());
    QVERIFY(leftJob != nullptr && leftJob->isRunning());
    QTRY_VERIFY_WITH_TIMEOUT(!leftJob->isRunning(), 5000);
    QCOMPARE(leftJob->finalStatus(), QStringLiteral("stopped"));

    // Played: kept for as long as the player stays, however quiet it is.
    const QJsonObject played = start("films/played.mkv");
    RunningJob *playedJob = JobRegistry::instance().find(
        played.value(QStringLiteral("requestId")).toString());
    QVERIFY(playedJob != nullptr);

    QTcpServer rclone; // standing in, sending the headers and then nothing
    QVERIFY(rclone.listen(QHostAddress::LocalHost, playedJob->streamPort()));
    QObject::connect(&rclone, &QTcpServer::newConnection, &rclone, [&]() {
      QTcpSocket *peer = rclone.nextPendingConnection();
      peer->write("HTTP/1.1 200 OK\r\nContent-Length: 1000000\r\n\r\nx");
    });

    QTcpSocket player;
    QVERIFY(openConnection(&player));
    player.write("GET " +
                 played.value(QStringLiteral("path")).toString().toUtf8() +
                 " HTTP/1.1\r\nHost: x\r\n\r\n");
    QByteArray seen;
    QVERIFY(waitFor(&player, &seen, "200 OK"));
    QTest::qWait(1500); // well past the limit, paused
    QVERIFY2(playedJob->isRunning(), "a stream being played was stopped");

    // The player goes; the stream follows after the limit.
    player.abort();
    QTRY_VERIFY_WITH_TIMEOUT(!playedJob->isRunning(), 5000);

    qputenv("FAKE_RCLONE_SLEEP_MS", "0");
    ApiServer::setStreamIdleLimitMs(-1);
  }

  void aMountNeedsSomewhereToGo() {
    QVERIFY(useFakeRclone(1));
    for (const char *body :
         {"{\"path\":\"films\"}", "{\"path\":\"films\",\"mountPoint\":\"  \"}"}) {
      QCOMPARE(statusOf(send("POST", "/api/v1/remotes/fake/mount", mToken, body)),
               400);
    }
  }

  // ---- between this machine and a remote -------------------------------------

  // What the page's chooser asks: what is on this machine, as far as the API
  // may look.
  void theMachinesFoldersAreListedAsFarAsItWasGiven() {
    QTemporaryDir given;
    QTemporaryDir other;
    const QString root = givenFolder(&given);

    // Where a chooser starts: the roots.
    const QByteArray top = ask("GET", "/api/v1/local/ls", mToken);
    QCOMPARE(statusOf(top), 200);
    const QJsonObject start = bodyOf(top).object();
    QCOMPARE(start.value(QStringLiteral("restricted")).toBool(), true);
    QCOMPARE(start.value(QStringLiteral("entries")).toArray().size(), 1);

    const QByteArray inside = ask(
        "GET",
        QByteArray("/api/v1/local/ls?path=") + QUrl::toPercentEncoding(root),
        mToken);
    QCOMPARE(statusOf(inside), 200);
    QCOMPARE(bodyOf(inside).object().value(QStringLiteral("entries"))
                 .toArray().at(0).toObject().value(QStringLiteral("name"))
                 .toString(),
             QStringLiteral("films"));

    // And no further.
    QCOMPARE(statusOf(ask("GET",
                          QByteArray("/api/v1/local/ls?path=") +
                              QUrl::toPercentEncoding(other.path()),
                          mToken)),
             403);
    QCOMPARE(statusOf(ask("GET",
                          QByteArray("/api/v1/local/ls?path=") +
                              QUrl::toPercentEncoding(root + "/films/../.."),
                          mToken)),
             403);

    // Like everything else, not without a secret.
    QCOMPARE(statusOf(ask("GET", "/api/v1/local/ls", QByteArray())), 401);
    qunsetenv("RB_LOCAL_ROOTS");
  }

  // An upload is a transfer from then on, and a folder lands under its own
  // name -- which is read off the disk, not taken on the caller's word.
  void anUploadStartsFromWhereItWasAllowedTo() {
    QVERIFY(useFakeRclone(1));
    qputenv("FAKE_RCLONE_SLEEP_MS", "0");
    QTemporaryDir given;
    QTemporaryDir other;
    const QString root = givenFolder(&given);

    const auto body = [](const QString &from) {
      QJsonObject object;
      object.insert(QStringLiteral("from"), from);
      object.insert(QStringLiteral("to"), QStringLiteral("backup"));
      return QJsonDocument(object).toJson(QJsonDocument::Compact);
    };

    const QByteArray folder = send("POST", "/api/v1/remotes/fake/upload",
                                   mToken, body(root + "/films"));
    QCOMPARE(statusOf(folder), 202);
    QCOMPARE(bodyOf(folder).object().value(QStringLiteral("dest")).toString(),
             QStringLiteral("fake:backup/films"));

    const QByteArray file = send("POST", "/api/v1/remotes/fake/upload", mToken,
                                 body(root + "/films/a.mkv"));
    QCOMPARE(statusOf(file), 202);
    QCOMPARE(bodyOf(file).object().value(QStringLiteral("dest")).toString(),
             QStringLiteral("fake:backup"));

    // From somewhere it was not given: refused, and nothing started.
    const int before = RunHistory::count();
    const QByteArray outside = send("POST", "/api/v1/remotes/fake/upload",
                                    mToken, body(other.path()));
    QCOMPARE(statusOf(outside), 403);
    QCOMPARE(bodyOf(outside).object().value(QStringLiteral("code")).toString(),
             QStringLiteral("outside_roots"));

    // And something that is not there is not an upload of nothing.
    QCOMPARE(statusOf(send("POST", "/api/v1/remotes/fake/upload", mToken,
                           body(root + "/films/no-such-file"))),
             400);
    QCOMPARE(RunHistory::count(), before);
    qunsetenv("RB_LOCAL_ROOTS");
  }

  void aDownloadGoesOnlyWhereItWasAllowedTo() {
    QVERIFY(useFakeRclone(1));
    qputenv("FAKE_RCLONE_SLEEP_MS", "0");
    QTemporaryDir given;
    QTemporaryDir other;
    const QString root = givenFolder(&given);

    const auto body = [](const QString &to, bool isDir) {
      QJsonObject object;
      object.insert(QStringLiteral("path"), QStringLiteral("films/2026"));
      object.insert(QStringLiteral("isDir"), isDir);
      object.insert(QStringLiteral("to"), to);
      return QJsonDocument(object).toJson(QJsonDocument::Compact);
    };

    const QByteArray reply = send("POST", "/api/v1/remotes/fake/download",
                                  mToken, body(root + "/films", true));
    QCOMPARE(statusOf(reply), 202);
    const QJsonObject object = bodyOf(reply).object();
    QCOMPARE(object.value(QStringLiteral("source")).toString(),
             QStringLiteral("fake:films/2026"));
    // The folder arrives under its own name inside the place chosen.
    QVERIFY2(object.value(QStringLiteral("dest")).toString().endsWith(
                 QStringLiteral("/films/2026")),
             qPrintable(object.value(QStringLiteral("dest")).toString()));

    QCOMPARE(statusOf(send("POST", "/api/v1/remotes/fake/download", mToken,
                           body(other.path(), true))),
             403);
    // A place to download to has to be a folder that is there.
    QCOMPARE(statusOf(send("POST", "/api/v1/remotes/fake/download", mToken,
                           body(root + "/films/a.mkv", false))),
             400);
    qunsetenv("RB_LOCAL_ROOTS");
  }

  void makingAFolderNeedsASecretAndARealRemote() {
    QVERIFY(useFakeRclone(1));
    const QByteArray body = "{\"path\":\"\",\"name\":\"x\"}";
    QCOMPARE(statusOf(send("POST", "/api/v1/remotes/fake/mkdir", QByteArray(),
                           body)),
             401);
    QCOMPARE(statusOf(send("POST", "/api/v1/remotes/no-such/mkdir", mToken,
                           body)),
             404);
  }

  // The one that was broken in the first version. Hitting the limit cancels
  // the listing, and a cancelled listing says nothing by design -- so the
  // request was never answered and the page sat on "reading..." forever. A
  // real folder of 7,103 files found it; this keeps it found.
  void aFolderBiggerThanTheLimitStillAnswers() {
    QVERIFY2(useFakeRclone(40), "the stand-in rclone did not list a remote");

    const QByteArray reply =
        ask("GET", "/api/v1/remotes/fake/ls?limit=10", mToken);

    QVERIFY2(statusOf(reply) != 0, "the request was never answered at all");
    QCOMPARE(statusOf(reply), 200);

    const QJsonObject object = bodyOf(reply).object();
    QCOMPARE(object.value(QStringLiteral("truncated")).toBool(), true);

    // What came back is what was asked for, not everything and not nothing.
    QVERIFY(object.value(QStringLiteral("entries")).toArray().size() <= 10);
  }

  // An rclone that cannot be run has to be reported, or a folder that will
  // not open shows as an empty one -- which reads as "there is nothing here".
  void aListingThatCannotRunSaysSoRatherThanShowingAnEmptyFolder() {
    QVERIFY(useFakeRclone(3));
    SetRclone(QStringLiteral("no-such-rclone-anywhere"));

    const QByteArray reply = ask("GET", "/api/v1/remotes/fake/ls", mToken);
    QCOMPARE(statusOf(reply), 502);
    QVERIFY2(!bodyOf(reply).object().value(QStringLiteral("error"))
                  .toString().isEmpty(),
             "it failed without saying why");
  }

  // A window is closed by whoever is sitting at it. A page being able to
  // close it from somewhere else would be a surprise, so stopping over the
  // API is something only a daemon agrees to.
  void aWindowCannotBeClosedFromThePage() {
    ApiServer::instance().setShutdownAllowed(false);
    QSignalSpy asked(&ApiServer::instance(), &ApiServer::shutdownRequested);

    const QByteArray reply = ask("POST", "/api/v1/shutdown", mToken);
    QCOMPARE(statusOf(reply), 409);
    QTest::qWait(50);
    QCOMPARE(asked.count(), 0);
  }

  // And a daemon does, but only for somebody signed in -- and it answers
  // before it stops, so the caller hears "accepted" rather than a connection
  // that went quiet.
  void aDaemonCanBeAskedToStop() {
    ApiServer::instance().setShutdownAllowed(true);
    QSignalSpy asked(&ApiServer::instance(), &ApiServer::shutdownRequested);

    QCOMPARE(statusOf(ask("POST", "/api/v1/shutdown", QByteArray())), 401);
    QCOMPARE(asked.count(), 0);

    const QByteArray reply = ask("POST", "/api/v1/shutdown", mToken);
    QCOMPARE(statusOf(reply), 202);
    QCOMPARE(bodyOf(reply).object().value(QStringLiteral("stopping")).toBool(),
             true);

    if (asked.isEmpty()) {
      QVERIFY2(asked.wait(2000), "the request to stop went nowhere");
    }
    QCOMPARE(asked.count(), 1);

    ApiServer::instance().setShutdownAllowed(false);
  }

  // HEAD is GET with the body cut off, because health checks and proxies
  // send it. A front page that answers "method not allowed" to a monitor
  // looks exactly like a front page that is broken.
  void headIsAnsweredLikeGetWithoutTheBody() {
    const QByteArray page = ask("HEAD", "/", QByteArray());
    QCOMPARE(statusOf(page), 200);
    QVERIFY2(page.contains("Content-Length: "), "HEAD gave no length");
    QVERIFY2(!page.contains("<title>"), "HEAD came back with a body");

    // And it is not a way around the token.
    QCOMPARE(statusOf(ask("HEAD", "/api/v1/tasks", QByteArray())), 401);
    QCOMPARE(statusOf(ask("HEAD", "/api/v1/tasks", mToken)), 200);

    // The stream says what it is and stays shut, rather than opening one
    // nobody is coming back for.
    const QByteArray stream = ask("HEAD", "/api/v1/events", mToken);
    QCOMPARE(statusOf(stream), 200);
    QVERIFY(stream.contains("text/event-stream"));
  }

  // The three files are a list, not a directory, so there is nothing for a
  // path to walk out of. Worth a test rather than a comment: this is the
  // shape of bug that hands out any file the daemon can read.
  void nothingElseOnDiskIsReachable() {
    for (const char *target :
         {"/../src/api_server.cpp", "/..%2fapi_server.cpp",
          "/index.html/../../secrets", "/app.js.map", "/favicon.ico"}) {
      const QByteArray reply = ask("GET", target, QByteArray());
      QCOMPARE(statusOf(reply), 404);
      QVERIFY2(!reply.contains("ApiServer"), target);
    }
  }

  // A token is made on first use rather than left empty, because "no token
  // set" must never come out as "no password required".
  void aTokenIsMadeRatherThanLeftEmpty() {
    ApiServer::setToken(QString());
    QVERIFY(ApiServer::token().isEmpty());

    const QString made = ApiServer::ensureToken();
    QVERIFY(!made.isEmpty());
    QCOMPARE(ApiServer::token(), made);

    // And asking again keeps the one already in use, or every restart would
    // invalidate whatever a client had been told.
    QCOMPARE(ApiServer::ensureToken(), made);

    ApiServer::setToken(QString::fromUtf8(mToken));
  }

  // With no token stored at all, the answer is still no. An empty expected
  // token compared against an empty offered one must not read as a match.
  void anEmptyStoredTokenLetsNobodyIn() {
    ApiServer::setToken(QString());

    QCOMPARE(statusOf(ask("GET", "/api/v1/system", QByteArray())), 401);
    QCOMPARE(statusOf(ask("GET", "/api/v1/system", "Bearer ")), 401);

    ApiServer::setToken(QString::fromUtf8(mToken));
  }
};

QTEST_MAIN(TestApiServer)
#include "test_api_server.moc"
