#include "http_message.h"

#include <QTest>

// Reading HTTP by hand, because the API is small and a web framework is a
// dependency every packager has to satisfy. The fiddly half is here, away
// from sockets: where a header ends, what counts as complete, and what is
// malformed enough to hang up on.
//
// See docs/API.md S11.
class TestHttpMessage : public QObject {
  Q_OBJECT

private:
  static QByteArray get(const QByteArray &target,
                        const QByteArray &extra = QByteArray()) {
    return "GET " + target + " HTTP/1.1\r\nHost: localhost\r\n" + extra +
           "\r\n";
  }

private slots:
  void readsAnOrdinaryRequest() {
    QByteArray buffer = get("/api/v1/tasks");
    HttpRequest request;

    QCOMPARE(ParseHttpRequest(buffer, &request), HttpParseResult::Complete);
    QCOMPARE(request.method, QStringLiteral("GET"));
    QCOMPARE(request.path, QStringLiteral("/api/v1/tasks"));
    QVERIFY(request.body.isEmpty());
    QVERIFY2(buffer.isEmpty(), "the request was not taken out of the buffer");
  }

  // Headers are the client's to spell. Matching them case-sensitively would
  // reject a perfectly correct request from something that writes
  // "authorization" in lower case.
  void headerNamesAreMatchedWhateverTheirCase() {
    QByteArray buffer = get("/api/v1/tasks", "AuThOrIzAtIoN: Bearer abc\r\n");
    HttpRequest request;

    QCOMPARE(ParseHttpRequest(buffer, &request), HttpParseResult::Complete);
    QCOMPARE(request.header(QStringLiteral("Authorization")),
             QStringLiteral("Bearer abc"));
    QCOMPARE(request.header(QStringLiteral("authorization")),
             QStringLiteral("Bearer abc"));
    QVERIFY(request.header(QStringLiteral("nothing-here")).isEmpty());
  }

  // A task id is "{...}" and a remote path has spaces and slashes in it.
  // Decoded once, in the parser, so no route has to remember.
  void theTargetIsDecodedOnce() {
    QByteArray buffer =
        get("/api/v1/tasks/%7Babc-123%7D?path=My%20Films%2F2026&dryRun=true");
    HttpRequest request;

    QCOMPARE(ParseHttpRequest(buffer, &request), HttpParseResult::Complete);
    QCOMPARE(request.path, QStringLiteral("/api/v1/tasks/{abc-123}"));
    QCOMPARE(request.queryValue(QStringLiteral("path")),
             QStringLiteral("My Films/2026"));
    QCOMPARE(request.queryValue(QStringLiteral("dryRun")),
             QStringLiteral("true"));
    QVERIFY(request.queryValue(QStringLiteral("absent")).isEmpty());
  }

  void aBodyIsReadWhenItHasAllArrived() {
    const QByteArray body = "{\"dryRun\":true}";
    QByteArray buffer = "POST /api/v1/tasks/x/run HTTP/1.1\r\n"
                        "Content-Length: " +
                        QByteArray::number(body.size()) +
                        "\r\n"
                        "\r\n" +
                        body;
    HttpRequest request;

    QCOMPARE(ParseHttpRequest(buffer, &request), HttpParseResult::Complete);
    QCOMPARE(request.method, QStringLiteral("POST"));
    QCOMPARE(request.body, body);
  }

  // The case that matters most: a request arriving in pieces, which is what
  // a socket actually delivers. Treating a partial one as complete would
  // answer half a question.
  void anIncompleteRequestIsNotAnswered() {
    HttpRequest request;

    QByteArray headersOnly = "GET /api/v1/tasks HTTP/1.1\r\nHost: x\r\n";
    QCOMPARE(ParseHttpRequest(headersOnly, &request),
             HttpParseResult::Incomplete);

    QByteArray shortBody = "POST /x HTTP/1.1\r\nContent-Length: 10\r\n\r\nabc";
    QCOMPARE(ParseHttpRequest(shortBody, &request),
             HttpParseResult::Incomplete);
    QVERIFY2(shortBody.endsWith("abc"),
             "an incomplete request was taken out of the buffer");
  }

  // A client may send the next request without waiting for this answer.
  // Consuming more than one request's worth would read the second as the
  // body of the first.
  void onlyOneRequestIsTakenAtATime() {
    QByteArray buffer = get("/one") + get("/two");
    HttpRequest first;
    HttpRequest second;

    QCOMPARE(ParseHttpRequest(buffer, &first), HttpParseResult::Complete);
    QCOMPARE(first.path, QStringLiteral("/one"));

    QCOMPARE(ParseHttpRequest(buffer, &second), HttpParseResult::Complete);
    QCOMPARE(second.path, QStringLiteral("/two"));
    QVERIFY(buffer.isEmpty());
  }

  void nonsenseIsRefusedRatherThanGuessedAt() {
    HttpRequest request;

    QByteArray noVersion = "GET /x\r\n\r\n";
    QCOMPARE(ParseHttpRequest(noVersion, &request), HttpParseResult::Malformed);

    QByteArray headerWithNoColon = "GET /x HTTP/1.1\r\nrubbish\r\n\r\n";
    QCOMPARE(ParseHttpRequest(headerWithNoColon, &request),
             HttpParseResult::Malformed);

    QByteArray lengthNotANumber =
        "POST /x HTTP/1.1\r\nContent-Length: soon\r\n\r\n";
    QCOMPARE(ParseHttpRequest(lengthNotANumber, &request),
             HttpParseResult::Malformed);
  }

  // Bounded on purpose: a connection that sends header bytes for ever must
  // not be able to grow the buffer until the process dies.
  void anEndlessHeaderIsGivenUpOn() {
    HttpRequest request;
    QByteArray flood = "GET /x HTTP/1.1\r\nX: " + QByteArray(64 * 1024, 'a');
    QCOMPARE(ParseHttpRequest(flood, &request), HttpParseResult::Malformed);
  }

  void aHugeBodyIsRefusedRatherThanBuffered() {
    HttpRequest request;
    QByteArray huge = "POST /x HTTP/1.1\r\nContent-Length: 99999999\r\n\r\n";
    QCOMPARE(ParseHttpRequest(huge, &request), HttpParseResult::Malformed);
  }

  void aResponseSaysWhatItIsAndHowLong() {
    const QByteArray body = "{\"ok\":true}";
    const QByteArray out = FormatHttpResponse(200, body);

    QVERIFY(out.startsWith("HTTP/1.1 200 OK\r\n"));
    QVERIFY(out.contains("Content-Type: application/json\r\n"));
    QVERIFY(out.contains("Content-Length: " + QByteArray::number(body.size())));
    QVERIFY(out.endsWith(body));

    // A browser told nothing about the type of a JSON body can be talked
    // into running it, and this API answers requests carrying a token.
    QVERIFY(out.contains("X-Content-Type-Options: nosniff"));
    QVERIFY(out.contains("Cache-Control: no-store"));
  }

  void everyStatusItUsesHasWords() {
    for (int status : {200, 201, 204, 400, 401, 403, 404, 405, 409, 413, 500,
                       503}) {
      QVERIFY2(HttpReasonPhrase(status) != QByteArray("Unknown"),
               qPrintable(QString::number(status)));
    }
  }
};

QTEST_APPLESS_MAIN(TestHttpMessage)
#include "test_http_message.moc"
