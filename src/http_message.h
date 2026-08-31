#pragma once

// Core (L0): reading and writing HTTP/1.1 messages.
//
// Qt's HttpServer module is not part of the installations this builds
// against, and pulling in a web framework for an API this small would be a
// dependency every packager has to satisfy -- including the Alpine image the
// daemon is meant to live in. QTcpServer plus this is enough: the API speaks
// JSON over a handful of routes and needs no more of HTTP than that.
//
// Kept apart from the server so the fiddly half -- where a header ends, what
// counts as a complete request, what a malformed one is -- can be tested
// without opening a socket. See docs/API.md S11.

#include <QByteArray>
#include <QHash>
#include <QString>

struct HttpRequest {
  QString method;
  QString path;  // without the query
  QString query; // raw, after the '?'
  QHash<QString, QString> headers; // names lowercased
  QByteArray body;

  bool isValid() const { return !method.isEmpty(); }

  // Headers are matched without regard to case, because a client chooses its
  // own spelling and every one of them is correct.
  QString header(const QString &name) const {
    return headers.value(name.toLower());
  }

  // One value out of the query string, empty when absent. Percent-decoding
  // included: a path like "My Films/2026" arrives as "My%20Films%2F2026".
  QString queryValue(const QString &name) const;
};

// How much of a request is in the buffer.
enum class HttpParseResult {
  Incomplete, // keep reading
  Complete,
  Malformed, // give up on this connection
};

// Reads one request from the front of `buffer`. On Complete the bytes it
// consumed are removed from the buffer, so a client that pipelines requests
// down one connection is read correctly rather than having the second one
// treated as the body of the first.
HttpParseResult ParseHttpRequest(QByteArray &buffer, HttpRequest *request);

// A complete response, ready to write. `contentType` defaults to JSON
// because everything this API returns is JSON; the exception is the empty
// body of a 204.
QByteArray FormatHttpResponse(int status, const QByteArray &body,
                              const QByteArray &contentType = "application/json",
                              const QHash<QString, QString> &extraHeaders = {});

// The words that go with a status code, for the status line.
QByteArray HttpReasonPhrase(int status);
