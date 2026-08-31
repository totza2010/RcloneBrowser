#include "http_message.h"

#include <QDateTime>
#include <QUrl>

namespace {

// A request line and headers longer than this is not a request anybody
// meant to send. Bounded so that a connection sending an endless stream of
// header bytes cannot grow the buffer until the process dies.
constexpr int kMaxHeaderBytes = 16 * 1024;

// The API takes small JSON bodies. Anything larger is refused rather than
// buffered.
constexpr int kMaxBodyBytes = 1024 * 1024;

} // namespace

QString HttpRequest::queryValue(const QString &name) const {
  for (const QString &pair : query.split(QLatin1Char('&'), Qt::SkipEmptyParts)) {
    const int equals = pair.indexOf(QLatin1Char('='));
    const QString key = equals < 0 ? pair : pair.left(equals);
    if (QUrl::fromPercentEncoding(key.toUtf8()) != name) {
      continue;
    }
    return equals < 0
               ? QString()
               : QUrl::fromPercentEncoding(pair.mid(equals + 1).toUtf8());
  }
  return QString();
}

HttpParseResult ParseHttpRequest(QByteArray &buffer, HttpRequest *request) {
  const int headerEnd = buffer.indexOf("\r\n\r\n");
  if (headerEnd < 0) {
    return buffer.size() > kMaxHeaderBytes ? HttpParseResult::Malformed
                                           : HttpParseResult::Incomplete;
  }
  if (headerEnd > kMaxHeaderBytes) {
    return HttpParseResult::Malformed;
  }

  const QByteArray head = buffer.left(headerEnd);
  const QList<QByteArray> lines = head.split('\n');
  if (lines.isEmpty()) {
    return HttpParseResult::Malformed;
  }

  // "GET /api/v1/tasks?x=1 HTTP/1.1"
  const QList<QByteArray> start =
      QByteArray(lines.first()).trimmed().split(' ');
  if (start.size() < 3) {
    return HttpParseResult::Malformed;
  }

  HttpRequest parsed;
  parsed.method = QString::fromLatin1(start.at(0)).toUpper();

  const QString target = QString::fromLatin1(start.at(1));
  const int question = target.indexOf(QLatin1Char('?'));
  if (question < 0) {
    parsed.path = target;
  } else {
    parsed.path = target.left(question);
    parsed.query = target.mid(question + 1);
  }

  // Decoded once, here, so no route has to remember to do it. A task id in a
  // path arrives as "%7B...%7D" because braces are not path characters.
  parsed.path = QUrl::fromPercentEncoding(parsed.path.toUtf8());

  for (int i = 1; i < lines.size(); ++i) {
    const QByteArray line = QByteArray(lines.at(i)).trimmed();
    if (line.isEmpty()) {
      continue;
    }
    const int colon = line.indexOf(':');
    if (colon <= 0) {
      return HttpParseResult::Malformed;
    }
    parsed.headers.insert(
        QString::fromLatin1(line.left(colon)).trimmed().toLower(),
        QString::fromUtf8(line.mid(colon + 1)).trimmed());
  }

  bool lengthReadable = true;
  const QString lengthText = parsed.header(QStringLiteral("content-length"));
  const int length = lengthText.isEmpty()
                         ? 0
                         : lengthText.toInt(&lengthReadable);
  if (!lengthReadable || length < 0 || length > kMaxBodyBytes) {
    return HttpParseResult::Malformed;
  }

  const int bodyStart = headerEnd + 4;
  if (buffer.size() - bodyStart < length) {
    return HttpParseResult::Incomplete;
  }

  parsed.body = buffer.mid(bodyStart, length);

  // Only what this request used. A client is allowed to send the next request
  // down the same connection without waiting for the answer to this one.
  buffer.remove(0, bodyStart + length);

  *request = parsed;
  return HttpParseResult::Complete;
}

QByteArray HttpReasonPhrase(int status) {
  switch (status) {
  case 200:
    return "OK";
  case 201:
    return "Created";
  case 204:
    return "No Content";
  case 400:
    return "Bad Request";
  case 401:
    return "Unauthorized";
  case 403:
    return "Forbidden";
  case 404:
    return "Not Found";
  case 405:
    return "Method Not Allowed";
  case 409:
    return "Conflict";
  case 413:
    return "Payload Too Large";
  case 500:
    return "Internal Server Error";
  case 503:
    return "Service Unavailable";
  }
  return "Unknown";
}

QByteArray FormatHttpResponse(int status, const QByteArray &body,
                              const QByteArray &contentType,
                              const QHash<QString, QString> &extraHeaders) {
  QByteArray out;
  out.reserve(body.size() + 256);

  out += "HTTP/1.1 " + QByteArray::number(status) + " " +
         HttpReasonPhrase(status) + "\r\n";
  out += "Content-Type: " + contentType + "\r\n";
  out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";

  // Said explicitly rather than left to a default. A browser that guesses the
  // type of a JSON body it was told nothing about can be talked into running
  // it, and this API answers requests that carry a token.
  out += "X-Content-Type-Options: nosniff\r\n";
  out += "Cache-Control: no-store\r\n";
  out += "Date: " +
         QDateTime::currentDateTimeUtc()
             .toString(QStringLiteral("ddd, dd MMM yyyy hh:mm:ss 'GMT'"))
             .toLatin1() +
         "\r\n";

  for (auto it = extraHeaders.constBegin(); it != extraHeaders.constEnd();
       ++it) {
    out += it.key().toLatin1() + ": " + it.value().toUtf8() + "\r\n";
  }

  out += "\r\n";
  out += body;
  return out;
}
