#include "api_auth.h"
#include "api_server.h"
#include "database.h"
#include "job_options.h"
#include "job_queue.h"
#include "list_of_job_options.h"
#include "remote_registry.h"
#include "run_history.h"
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

  // Commands exist now, but making and deleting tasks is not one of them: a
  // task carries its own rclone flags, so writing one is writing a command
  // line, and that would turn a stolen token into "run any program here".
  // Refused in as many words rather than quietly ignored.
  void writingATaskIsRefusedRatherThanIgnored() {
    QCOMPARE(statusOf(ask("POST", "/api/v1/tasks", mToken)), 405);
    QCOMPARE(statusOf(ask("DELETE", "/api/v1/tasks/x", mToken)), 405);

    // And the 405 says which of the two the caller got wrong -- the endpoint
    // or the method -- rather than leaving them to guess.
    QVERIFY(ask("POST", "/api/v1/tasks", mToken).contains("read"));
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
