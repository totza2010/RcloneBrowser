#include "api_server.h"
#include "app_core.h"
#include "debug_log.h"
#include "http_message.h"
#include "job_options.h"
#include "job_queue.h"
#include "job_registry.h"
#include "list_of_job_options.h"
#include "remote_registry.h"
#include "run_history.h"
#include "running_job.h"
#include "schedule.h"
#include "scheduler_store.h"
#include "utils.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUuid>

namespace {

QByteArray toJson(const QJsonObject &object) {
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray toJson(const QJsonArray &array) {
  return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

QByteArray problem(const QString &message) {
  QJsonObject object;
  object.insert(QStringLiteral("error"), message);
  return toJson(object);
}

QJsonObject describe(const JobOptions &task) {
  QJsonObject object;
  object.insert(QStringLiteral("id"), task.uniqueId.toString());
  object.insert(QStringLiteral("name"), task.description);
  object.insert(QStringLiteral("source"), task.source);
  object.insert(QStringLiteral("dest"), task.dest);
  object.insert(QStringLiteral("isDownload"),
                task.jobType == JobOptions::JobType::Download);
  return object;
}

QJsonObject describe(const RunningJob &job) {
  QJsonObject object;
  object.insert(QStringLiteral("requestId"), job.requestId());
  object.insert(QStringLiteral("taskId"), job.taskId());
  object.insert(QStringLiteral("kind"), JobKindToString(job.kind()));
  object.insert(QStringLiteral("startedBy"), job.transferMode());
  object.insert(QStringLiteral("info"), job.description().info);
  object.insert(QStringLiteral("source"), job.description().source);
  object.insert(QStringLiteral("dest"), job.description().dest);
  object.insert(QStringLiteral("running"), job.isRunning());
  object.insert(QStringLiteral("state"), job.finalStatus());
  object.insert(QStringLiteral("startedAt"),
                job.startedAt().toString(Qt::ISODate));

  // SECURITY: the command carries backend tokens and the remote-control
  // password. displayCommand() is the redacted form; the raw arguments must
  // never leave this process (docs/ARCHITECTURE.md section 5).
  object.insert(QStringLiteral("command"),
                job.displayCommand().join(QLatin1Char(' ')));

  const JobStats &stats = job.stats();
  if (stats.valid) {
    QJsonObject figures;
    figures.insert(QStringLiteral("bytes"), double(stats.bytes));
    figures.insert(QStringLiteral("totalBytes"), double(stats.totalBytes));
    figures.insert(QStringLiteral("transfers"), double(stats.transfers));
    figures.insert(QStringLiteral("errors"), double(stats.errors));
    figures.insert(QStringLiteral("elapsedSeconds"), stats.elapsedSeconds);
    object.insert(QStringLiteral("stats"), figures);
  }
  return object;
}

QJsonObject describe(const Schedule &schedule) {
  QJsonObject object;
  object.insert(QStringLiteral("id"), schedule.id);
  object.insert(QStringLiteral("name"), schedule.name);
  object.insert(QStringLiteral("taskId"), schedule.taskId);
  object.insert(QStringLiteral("active"), schedule.active);
  object.insert(QStringLiteral("executionMode"), schedule.executionMode);
  object.insert(QStringLiteral("lastRun"), schedule.lastRun);
  object.insert(QStringLiteral("lastStatus"), schedule.lastStatus);

  const QDateTime next = schedule.nextRun(QDateTime::currentDateTime());
  object.insert(QStringLiteral("nextRun"),
                next.isValid() ? next.toString(Qt::ISODate) : QString());
  return object;
}

} // namespace

ApiServer &ApiServer::instance() {
  static ApiServer server;
  return server;
}

ApiServer::ApiServer(QObject *parent) : QObject(parent) {}

QString ApiServer::token() {
  return GetSettings()->value("Settings/apiToken").toString();
}

void ApiServer::setToken(const QString &value) {
  GetSettings()->setValue("Settings/apiToken", value);
}

QString ApiServer::ensureToken() {
  QString existing = token();
  if (existing.isEmpty()) {
    // Made rather than left empty. "No token set" must never become "no
    // password required": that is how an API ends up open on a machine
    // somebody else can reach.
    existing = QUuid::createUuid().toString(QUuid::WithoutBraces);
    setToken(existing);
    qCDebug(rbApp) << "made an API token; it is in the settings";
  }
  return existing;
}

bool ApiServer::isListening() const {
  return mServer != nullptr && mServer->isListening();
}

quint16 ApiServer::port() const {
  return mServer != nullptr ? mServer->serverPort() : 0;
}

bool ApiServer::start(const QString &address, quint16 port, QString *error) {
  if (isListening()) {
    return true;
  }

  ensureToken();

  if (mServer == nullptr) {
    mServer = new QTcpServer(this);
    QObject::connect(mServer, &QTcpServer::newConnection, this,
                     [this]() { accept(); });
  }

  const QHostAddress host(address.isEmpty() ? QStringLiteral("127.0.0.1")
                                            : address);
  if (!mServer->listen(host, port)) {
    if (error != nullptr) {
      *error = mServer->errorString();
    }
    // Said rather than swallowed: a daemon whose API quietly did not open is
    // worse than one that refused to start, because nothing shows it.
    qCDebug(rbApp) << "the API could not listen on" << address << port << ":"
                   << mServer->errorString();
    return false;
  }

  qCDebug(rbApp) << "API listening on" << address << mServer->serverPort();
  emit started(mServer->serverPort());
  return true;
}

void ApiServer::stop() {
  if (mServer == nullptr) {
    return;
  }
  mServer->close();
  qCDebug(rbApp) << "API stopped";
  emit stopped();
}

void ApiServer::accept() {
  while (QTcpSocket *socket = mServer->nextPendingConnection()) {
    // One buffer per connection, kept on the socket itself, so a slow client
    // cannot get its half-sent request mixed with anybody else's.
    socket->setProperty("buffer", QByteArray());

    QObject::connect(socket, &QTcpSocket::readyRead, this,
                     [this, socket]() { read(socket); });
    QObject::connect(socket, &QTcpSocket::disconnected, socket,
                     &QObject::deleteLater);
  }
}

void ApiServer::read(QTcpSocket *socket) {
  QByteArray buffer = socket->property("buffer").toByteArray();
  buffer += socket->readAll();

  for (;;) {
    HttpRequest request;
    const HttpParseResult result = ParseHttpRequest(buffer, &request);

    if (result == HttpParseResult::Incomplete) {
      break;
    }
    if (result == HttpParseResult::Malformed) {
      const QString why = QStringLiteral("that is not a request I can read");
      socket->write(FormatHttpResponse(400, problem(why)));
      socket->disconnectFromHost();
      return;
    }
    answer(socket, request);
  }

  socket->setProperty("buffer", buffer);
}

void ApiServer::answer(QTcpSocket *socket, const HttpRequest &request) {
  // Checked before the path is even looked at. An endpoint that answers
  // without a token is an endpoint somebody forgot to protect, and the way
  // not to forget is for the check not to be per-endpoint at all.
  //
  // /ping is the one exception, and it says nothing: it exists so a client
  // can tell "the server is there but my token is wrong" from "there is no
  // server", which are different problems with different fixes.
  if (request.path != QLatin1String("/api/v1/ping")) {
    const QString expected = token();
    const QString offered = request.header(QStringLiteral("authorization"));

    if (expected.isEmpty() ||
        offered != QStringLiteral("Bearer ") + expected) {
      qCDebug(rbApp) << "refused an API request without a valid token"
                     << request.method << request.path;
      const QString why = QStringLiteral("a valid token is required");
      socket->write(FormatHttpResponse(401, problem(why)));
      return;
    }
  }

  QByteArray body;
  const int status = route(request, &body);
  socket->write(FormatHttpResponse(status, body));
}

int ApiServer::route(const HttpRequest &request, QByteArray *body) {
  const QString &path = request.path;

  // Read before write, as docs/API.md section 11 says: a dashboard has to be
  // able to see before it needs to command, and the commands can be added
  // later without changing any of this.
  if (request.method != QLatin1String("GET")) {
    *body = problem(QStringLiteral("only GET is answered so far"));
    return 405;
  }

  if (path == QLatin1String("/api/v1/ping")) {
    QJsonObject object;
    object.insert(QStringLiteral("ok"), true);
    *body = toJson(object);
    return 200;
  }

  if (path == QLatin1String("/api/v1/system")) {
    QJsonObject object;
    object.insert(QStringLiteral("rcloneVersion"),
                  GetSettings()->value("Settings/rcloneVersion").toString());
    object.insert(QStringLiteral("runningJobs"),
                  JobRegistry::instance().runningCount());
    object.insert(QStringLiteral("queued"), JobQueue::instance().count());
    object.insert(QStringLiteral("queueRunning"),
                  JobQueue::instance().isRunning());
    object.insert(QStringLiteral("schedulerRunning"),
                  SchedulerStore::instance().isRunning());
    object.insert(QStringLiteral("coreStarted"),
                  AppCore::instance().isStarted());
    *body = toJson(object);
    return 200;
  }

  if (path == QLatin1String("/api/v1/remotes")) {
    QJsonArray array;
    for (const Remote &remote : RemoteRegistry::instance().remotes()) {
      QJsonObject object;
      object.insert(QStringLiteral("name"), remote.name);
      object.insert(QStringLiteral("type"), remote.type);
      array.append(object);
    }
    *body = toJson(array);
    return 200;
  }

  if (path == QLatin1String("/api/v1/tasks")) {
    QJsonArray array;
    for (const JobOptions *task : ListOfJobOptions::getInstance()->getTasks()) {
      array.append(describe(*task));
    }
    *body = toJson(array);
    return 200;
  }

  if (path.startsWith(QLatin1String("/api/v1/tasks/"))) {
    const QString id = path.mid(QStringLiteral("/api/v1/tasks/").size());
    const JobOptions *task = ListOfJobOptions::getInstance()->find(id);
    if (task == nullptr) {
      *body = problem(QStringLiteral("no such task"));
      return 404;
    }
    *body = toJson(describe(*task));
    return 200;
  }

  if (path == QLatin1String("/api/v1/queue")) {
    QJsonArray array;
    for (const QueueEntry &entry : JobQueue::instance().entries()) {
      QJsonObject object;
      object.insert(QStringLiteral("requestId"), entry.requestId);
      object.insert(QStringLiteral("taskId"), entry.taskId);
      object.insert(QStringLiteral("dryRun"), entry.dryRun);
      object.insert(QStringLiteral("running"),
                    entry.requestId ==
                        JobQueue::instance().runningRequestId());
      array.append(object);
    }
    *body = toJson(array);
    return 200;
  }

  if (path == QLatin1String("/api/v1/jobs")) {
    QJsonArray array;
    for (const RunningJob *job : JobRegistry::instance().jobs()) {
      array.append(describe(*job));
    }
    *body = toJson(array);
    return 200;
  }

  if (path == QLatin1String("/api/v1/schedules")) {
    QJsonArray array;
    for (const Schedule &schedule : SchedulerStore::instance().schedules()) {
      array.append(describe(schedule));
    }
    *body = toJson(array);
    return 200;
  }

  if (path == QLatin1String("/api/v1/history")) {
    bool readable = true;
    const int asked = request.queryValue(QStringLiteral("limit")).toInt(&readable);
    const int limit = readable && asked > 0 ? qMin(asked, 500) : 50;

    QJsonArray array;
    for (const JobRunRecord &row : RunHistory::recent(limit)) {
      QJsonObject object;
      object.insert(QStringLiteral("requestId"), row.requestId);
      object.insert(QStringLiteral("taskName"), row.taskName);
      object.insert(QStringLiteral("kind"), row.kind);
      object.insert(QStringLiteral("startedBy"), row.transferMode);
      object.insert(QStringLiteral("state"), row.state);
      object.insert(QStringLiteral("exitCode"), row.exitCode);
      object.insert(QStringLiteral("bytes"), double(row.bytes));
      array.append(object);
    }
    *body = toJson(array);
    return 200;
  }

  *body = problem(QStringLiteral("no such endpoint"));
  return 404;
}
