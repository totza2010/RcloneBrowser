#include "api_server.h"
#include "database.h"
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
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

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
  QByteArray collect(QTcpSocket *socket) {
    QByteArray answer;
    QDeadlineTimer deadline(5000);

    while (!deadline.hasExpired()) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
      answer += socket->readAll();

      const int headerEnd = answer.indexOf("\r\n\r\n");
      if (headerEnd < 0) {
        continue;
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
  QByteArray askRaw(const QByteArray &request) {
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
    return collect(&socket);
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
    return askRaw(request);
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

  // Read before write. Until the commands exist, saying so is better than
  // pretending a POST did something.
  void writingIsRefusedRatherThanIgnored() {
    QCOMPARE(statusOf(ask("POST", "/api/v1/tasks", mToken)), 405);
    QCOMPARE(statusOf(ask("DELETE", "/api/v1/tasks/x", mToken)), 405);
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
