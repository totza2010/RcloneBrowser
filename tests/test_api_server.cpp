#include "api_auth.h"
#include "api_server.h"
#include "database.h"
#include "job_options.h"
#include "job_queue.h"
#include "list_of_job_options.h"
#include "remote_registry.h"
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
