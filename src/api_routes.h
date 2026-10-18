#pragma once

// Orchestration (L1): every route the API answers, as a list.
//
// The routes themselves are written as code in api_server.cpp -- a sequence
// of "if this method and this path" -- because each one does something
// different. That is fine for answering and useless for describing: nothing
// could be asked "what routes are there", so the only documentation was a
// table in a Markdown file, written by hand, true on the day it was written.
//
// This is that table, in the program. The OpenAPI description the API
// serves (GET /api/v1/openapi.json) and the page's API tab are made from it,
// so other programs and people read the same thing.
//
// A list beside the code can still drift from it, so the drift is checked
// rather than hoped against: every request the server receives is looked up
// here (Match), and one that is not here is noted (Unlisted). The API
// tests send every route they test and then require that nothing they sent
// was unlisted, apart from the requests that are deliberately nonsense. A
// route added to the server and not here fails the tests.
//
// See docs/WEB-API-PLAN.md section A3.

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace ApiRoutes {

struct Route {
  QString method;  // "GET", "POST", "PUT", "DELETE"
  QString path;    // "/api/v1/tasks/{id}" -- {name} is one segment
  QString tag;     // the group it is listed under
  QString summary; // one line
  QString about;   // what is worth knowing beyond the line
  bool open = false; // answered without a token or a session
  // "name: what it is; name: what it is" -- the query string
  QString query;
  // An example body, as JSON text; empty for none.
  QString body;
  // "200: what it means; 404: what it means"
  QString answers;
};

const QList<Route> &All();

// The route a request is for, or nullptr. HEAD is matched as GET, and a path
// with a literal segment ("/tasks/defaults") is preferred to one with a
// placeholder in the same place ("/tasks/{id}").
const Route *Match(const QString &method, const QString &path);

// The whole list, as OpenAPI 3.1.
QJsonObject OpenApi(const QString &version);

// Requests the server received that are on no route, as "METHOD /path".
// Kept so the tests can say a route is missing from the list. See above.
void NoteUnlisted(const QString &method, const QString &path);
QStringList Unlisted();

} // namespace ApiRoutes
