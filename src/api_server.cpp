#include "api_server.h"
#include "api_auth.h"
#include "api_routes.h"
#include "app_core.h"
#include "directory_listing.h"
#include "file_reader.h"
#include "job_json.h"
#include "backup_plan.h"
#include "folder_watches.h"
#include "webhooks.h"
#include "extra_flags.h"
#include "debug_log.h"
#include "http_message.h"
#include "job_options.h"
#include "job_queue.h"
#include "job_registry.h"
#include "list_of_job_options.h"
#include "local_files.h"
#include "remote_ops.h"
#include "rclone_config.h"
#include "rclone_flags.h"
#include "remote_registry.h"
#include "run_history.h"
#include "running_job.h"
#include "schedule.h"
#include "schedule_json.h"
#include "scheduler_store.h"
#include "task_json.h"
#include "task_builder.h"
#include "utils.h"

#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QStorageInfo>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QHostAddress>
#include <QSharedPointer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUuid>

// The resource is compiled into rbcore, and a static library's resources are
// not registered unless something asks for them. Left out, the daemon serves
// a blank page and nothing says why.
static void InitWebUiResources() {
  Q_INIT_RESOURCE(webui);
  Q_INIT_RESOURCE(webui_i18n);
}

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

// The whole task, in words. See task_json.h.
QJsonObject describe(const JobOptions &task) { return TaskJson::Write(task); }

// Whether the end of a task that is on this machine is somewhere the API may
// use: what an upload sends from, where a download arrives. A task is a
// transfer waiting to happen, so the line that holds for a transfer started
// now has to hold for one saved for later -- otherwise saving it and running
// it would be a way round.
bool localEndAllowed(const JobOptions &task) {
  const QString local =
      task.jobType == JobOptions::Download ? task.dest : task.source;
  return !LocalFiles::Allowed(local).isEmpty();
}

// Why a task may not be saved as it stands, as far as its two ends go.
struct TaskRefusal {
  int status = 0; // 0 when there is nothing wrong
  QString code;
  QString why;
};

// The ends of a task, held to what is actually there.
//
// A path field is somewhere anything can be typed, and what was being saved
// was whatever was typed: a remote that does not exist, a file that is not
// there. Such a task sits in the list looking ordinary until it runs. So the
// remote has to be one of the remotes, and what an upload sends has to
// exist. (Where a download lands need not: rclone makes it.)
//
// `before` is the task as it was, when one is being changed. An end that
// was not touched is not judged again -- otherwise a task whose remote has
// since been removed could not even be renamed, or have that end put right
// one field at a time.
TaskRefusal endsProblem(const JobOptions &task, const JobOptions *before) {
  const bool download = task.jobType == JobOptions::Download;
  const QString &far = download ? task.source : task.dest;
  const QString &near = download ? task.dest : task.source;
  const bool sameKind = before != nullptr && before->jobType == task.jobType;
  const bool farChanged =
      !sameKind || (download ? before->source : before->dest) != far;
  const bool nearChanged =
      !sameKind || (download ? before->dest : before->source) != near;

  if (farChanged) {
    const int colon = far.indexOf(QLatin1Char(':'));
    const QString name = colon > 0 ? far.left(colon) : QString();
    if (name.isEmpty() || RemoteRegistry::instance().find(name) == nullptr) {
      return {400, QStringLiteral("no_such_remote"),
              QStringLiteral("\"%1\" does not begin with a remote there is; "
                             "it has to be written remote:path")
                  .arg(far)};
    }
  }
  if (!localEndAllowed(task)) {
    return {403, QStringLiteral("outside_roots"),
            QStringLiteral(
                "that is not a folder on this machine the API may use")};
  }
  if (nearChanged && !download && !QFileInfo::exists(near)) {
    return {400, QStringLiteral("no_such_path"),
            QStringLiteral("there is no \"%1\" on the machine the program "
                           "runs on")
                .arg(near)};
  }
  return {};
}

// What is wrong with a task's extra flags, or nothing. See extra_flags.h.
QString flagsProblem(const QString &extra) {
  RcloneFlagRegistry::instance().ensureLoaded();
  return ExtraFlags::Check(extra, RcloneFlagRegistry::instance().flags(),
                           LocalFiles::IsRestricted());
}

qint64 gStreamIdleOverrideMs = -1;

QJsonObject describe(const RunningJob &job) {
  return JobJson::Write(job, int(ApiServer::streamIdleLimitMs() / 1000));
}

// A webhook as it may be shown: everything but the secret.
QJsonObject describeHook(const Webhook &hook) {
  QJsonObject object;
  object.insert(QStringLiteral("id"), hook.id);
  object.insert(QStringLiteral("name"), hook.name);
  object.insert(QStringLiteral("url"), hook.url);
  object.insert(QStringLiteral("format"), hook.format);
  object.insert(QStringLiteral("failuresOnly"), hook.failuresOnly);
  object.insert(QStringLiteral("everyJob"), hook.everyJob);
  object.insert(QStringLiteral("hasSecret"), !hook.secret.isEmpty());
  object.insert(QStringLiteral("lastResult"), hook.lastResult);
  object.insert(QStringLiteral("lastAt"),
                JobJson::Stamp(hook.lastAt));
  return object;
}

Webhook hookFrom(const QJsonObject &given) {
  Webhook hook;
  hook.name = given.value(QStringLiteral("name")).toString();
  hook.url = given.value(QStringLiteral("url")).toString();
  hook.format = given.value(QStringLiteral("format")).toString(QStringLiteral("json"));
  hook.failuresOnly = given.value(QStringLiteral("failuresOnly")).toBool(false);
  hook.everyJob = given.value(QStringLiteral("everyJob")).toBool(false);
  hook.secret = given.value(QStringLiteral("secret")).toString();
  return hook;
}

// A moment, with its offset from UTC. Without one, "10:52" means a different
// instant to every reader in a different time zone, and a program reading
// this has no window to look at to guess which.
QString stamp(qint64 msSinceEpoch) { return JobJson::Stamp(msSinceEpoch); }

// One run, as the answer to "when did this last happen": when it began, when
// it ended, how long that was and how it went. Null when it never has.
QJsonValue describeRun(const JobRunRecord &run) {
  if (run.requestId.isEmpty()) {
    return QJsonValue(QJsonValue::Null);
  }

  QJsonObject object;
  object.insert(QStringLiteral("requestId"), run.requestId);
  object.insert(QStringLiteral("taskName"), run.taskName);
  object.insert(QStringLiteral("state"), run.state);
  object.insert(QStringLiteral("startedAt"), stamp(run.startedAt));
  object.insert(QStringLiteral("finishedAt"), stamp(run.finishedAt));

  // Given, not left for the reader to subtract: a run still going has no
  // end to subtract from, and null says that where a number could not.
  if (run.finishedAt > 0 && run.startedAt > 0) {
    object.insert(QStringLiteral("durationSeconds"),
                  double(run.finishedAt - run.startedAt) / 1000.0);
  } else {
    object.insert(QStringLiteral("durationSeconds"),
                  QJsonValue(QJsonValue::Null));
  }
  object.insert(QStringLiteral("dryRun"), run.dryRun);
  object.insert(QStringLiteral("bytes"), double(run.bytes));
  object.insert(QStringLiteral("transfers"), double(run.transfers));
  object.insert(QStringLiteral("errors"), double(run.errors));
  object.insert(QStringLiteral("source"), run.source);
  object.insert(QStringLiteral("dest"), run.dest);
  return object;
}

// A key as it may be shown: everything but the secret, which is not kept.
QJsonObject describeKey(const ApiAuth::Key &key) {
  QJsonObject object;
  object.insert(QStringLiteral("id"), key.id);
  object.insert(QStringLiteral("name"), key.name);
  object.insert(QStringLiteral("scope"), ApiAuth::ScopeName(key.scope));
  object.insert(QStringLiteral("created"), JobJson::Stamp(key.created));
  object.insert(QStringLiteral("lastUsed"),
                key.lastUsed.isValid() ? JobJson::Stamp(key.lastUsed)
                                       : QString());
  return object;
}

// The whole schedule, in words. See schedule_json.h.
QJsonObject describe(const Schedule &schedule) {
  return ScheduleJson::Write(schedule, QDateTime::currentDateTime());
}

// The options a command was given. A command with no options is one sent
// with no body at all, so a missing body is an empty object rather than an
// error -- otherwise every caller would have to send "{}".
QJsonObject options(const HttpRequest &request) {
  if (request.body.isEmpty()) {
    return QJsonObject();
  }
  return QJsonDocument::fromJson(request.body).object();
}

// Accepted in the body or in the query, because a dry run is the one option
// somebody types by hand into curl.
bool wantsDryRun(const HttpRequest &request) {
  const QString asked = request.queryValue(QStringLiteral("dryRun"));
  if (!asked.isEmpty()) {
    return asked != QLatin1String("0") && asked != QLatin1String("false");
  }
  return options(request).value(QStringLiteral("dryRun")).toBool();
}

// The same rule the queue keeps for itself: a second copy of a task that is
// already going would be two rclone processes writing one destination. The
// API is a third way to start a task, and a rule that only two of the three
// obey is not a rule.
bool alreadyRunning(const QString &taskId) {
  for (const RunningJob *job : JobRegistry::instance().jobs()) {
    if (job->isRunning() && job->taskId() == taskId) {
      return true;
    }
  }
  return false;
}

// The path a job's output went to, whether it is still running or long
// finished. A log that can only be read while the job is alive is no use for
// the thing people actually ask -- what happened last night.
QString logPathFor(const QString &requestId) {
  if (const RunningJob *job = JobRegistry::instance().find(requestId)) {
    if (!job->logPath().isEmpty()) {
      return job->logPath();
    }
  }
  return RunHistory::find(requestId).logPath;
}

// The last `lines` lines of a file, without reading the whole of it. A
// transfer of a large tree writes a log that is measured in megabytes and
// nobody wants all of it down a socket.
QByteArray tailOf(const QString &path, int lines) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return QByteArray();
  }

  const qint64 window = 1024 * 1024;
  const qint64 size = file.size();
  if (size > window) {
    file.seek(size - window);
    file.readLine(); // the first line in the window is probably half a line
  }

  QByteArray text = file.readAll();
  int cut = text.size();
  for (int found = 0; cut > 0; --cut) {
    if (text.at(cut - 1) == '\n' && ++found > lines) {
      break;
    }
  }
  return cut > 0 ? text.mid(cut) : text;
}

// Whether this is something the API can show, for telling "there is no such
// endpoint" from "that endpoint does not do that". A client that sent POST
// where it meant GET deserves to be told which of the two it got wrong.
bool isSomethingWeCanShow(const QString &path) {
  static const QStringList exact = {
      QStringLiteral("/api/v1/ping"),      QStringLiteral("/api/v1/system"),
      QStringLiteral("/api/v1/remotes"),   QStringLiteral("/api/v1/tasks"),
      QStringLiteral("/api/v1/queue"),     QStringLiteral("/api/v1/jobs"),
      QStringLiteral("/api/v1/schedules"), QStringLiteral("/api/v1/history"),
      QStringLiteral("/api/v1/events"),  QStringLiteral("/api/v1/openapi.json")};
  if (exact.contains(path)) {
    return true;
  }
  for (const QString &prefix :
       {QStringLiteral("/api/v1/tasks/"), QStringLiteral("/api/v1/jobs/")}) {
    if (path.startsWith(prefix)) {
      return true;
    }
  }
  return false;
}

// The segments after /api/v1/, empty when the path is not ours at all.
QStringList segmentsOf(const QString &path) {
  const QString prefix = QStringLiteral("/api/v1/");
  if (!path.startsWith(prefix)) {
    return QStringList();
  }
  return path.mid(prefix.size()).split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

QByteArray startedJob(const QString &requestId, const QString &how) {
  QJsonObject object;
  object.insert(QStringLiteral("requestId"), requestId);
  object.insert(QStringLiteral("started"), how);
  return toJson(object);
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

bool ApiServer::mayAsk(const HttpRequest &request) {
  return callerOf(request).known;
}

ApiServer::Caller ApiServer::callerOf(const HttpRequest &request) {
  Caller caller;
  const QString offered = request.header(QStringLiteral("authorization"));
  if (!offered.startsWith(QLatin1String("Bearer "))) {
    return caller;
  }

  const QString presented = offered.mid(7);
  if (presented.isEmpty()) {
    return caller;
  }

  // The token, for anything unattended. Compared against the stored one, and
  // an empty stored token is refused rather than matched: "none set" must
  // never come out as "none required".
  const QString expected = token();
  if ((!expected.isEmpty() && presented == expected) ||
      // Or a session, for a person who signed in.
      ApiAuth::Sessions::instance().accepts(presented)) {
    caller.known = true;
    caller.scope = int(ApiAuth::Scope::Admin);
    caller.who = QStringLiteral("api");
    return caller;
  }

  // Or a key, for another program, which may do as much as it was given.
  ApiAuth::Key key;
  if (ApiAuth::Keys::instance().accepts(presented, &key)) {
    caller.known = true;
    caller.scope = int(key.scope);
    caller.who = QStringLiteral("api:") + key.name;
  }
  return caller;
}

qint64 ApiServer::streamIdleLimitMs() {
  if (gStreamIdleOverrideMs >= 0) {
    return gStreamIdleOverrideMs;
  }
  bool readable = false;
  int minutes = qEnvironmentVariableIntValue("RB_STREAM_IDLE_MINUTES", &readable);
  if (!readable) {
    minutes = GetSettings()->value("Settings/streamIdleMinutes", 30).toInt();
  }
  return qMax(0, minutes) * 60LL * 1000LL;
}

void ApiServer::setStreamIdleLimitMs(qint64 ms) { gStreamIdleOverrideMs = ms; }

void ApiServer::watchStream(const QString &requestId) {
  StreamUse use;
  // Starting counts as use: a stream nobody ever opened is stopped as long
  // after it began as one that was played and then left.
  use.last = QDateTime::currentDateTime();
  mStreams.insert(requestId, use);

  if (mStreamWatch == nullptr) {
    mStreamWatch = new QTimer(this);
    QObject::connect(mStreamWatch, &QTimer::timeout, this,
                     &ApiServer::stopIdleStreams);
  }
  // Looked at four times in the time a stream may sit idle, and never more
  // than every half minute: being a little late to stop one costs nothing.
  const qint64 limit = streamIdleLimitMs();
  mStreamWatch->setInterval(
      int(qBound(qint64(50), limit > 0 ? limit / 4 : qint64(30000),
                 qint64(30000))));
  if (!mStreamWatch->isActive()) {
    mStreamWatch->start();
  }
}

void ApiServer::stopIdleStreams() {
  const qint64 limit = streamIdleLimitMs();
  const QDateTime now = QDateTime::currentDateTime();

  for (auto it = mStreams.begin(); it != mStreams.end();) {
    RunningJob *job = JobRegistry::instance().find(it.key());
    if (job == nullptr || !job->isRunning()) {
      it = mStreams.erase(it); // stopped some other way
      continue;
    }
    // Somebody connected is somebody playing, even paused: a player keeps
    // its connection while it waits, and stopping under it would be the
    // film vanishing when the pause button is let go.
    if (limit > 0 && it.value().open == 0 && it.value().last.msecsTo(now) >= limit) {
      qCDebug(rbApp) << "stopping a stream nobody has played for"
                     << it.value().last.secsTo(now) << "s:"
                     << job->description().source;
      job->stop();
      it = mStreams.erase(it);
      continue;
    }
    ++it;
  }

  if (mStreams.isEmpty() && mStreamWatch != nullptr) {
    mStreamWatch->stop();
  }
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
  InitWebUiResources();

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

void ApiServer::listDirectory(QTcpSocket *socket, const HttpRequest &request,
                             const QString &remote, const QString &asked) {
  // Trimmed once, here, so that "films", "/films" and "films/" are one
  // folder rather than three -- and so joining a name onto it below cannot
  // produce a double slash.
  QString path = asked;
  while (path.startsWith(QLatin1Char('/'))) {
    path.remove(0, 1);
  }
  while (path.endsWith(QLatin1Char('/'))) {
    path.chop(1);
  }

  if (RemoteRegistry::instance().find(remote) == nullptr) {
    socket->write(
        FormatHttpResponse(404, problem(QStringLiteral("no such remote"))));
    return;
  }

  bool readable = true;
  const int wanted = request.queryValue(QStringLiteral("limit")).toInt(&readable);
  const int limit = readable && wanted > 0 ? qMin(wanted, 100000) : 20000;

  auto collected = QSharedPointer<QVector<LsjsonEntry>>::create();
  auto truncated = QSharedPointer<bool>::create(false);
  auto answered = QSharedPointer<bool>::create(false);

  auto *listing = new DirectoryListing(remote, path, this);

  // Every way out of here goes through this, and it can only happen once.
  //
  // That is the whole lesson of the first version: it answered from
  // finished() alone, and the path that stopped early -- a folder with more
  // entries than were asked for -- cancelled the listing instead, which
  // emits nothing by design. The request was never answered at all. A folder
  // of 7,103 files sat on "reading..." forever with nothing in any log to say
  // why, because nothing had gone wrong; something had simply not happened.
  const auto respond = [socket, listing, answered](int status,
                                                   const QByteArray &body) {
    if (*answered) {
      return;
    }
    *answered = true;
    socket->write(FormatHttpResponse(status, body));
    listing->cancel();
    listing->deleteLater();
  };

  const auto sendWhatWeHave = [=]() {
    QJsonArray array;
    for (const LsjsonEntry &entry : *collected) {
      // The path rclone gives is relative to the folder that was listed, so
      // an entry of "films/2012" comes back as plain "2012". Sent on as it
      // stands, the next click asks for "2012" at the root of the remote --
      // which is a different folder, or, as the first person to try it saw,
      // no folder at all.
      //
      // Joined here rather than in the page: "path" means "the path to pass
      // back to ls", and every client that ever reads this should get the
      // same answer to what it means.
      const QString full =
          path.isEmpty() ? entry.path
                         : path + QLatin1Char('/') + entry.path;

      QJsonObject object;
      object.insert(QStringLiteral("name"), entry.name);
      object.insert(QStringLiteral("path"), full);
      object.insert(QStringLiteral("isDir"), entry.isDir);
      object.insert(QStringLiteral("size"), double(entry.size));
      object.insert(QStringLiteral("modTime"),
                    entry.modTime.isValid()
                        ? entry.modTime.toString(Qt::ISODate)
                        : QString());
      array.append(object);
    }

    QJsonObject object;
    object.insert(QStringLiteral("remote"), remote);
    object.insert(QStringLiteral("path"), path);
    object.insert(QStringLiteral("truncated"), *truncated);
    object.insert(QStringLiteral("entries"), array);
    respond(200, toJson(object));
  };

  QObject::connect(
      listing, &DirectoryListing::entries, socket,
      [=](const QVector<LsjsonEntry> &batch) {
        for (const LsjsonEntry &entry : batch) {
          if (collected->size() >= limit) {
            // Answered with what there is rather than read to the end: a
            // folder of a hundred thousand entries would otherwise be held in
            // memory twice and sent down a socket nobody can read it from.
            *truncated = true;
            sendWhatWeHave();
            return;
          }
          collected->append(entry);
        }
      });

  QObject::connect(listing, &DirectoryListing::finished, socket,
                   [=]() { sendWhatWeHave(); });

  QObject::connect(listing, &DirectoryListing::failed, socket,
                   [=](const QString &reason) {
                     // Said, not swallowed. A folder that will not open used
                     // to show as an empty list, which reads as "there is
                     // nothing here" -- a different thing entirely.
                     qCDebug(rbApp) << "listing" << remote << path
                                    << "failed:" << reason;
                     respond(502, problem(reason));
                   });

  // A backstop, not a working part. Every path above answers on its own; this
  // is here so that a path nobody thought of cannot leave somebody waiting
  // forever -- which is exactly what happened the first time.
  auto *deadline = new QTimer(listing);
  deadline->setSingleShot(true);
  deadline->setInterval(120000);
  QObject::connect(deadline, &QTimer::timeout, socket, [=]() {
    qCDebug(rbApp) << "listing" << remote << path << "gave up after 120s";
    respond(504, problem(QStringLiteral(
                     "the remote did not answer within two minutes")));
  });
  deadline->start();

  // The listing belongs to whoever asked for it. If they go away -- closed
  // the tab, lost the network -- rclone should stop, not go on reading a
  // directory for nobody.
  QObject::connect(socket, &QTcpSocket::disconnected, listing, [listing]() {
    listing->cancel();
    listing->deleteLater();
  });

  qCDebug(rbApp) << "API listing" << remote << path << "limit=" << limit;
  listing->start();
}

void ApiServer::testWebhook(QTcpSocket *socket, const QString &id) {
  if (Webhooks::instance().find(id) == nullptr) {
    socket->write(FormatHttpResponse(
        404, problem(QStringLiteral("there is no such webhook"))));
    return;
  }
  socket->setProperty("stream", true); // answered once, when it has answered
  QPointer<QTcpSocket> held(socket);
  Webhooks::instance().test(id, [held](int status, const QString &why) {
    if (!held) {
      return;
    }
    QJsonObject object;
    object.insert(QStringLiteral("status"), status);
    if (!why.isEmpty()) {
      object.insert(QStringLiteral("error"), why);
    }
    held->write(FormatHttpResponse(why.isEmpty() ? 200 : 502, toJson(object)));
  });
}

void ApiServer::readFile(QTcpSocket *socket, const HttpRequest &request,
                         const QString &remote) {
  QString path = request.queryValue(QStringLiteral("path"));
  while (path.startsWith(QLatin1Char('/'))) {
    path.remove(0, 1);
  }
  if (path.isEmpty() || path.endsWith(QLatin1Char('/'))) {
    socket->write(FormatHttpResponse(400, problem(QStringLiteral("name a file"))));
    return;
  }
  if (RemoteRegistry::instance().find(remote) == nullptr) {
    socket->write(
        FormatHttpResponse(404, problem(QStringLiteral("no such remote"))));
    return;
  }

  // Unless asked: a picture or a PDF is no use cut short, so all of it up to
  // the most; anything else is looked at from its start. Held in memory
  // until it is sent, hence the most.
  const QString name = path.section(QLatin1Char('/'), -1);
  const QString view = FileReader::ViewOf(name);
  const qint64 most = 64LL * 1024 * 1024;
  const bool whole =
      view == QLatin1String("image") || view == QLatin1String("pdf");
  bool readable = true;
  const qint64 wanted =
      request.queryValue(QStringLiteral("limit")).toLongLong(&readable);
  const qint64 limit = readable && wanted > 0 ? qMin(wanted, most)
                       : whole                ? most
                                              : 2LL * 1024 * 1024;
  auto *reader = new FileReader(remote, path, limit, this);
  auto answered = QSharedPointer<bool>::create(false);
  const auto respond = [socket, reader, answered](const QByteArray &response) {
    if (*answered) {
      return;
    }
    *answered = true;
    socket->write(response);
    reader->cancel();
    reader->deleteLater();
  };

  QObject::connect(
      reader, &FileReader::finished, socket,
      [=](const QByteArray &data, bool truncated) {
        QHash<QString, QString> about;
        about.insert(QStringLiteral("X-Rb-View"), FileReader::ViewOf(name));
        about.insert(QStringLiteral("X-Rb-Type"), FileReader::TypeOf(name));
        about.insert(QStringLiteral("X-Rb-Truncated"),
                     truncated ? QStringLiteral("true") : QStringLiteral("false"));
        respond(FormatHttpResponse(200, data, "application/octet-stream", about));
      });
  QObject::connect(reader, &FileReader::failed, socket,
                   [=](const QString &reason) {
                     qCDebug(rbApp) << "reading" << remote << path
                                    << "failed:" << reason;
                     respond(FormatHttpResponse(502, problem(reason)));
                   });

  // The same backstop as a listing's.
  auto *deadline = new QTimer(reader);
  deadline->setSingleShot(true);
  deadline->setInterval(120000);
  QObject::connect(deadline, &QTimer::timeout, socket, [=]() {
    respond(FormatHttpResponse(
        504, problem(QStringLiteral(
                 "the remote did not answer within two minutes"))));
  });
  deadline->start();

  QObject::connect(socket, &QTcpSocket::disconnected, reader, [reader]() {
    reader->cancel();
    reader->deleteLater();
  });

  qCDebug(rbApp) << "API reading" << remote << path << "limit=" << limit;
  reader->start();
}

void ApiServer::answerWhenDone(QTcpSocket *socket, RunningJob *job,
                               const QJsonObject &onSuccess, bool withOutput) {
  // What rclone said. For most commands only the end of it matters, as the
  // explanation when something went wrong. For some the output is the
  // result -- the link that was made, the list of duplicates found -- and
  // then all of it is kept, up to a size nobody would want more than.
  const int keep = withOutput ? 4000 : 8;
  auto said = QSharedPointer<QStringList>::create();
  QObject::connect(job, &RunningJob::outputLine, socket,
                   [said, keep](const QString &line) {
                     said->append(line);
                     while (said->size() > keep) {
                       said->removeFirst();
                     }
                   });

  const auto finish = [socket, job, said, onSuccess, withOutput]() {
    QJsonObject object = onSuccess;
    object.insert(QStringLiteral("requestId"), job->requestId());

    if (job->state() == JobState::Finished) {
      if (withOutput) {
        object.insert(QStringLiteral("output"), said->join(QLatin1Char('\n')));

        // A link is the one line of it that is an address.
        for (int i = said->size() - 1; i >= 0; --i) {
          const QString line = said->at(i).trimmed();
          if (line.startsWith(QLatin1String("http://")) ||
              line.startsWith(QLatin1String("https://"))) {
            object.insert(QStringLiteral("url"), line);
            break;
          }
        }
      }
      socket->write(FormatHttpResponse(200, toJson(object)));
      return;
    }

    // rclone's own words, because "it failed" is the one thing the caller
    // already knows. Falls back to the exit code when rclone said nothing.
    QString why = said->join(QLatin1Char('\n')).trimmed();
    if (why.isEmpty()) {
      why = QStringLiteral("rclone exited with code %1").arg(job->exitCode());
    }
    object.insert(QStringLiteral("error"), why);
    object.insert(QStringLiteral("code"), QStringLiteral("rclone_failed"));
    socket->write(FormatHttpResponse(502, toJson(object)));
  };

  if (!job->isRunning()) {
    finish(); // could not be started at all, and has already ended
    return;
  }
  QObject::connect(job, &RunningJob::finished, socket,
                   [finish](JobState) { finish(); });
}

void ApiServer::remoteQuestion(QTcpSocket *socket, const HttpRequest &request,
                               const QString &remote,
                               const QString &question) {
  const auto refuse = [socket](int status, const QString &code,
                               const QString &why) {
    QJsonObject object;
    object.insert(QStringLiteral("error"), why);
    object.insert(QStringLiteral("code"), code);
    socket->write(FormatHttpResponse(status, toJson(object)));
  };

  if (RemoteRegistry::instance().find(remote) == nullptr) {
    refuse(404, QStringLiteral("no_such_remote"),
           QStringLiteral("no such remote"));
    return;
  }

  const QString path = request.queryValue(QStringLiteral("path"));

  const auto flag = [&request](const char *name) {
    const QString value = request.queryValue(QLatin1String(name)).toLower();
    return value == QLatin1String("1") || value == QLatin1String("true");
  };
  const bool namesOnly =
      request.queryValue(QStringLiteral("format")) == QLatin1String("txt");

  RunningJob *job = nullptr;
  if (question == QLatin1String("size")) {
    job = RemoteOps::Size(remote, path, QStringList(), true);
  } else if (question == QLatin1String("about")) {
    job = RemoteOps::About(remote, true);
  } else if (question == QLatin1String("check")) {
    const QString other =
        request.queryValue(QStringLiteral("toRemote")).isEmpty()
            ? remote
            : request.queryValue(QStringLiteral("toRemote"));
    if (RemoteRegistry::instance().find(other) == nullptr) {
      refuse(404, QStringLiteral("no_such_remote"),
             QStringLiteral("no such remote to compare with"));
      return;
    }

    RemoteOps::CheckOptions options;
    options.oneWay = flag("oneWay");
    options.sizeOnly = flag("sizeOnly");
    options.download = flag("download");

    QString why;
    job = RemoteOps::Check(remote, path, other,
                           request.queryValue(QStringLiteral("to")), options,
                           &why);
    if (job == nullptr) {
      refuse(400, QStringLiteral("refused"), why);
      return;
    }
  } else if (question == QLatin1String("export")) {
    RemoteOps::ExportOptions options;
    options.maxDepth =
        qMax(0, request.queryValue(QStringLiteral("maxDepth")).toInt());
    job = RemoteOps::ListForExport(remote, path, options);
  } else {
    job = RemoteOps::Tree(remote, path);
  }
  qCDebug(rbApp) << "API asking" << question << "of" << remote << path;

  // Everything rclone prints, because here the output is the answer. Capped,
  // because a tree of a large remote is not something to hold whole. A
  // listing for export is the one that is meant to be large.
  const int cap = (question == QLatin1String("export") ? 64 : 2) * 1024 * 1024;
  auto said = QSharedPointer<QStringList>::create();
  auto size = QSharedPointer<int>::create(0);
  auto truncated = QSharedPointer<bool>::create(false);

  QObject::connect(job, &RunningJob::outputLine, socket,
                   [said, size, truncated, cap](const QString &line) {
                     if (*size > cap) {
                       *truncated = true;
                       return;
                     }
                     *size += int(line.size()) + 1;
                     said->append(line);
                   });

  const auto finish = [=]() {
    const QString text = said->join(QLatin1Char('\n'));

    QJsonObject object;
    object.insert(QStringLiteral("remote"), remote);
    object.insert(QStringLiteral("path"), RemoteOps::Join(QString(), path));
    object.insert(QStringLiteral("requestId"), job->requestId());

    if (question == QLatin1String("check")) {
      // rclone ends a check with an error when the two sides differ. That is
      // the answer to the question, not a failure to answer it -- so it is
      // told apart by whether rclone got as far as counting.
      const RemoteOps::CheckResult found = RemoteOps::ReadCheckOutput(*said);
      if (found.completed()) {
        object.insert(QStringLiteral("matched"), found.matched());
        object.insert(QStringLiteral("differences"), found.differences);
        if (found.matching >= 0) {
          object.insert(QStringLiteral("matching"), found.matching);
        }
        object.insert(QStringLiteral("output"), text);
        object.insert(QStringLiteral("truncated"), *truncated);
        socket->write(FormatHttpResponse(200, toJson(object)));
        return;
      }
    }

    if (question == QLatin1String("export") &&
        job->state() == JobState::Finished) {
      if (*truncated) {
        object.insert(QStringLiteral("error"),
                      QStringLiteral("that listing is too large to send; "
                                     "narrow it with maxDepth or a folder "
                                     "further down"));
        object.insert(QStringLiteral("code"), QStringLiteral("too_large"));
        socket->write(FormatHttpResponse(413, toJson(object)));
        return;
      }

      QByteArray file;
      for (const QString &line : *said) {
        qint64 size = 0;
        QString modified;
        QString name;
        if (RemoteOps::ParseLslLine(line, &size, &modified, &name)) {
          file += RemoteOps::ExportRow(name, modified, size, namesOnly).toUtf8();
          file += '\n';
        }
      }

      QHash<QString, QString> headers;
      headers.insert(QStringLiteral("Content-Disposition"),
                     QStringLiteral("attachment; filename=\"export.%1\"")
                         .arg(namesOnly ? QStringLiteral("txt")
                                        : QStringLiteral("csv")));
      socket->write(FormatHttpResponse(
          200, file, namesOnly ? "text/plain; charset=utf-8"
                               : "text/csv; charset=utf-8",
          headers));
      return;
    }

    if (job->state() != JobState::Finished) {
      object.insert(QStringLiteral("error"),
                    text.trimmed().isEmpty()
                        ? QStringLiteral("rclone exited with code %1")
                              .arg(job->exitCode())
                        : text.trimmed());
      object.insert(QStringLiteral("code"), QStringLiteral("rclone_failed"));
      socket->write(FormatHttpResponse(502, toJson(object)));
      return;
    }

    if (question == QLatin1String("tree")) {
      object.insert(QStringLiteral("output"), text);
      object.insert(QStringLiteral("truncated"), *truncated);
      socket->write(FormatHttpResponse(200, toJson(object)));
      return;
    }

    // Numbers, from the JSON rclone was asked for. It is found between the
    // first brace and the last, because rclone may say something else first
    // -- a notice about a config file, a retry.
    const int from = text.indexOf(QLatin1Char('{'));
    const int to = text.lastIndexOf(QLatin1Char('}'));
    const QJsonObject figures =
        from >= 0 && to > from
            ? QJsonDocument::fromJson(text.mid(from, to - from + 1).toUtf8())
                  .object()
            : QJsonObject();

    if (figures.isEmpty()) {
      object.insert(QStringLiteral("error"),
                    QStringLiteral("rclone answered, but not with figures"));
      object.insert(QStringLiteral("code"), QStringLiteral("unreadable"));
      object.insert(QStringLiteral("output"), text);
      socket->write(FormatHttpResponse(502, toJson(object)));
      return;
    }

    for (auto it = figures.begin(); it != figures.end(); ++it) {
      object.insert(it.key(), it.value());
    }
    socket->write(FormatHttpResponse(200, toJson(object)));
  };

  if (!job->isRunning()) {
    finish();
    return;
  }
  QObject::connect(job, &RunningJob::finished, socket,
                   [finish](JobState) { finish(); });

  // Whoever asked has gone: stop asking on their behalf.
  QObject::connect(socket, &QTcpSocket::disconnected, job, [job]() {
    if (job->isRunning()) {
      job->stop();
    }
  });
}

void ApiServer::remoteOperation(QTcpSocket *socket, const HttpRequest &request,
                                const QString &remote,
                                const QString &operation) {
  const auto refuse = [socket](int status, const QString &code,
                               const QString &why) {
    QJsonObject object;
    object.insert(QStringLiteral("error"), why);
    object.insert(QStringLiteral("code"), code);
    socket->write(FormatHttpResponse(status, toJson(object)));
  };

  if (AppCore::instance().isStopping()) {
    refuse(503, QStringLiteral("shutting_down"),
           QStringLiteral("the program is shutting down"));
    return;
  }
  if (RemoteRegistry::instance().find(remote) == nullptr) {
    refuse(404, QStringLiteral("no_such_remote"),
           QStringLiteral("no such remote"));
    return;
  }

  const QJsonObject given = options(request);
  const QString path = given.value(QStringLiteral("path")).toString();

  if (operation == QLatin1String("mkdir")) {
    const QString name = given.value(QStringLiteral("name")).toString();

    QString why;
    RunningJob *job = RemoteOps::MakeFolder(remote, path, name, &why);
    if (job == nullptr) {
      refuse(400, QStringLiteral("bad_name"), why);
      return;
    }
    qCDebug(rbApp) << "API making folder" << remote << path << name;

    QJsonObject made;
    made.insert(QStringLiteral("remote"), remote);
    made.insert(QStringLiteral("path"), RemoteOps::Join(path, name));
    answerWhenDone(socket, job, made);
    return;
  }

  if (operation == QLatin1String("rename")) {
    const QString name = given.value(QStringLiteral("name")).toString();

    QString why;
    RunningJob *job = RemoteOps::Rename(remote, path, name, &why);
    if (job == nullptr) {
      refuse(400, QStringLiteral("bad_name"), why);
      return;
    }
    qCDebug(rbApp) << "API renaming" << remote << path << "to" << name;

    QJsonObject renamed;
    renamed.insert(QStringLiteral("remote"), remote);
    renamed.insert(QStringLiteral("from"), RemoteOps::Join(QString(), path));
    renamed.insert(QStringLiteral("path"),
                   RemoteOps::Join(RemoteOps::ParentOf(path), name));
    answerWhenDone(socket, job, renamed);
    return;
  }

  if (operation == QLatin1String("copy") ||
      operation == QLatin1String("move")) {
    const bool move = operation == QLatin1String("move");

    RemoteOps::Item item;
    item.remote = remote;
    item.path = path;
    item.isFolder = given.value(QStringLiteral("isDir")).toBool(false);

    // Somewhere else on the same remote unless another is named.
    const QString toRemote =
        given.value(QStringLiteral("toRemote")).toString(remote);
    if (RemoteRegistry::instance().find(toRemote) == nullptr) {
      refuse(404, QStringLiteral("no_such_remote"),
             QStringLiteral("no such remote to copy to"));
      return;
    }

    QString why;
    RunningJob *job = RemoteOps::CopyInto(
        move, item, toRemote, given.value(QStringLiteral("to")).toString(),
        given.value(QStringLiteral("dryRun")).toBool(false), &why);
    if (job == nullptr) {
      refuse(400, QStringLiteral("refused"), why);
      return;
    }
    qCDebug(rbApp) << "API" << operation << remote << path << "to" << toRemote
                   << given.value(QStringLiteral("to")).toString();

    // Answered now, not when it is done. Everything else here takes as long
    // as a question takes; a copy takes as long as the files are large, and
    // nobody should hold a connection open for that. It is a job like any
    // transfer: /jobs and the event stream say how it is going.
    QJsonObject started;
    started.insert(QStringLiteral("requestId"), job->requestId());
    started.insert(QStringLiteral("started"), operation);
    started.insert(QStringLiteral("source"), job->description().source);
    started.insert(QStringLiteral("dest"), job->description().dest);
    socket->write(FormatHttpResponse(202, toJson(started)));
    return;
  }

  if (operation == QLatin1String("upload") ||
      operation == QLatin1String("download")) {
    const bool download = operation == QLatin1String("download");
    const bool dryRun = given.value(QStringLiteral("dryRun")).toBool(false);

    // The end on this machine, resolved and checked against what the API is
    // allowed to see. The other end is the remote in the address.
    const QString asked = given
                              .value(download ? QStringLiteral("to")
                                              : QStringLiteral("from"))
                              .toString();
    const QString local = LocalFiles::Allowed(asked);
    if (local.isEmpty()) {
      refuse(403, QStringLiteral("outside_roots"),
             QStringLiteral(
                 "that is not a folder on this machine the API may use"));
      return;
    }

    RunningJob *job = nullptr;
    if (download) {
      if (!QFileInfo(local).isDir()) {
        refuse(400, QStringLiteral("refused"),
               QStringLiteral("the place to download to has to be a folder "
                              "that exists"));
        return;
      }
      RemoteOps::Item item;
      item.remote = remote;
      item.path = path;
      item.isFolder = given.value(QStringLiteral("isDir")).toBool(false);

      QString why;
      job = RemoteOps::Download(item, local, dryRun, &why);
      if (job == nullptr) {
        refuse(400, QStringLiteral("refused"), why);
        return;
      }
    } else {
      // Whether it is a folder is read off the disk rather than taken on
      // trust: it decides whether the upload lands under its own name.
      const QFileInfo source(local);
      if (!source.exists()) {
        refuse(400, QStringLiteral("refused"),
               QStringLiteral("there is nothing at that path to upload"));
        return;
      }
      job = RemoteOps::Upload(local, source.isDir(), remote,
                              given.value(QStringLiteral("to")).toString(),
                              dryRun);
    }
    qCDebug(rbApp) << "API" << operation << remote << path << local;

    QJsonObject started;
    started.insert(QStringLiteral("requestId"), job->requestId());
    started.insert(QStringLiteral("started"), operation);
    started.insert(QStringLiteral("source"), job->description().source);
    started.insert(QStringLiteral("dest"), job->description().dest);
    socket->write(FormatHttpResponse(202, toJson(started)));
    return;
  }

  if (operation == QLatin1String("mount")) {
    RemoteOps::MountOptions options;
    options.readOnly = given.value(QStringLiteral("readOnly")).toBool(false);
    options.cache = given.value(QStringLiteral("cache")).toString();

    QString why;
    RunningJob *job = RemoteOps::Mount(
        remote, path, given.value(QStringLiteral("mountPoint")).toString(),
        options, &why);
    if (job == nullptr) {
      refuse(400, QStringLiteral("refused"), why);
      return;
    }
    qCDebug(rbApp) << "API mounting" << remote << path;

    // Answered now. A mount runs for as long as it is wanted; whether it
    // came up is told by the job, and POST /jobs/{id}/stop takes it down.
    QJsonObject started;
    started.insert(QStringLiteral("requestId"), job->requestId());
    started.insert(QStringLiteral("started"), QStringLiteral("mount"));
    started.insert(QStringLiteral("source"), job->description().source);
    started.insert(QStringLiteral("mountPoint"), job->description().dest);
    socket->write(FormatHttpResponse(202, toJson(started)));
    return;
  }

  if (operation == QLatin1String("serve")) {
    RemoteOps::ServeOptions options;
    options.protocol = given.value(QStringLiteral("protocol")).toString();
    const int port = given.value(QStringLiteral("port")).toInt(0);
    if (port < 0 || port > 65535) {
      refuse(400, QStringLiteral("refused"),
             QStringLiteral("a port is a number from 1 to 65535"));
      return;
    }
    options.port = quint16(port);
    options.everyone = given.value(QStringLiteral("everyone")).toBool(false);
    options.readOnly = given.value(QStringLiteral("readOnly")).toBool(true);
    options.user = given.value(QStringLiteral("user")).toString();
    options.password = given.value(QStringLiteral("password")).toString();

    QString why;
    RunningJob *job = RemoteOps::Serve(remote, path, options, &why);
    if (job == nullptr) {
      refuse(400, QStringLiteral("refused"), why);
      return;
    }
    qCDebug(rbApp) << "API serving" << remote << path << "as"
                   << job->serveAddress().protocol << "on port"
                   << job->serveAddress().port;

    // Answered now, like a mount: it runs until it is stopped.
    QJsonObject started;
    started.insert(QStringLiteral("requestId"), job->requestId());
    started.insert(QStringLiteral("started"), QStringLiteral("serve"));
    started.insert(QStringLiteral("source"), job->description().source);
    started.insert(QStringLiteral("protocol"), job->serveAddress().protocol);
    started.insert(QStringLiteral("port"), int(job->serveAddress().port));
    started.insert(QStringLiteral("everyone"), job->serveAddress().everyone);
    started.insert(QStringLiteral("readOnly"), job->serveAddress().readOnly);
    socket->write(FormatHttpResponse(202, toJson(started)));
    return;
  }

  if (operation == QLatin1String("stream")) {
    RemoteOps::Item item;
    item.remote = remote;
    item.path = path;
    item.isFolder = given.value(QStringLiteral("isDir")).toBool(false);

    QString why;
    RunningJob *job = RemoteOps::Stream(item, &why);
    if (job == nullptr) {
      refuse(409, QStringLiteral("refused"), why);
      return;
    }
    qCDebug(rbApp) << "API streaming" << remote << path;
    watchStream(job->requestId());

    // Answered now, like a mount: it runs until it is stopped.
    QJsonObject started;
    started.insert(QStringLiteral("requestId"), job->requestId());
    started.insert(QStringLiteral("started"), QStringLiteral("stream"));
    started.insert(QStringLiteral("source"), job->description().source);
    started.insert(QStringLiteral("isDir"), item.isFolder);
    started.insert(QStringLiteral("path"), job->streamPath());
    started.insert(QStringLiteral("stopsWhenIdleFor"),
                   int(streamIdleLimitMs() / 1000));
    socket->write(FormatHttpResponse(202, toJson(started)));
    return;
  }

  if (operation == QLatin1String("link")) {
    QString why;
    RunningJob *job = RemoteOps::Link(remote, path, &why);
    if (job == nullptr) {
      refuse(400, QStringLiteral("refused"), why);
      return;
    }
    qCDebug(rbApp) << "API making a public link" << remote << path;

    QJsonObject shared;
    shared.insert(QStringLiteral("remote"), remote);
    shared.insert(QStringLiteral("path"), RemoteOps::Join(QString(), path));
    answerWhenDone(socket, job, shared, true);
    return;
  }

  if (operation == QLatin1String("cleanup")) {
    qCDebug(rbApp) << "API cleaning up" << remote;
    QJsonObject cleaned;
    cleaned.insert(QStringLiteral("remote"), remote);
    answerWhenDone(socket, RemoteOps::Cleanup(remote), cleaned, true);
    return;
  }

  if (operation == QLatin1String("dedupe")) {
    const QString mode = given.value(QStringLiteral("mode")).toString();
    const bool dryRun = given.value(QStringLiteral("dryRun")).toBool(false);

    // No flags of the caller's own. The window's dialog has a box for them;
    // a request over HTTP does not, for the reason tasks cannot be written
    // this way either.
    QString why;
    RunningJob *job =
        RemoteOps::Dedupe(remote, path, mode, dryRun, QStringList(), &why);
    if (job == nullptr) {
      refuse(400, QStringLiteral("bad_mode"), why);
      return;
    }
    qCDebug(rbApp) << "API deduplicating" << remote << path << mode << dryRun;

    QJsonObject settled;
    settled.insert(QStringLiteral("remote"), remote);
    settled.insert(QStringLiteral("path"), RemoteOps::Join(QString(), path));
    settled.insert(QStringLiteral("mode"), mode);
    settled.insert(QStringLiteral("dryRun"), dryRun);
    answerWhenDone(socket, job, settled, true);
    return;
  }

  if (operation == QLatin1String("delete")) {
    // Whether it is a folder has to be said outright, and saying nothing
    // means "a file" -- the safer of the two to be wrong about, because the
    // command for a file refuses a folder.
    RemoteOps::Item item;
    item.remote = remote;
    item.path = path;
    item.isFolder = given.value(QStringLiteral("isDir")).toBool(false);

    QString why;
    RunningJob *job = RemoteOps::Delete(item, &why);
    if (job == nullptr) {
      refuse(400, QStringLiteral("refused"), why);
      return;
    }
    qCDebug(rbApp) << "API deleting" << (item.isFolder ? "folder" : "file")
                   << remote << path;

    QJsonObject gone;
    gone.insert(QStringLiteral("remote"), remote);
    gone.insert(QStringLiteral("path"), RemoteOps::Join(QString(), path));
    gone.insert(QStringLiteral("isDir"), item.isFolder);
    answerWhenDone(socket, job, gone);
    return;
  }

  refuse(404, QStringLiteral("no_such_operation"),
         QStringLiteral("no such operation"));
}

void ApiServer::relayStream(QTcpSocket *client, const HttpRequest &request) {
  const auto refuse = [client](int status, const QString &why) {
    client->write(FormatHttpResponse(status, problem(why)));
  };

  if (request.method != QLatin1String("GET") &&
      request.method != QLatin1String("HEAD")) {
    refuse(405, QStringLiteral("a stream is only read"));
    return;
  }

  // Which stream: the one whose path begins the way this one does, as far
  // as the end of the random part. That part is the whole of the permission,
  // so nothing is said about why when it does not match -- a stream that
  // has stopped and one that never was are the same answer.
  const QString prefix = QLatin1String(RemoteOps::kStreamPrefix);
  const QString secret = request.path.mid(prefix.size())
                             .section(QLatin1Char('/'), 0, 0);
  const RunningJob *serving = nullptr;
  if (secret.size() >= 32) {
    const QString base = prefix + secret + QLatin1Char('/');
    for (const RunningJob *job : JobRegistry::instance().jobs()) {
      if (job->isRunning() && job->kind() == JobKind::Stream &&
          job->streamPort() != 0 && job->streamPath().startsWith(base)) {
        serving = job;
        break;
      }
    }
  }
  if (serving == nullptr) {
    refuse(404, QStringLiteral("no such stream"));
    return;
  }

  // From here the connection belongs to the stream: read() leaves it alone.
  client->setProperty("stream", true);

  // Counted as a player for as long as it is connected, and its traffic
  // keeps the stream from being stopped as idle. See stopIdleStreams().
  const QString streamId = serving->requestId();
  if (!mStreams.contains(streamId)) {
    watchStream(streamId); // one started before this ran, or elsewhere
  }
  mStreams[streamId].open += 1;
  mStreams[streamId].last = QDateTime::currentDateTime();

  // The request, written out again. Only what a player needs is carried
  // across -- above all Range, which is how it seeks -- and never the
  // Authorization or Cookie a browser may have sent along: rclone has no
  // use for this API's secrets.
  QByteArray head = request.method.toUtf8() + ' ' +
                    QUrl::toPercentEncoding(request.path, "/");
  if (!request.query.isEmpty()) {
    head += '?' + request.query.toUtf8();
  }
  head += " HTTP/1.1\r\nHost: 127.0.0.1:" +
          QByteArray::number(serving->streamPort()) + "\r\n";
  for (const char *name :
       {"range", "if-range", "if-modified-since", "if-none-match", "accept"}) {
    const QString value = request.header(QLatin1String(name));
    if (!value.isEmpty()) {
      head += QByteArray(name) + ": " + value.toUtf8() + "\r\n";
    }
  }
  // One request to a connection. A player asks again for every seek, and a
  // connection each is cheap next to working out where one answer ends and
  // the next begins in bytes that are only being passed along.
  head += "Connection: close\r\n\r\n";

  // A child of the client's socket, so it goes when that does: a player
  // that is closed half way through a film stops the reading from rclone.
  auto *upstream = new QTcpSocket(client);
  // Held to a megabyte, and the client's side to four below. A film is
  // bigger than memory; when the player is slower than the remote, what has
  // not been sent yet has to stay with rclone rather than pile up here.
  upstream->setReadBufferSize(1 << 20);

  const auto pump = [client, upstream]() {
    while (upstream->bytesAvailable() > 0 &&
           client->bytesToWrite() < (4 << 20)) {
      client->write(upstream->read(64 * 1024));
    }
    if (upstream->state() == QAbstractSocket::UnconnectedState &&
        upstream->bytesAvailable() == 0) {
      // Everything rclone sent has been handed on; this closes once it has
      // actually gone.
      client->disconnectFromHost();
    }
  };

  QObject::connect(upstream, &QTcpSocket::connected, upstream,
                   [upstream, head]() { upstream->write(head); });
  QObject::connect(upstream, &QTcpSocket::readyRead, upstream,
                   [this, streamId, client, pump]() {
                     client->setProperty("answered", true);
                     const auto found = mStreams.find(streamId);
                     if (found != mStreams.end()) {
                       found->last = QDateTime::currentDateTime();
                     }
                     pump();
                   });
  QObject::connect(client, &QTcpSocket::bytesWritten, upstream, pump);
  QObject::connect(upstream, &QTcpSocket::disconnected, upstream, pump);
  QObject::connect(
      upstream, &QTcpSocket::errorOccurred, upstream,
      [client, upstream](QAbstractSocket::SocketError error) {
        if (error == QAbstractSocket::RemoteHostClosedError) {
          return; // the ordinary end of an answer; disconnected deals with it
        }
        qCDebug(rbApp) << "a stream could not be reached:"
                       << upstream->errorString();
        if (!client->property("answered").toBool()) {
          // rclone is not up yet -- it takes a moment after the job starts
          // -- or has gone. Said, so a player shows something.
          client->write(FormatHttpResponse(
              502, problem(QStringLiteral(
                       "the stream is not answering; it may still be "
                       "starting, or it has stopped"))));
        }
        client->disconnectFromHost();
      });

  // The player going away first is the ordinary case, not the odd one: it
  // closes the connection every time it seeks, and when it is shut half way
  // through a film. From that moment nothing above may run again -- the
  // client's socket is on its way to being deleted, and this one, being its
  // child, is torn down inside that deletion, where a last "disconnected"
  // from it would reach for a socket that is already half gone. That is a
  // crash, and it was one: the first real film played through this took the
  // program down when the player closed. So every connection from this
  // socket is cut before anything else, and then it is dropped.
  // Connected with this server as the context, not the socket being torn
  // down, so the count goes down however the connection ends.
  QObject::connect(client, &QTcpSocket::disconnected, this,
                   [this, streamId]() {
                     const auto found = mStreams.find(streamId);
                     if (found != mStreams.end()) {
                       found->open = qMax(0, found->open - 1);
                       found->last = QDateTime::currentDateTime();
                     }
                   });
  QObject::connect(client, &QTcpSocket::disconnected, upstream,
                   [client, upstream]() {
                     upstream->disconnect();
                     QObject::disconnect(client, nullptr, upstream, nullptr);
                     upstream->abort();
                   });

  upstream->connectToHost(QHostAddress::LocalHost, serving->streamPort());
}

void ApiServer::remoteConfig(QTcpSocket *socket, const QString &remote) {
  socket->setProperty("stream", true); // answered once, when rclone has
  RcloneConfigRegistry &config = RcloneConfigRegistry::instance();

  // The backends first. Without them the only guide to what is secret is a
  // setting's name, and that is not enough -- see DescribeSettings -- so
  // the first question asked after a start waits for rclone to describe its
  // backends rather than answering with everything hidden. Fifteen seconds
  // at most: an rclone that cannot answer that still gets an answer, with
  // every value left out.
  if (!config.isLoaded() && !socket->property("configWaited").toBool()) {
    QPointer<QTcpSocket> waiting(socket);
    afterBackendsAreKnown(socket, [this, waiting, remote]() {
      if (waiting) {
        waiting->setProperty("configWaited", true);
        remoteConfig(waiting, remote);
      }
    });
    return;
  }

  QPointer<QTcpSocket> held(socket);
  config.readDump(socket, [held, remote](bool ok, const QJsonObject &dump,
                                         const QString &why) {
    if (!held) {
      return;
    }
    const auto answer = [held](int status, const QJsonObject &object) {
      held->write(FormatHttpResponse(status, toJson(object)));
      held->flush();
      held->disconnectFromHost();
    };
    if (!ok) {
      QJsonObject object;
      object.insert(QStringLiteral("error"), why);
      object.insert(QStringLiteral("code"), QStringLiteral("config_unreadable"));
      answer(502, object);
      return;
    }
    if (!dump.contains(remote)) {
      QJsonObject object;
      object.insert(QStringLiteral("error"), QStringLiteral("no such remote"));
      object.insert(QStringLiteral("code"), QStringLiteral("no_such_remote"));
      answer(404, object);
      return;
    }

    // The section holds every secret the remote has. Only what
    // DescribeSettings makes of it leaves this function.
    const QJsonObject section = dump.value(remote).toObject();
    const QString type = section.value(QStringLiteral("type")).toString();
    const Provider *provider =
        FindProvider(RcloneConfigRegistry::instance().providers(), type);

    QJsonObject object;
    object.insert(QStringLiteral("name"), remote);
    object.insert(QStringLiteral("type"), type);
    object.insert(QStringLiteral("description"),
                  provider != nullptr ? provider->description : QString());
    // False while the backends have not been read yet: then nothing is
    // known about which settings are secret beyond their names, and help and
    // defaults are missing. Asking again shortly gives the whole answer.
    object.insert(QStringLiteral("described"), provider != nullptr);
    object.insert(QStringLiteral("settings"), DescribeSettings(section, provider));
    answer(200, object);
  });
}

void ApiServer::afterBackendsAreKnown(QTcpSocket *socket,
                                      std::function<void()> then) {
  RcloneConfigRegistry &config = RcloneConfigRegistry::instance();
  if (config.isLoaded()) {
    then();
    return;
  }
  config.ensureLoaded();
  auto ran = QSharedPointer<bool>::create(false);
  const auto once = [ran, then]() {
    if (*ran) {
      return;
    }
    *ran = true;
    then();
  };
  QObject::connect(&config, &RcloneConfigRegistry::providersLoaded, socket,
                   once, Qt::SingleShotConnection);
  QTimer::singleShot(15000, socket, once);
}

void ApiServer::changeRemote(QTcpSocket *socket, const HttpRequest &request,
                             const QString &remote) {
  socket->setProperty("stream", true);
  QPointer<QTcpSocket> held(socket);
  const auto answer = [held](const RemoteChange &change) {
    if (!held) {
      return;
    }
    QJsonObject object;
    int status = 200;
    if (change.ok) {
      object.insert(QStringLiteral("ok"), true);
    } else {
      object.insert(QStringLiteral("error"), change.why);
      object.insert(QStringLiteral("code"), change.code);
      status = change.code == QLatin1String("no_such_remote") ? 404
               : change.code == QLatin1String("bad_name")     ? 400
               : change.code == QLatin1String("signs_in_with_browser") ? 409
               : change.code == QLatin1String("rclone_refused") ||
                       change.code == QLatin1String("config_unreadable")
                   ? 502
                   : 409;
    }
    object.insert(QStringLiteral("tasks"), QJsonArray::fromStringList(change.tasks));
    if (!change.question.isEmpty()) {
      // Not finished: rclone has a question, which is the caller's to
      // answer through /answer.
      object.insert(QStringLiteral("question"), change.question);
      object.insert(QStringLiteral("asks"), change.asks);
    }
    if (change.ok && held->property("made").toBool()) {
      status = 201;
    }
    held->write(FormatHttpResponse(status, toJson(object)));
    held->flush();
    held->disconnectFromHost();
  };

  if (request.method == QLatin1String("POST") &&
      request.path.endsWith(QLatin1String("/answer"))) {
    const QJsonObject given = options(request);
    qCDebug(rbApp) << "API answering rclone about remote" << remote;
    ContinueRemote(socket, remote, given.value(QStringLiteral("state")).toString(),
                   given.value(QStringLiteral("result")).toVariant().toString(),
                   answer);
    return;
  }

  if (request.method == QLatin1String("DELETE")) {
    const QString force = request.queryValue(QStringLiteral("force"));
    qCDebug(rbApp) << "API deleting remote" << remote;
    DeleteRemote(socket, remote,
                 force == QLatin1String("1") || force == QLatin1String("true"),
                 answer);
    return;
  }

  const QJsonObject given = options(request);
  for (const QString &key : given.keys()) {
    if (key != QLatin1String("name") && key != QLatin1String("settings") &&
        !(remote.isEmpty() && key == QLatin1String("type"))) {
      // Said rather than ignored: a change that silently did nothing is
      // worse than one refused.
      RemoteChange change;
      change.code = QStringLiteral("bad_name");
      change.why = QStringLiteral("\"%1\" is not something a remote is "
                                  "given; its options go in \"settings\"")
                       .arg(key);
      answer(change);
      return;
    }
  }
  const QJsonObject settings = given.value(QStringLiteral("settings")).toObject();
  const QString name = given.value(QStringLiteral("name")).toString().trimmed();
  if ((given.contains(QStringLiteral("name")) && name.isEmpty()) ||
      (!remote.isEmpty() && settings.isEmpty() && name.isEmpty())) {
    RemoteChange change;
    change.code = QStringLiteral("bad_name");
    change.why = given.contains(QStringLiteral("name"))
                     ? QStringLiteral("a remote needs a name")
                     : QStringLiteral("nothing to change: give \"settings\" "
                                      "or a new \"name\"");
    answer(change);
    return;
  }

  afterBackendsAreKnown(socket, [this, held, remote, settings, name, given, answer]() {
    if (!held) {
      return;
    }
    if (remote.isEmpty()) {
      held->setProperty("made", true);
      qCDebug(rbApp) << "API making remote" << name;
      CreateRemote(held, name, given.value(QStringLiteral("type")).toString(),
                   settings, answer);
      return;
    }
    // The settings first, then the name, so the settings are checked
    // against the remote while it still has the name it is known by.
    qCDebug(rbApp) << "API changing remote" << remote;
    UpdateRemote(held, remote, settings,
                 [held, remote, name, answer](const RemoteChange &changed) {
                   if (!held) {
                     return;
                   }
                   if (!changed.ok || name.isEmpty() || name == remote) {
                     answer(changed);
                     return;
                   }
                   RenameRemote(held, remote, name,
                                [changed, answer](RemoteChange renamed) {
                                  if (renamed.question.isEmpty()) {
                                    renamed.question = changed.question;
                                    renamed.asks = changed.asks;
                                  }
                                  answer(renamed);
                                });
                 });
  });
}

bool ApiServer::startReceiving(QTcpSocket *socket, const HttpRequest &head,
                               const QByteArray &firstBytes) {
  socket->setProperty("stream", true);
  const auto refuse = [socket](int status, const QString &code,
                               const QString &why) {
    QJsonObject object;
    object.insert(QStringLiteral("error"), why);
    object.insert(QStringLiteral("code"), code);
    socket->write(FormatHttpResponse(status, toJson(object)));
    socket->flush();
    socket->disconnectFromHost();
    return false;
  };

  // The same door as every other request: who is asking, and whether they
  // may change files on a remote.
  const Caller caller = callerOf(head);
  if (!caller.known) {
    return refuse(401, QStringLiteral("unauthorized"),
                  QStringLiteral("a token or a session is required"));
  }
  const ApiRoutes::Route *route = ApiRoutes::Match(head.method, head.path);
  const int needs = route != nullptr ? int(ApiAuth::ScopeNamed(route->scope))
                                     : int(ApiAuth::Scope::Admin);
  if (caller.scope < needs) {
    return refuse(403, QStringLiteral("insufficient_scope"),
                  QStringLiteral("this key may %1; that needs %2")
                      .arg(ApiAuth::ScopeName(ApiAuth::Scope(caller.scope)),
                           ApiAuth::ScopeName(ApiAuth::Scope(needs))));
  }
  if (AppCore::instance().isStopping()) {
    return refuse(503, QStringLiteral("stopping"),
                  QStringLiteral("the program is shutting down"));
  }

  const QString remote = segmentsOf(head.path).at(1);
  if (RemoteRegistry::instance().find(remote) == nullptr) {
    return refuse(404, QStringLiteral("no_such_remote"), QStringLiteral("no such remote"));
  }
  const QString name = head.queryValue(QStringLiteral("name")).trimmed();
  const QString nameProblem = RemoteOps::WhyNotAName(name);
  if (!nameProblem.isEmpty()) {
    return refuse(400, QStringLiteral("bad_name"), nameProblem);
  }

  // How long it is has to be said: that is what tells the end of the file
  // from the connection dropping half way, and what the room is checked for.
  bool readable = false;
  const qint64 length =
      head.header(QStringLiteral("content-length")).toLongLong(&readable);
  if (!readable || length < 0) {
    return refuse(411, QStringLiteral("length_required"),
                  QStringLiteral("say how long the file is (Content-Length)"));
  }

  const QString staging = RemoteOps::UploadStagingFolder();
  QDir().mkpath(staging);
  const QStorageInfo room(staging);
  // The file and a margin, so a full disk is said now rather than found
  // part way through somebody's film.
  if (room.isValid() && room.bytesAvailable() < length + 64LL * 1024 * 1024) {
    return refuse(507, QStringLiteral("no_room"),
                  QStringLiteral("there is not room for it where files are set "
                                 "down before they go up (%1)")
                      .arg(staging));
  }

  Receiving receiving;
  receiving.folder = QDir(staging).filePath(QUuid::createUuid().toString(QUuid::Id128));
  receiving.remote = remote;
  receiving.into = head.queryValue(QStringLiteral("path"));
  receiving.remaining = length;
  QDir().mkpath(receiving.folder);
  receiving.file = new QFile(QDir(receiving.folder).filePath(name), socket);
  if (!receiving.file->open(QIODevice::WriteOnly)) {
    QDir(receiving.folder).removeRecursively();
    return refuse(507, QStringLiteral("no_room"),
                  QStringLiteral("it could not be set down: %1")
                      .arg(receiving.file->errorString()));
  }
  mReceiving.insert(socket, receiving);
  mCallerName = caller.who;
  qCDebug(rbApp) << "receiving a file from a browser" << name << length
                 << "bytes for" << remote << receiving.into;

  // Gone before it all came: what came is thrown away.
  QObject::connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
    if (mReceiving.contains(socket) && !mReceiving.value(socket).done) {
      qCDebug(rbApp) << "a file being received was abandoned";
      dropReceiving(socket);
    }
  });

  receiveMore(socket, firstBytes);
  return true;
}

void ApiServer::receiveMore(QTcpSocket *socket, const QByteArray &bytes) {
  auto found = mReceiving.find(socket);
  if (found == mReceiving.end() || found->done) {
    return;
  }
  Receiving &receiving = found.value();

  const QByteArray part = bytes.left(int(qMin<qint64>(bytes.size(), receiving.remaining)));
  if (!part.isEmpty() && receiving.file->write(part) != part.size()) {
    const QString why = receiving.file->errorString();
    dropReceiving(socket);
    QJsonObject object;
    object.insert(QStringLiteral("error"),
                  QStringLiteral("it could not be set down: %1").arg(why));
    object.insert(QStringLiteral("code"), QStringLiteral("no_room"));
    socket->write(FormatHttpResponse(507, toJson(object)));
    socket->flush();
    socket->disconnectFromHost();
    return;
  }
  receiving.remaining -= part.size();
  if (receiving.remaining > 0) {
    return;
  }

  // All of it. Up it goes, as a transfer like any other.
  receiving.done = true;
  receiving.file->close();
  const QString staged = receiving.file->fileName();
  const QString folder = receiving.folder;
  RunningJob *job = RemoteOps::UploadReceived(staged, receiving.remote, receiving.into);

  QJsonObject object;
  int status = 202;
  if (job == nullptr) {
    QDir(folder).removeRecursively();
    status = 500;
    object.insert(QStringLiteral("error"), QStringLiteral("rclone could not be started"));
  } else {
    // The staged copy is moved by rclone; what is left of its folder goes
    // when the job ends, however it ends.
    QObject::connect(job, &RunningJob::finished, this,
                     [folder](JobState) { QDir(folder).removeRecursively(); });
    object.insert(QStringLiteral("requestId"), job->requestId());
    object.insert(QStringLiteral("started"), QStringLiteral("upload"));
    object.insert(QStringLiteral("dest"), job->description().dest);
  }
  mReceiving.remove(socket);
  socket->write(FormatHttpResponse(status, toJson(object)));
  socket->flush();
  socket->disconnectFromHost();
}

void ApiServer::dropReceiving(QTcpSocket *socket) {
  auto found = mReceiving.find(socket);
  if (found == mReceiving.end()) {
    return;
  }
  if (found->file != nullptr) {
    found->file->close();
  }
  QDir(found->folder).removeRecursively();
  mReceiving.erase(found);
}

void ApiServer::changeBandwidth(QTcpSocket *socket, const HttpRequest &request,
                                const QString &requestId) {
  socket->setProperty("stream", true);
  QPointer<QTcpSocket> held(socket);
  const auto answer = [held](int status, const QJsonObject &object) {
    if (!held) {
      return;
    }
    held->write(FormatHttpResponse(status, toJson(object)));
    held->flush();
    held->disconnectFromHost();
  };
  const auto refuse = [answer](int status, const QString &code, const QString &why) {
    QJsonObject object;
    object.insert(QStringLiteral("error"), why);
    object.insert(QStringLiteral("code"), code);
    answer(status, object);
  };

  const QString asked = options(request).value(QStringLiteral("rate")).toString().trimmed();
  if (!RcClient::IsRate(asked)) {
    refuse(400, QStringLiteral("bad_rate"),
           QStringLiteral("\"rate\" has to be a speed such as 500k, 2M or 1.5G, "
                          "or off"));
    return;
  }

  QList<RunningJob *> jobs;
  if (!requestId.isEmpty()) {
    RunningJob *job = JobRegistry::instance().find(requestId);
    if (job == nullptr) {
      refuse(404, QStringLiteral("no_such_job"), QStringLiteral("no such job"));
      return;
    }
    if (!job->isRunning() || job->kind() != JobKind::Transfer) {
      refuse(409, QStringLiteral("not_a_running_transfer"),
             QStringLiteral("only a running transfer has a speed to change"));
      return;
    }
    jobs << job;
  } else {
    for (RunningJob *job : JobRegistry::instance().jobs()) {
      if (job->isRunning() && job->kind() == JobKind::Transfer) {
        jobs << job;
      }
    }
    if (jobs.isEmpty()) {
      QJsonObject object;
      object.insert(QStringLiteral("changed"), 0);
      object.insert(QStringLiteral("of"), 0);
      answer(200, object);
      return;
    }
  }

  // Asked of each, and answered when all have said.
  auto left = QSharedPointer<int>::create(int(jobs.size()));
  auto took = QSharedPointer<int>::create(0);
  auto said = QSharedPointer<QString>::create();
  const bool one = !requestId.isEmpty();
  const int asking = int(jobs.size());
  for (RunningJob *job : jobs) {
    qCDebug(rbApp) << "API changing the speed of" << job->requestId() << "to" << asked;
    job->setBandwidth(asked, [=](bool ok, const QString &now) {
      if (ok) {
        *took += 1;
        *said = now;
      }
      if (--*left > 0) {
        return;
      }
      if (one && *took == 0) {
        refuse(409, QStringLiteral("no_remote_control"),
               QStringLiteral("rclone's remote control for this job did not "
                              "answer; it may still be starting"));
        return;
      }
      QJsonObject object;
      object.insert(QStringLiteral("rate"), one ? *said : asked);
      object.insert(QStringLiteral("changed"), *took);
      object.insert(QStringLiteral("of"), asking);
      answer(200, object);
    });
  }
}

void ApiServer::openEventStream(QTcpSocket *socket) {
  socket->setProperty("stream", true);

  // Written by hand rather than through FormatHttpResponse: that one ends
  // the message with a Content-Length, and the length of this is not known
  // because it is not over.
  QByteArray head = "HTTP/1.1 200 OK\r\n";
  head += "Content-Type: text/event-stream\r\n";
  head += "Cache-Control: no-store\r\n";
  head += "X-Content-Type-Options: nosniff\r\n";
  head += "Connection: keep-alive\r\n\r\n";
  socket->write(head);
  socket->flush();

  mListeners.append(socket);
  QObject::connect(socket, &QTcpSocket::disconnected, this,
                   [this, socket]() { mListeners.removeAll(socket); });

  qCDebug(rbApp) << "an event stream opened; listeners=" << mListeners.size();

  // Something straight away, so a client knows it is connected rather than
  // waiting for the first thing to happen -- which on a quiet night is never.
  QJsonObject hello;
  hello.insert(QStringLiteral("queued"), JobQueue::instance().count());
  hello.insert(QStringLiteral("queueRunning"), JobQueue::instance().isRunning());
  hello.insert(QStringLiteral("runningJobs"),
               JobRegistry::instance().runningCount());
  broadcast(QStringLiteral("hello"), hello);

  watchWhatHappens();

  if (mHeartbeat == nullptr) {
    mHeartbeat = new QTimer(this);
    mHeartbeat->setInterval(20000);
    QObject::connect(mHeartbeat, &QTimer::timeout, this, [this]() {
      // A comment, which the event-source protocol ignores. It exists so a
      // connection that died quietly -- a sleeping laptop, a proxy with its
      // own ideas about idle sockets -- is noticed rather than believed.
      for (QTcpSocket *listener : mListeners) {
        listener->write(": still here\n\n");
        listener->flush();
      }
    });
  }
  mHeartbeat->start();
}

bool ApiServer::serveWebUi(const HttpRequest &request, QByteArray *body,
                           QByteArray *contentType, int *status) {
  *contentType = "application/json";

  if (request.method != QLatin1String("GET")) {
    *status = 405;
    *body = problem(QStringLiteral("the web interface is only read"));
    return true;
  }

  // A fixed list, not a directory. Anything that turns a path from outside
  // into a file name is a way to read files that were never meant to be
  // served -- and it is not needed here, because there are three files and
  // they are known when this is compiled.
  static const QHash<QString, QString> pages = {
      {QStringLiteral("/"), QStringLiteral(":/webui/index.html")},
      {QStringLiteral("/index.html"), QStringLiteral(":/webui/index.html")},
      {QStringLiteral("/app.css"), QStringLiteral(":/webui/app.css")},
      {QStringLiteral("/app.js"), QStringLiteral(":/webui/app.js")},
  };

  // The page's words in another language: /i18n/<language>.json. Held to a
  // language's name, and opened from the compiled-in resource, the same as
  // the pictures below.
  if (request.path.startsWith(QLatin1String("/i18n/")) &&
      request.path.endsWith(QLatin1String(".json"))) {
    QString name = request.path.mid(6);
    name.chop(5);
    static const QRegularExpression language(
        QStringLiteral("^[a-z]{2,3}(_[A-Z]{2})?$"));
    QFile words(QStringLiteral(":/webui/i18n/%1.json").arg(name));
    if (!language.match(name).hasMatch() || !words.open(QIODevice::ReadOnly)) {
      *status = 404;
      *body = problem(QStringLiteral("no such language"));
      return true;
    }
    *status = 200;
    *body = words.readAll();
    *contentType = "application/json";
    return true;
  }

  // The pictures the window puts beside each kind of remote, so the page can
  // show the same ones. They are in the application's resources rather than
  // the web interface's, so a build without the window has none and says
  // 404, which a page can live with.
  //
  // The one place a name from outside becomes part of a file name, so it is
  // held to lower-case letters, digits and underscores -- nothing that can
  // climb out of the folder -- and what is opened is a compiled-in resource,
  // not a file on disk.
  static const QString icons = QStringLiteral("/icons/remote/");
  if (request.path.startsWith(icons) &&
      request.path.endsWith(QLatin1String(".png"))) {
    QString name = request.path.mid(icons.size());
    name.chop(4);
    static const QRegularExpression plain(QStringLiteral("^[a-z0-9_]{1,40}$"));

    QString found;
    if (plain.match(name).hasMatch()) {
      const QString folder = QStringLiteral(":/media/images/remotes_icons/");
      // A kind there is no picture for gets the one for "unknown", in the
      // same shade as was asked for.
      const QString fallback = name.endsWith(QLatin1String("_inv"))
                                   ? QStringLiteral("unknown_inv")
                                   : QStringLiteral("unknown");
      for (const QString &candidate : {name, fallback}) {
        if (QFile::exists(folder + candidate + QStringLiteral(".png"))) {
          found = folder + candidate + QStringLiteral(".png");
          break;
        }
      }
    }
    QFile picture(found);
    if (found.isEmpty() || !picture.open(QIODevice::ReadOnly)) {
      *status = 404;
      *body = problem(QStringLiteral("no such picture"));
      return true;
    }
    *status = 200;
    *body = picture.readAll();
    *contentType = "image/png";
    return true;
  }

  const QString resource = pages.value(request.path);
  if (resource.isEmpty()) {
    *status = 404;
    *body = problem(QStringLiteral("no such page"));
    return true;
  }

  QFile file(resource);
  if (!file.open(QIODevice::ReadOnly)) {
    // Only reachable if the resource was left out of the build. Said out
    // loud, because the symptom otherwise is a blank page.
    qCDebug(rbApp) << "the web UI is missing from this build:" << resource;
    *status = 500;
    *body = problem(QStringLiteral("the web interface is not in this build"));
    return true;
  }

  *status = 200;
  *body = file.readAll();
  *contentType = resource.endsWith(QLatin1String(".css"))    ? "text/css"
                 : resource.endsWith(QLatin1String(".js"))   ? "application/javascript"
                                                             : "text/html; charset=utf-8";
  return true;
}

void ApiServer::broadcast(const QString &event, const QJsonObject &data) {
  if (mListeners.isEmpty()) {
    return;
  }

  QByteArray message = "event: " + event.toUtf8() + "\n";
  message += "data: " + toJson(data) + "\n\n";

  for (QTcpSocket *listener : mListeners) {
    listener->write(message);
    listener->flush();
  }
}

void ApiServer::watchWhatHappens() {
  if (mWatching) {
    return;
  }
  mWatching = true;

  const auto sendJob = [this](RunningJob *job) {
    broadcast(QStringLiteral("job"), describe(*job));
  };

  QObject::connect(&JobRegistry::instance(), &JobRegistry::jobStarted, this,
                   [this, sendJob](RunningJob *job) {
                     sendJob(job);
                     // The figures come from rclone about once a second, which
                     // is already the rate a dashboard wants. Forwarding them
                     // beats every client polling for them separately.
                     QObject::connect(job, &RunningJob::statsUpdated, this,
                                      [sendJob, job]() { sendJob(job); });
                   });
  QObject::connect(&JobRegistry::instance(), &JobRegistry::jobFinished, this,
                   sendJob);

  QObject::connect(&JobQueue::instance(), &JobQueue::changed, this, [this]() {
    QJsonObject object;
    object.insert(QStringLiteral("queued"), JobQueue::instance().count());
    object.insert(QStringLiteral("running"), JobQueue::instance().isRunning());
    object.insert(QStringLiteral("runningRequestId"),
                  JobQueue::instance().runningRequestId());
    broadcast(QStringLiteral("queue"), object);
  });

  // A schedule made, changed, switched or deleted, or the whole scheduler
  // switched -- here or in the window.
  QObject::connect(&SchedulerStore::instance(), &SchedulerStore::changed, this,
                   [this]() {
                     QJsonObject object;
                     object.insert(QStringLiteral("changed"), true);
                     object.insert(QStringLiteral("running"),
                                   SchedulerStore::instance().isRunning());
                     broadcast(QStringLiteral("schedule"), object);
                   });

  // A task made, changed or deleted -- here or in the window. Without this a
  // page open beside the window would go on showing a task the window had
  // just deleted, and offering to run it.
  QObject::connect(ListOfJobOptions::getInstance(),
                   &ListOfJobOptions::tasksListUpdated, this, [this]() {
                     QJsonObject object;
                     object.insert(
                         QStringLiteral("count"),
                         int(ListOfJobOptions::getInstance()->getTasks().size()));
                     broadcast(QStringLiteral("tasks"), object);
                   });

  // Held is sent as well as fired. "Nothing happened" is the thing people
  // report, and a dashboard that can only show what did happen leaves them
  // with the same silence as before.
  QObject::connect(&AppCore::instance(), &AppCore::scheduleFired, this,
                   [this](const QString &id, const QString &requestId) {
                     QJsonObject object;
                     object.insert(QStringLiteral("scheduleId"), id);
                     object.insert(QStringLiteral("requestId"), requestId);
                     object.insert(QStringLiteral("fired"), true);
                     broadcast(QStringLiteral("schedule"), object);
                   });
  QObject::connect(&AppCore::instance(), &AppCore::scheduleHeld, this,
                   [this](const QString &id, const QString &reason) {
                     QJsonObject object;
                     object.insert(QStringLiteral("scheduleId"), id);
                     object.insert(QStringLiteral("held"), reason);
                     broadcast(QStringLiteral("schedule"), object);
                   });
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
  // A file on its way in: every byte is the file's. Before the check below,
  // because such a connection is also one this server no longer answers
  // requests on.
  if (mReceiving.contains(socket)) {
    receiveMore(socket, socket->readAll());
    return;
  }

  // Once a connection is an event stream it is ours to write to, not to read
  // from. Anything the client sends down it afterwards is dropped rather
  // than answered into the middle of the stream.
  if (socket->property("stream").toBool()) {
    socket->readAll();
    return;
  }

  QByteArray buffer = socket->property("buffer").toByteArray();
  buffer += socket->readAll();

  // A file being sent is recognised by its head, before its body has come:
  // waiting for the whole of it, as for any other request, would be holding
  // a film in memory.
  {
    HttpRequest head;
    int bodyStart = 0;
    if (ParseHttpHead(buffer, &head, &bodyStart) == HttpParseResult::Complete &&
        head.method == QLatin1String("POST")) {
      const QStringList parts = segmentsOf(head.path);
      if (parts.size() == 3 && parts.at(0) == QLatin1String("remotes") &&
          parts.at(2) == QLatin1String("receive")) {
        socket->setProperty("buffer", QByteArray());
        startReceiving(socket, head, buffer.mid(bodyStart));
        return;
      }
    }
  }

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
  // HEAD is GET with the body cut off. Health checks and proxies send it,
  // and a front page that answers "method not allowed" to a monitor looks
  // exactly like a front page that is broken.
  HttpRequest asked = request;
  const bool headOnly = asked.method == QLatin1String("HEAD");
  if (headOnly) {
    asked.method = QStringLiteral("GET");
  }

  const auto reply = [socket, headOnly](const QByteArray &response) {
    if (!headOnly) {
      socket->write(response);
      return;
    }
    // The headers a GET would have given, including the length it would have
    // had. That is what makes the answer worth anything to whoever asked.
    const int end = response.indexOf("\r\n\r\n");
    socket->write(end < 0 ? response : response.left(end + 4));
  };

  // A stream, which is neither the API nor the page. Answered without a
  // token because a player has no way to send one; the path itself is what
  // lets it in. See relayStream().
  if (request.path.startsWith(QLatin1String(RemoteOps::kStreamPrefix))) {
    relayStream(socket, request);
    return;
  }

  // Anything that is not the API is the Web UI's, and is answered without a
  // token -- including its 404s. See serveWebUi().
  if (!asked.path.startsWith(QLatin1String("/api/"))) {
    QByteArray body;
    QByteArray contentType;
    int status = 0;
    serveWebUi(asked, &body, &contentType, &status);
    reply(FormatHttpResponse(status, body, contentType));
    return;
  }

  // Checked before the path is even looked at. An endpoint that answers
  // without a token is an endpoint somebody forgot to protect, and the way
  // not to forget is for the check not to be per-endpoint at all.
  //
  // /ping is the one exception, and it says nothing: it exists so a client
  // can tell "the server is there but my token is wrong" from "there is no
  // server", which are different problems with different fixes.
  //
  // Three paths are answered without one, and each is here rather than in a
  // route because an exception nobody can see is an exception nobody checks:
  //   /ping   says the server is there and nothing else
  //   /auth   says whether a password has been set, so the page knows what
  //           form to show
  //   /login  is how the password is turned into a session in the first place
  //   /openapi.json  describes the routes and nothing of this installation;
  //           tools that read one expect to fetch it before they have a key
  //   /languages  the languages the page is in: the sign-in form is in one
  static const QStringList open = {QStringLiteral("/api/v1/ping"),
                                   QStringLiteral("/api/v1/auth"),
                                   QStringLiteral("/api/v1/login"),
                                   QStringLiteral("/api/v1/openapi.json"),
                                   QStringLiteral("/api/v1/languages")};

  // Every request is looked up in the list the documentation is made from,
  // and one that is not there is noted. Nothing else changes: this only
  // lets the tests say a route was added to the code and not to the list.
  // See api_routes.h.
  if (ApiRoutes::Match(request.method, asked.path) == nullptr) {
    ApiRoutes::NoteUnlisted(request.method, asked.path);
  }

  const Caller caller = callerOf(asked);
  if (!open.contains(asked.path) && !caller.known) {
    qCDebug(rbApp) << "refused an API request from somebody not signed in"
                   << request.method << asked.path;
    const QString why = QStringLiteral("a token or a session is required");
    reply(FormatHttpResponse(401, problem(why)));
    return;
  }

  // As much as the caller may do. The level each route needs is in the same
  // list the documentation is made from. A route that is not in that list
  // needs everything: what is not described is not handed to a key, so a
  // route added to the code and forgotten in the list fails closed.
  if (!open.contains(asked.path)) {
    const ApiRoutes::Route *route = ApiRoutes::Match(request.method, asked.path);
    const int needs = route != nullptr
                          ? int(ApiAuth::ScopeNamed(route->scope))
                          : int(ApiAuth::Scope::Admin);
    if (caller.scope < needs) {
      qCDebug(rbApp) << "refused" << caller.who << request.method << asked.path
                     << "which needs" << ApiAuth::ScopeName(ApiAuth::Scope(needs));
      QJsonObject object;
      object.insert(QStringLiteral("error"),
                    QStringLiteral("this key may %1; that needs %2")
                        .arg(ApiAuth::ScopeName(ApiAuth::Scope(caller.scope)),
                             ApiAuth::ScopeName(ApiAuth::Scope(needs))));
      object.insert(QStringLiteral("code"), QStringLiteral("insufficient_scope"));
      object.insert(QStringLiteral("needs"),
                    ApiAuth::ScopeName(ApiAuth::Scope(needs)));
      reply(FormatHttpResponse(403, toJson(object)));
      return;
    }
  }
  // Who asked, for the history: "api:sonarr" rather than "api".
  mCallerName = caller.who;

  // Not a route either: this one has to run rclone and wait for it.
  {
    const QStringList parts = segmentsOf(asked.path);
    if (asked.method == QLatin1String("GET") && parts.size() == 3 &&
        parts.at(0) == QLatin1String("remotes") &&
        parts.at(2) == QLatin1String("ls")) {
      if (headOnly) {
        reply(FormatHttpResponse(200, QByteArray()));
        return;
      }
      listDirectory(socket, asked, parts.at(1),
                    asked.queryValue(QStringLiteral("path")));
      return;
    }
    if (asked.method == QLatin1String("GET") && parts.size() == 3 &&
        parts.at(0) == QLatin1String("remotes") &&
        parts.at(2) == QLatin1String("cat")) {
      readFile(socket, asked, parts.at(1));
      return;
    }
  }

  // Nor this: how a remote is set up is read from rclone each time.
  {
    const QStringList parts = segmentsOf(asked.path);
    if (asked.method == QLatin1String("GET") && parts.size() == 3 &&
        parts.at(0) == QLatin1String("remotes") &&
        parts.at(2) == QLatin1String("config")) {
      if (headOnly) {
        reply(FormatHttpResponse(200, QByteArray()));
        return;
      }
      remoteConfig(socket, parts.at(1));
      return;
    }
    if ((asked.method == QLatin1String("PUT") && parts.size() == 3 &&
         parts.at(0) == QLatin1String("remotes") &&
         parts.at(2) == QLatin1String("config")) ||
        (asked.method == QLatin1String("DELETE") && parts.size() == 2 &&
         parts.at(0) == QLatin1String("remotes"))) {
      changeRemote(socket, asked, parts.at(1));
      return;
    }
    if (asked.method == QLatin1String("POST") && parts.size() == 1 &&
        parts.at(0) == QLatin1String("remotes")) {
      changeRemote(socket, asked, QString());
      return;
    }
    if (asked.method == QLatin1String("POST") && parts.size() == 3 &&
        parts.at(0) == QLatin1String("webhooks") &&
        parts.at(2) == QLatin1String("test")) {
      testWebhook(socket, parts.at(1));
      return;
    }
    if (asked.method == QLatin1String("POST") && parts.size() == 3 &&
        parts.at(0) == QLatin1String("remotes") &&
        parts.at(2) == QLatin1String("answer")) {
      changeRemote(socket, asked, parts.at(1));
      return;
    }
    if (asked.method == QLatin1String("POST") &&
        ((parts.size() == 3 && parts.at(0) == QLatin1String("jobs") &&
          parts.at(2) == QLatin1String("bandwidth")) ||
         (parts.size() == 1 && parts.at(0) == QLatin1String("bandwidth")))) {
      changeBandwidth(socket, asked, parts.size() == 3 ? parts.at(1) : QString());
      return;
    }
  }

  // Nor are these: they run rclone and the answer is what it prints.
  {
    const QStringList parts = segmentsOf(asked.path);
    static const QStringList questions = {
        QStringLiteral("size"),  QStringLiteral("about"),
        QStringLiteral("tree"),  QStringLiteral("check"),
        QStringLiteral("export")};
    if (asked.method == QLatin1String("GET") && parts.size() == 3 &&
        parts.at(0) == QLatin1String("remotes") &&
        questions.contains(parts.at(2))) {
      if (headOnly) {
        reply(FormatHttpResponse(200, QByteArray()));
        return;
      }
      remoteQuestion(socket, asked, parts.at(1), parts.at(2));
      return;
    }
  }

  // Nor is this: it runs rclone and answers when rclone has finished.
  {
    const QStringList parts = segmentsOf(asked.path);
    static const QStringList operations = {
        QStringLiteral("mkdir"),   QStringLiteral("rename"),
        QStringLiteral("delete"),  QStringLiteral("link"),
        QStringLiteral("cleanup"), QStringLiteral("dedupe"),
        QStringLiteral("copy"),    QStringLiteral("move"),
        QStringLiteral("mount"),   QStringLiteral("upload"),
        QStringLiteral("download"), QStringLiteral("stream"),
        QStringLiteral("serve")};
    if (asked.method == QLatin1String("POST") && parts.size() == 3 &&
        parts.at(0) == QLatin1String("remotes") &&
        operations.contains(parts.at(2))) {
      remoteOperation(socket, asked, parts.at(1), parts.at(2));
      return;
    }
  }

  // Not a route: a route returns a body and this one never ends.
  if (asked.method == QLatin1String("GET") &&
      asked.path == QLatin1String("/api/v1/events")) {
    if (headOnly) {
      // Answered with what the stream would say it is, and no stream. A HEAD
      // that opened one would be a connection nobody is ever coming back for.
      reply(FormatHttpResponse(200, QByteArray(), "text/event-stream"));
      return;
    }
    openEventStream(socket);
    return;
  }

  QByteArray body;
  QByteArray contentType = "application/json";
  const int status = route(asked, &body, &contentType);
  reply(FormatHttpResponse(status, body, contentType));

  if (mShutdownPending) {
    mShutdownPending = false;

    // Pushed out before anything is stopped. The first version started
    // stopping on the next turn of the event loop, which with nothing running
    // was sixty milliseconds from "accepted" to a process that no longer
    // existed -- and the answer was still sitting in a buffer when it went.
    // The caller got an empty reply to a request that had worked.
    socket->flush();
    QTimer::singleShot(0, this, [this]() { emit shutdownRequested(); });
  }
}

int ApiServer::route(const HttpRequest &request, QByteArray *body,
                     QByteArray *contentType) {
  *contentType = "application/json";

  if (request.method == QLatin1String("GET")) {
    return get(request, body, contentType);
  }
  if (request.method == QLatin1String("POST") ||
      request.method == QLatin1String("PUT") ||
      request.method == QLatin1String("DELETE")) {
    return command(request, body);
  }

  *body = problem(QStringLiteral("that is not a method this answers"));
  return 405;
}

// Everything that changes something, kept in its own function away from the
// reading half. A GET that changes something is the oldest bug in web
// programming, and the surest way not to write one is for the code that
// answers a GET to have no way to.
//
// What is deliberately absent: creating and editing tasks, and anything that
// says where rclone is or what scripts run. A task carries its own rclone
// flags, so writing a task is writing a command line -- which would turn a
// stolen token into "run any program on this machine". Those stay with the
// window until there is a way to say them that is not a command line.
int ApiServer::authCommand(const HttpRequest &request, QByteArray *body) {
  const QJsonObject given = options(request);
  const QString path = request.path;

  if (path == QLatin1String("/api/v1/login")) {
    // Refused before the password is even looked at, so that being locked
    // out costs a guesser the same whether the user name was right or not.
    if (ApiAuth::Attempts::instance().isLockedOut()) {
      const int seconds = ApiAuth::Attempts::instance().secondsRemaining();
      qCDebug(rbApp) << "refused a sign-in: locked out for" << seconds << "s";
      *body = problem(QStringLiteral("too many attempts; wait %1 seconds")
                          .arg(seconds));
      return 429;
    }

    if (!ApiAuth::HasPassword()) {
      *body = problem(QStringLiteral("no password has been set"));
      return 409;
    }

    const QString user = given.value(QStringLiteral("user")).toString();
    const QString password = given.value(QStringLiteral("password")).toString();

    // The user name is checked too, but the answer says only that the pair
    // was wrong: which half was right is not something to hand out.
    if (user != ApiAuth::UserName() ||
        !ApiAuth::PasswordMatches(password, ApiAuth::StoredPassword())) {
      ApiAuth::Attempts::instance().failed();
      qCDebug(rbApp) << "a sign-in was refused";
      *body = problem(QStringLiteral("that is not the right user and password"));
      return 401;
    }

    ApiAuth::Attempts::instance().succeeded();
    const QString session = ApiAuth::Sessions::instance().open();
    qCDebug(rbApp) << "somebody signed in; sessions="
                   << ApiAuth::Sessions::instance().count();

    QJsonObject object;
    object.insert(QStringLiteral("session"), session);
    object.insert(QStringLiteral("idleMinutes"),
                  ApiAuth::Sessions::idleMinutes());
    *body = toJson(object);
    return 200;
  }

  if (path == QLatin1String("/api/v1/logout")) {
    const QString offered = request.header(QStringLiteral("authorization"));
    ApiAuth::Sessions::instance().close(offered.mid(7));
    *body = QByteArray();
    return 204;
  }

  if (path == QLatin1String("/api/v1/password")) {
    const QString current = given.value(QStringLiteral("current")).toString();
    const QString wanted = given.value(QStringLiteral("new")).toString();

    // Setting the first password needs no old one -- there is none -- but the
    // request still had to carry the token to get this far. Changing an
    // existing one needs the old one even so: a page left open on an
    // unattended machine must not be a way to take the password over.
    if (ApiAuth::HasPassword() &&
        !ApiAuth::PasswordMatches(current, ApiAuth::StoredPassword())) {
      *body = problem(QStringLiteral("the current password is wrong"));
      return 403;
    }

    if (wanted.size() < 8) {
      *body = problem(QStringLiteral("a password needs at least 8 characters"));
      return 400;
    }

    ApiAuth::SetPassword(wanted);
    qCDebug(rbApp) << "the administrator password was set";

    // Everybody was signed out by the change, including whoever asked for
    // it, so they are given a session back rather than being thrown out of
    // the page they are standing in.
    QJsonObject object;
    object.insert(QStringLiteral("session"),
                  ApiAuth::Sessions::instance().open());
    *body = toJson(object);
    return 200;
  }

  return 0; // not one of ours
}

int ApiServer::command(const HttpRequest &request, QByteArray *body) {
  const QStringList parts = segmentsOf(request.path);
  const bool post = request.method == QLatin1String("POST");

  if (post) {
    const int status = authCommand(request, body);
    if (status != 0) {
      return status;
    }
  }

  // shutdown -- the polite way to stop a daemon from somewhere that cannot
  // send it a signal.
  if (post && parts.size() == 1 && parts.at(0) == QLatin1String("shutdown")) {
    if (!mShutdownAllowed) {
      *body = problem(QStringLiteral(
          "this is running as a window; close it from there"));
      return 409;
    }
    if (AppCore::instance().isStopping()) {
      *body = problem(QStringLiteral("it is already stopping"));
      return 409;
    }

    qCDebug(rbApp) << "API asked the program to stop";

    // Only noted here. answer() acts on it once this reply has actually been
    // handed to the socket -- see there for why "after" has to mean that.
    mShutdownPending = true;

    QJsonObject object;
    object.insert(QStringLiteral("stopping"), true);
    object.insert(QStringLiteral("runningJobs"),
                  JobRegistry::instance().runningCount());
    *body = toJson(object);
    return 202;
  }

  // Nothing new is started once stopping has begun. Refused with the reason
  // rather than started and immediately killed.
  if (post && AppCore::instance().isStopping()) {
    *body = problem(QStringLiteral("the program is shutting down"));
    return 503;
  }

  // ---- writing tasks -------------------------------------------------------
  //
  // A task carries rclone flags of its own choosing ("extra"), so writing one
  // is writing a command line. That was the reason this was left out. It is
  // in now, with the flags held to two rules (extra_flags.h): one has to be
  // a flag this rclone has, and the ones that run a program or move rclone's
  // own files are refused whatever rclone thinks of them. The fields of a
  // mount, its script among them, are not written here at all.
  const bool put = request.method == QLatin1String("PUT");
  const bool del = request.method == QLatin1String("DELETE");

  const auto refuseTask = [body](int status, const QString &code,
                                 const QString &why) {
    QJsonObject object;
    object.insert(QStringLiteral("error"), why);
    object.insert(QStringLiteral("code"), code);
    *body = toJson(object);
    return status;
  };

  // tasks/preview -- the command a task would run, without saving anything.
  // What the window's dialog shows on its "rclone cmd" tab.
  if (post && parts.size() == 2 && parts.at(0) == QLatin1String("tasks") &&
      parts.at(1) == QLatin1String("preview")) {
    const QJsonObject given = options(request);
    JobOptions task(given.value(QStringLiteral("type")).toString() ==
                    QLatin1String("download"));
    task.operation = JobOptions::Copy;

    QString why;
    if (!TaskJson::Read(given, &task, &why)) {
      return refuseTask(400, QStringLiteral("bad_field"), why);
    }
    why = flagsProblem(task.extra);
    if (!why.isEmpty()) {
      return refuseTask(400, QStringLiteral("bad_flag"), why);
    }

    QStringList command = task.getOptions();
    if (given.value(QStringLiteral("dryRun")).toBool(false)) {
      command << QStringLiteral("--dry-run");
    }
    QJsonObject object;
    object.insert(QStringLiteral("command"),
                  RedactArgs(GetRcloneCmd(command)).join(QLatin1Char(' ')));
    *body = toJson(object);
    return 200;
  }

  // tasks -- make one
  if (post && parts.size() == 1 && parts.at(0) == QLatin1String("tasks")) {
    const QJsonObject given = options(request);
    if (given.isEmpty()) {
      return refuseTask(400, QStringLiteral("bad_field"),
                        QStringLiteral("a task needs to be described"));
    }

    auto *task = new JobOptions(
        given.value(QStringLiteral("type")).toString() ==
        QLatin1String("download"));
    task->operation = JobOptions::Copy;
    task->syncTiming = JobOptions::During;

    QString why;
    if (!TaskJson::Read(given, task, &why)) {
      delete task;
      return refuseTask(400, QStringLiteral("bad_field"), why);
    }
    why = flagsProblem(task->extra);
    if (!why.isEmpty()) {
      delete task;
      return refuseTask(400, QStringLiteral("bad_flag"), why);
    }
    // Checked before the path is looked at, so a task with nothing to send
    // is told so rather than told its nothing is out of bounds.
    TaskBuilder::Problem problem = TaskBuilder::Check(*task);
    if (problem != TaskBuilder::Problem::None &&
        problem != TaskBuilder::Problem::NoName) {
      delete task;
      return refuseTask(400, QStringLiteral("incomplete"),
                        TaskBuilder::Explain(problem));
    }
    const TaskRefusal ends = endsProblem(*task, nullptr);
    if (ends.status != 0) {
      delete task;
      return refuseTask(ends.status, ends.code, ends.why);
    }
    // Whether an upload is of a folder is read off the disk when nobody
    // said. It is only a description -- the paths are what run -- but a
    // wrong one shows the wrong icon for ever after.
    if (!given.contains(QStringLiteral("isFolder")) &&
        task->jobType == JobOptions::Upload) {
      task->isFolder = QFileInfo(task->source).isDir();
    }

    const QString id =
        TaskBuilder::Create(task, QDateTime::currentDateTime(), &problem);
    if (id.isEmpty()) {
      delete task;
      return refuseTask(400, QStringLiteral("incomplete"),
                        TaskBuilder::Explain(problem));
    }
    qCDebug(rbApp) << "API created task" << id << task->description;
    *body = toJson(describe(*task));
    return 201;
  }

  // tasks/{id} -- change one, or take it away
  if ((put || del) && parts.size() == 2 &&
      parts.at(0) == QLatin1String("tasks")) {
    JobOptions *task = ListOfJobOptions::getInstance()->find(parts.at(1));
    if (task == nullptr) {
      return refuseTask(404, QStringLiteral("no_such_task"),
                        QStringLiteral("no such task"));
    }

    if (del) {
      if (alreadyRunning(parts.at(1))) {
        return refuseTask(409, QStringLiteral("running"),
                          QStringLiteral("that task is running; stop it first"));
      }
      // Nor one a schedule still points at: the schedule would be left
      // armed with nothing to run. The window does not offer the delete
      // either; the schedule goes first.
      for (const Schedule &schedule : SchedulerStore::instance().schedules()) {
        if (schedule.taskId == task->uniqueId.toString()) {
          return refuseTask(
              409, QStringLiteral("scheduled"),
              QStringLiteral("the schedule \"%1\" runs that task; delete "
                             "the schedule first")
                  .arg(schedule.name));
        }
      }
      qCDebug(rbApp) << "API deleted task" << parts.at(1) << task->description;
      ListOfJobOptions::getInstance()->Forget(task);
      // Whatever was waiting to run it has nothing to run now.
      JobQueue::instance().dropMissingTasks();
      QJsonObject object;
      object.insert(QStringLiteral("deleted"), parts.at(1));
      *body = toJson(object);
      return 200;
    }

    JobOptions changed = *task;
    QString why;
    if (!TaskJson::Read(options(request), &changed, &why)) {
      return refuseTask(400, QStringLiteral("bad_field"), why);
    }
    const TaskBuilder::Problem problem = TaskBuilder::Check(changed);
    if (problem != TaskBuilder::Problem::None) {
      return refuseTask(400, QStringLiteral("incomplete"),
                        TaskBuilder::Explain(problem));
    }
    // Judged only if it was touched, like the ends: a task that already
    // carries a flag the window let it have can still be renamed.
    if (changed.extra != task->extra) {
      why = flagsProblem(changed.extra);
      if (!why.isEmpty()) {
        return refuseTask(400, QStringLiteral("bad_flag"), why);
      }
    }
    const TaskRefusal ends = endsProblem(changed, task);
    if (ends.status != 0) {
      return refuseTask(ends.status, ends.code, ends.why);
    }

    *task = changed;
    ListOfJobOptions::getInstance()->Persist(task);
    qCDebug(rbApp) << "API changed task" << parts.at(1) << task->description;
    *body = toJson(describe(*task));
    return 200;
  }

  // tasks/{id}/run and tasks/{id}/enqueue
  // tasks/{id}/watch -- run it when its folder changes, or stop
  if ((post || del) && parts.size() == 3 && parts.at(0) == QLatin1String("tasks") &&
      parts.at(2) == QLatin1String("watch")) {
    const QString id = parts.at(1);
    if (ListOfJobOptions::getInstance()->find(id) == nullptr) {
      return refuseTask(404, QStringLiteral("no_such_task"),
                        QStringLiteral("there is no such task"));
    }
    if (del) {
      if (!FolderWatches::instance().unwatch(id)) {
        return refuseTask(404, QStringLiteral("not_watched"),
                          QStringLiteral("its folder is not being watched"));
      }
    } else {
      QString why;
      if (!FolderWatches::instance().watch(
              id, options(request).value(QStringLiteral("quietSeconds")).toInt(30),
              &why)) {
        return refuseTask(400, QStringLiteral("cannot_watch"), why);
      }
      qCDebug(rbApp) << "API watching the folder of task" << id;
    }
    QJsonObject object;
    object.insert(QStringLiteral("taskId"), id);
    object.insert(QStringLiteral("watched"), !del);
    *body = toJson(object);
    return 200;
  }

  if (post && parts.size() == 3 && parts.at(0) == QLatin1String("tasks")) {
    const QString id = parts.at(1);
    const QString verb = parts.at(2);

    JobOptions *task = ListOfJobOptions::getInstance()->find(id);
    if (task == nullptr) {
      *body = problem(QStringLiteral("no such task"));
      return 404;
    }

    if (verb == QLatin1String("enqueue")) {
      const QString requestId =
          JobQueue::instance().enqueue(id, wantsDryRun(request));
      qCDebug(rbApp) << "API queued task" << id << "request=" << requestId;
      *body = startedJob(requestId, QStringLiteral("queued"));
      return 202;
    }

    if (verb == QLatin1String("run")) {
      if (alreadyRunning(id)) {
        // Refused rather than started. Two copies of one task write one
        // destination, and a caller asking twice is far more likely to be a
        // dashboard's double click than a deliberate second copy.
        *body = problem(QStringLiteral("that task is already running"));
        return 409;
      }

      const QString requestId = QUuid::createUuid().toString();
      qCDebug(rbApp) << "API running task" << id << "request=" << requestId;
      // The first run of a bisync. Asked for by name, never assumed.
      const bool resync =
          options(request).value(QStringLiteral("resync")).toBool(false);
      if (resync && task->operation != JobOptions::Bisync) {
        *body = problem(QStringLiteral("only a bisync is resynced"));
        return 400;
      }
      StartTask(task, mCallerName.isEmpty() ? QStringLiteral("api") : mCallerName,
                requestId, wantsDryRun(request), resync);
      *body = startedJob(requestId, QStringLiteral("now"));
      return 202;
    }
  }

  // ---- writing schedules --------------------------------------------------
  //
  // A schedule names a task that already exists and says when. It carries
  // no command of its own, so unlike a task there is nothing in one that
  // could be turned into running something else: the worst a schedule can
  // do is run a task that is already there, at a bad time.

  // ---- keys ---------------------------------------------------------------

  // keys -- make one. The key is in this answer and nowhere else.
  if (post && parts.size() == 1 && parts.at(0) == QLatin1String("keys")) {
    const QJsonObject given = options(request);
    ApiAuth::Key made;
    QString why;
    const QString key = ApiAuth::Keys::instance().create(
        given.value(QStringLiteral("name")).toString(),
        ApiAuth::ScopeNamed(given.value(QStringLiteral("scope")).toString()),
        &made, &why);
    if (key.isEmpty()) {
      return refuseTask(400, QStringLiteral("bad_field"), why);
    }
    qCDebug(rbApp) << "API made a key" << made.id << made.name
                   << ApiAuth::ScopeName(made.scope);
    QJsonObject object = describeKey(made);
    object.insert(QStringLiteral("key"), key);
    *body = toJson(object);
    return 201;
  }

  // ---- webhooks -------------------------------------------------------------

  if (post && parts.size() == 1 && parts.at(0) == QLatin1String("webhooks")) {
    QString why;
    const QString id = Webhooks::instance().add(hookFrom(options(request)), &why);
    if (id.isEmpty()) {
      return refuseTask(400, QStringLiteral("bad_field"), why);
    }
    qCDebug(rbApp) << "API made webhook" << id;
    *body = toJson(describeHook(*Webhooks::instance().find(id)));
    return 201;
  }
  if (put && parts.size() == 2 && parts.at(0) == QLatin1String("webhooks")) {
    if (Webhooks::instance().find(parts.at(1)) == nullptr) {
      return refuseTask(404, QStringLiteral("no_such_webhook"),
                        QStringLiteral("there is no such webhook"));
    }
    const QJsonObject given = options(request);
    QString why;
    if (!Webhooks::instance().update(
            parts.at(1), hookFrom(given),
            given.value(QStringLiteral("clearSecret")).toBool(false), &why)) {
      return refuseTask(400, QStringLiteral("bad_field"), why);
    }
    *body = toJson(describeHook(*Webhooks::instance().find(parts.at(1))));
    return 200;
  }
  if (del && parts.size() == 2 && parts.at(0) == QLatin1String("webhooks")) {
    if (!Webhooks::instance().remove(parts.at(1))) {
      return refuseTask(404, QStringLiteral("no_such_webhook"),
                        QStringLiteral("there is no such webhook"));
    }
    QJsonObject object;
    object.insert(QStringLiteral("deleted"), parts.at(1));
    *body = toJson(object);
    return 200;
  }

  // keys/{id} -- take one back
  if (del && parts.size() == 2 && parts.at(0) == QLatin1String("keys")) {
    if (!ApiAuth::Keys::instance().remove(parts.at(1))) {
      return refuseTask(404, QStringLiteral("no_such_key"),
                        QStringLiteral("there is no such key"));
    }
    qCDebug(rbApp) << "API took back key" << parts.at(1);
    QJsonObject object;
    object.insert(QStringLiteral("deleted"), parts.at(1));
    *body = toJson(object);
    return 200;
  }

  // scheduler/start, scheduler/pause -- the whole scheduler, as the window's
  // two buttons do. Paused, it keeps its schedules and starts nothing.
  if (post && parts.size() == 2 && parts.at(0) == QLatin1String("scheduler") &&
      (parts.at(1) == QLatin1String("start") ||
       parts.at(1) == QLatin1String("pause"))) {
    if (parts.at(1) == QLatin1String("start")) {
      SchedulerStore::instance().start();
    } else {
      SchedulerStore::instance().pause();
    }
    qCDebug(rbApp) << "API switched the scheduler" << parts.at(1);
    QJsonObject object;
    object.insert(QStringLiteral("running"),
                  SchedulerStore::instance().isRunning());
    *body = toJson(object);
    return 200;
  }

  // What a schedule has to be about: a task there is, and one that is a
  // transfer. A mount runs until it is taken down, which is not something
  // to start again every morning; the window does not offer it either.
  const auto taskFor = [&refuseTask](Schedule *schedule, int *status) {
    const JobOptions *task =
        ListOfJobOptions::getInstance()->find(schedule->taskId);
    if (task == nullptr) {
      *status = refuseTask(
          400, QStringLiteral("no_such_task"),
          QStringLiteral("a schedule runs a task; \"taskId\" has to be the id "
                         "of one there is"));
      return false;
    }
    if (task->operation == JobOptions::Mount) {
      *status = refuseTask(400, QStringLiteral("not_a_transfer"),
                           QStringLiteral("a mount cannot be scheduled"));
      return false;
    }
    schedule->taskName = task->description;
    if (schedule->name.isEmpty()) {
      schedule->name = task->description;
    }
    return true;
  };

  // schedules -- make one
  // backups -- a task and its schedule, from a few words. See backup_plan.h.
  if (post && parts.size() == 1 && parts.at(0) == QLatin1String("backups")) {
    const QJsonObject given = options(request);
    BackupPlan plan;
    plan.name = given.value(QStringLiteral("name")).toString();
    plan.source = given.value(QStringLiteral("source")).toString();
    plan.dest = given.value(QStringLiteral("dest")).toString();
    plan.mirror = given.value(QStringLiteral("mirror")).toBool(true);
    plan.keepVersions = given.value(QStringLiteral("keepVersions")).toBool(true);
    for (const QJsonValue &day : given.value(QStringLiteral("days")).toArray()) {
      plan.days << day.toString();
    }
    plan.time = given.value(QStringLiteral("time")).toString(QStringLiteral("02:00"));

    auto *task = new JobOptions(false);
    Schedule schedule;
    QString why;
    if (!Backup::Build(plan, task, &schedule, &why)) {
      delete task;
      return refuseTask(400, QStringLiteral("bad_field"), why);
    }
    const TaskRefusal ends = endsProblem(*task, nullptr);
    if (ends.status != 0) {
      delete task;
      return refuseTask(ends.status, ends.code, ends.why);
    }
    // The old versions are a folder too, beside the backup: on this machine,
    // it has to be one the API may use, as the backup itself is.
    if (plan.keepVersions && task->jobType == JobOptions::Download &&
        LocalFiles::Allowed(Backup::VersionsFolder(task->dest)).isEmpty()) {
      delete task;
      return refuseTask(403, QStringLiteral("outside_roots"),
                        QStringLiteral("the old versions would go beside the "
                                       "backup, outside the folders the API "
                                       "may use"));
    }
    if (!Backup::Save(task, &schedule, &why)) {
      delete task;
      return refuseTask(400, QStringLiteral("incomplete"), why);
    }
    qCDebug(rbApp) << "API made a backup" << task->description;
    QJsonObject made;
    made.insert(QStringLiteral("task"), describe(*task));
    made.insert(QStringLiteral("schedule"), describe(schedule));
    *body = toJson(made);
    return 201;
  }

  if (post && parts.size() == 1 && parts.at(0) == QLatin1String("schedules")) {
    Schedule schedule;
    schedule.id = QUuid::createUuid().toString();

    QString why;
    if (!ScheduleJson::Read(options(request), &schedule, &why)) {
      return refuseTask(400, QStringLiteral("bad_field"), why);
    }
    int status = 0;
    if (!taskFor(&schedule, &status)) {
      return status;
    }
    SchedulerStore::instance().add(schedule);
    qCDebug(rbApp) << "API created schedule" << schedule.id << schedule.name;
    *body = toJson(describe(schedule));
    return 201;
  }

  // schedules/{id} -- change one, or take it away
  if ((put || del) && parts.size() == 2 &&
      parts.at(0) == QLatin1String("schedules")) {
    const Schedule *found = SchedulerStore::instance().find(parts.at(1));
    if (found == nullptr) {
      return refuseTask(404, QStringLiteral("no_such_schedule"),
                        QStringLiteral("there is no such schedule"));
    }

    if (del) {
      qCDebug(rbApp) << "API deleted schedule" << found->id << found->name;
      // A run it has already started goes on; it only loses what would
      // have started the next one.
      SchedulerStore::instance().remove(parts.at(1));
      QJsonObject object;
      object.insert(QStringLiteral("deleted"), parts.at(1));
      *body = toJson(object);
      return 200;
    }

    Schedule changed = *found;
    QString why;
    if (!ScheduleJson::Read(options(request), &changed, &why)) {
      return refuseTask(400, QStringLiteral("bad_field"), why);
    }
    int status = 0;
    if (!taskFor(&changed, &status)) {
      return status;
    }
    SchedulerStore::instance().update(changed);
    qCDebug(rbApp) << "API changed schedule" << changed.id << changed.name;
    *body = toJson(describe(changed));
    return 200;
  }

  // schedules/{id}/enable, schedules/{id}/disable -- one schedule's own
  // switch, which is the thing most often changed about one.
  if (post && parts.size() == 3 && parts.at(0) == QLatin1String("schedules") &&
      (parts.at(2) == QLatin1String("enable") ||
       parts.at(2) == QLatin1String("disable"))) {
    const Schedule *found = SchedulerStore::instance().find(parts.at(1));
    if (found == nullptr) {
      return refuseTask(404, QStringLiteral("no_such_schedule"),
                        QStringLiteral("there is no such schedule"));
    }
    Schedule changed = *found;
    changed.active = parts.at(2) == QLatin1String("enable");
    SchedulerStore::instance().update(changed);
    qCDebug(rbApp) << "API switched schedule" << changed.name << parts.at(2);
    *body = toJson(describe(changed));
    return 200;
  }

  // schedules/{id}/run -- the road the clock takes, not a second copy of
  // "run this schedule".
  if (post && parts.size() == 3 && parts.at(0) == QLatin1String("schedules") &&
      parts.at(2) == QLatin1String("run")) {
    QString reason;
    const QString requestId = AppCore::instance().runNow(parts.at(1), &reason);
    if (requestId.isEmpty()) {
      // The reason is passed on rather than flattened into "could not run".
      // "its last run has not finished" and "there is no such schedule" are
      // different problems, and saying which is the whole point.
      *body = problem(reason);
      return reason == QLatin1String("there is no such schedule") ? 404 : 409;
    }
    *body = startedJob(requestId, QStringLiteral("schedule"));
    return 202;
  }

  // jobs/{requestId}/stop
  if (post && parts.size() == 3 && parts.at(0) == QLatin1String("jobs") &&
      parts.at(2) == QLatin1String("stop")) {
    RunningJob *job = JobRegistry::instance().find(parts.at(1));
    if (job == nullptr) {
      *body = problem(QStringLiteral("no such job"));
      return 404;
    }
    if (!job->isRunning()) {
      *body = problem(QStringLiteral("that job has already finished"));
      return 409;
    }

    qCDebug(rbApp) << "API stopping job" << job->requestId();
    job->stop();

    // Asked, not done: a mount can refuse to come down while a file on it is
    // open, and the ending arrives through the same signals as any other.
    QJsonObject object;
    object.insert(QStringLiteral("requestId"), job->requestId());
    object.insert(QStringLiteral("stopping"), true);
    *body = toJson(object);
    return 202;
  }

  // queue/start and queue/pause
  if (post && parts.size() == 2 && parts.at(0) == QLatin1String("queue")) {
    if (parts.at(1) == QLatin1String("start")) {
      JobQueue::instance().start();
    } else if (parts.at(1) == QLatin1String("pause")) {
      JobQueue::instance().pause();
    } else if (parts.at(1) == QLatin1String("clear")) {
      // Everything that is waiting. The entry that is running stays: it is
      // a job now, and stopping it is how that one ends -- the same thing
      // the window's button has always promised.
      const int before = JobQueue::instance().count();
      JobQueue::instance().clear();
      qCDebug(rbApp) << "API cleared the queue of"
                     << before - JobQueue::instance().count();
      QJsonObject object;
      object.insert(QStringLiteral("removed"),
                    before - JobQueue::instance().count());
      object.insert(QStringLiteral("queued"), JobQueue::instance().count());
      *body = toJson(object);
      return 200;
    } else {
      *body = problem(QStringLiteral("the queue does not do that"));
      return 404;
    }

    qCDebug(rbApp) << "API set the queue running to"
                   << JobQueue::instance().isRunning();
    QJsonObject object;
    object.insert(QStringLiteral("running"), JobQueue::instance().isRunning());
    object.insert(QStringLiteral("queued"), JobQueue::instance().count());
    *body = toJson(object);
    return 200;
  }

  // queue/{requestId}/move -- a waiting entry put somewhere else in the
  // line. {"to": 0} is a place counted from the front; "up", "down", "top"
  // and "bottom" are what the window's buttons say, and what a person
  // clicking means without having to count.
  if (post && parts.size() == 3 && parts.at(0) == QLatin1String("queue") &&
      parts.at(2) == QLatin1String("move")) {
    const QList<QueueEntry> &entries = JobQueue::instance().entries();
    int from = -1;
    for (int i = 0; i < entries.size(); ++i) {
      if (entries.at(i).requestId == parts.at(1)) {
        from = i;
      }
    }
    if (from < 0) {
      *body = problem(QStringLiteral("nothing in the queue by that name"));
      return 404;
    }

    const QJsonValue asked = options(request).value(QStringLiteral("to"));
    // The front place is not on offer while it is running: everything the
    // queue does depends on the running job being first.
    const int first = JobQueue::instance().taskIsRunning() ? 1 : 0;
    const int last = int(entries.size()) - 1;
    int to = -1;
    if (asked.isDouble()) {
      to = asked.toInt();
    } else if (asked.toString() == QLatin1String("up")) {
      to = from - 1;
    } else if (asked.toString() == QLatin1String("down")) {
      to = from + 1;
    } else if (asked.toString() == QLatin1String("top")) {
      to = first;
    } else if (asked.toString() == QLatin1String("bottom")) {
      to = last;
    } else {
      *body = problem(QStringLiteral(
          "\"to\" has to be a place in the line, or up, down, top or bottom"));
      return 400;
    }

    if (from == to) {
      // Already there. Said as done rather than refused: asking for the top
      // when already at the top is not a mistake worth an error.
      QJsonObject object;
      object.insert(QStringLiteral("at"), from);
      *body = toJson(object);
      return 200;
    }
    if (to < first || to > last) {
      *body = problem(to < first && first == 1
                          ? QStringLiteral("the first place is the one running")
                          : QStringLiteral("there is no such place in the line"));
      return 409;
    }
    if (!JobQueue::instance().move(from, to)) {
      *body = problem(QStringLiteral("that one is running and stays first"));
      return 409;
    }
    qCDebug(rbApp) << "API moved queue entry" << parts.at(1) << from << "->"
                   << to;
    QJsonObject object;
    object.insert(QStringLiteral("at"), to);
    *body = toJson(object);
    return 200;
  }

  // queue/{requestId} -- take one waiting entry out
  if (!post && parts.size() == 2 && parts.at(0) == QLatin1String("queue")) {
    const QString requestId = parts.at(1);

    bool found = false;
    for (const QueueEntry &entry : JobQueue::instance().entries()) {
      found = found || entry.requestId == requestId;
    }
    if (!found) {
      *body = problem(QStringLiteral("nothing in the queue by that name"));
      return 404;
    }
    if (requestId == JobQueue::instance().runningRequestId()) {
      // Taking out the entry that is running would leave rclone going with
      // nothing holding it. Stopping the job is how that one ends, and
      // saying so beats half doing it.
      *body =
          problem(QStringLiteral("that one is running; stop the job instead"));
      return 409;
    }

    JobQueue::instance().remove(requestId);
    qCDebug(rbApp) << "API removed queue entry" << requestId;
    *body = QByteArray();
    return 204;
  }

  if (isSomethingWeCanShow(request.path)) {
    *body = problem(QStringLiteral("that endpoint can be read, not commanded"));
    return 405;
  }

  *body = problem(QStringLiteral("no such endpoint"));
  return 404;
}

int ApiServer::get(const HttpRequest &request, QByteArray *body,
                   QByteArray *contentType) {
  const QString &path = request.path;

  if (path == QLatin1String("/api/v1/ping")) {
    QJsonObject object;
    object.insert(QStringLiteral("ok"), true);
    *body = toJson(object);
    return 200;
  }

  // The kinds of remote there are, and what each can be set up with.
  if (path == QLatin1String("/api/v1/providers") ||
      path.startsWith(QLatin1String("/api/v1/providers/"))) {
    RcloneConfigRegistry &config = RcloneConfigRegistry::instance();
    config.ensureLoaded();

    if (path == QLatin1String("/api/v1/providers")) {
      QJsonArray array;
      for (const Provider &provider : config.providers()) {
        QJsonObject object;
        object.insert(QStringLiteral("name"), provider.name);
        object.insert(QStringLiteral("description"), provider.description);
        object.insert(QStringLiteral("prefix"), provider.prefix);
        object.insert(QStringLiteral("options"), int(provider.options.size()));
        array.append(object);
      }
      QJsonObject object;
      object.insert(QStringLiteral("loaded"), config.isLoaded());
      object.insert(QStringLiteral("providers"), array);
      *body = toJson(object);
      return 200;
    }

    const Provider *provider = FindProvider(
        config.providers(), path.mid(QStringLiteral("/api/v1/providers/").size()));
    if (provider == nullptr) {
      *body = problem(config.isLoaded()
                          ? QStringLiteral("no such kind of remote")
                          : QStringLiteral("the kinds of remote have not been "
                                           "read from rclone yet"));
      return 404;
    }
    QJsonArray options;
    for (const ProviderOption &option : provider->options) {
      QJsonObject o;
      o.insert(QStringLiteral("name"), option.name);
      o.insert(QStringLiteral("help"), option.help);
      o.insert(QStringLiteral("type"), option.type);
      o.insert(QStringLiteral("default"), option.defaultValue);
      o.insert(QStringLiteral("required"), option.required);
      o.insert(QStringLiteral("password"), option.isPassword);
      o.insert(QStringLiteral("sensitive"), option.sensitive);
      o.insert(QStringLiteral("advanced"), option.advanced);
      o.insert(QStringLiteral("exclusive"), option.exclusive);
      o.insert(QStringLiteral("hidden"), option.hide != 0);
      QJsonArray examples;
      for (const auto &example : option.examples) {
        examples.append(QJsonObject{{QStringLiteral("value"), example.first},
                                    {QStringLiteral("help"), example.second}});
      }
      o.insert(QStringLiteral("examples"), examples);
      options.append(o);
    }
    QJsonObject object;
    object.insert(QStringLiteral("name"), provider->name);
    object.insert(QStringLiteral("description"), provider->description);
    object.insert(QStringLiteral("prefix"), provider->prefix);
    object.insert(QStringLiteral("options"), options);
    *body = toJson(object);
    return 200;
  }

  // The languages the page can be shown in. Open: a sign-in page is in one.
  if (path == QLatin1String("/api/v1/languages")) {
    QJsonArray languages;
    const QStringList found = QDir(QStringLiteral(":/webui/i18n"))
                                  .entryList({QStringLiteral("*.json")}, QDir::Files,
                                             QDir::Name);
    for (QString name : found) {
      name.chop(5);
      const QLocale locale(name);
      QJsonObject one;
      one.insert(QStringLiteral("language"), name);
      const QString own = locale.nativeLanguageName();
      one.insert(QStringLiteral("name"),
                 own.isEmpty() ? name : own.left(1).toUpper() + own.mid(1));
      languages.append(one);
    }
    *body = toJson(languages);
    return 200;
  }

  if (path == QLatin1String("/api/v1/watches")) {
    QJsonArray array;
    for (const FolderWatch &watch : FolderWatches::instance().list()) {
      QJsonObject object;
      object.insert(QStringLiteral("taskId"), watch.taskId);
      object.insert(QStringLiteral("quietSeconds"), watch.quietSeconds);
      object.insert(QStringLiteral("lastChange"),
                    watch.lastChange.isValid() ? JobJson::Stamp(watch.lastChange)
                                               : QString());
      object.insert(QStringLiteral("lastQueued"),
                    watch.lastQueued.isValid() ? JobJson::Stamp(watch.lastQueued)
                                               : QString());
      array.append(object);
    }
    QJsonObject object;
    object.insert(QStringLiteral("pollSeconds"), FolderWatches::PollSeconds());
    object.insert(QStringLiteral("watches"), array);
    *body = toJson(object);
    return 200;
  }

  if (path == QLatin1String("/api/v1/webhooks")) {
    QJsonArray array;
    for (const Webhook &hook : Webhooks::instance().list()) {
      array.append(describeHook(hook));
    }
    *body = toJson(array);
    return 200;
  }

  if (path == QLatin1String("/api/v1/keys")) {
    QJsonArray array;
    for (const ApiAuth::Key &key : ApiAuth::Keys::instance().list()) {
      array.append(describeKey(key));
    }
    *body = toJson(array);
    return 200;
  }

  // The routes, described. Made from ApiRoutes, the same list every request
  // is checked against, so it cannot describe an API this is not.
  if (path == QLatin1String("/api/v1/openapi.json")) {
#ifdef RCLONE_BROWSER_VERSION
    const QString version = QStringLiteral(RCLONE_BROWSER_VERSION).trimmed();
#else
    const QString version = QStringLiteral("dev");
#endif
    *body = QJsonDocument(ApiRoutes::OpenApi(version))
                .toJson(QJsonDocument::Indented);
    return 200;
  }

  // Answered without a token, and says only what a sign-in page has to know
  // to draw itself. Whether a password exists is not a secret: the page has
  // to show either a password box or a token box, and guessing wrong at it
  // is worse than saying.
  if (path == QLatin1String("/api/v1/auth")) {
    QJsonObject object;
    object.insert(QStringLiteral("passwordSet"), ApiAuth::HasPassword());
    object.insert(QStringLiteral("user"), ApiAuth::UserName());
    object.insert(QStringLiteral("lockedOut"),
                  ApiAuth::Attempts::instance().isLockedOut());
    object.insert(QStringLiteral("lockSeconds"),
                  ApiAuth::Attempts::instance().secondsRemaining());
    *body = toJson(object);
    return 200;
  }

  if (path == QLatin1String("/api/v1/system")) {
    // Everything moving at once, for a dashboard: the speeds of the running
    // transfers added up.
    double speed = 0;
    int transferring = 0;
    for (const RunningJob *job : JobRegistry::instance().jobs()) {
      if (job->isRunning() && job->kind() == JobKind::Transfer) {
        ++transferring;
        speed += job->stats().valid ? job->stats().speed : 0;
      }
    }
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
    object.insert(QStringLiteral("transferring"), transferring);
    object.insert(QStringLiteral("speed"), speed); // bytes per second, all together
    // What POST /remotes/{name}/serve takes, so a page need not know.
    QJsonArray protocols;
    for (const QString &protocol : RemoteOps::ServeProtocols()) {
      QJsonObject one;
      one.insert(QStringLiteral("protocol"), protocol);
      one.insert(QStringLiteral("defaultPort"),
                 int(RemoteOps::DefaultServePort(protocol)));
      protocols.append(one);
    }
    object.insert(QStringLiteral("serveProtocols"), protocols);
    *body = toJson(object);
    return 200;
  }

  if (path == QLatin1String("/api/v1/remotes")) {
    QJsonArray array;
    for (const Remote &remote : RemoteRegistry::instance().remotes()) {
      QJsonObject object;
      object.insert(QStringLiteral("name"), remote.name);
      object.insert(QStringLiteral("type"), remote.type);
      // How its own flags begin, and whether it is a layer over another
      // remote -- so a form can offer the flags that mean something to it.
      object.insert(QStringLiteral("flagPrefix"),
                    ExtraFlags::FlagPrefixOfType(remote.type));
      object.insert(QStringLiteral("wraps"),
                    ExtraFlags::WrapsAnother(remote.type));
      object.insert(QStringLiteral("lastUpload"),
                    describeRun(RunHistory::lastUploadTo(remote.name)));
      object.insert(QStringLiteral("lastDownload"),
                    describeRun(RunHistory::lastDownloadFrom(remote.name)));
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

  // What a new transfer starts from, so a form can begin where the window's
  // dialog does.
  if (path == QLatin1String("/api/v1/tasks/defaults")) {
    *body = toJson(TaskJson::Defaults(
        request.queryValue(QStringLiteral("type")) == QLatin1String("download")));
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

  // The flags the rclone on this machine accepts, less the ones a task may
  // not be given here. What a form offers to choose from, so that a flag
  // cannot be mistyped into a task.
  if (path == QLatin1String("/api/v1/rclone/flags")) {
    RcloneFlagRegistry &registry = RcloneFlagRegistry::instance();
    registry.ensureLoaded();

    const QHash<QString, QString> backends =
        ExtraFlags::Backends(registry.flags());

    QJsonArray array;
    for (const RcloneFlag &flag : registry.flags()) {
      if (ExtraFlags::IsForbidden(flag.name, LocalFiles::IsRestricted())) {
        continue;
      }
      QJsonObject object;
      // Which kind of remote it is for; empty when it is for any.
      object.insert(QStringLiteral("backend"), backends.value(flag.name));
      object.insert(QStringLiteral("name"), flag.name);
      object.insert(QStringLiteral("short"), flag.shortName);
      object.insert(QStringLiteral("type"), flag.type);
      object.insert(QStringLiteral("takesValue"), flag.takesValue());
      object.insert(QStringLiteral("group"), flag.group);
      object.insert(QStringLiteral("description"), flag.description);
      array.append(object);
    }

    QJsonObject object;
    // False while rclone has not answered, or could not: an empty list then
    // means "not known", not "there are none".
    object.insert(QStringLiteral("loaded"), registry.isLoaded());
    object.insert(QStringLiteral("flags"), array);
    *body = toJson(object);
    return 200;
  }

  // One path on this machine: whether it is there, and what it is. For a
  // form to say so while the path is being typed rather than after.
  if (path == QLatin1String("/api/v1/local/stat")) {
    const QString asked = request.queryValue(QStringLiteral("path"));
    const QString real = asked.isEmpty() ? QString() : LocalFiles::Allowed(asked);
    if (real.isEmpty()) {
      QJsonObject object;
      object.insert(
          QStringLiteral("error"),
          QStringLiteral("that is not a folder on this machine the API may use"));
      object.insert(QStringLiteral("code"), QStringLiteral("outside_roots"));
      *body = toJson(object);
      return 403;
    }
    const QFileInfo info(real);
    QJsonObject object;
    object.insert(QStringLiteral("path"), real);
    object.insert(QStringLiteral("exists"), info.exists());
    object.insert(QStringLiteral("isDir"), info.isDir());
    *body = toJson(object);
    return 200;
  }

  // The folders of this machine, as far as the API may see them. See
  // local_files.h for where the line is drawn and why.
  if (path == QLatin1String("/api/v1/local/ls")) {
    const QString asked = request.queryValue(QStringLiteral("path"));

    QList<LocalFiles::Entry> entries;
    QString why;
    if (!LocalFiles::List(asked, &entries, &why)) {
      QJsonObject object;
      object.insert(QStringLiteral("error"), why);
      object.insert(QStringLiteral("code"), QStringLiteral("outside_roots"));
      *body = toJson(object);
      return 403;
    }

    QJsonArray array;
    for (const LocalFiles::Entry &entry : entries) {
      QJsonObject object;
      object.insert(QStringLiteral("name"), entry.name);
      object.insert(QStringLiteral("path"), entry.path);
      object.insert(QStringLiteral("isDir"), entry.isDir);
      object.insert(QStringLiteral("size"), double(entry.size));
      object.insert(QStringLiteral("modTime"),
                    entry.modified.isValid()
                        ? stamp(entry.modified.toMSecsSinceEpoch())
                        : QString());
      array.append(object);
    }

    QJsonObject object;
    object.insert(QStringLiteral("path"),
                  asked.isEmpty() ? QString() : LocalFiles::Allowed(asked));
    object.insert(QStringLiteral("restricted"), LocalFiles::IsRestricted());
    object.insert(QStringLiteral("entries"), array);
    *body = toJson(object);
    return 200;
  }

  // A job's output, as text. Deliberately not JSON: a log wrapped in a JSON
  // string is a log with every newline spelled out, which nobody can read in
  // a browser and every client has to unwrap again.
  if (path.startsWith(QLatin1String("/api/v1/jobs/")) &&
      path.endsWith(QLatin1String("/log"))) {
    const QString id =
        path.mid(QStringLiteral("/api/v1/jobs/").size(),
                 path.size() - QStringLiteral("/api/v1/jobs//log").size());

    const QString file = logPathFor(id);
    if (file.isEmpty() || !QFileInfo::exists(file)) {
      // Told apart on purpose: a job that wrote no log (logging off, or
      // rclone printed nothing) is not the same as a job that never was.
      const bool known = JobRegistry::instance().find(id) != nullptr ||
                         !RunHistory::find(id).requestId.isEmpty();
      *body = problem(known ? QStringLiteral("that job wrote no log")
                            : QStringLiteral("no such job"));
      return 404;
    }

    bool readable = true;
    const int asked =
        request.queryValue(QStringLiteral("tail")).toInt(&readable);
    const int lines = readable && asked > 0 ? qMin(asked, 5000) : 200;

    *contentType = "text/plain; charset=utf-8";
    *body = tailOf(file, lines);
    return 200;
  }

  if (path.startsWith(QLatin1String("/api/v1/jobs/"))) {
    const QString id = path.mid(QStringLiteral("/api/v1/jobs/").size());
    const RunningJob *job = JobRegistry::instance().find(id);
    if (job == nullptr) {
      *body = problem(QStringLiteral("no such job"));
      return 404;
    }
    *body = toJson(describe(*job));
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

  // history/{requestId} -- one run, with everything kept about it
  if (path.startsWith(QLatin1String("/api/v1/history/"))) {
    const QString id = path.mid(int(qstrlen("/api/v1/history/")));
    const JobRunRecord row = RunHistory::find(id);
    if (row.requestId.isEmpty()) {
      *body = problem(QStringLiteral("no such run"));
      return 404;
    }
    QJsonObject object = describeRun(row).toObject();
    object.insert(QStringLiteral("taskId"), row.taskId);
    object.insert(QStringLiteral("kind"), row.kind);
    object.insert(QStringLiteral("startedBy"), row.transferMode);
    object.insert(QStringLiteral("info"), row.info);
    object.insert(QStringLiteral("exitCode"), row.exitCode);
    object.insert(QStringLiteral("totalBytes"), double(row.totalBytes));
    object.insert(QStringLiteral("logBytes"), double(row.logBytes));
    object.insert(QStringLiteral("hasLog"),
                  !row.logPath.isEmpty() && QFileInfo::exists(row.logPath));
    *body = toJson(object);
    return 200;
  }

  if (path == QLatin1String("/api/v1/history")) {
    bool readable = true;
    const int asked = request.queryValue(QStringLiteral("limit")).toInt(&readable);
    RunHistory::Search search;
    search.limit = readable && asked > 0 ? qMin(asked, 1000) : 50;
    // A moment as this API says them, or a plain count of milliseconds.
    const QString before = request.queryValue(QStringLiteral("before"));
    if (!before.isEmpty()) {
      bool number = false;
      const qint64 ms = before.toLongLong(&number);
      const QDateTime at = QDateTime::fromString(before, Qt::ISODate);
      if (!number && !at.isValid()) {
        *body = problem(QStringLiteral("\"before\" is a moment such as "
                                       "2026-10-19T10:52:00+07:00"));
        return 400;
      }
      search.before = number ? ms : at.toMSecsSinceEpoch();
    }
    search.state = request.queryValue(QStringLiteral("state"));
    search.kind = request.queryValue(QStringLiteral("kind"));
    search.taskId = request.queryValue(QStringLiteral("task"));
    search.text = request.queryValue(QStringLiteral("q"));

    QJsonArray array;
    for (const JobRunRecord &row : RunHistory::search(search)) {
      QJsonObject object;
      object.insert(QStringLiteral("taskId"), row.taskId);
      object.insert(QStringLiteral("info"), row.info);
      object.insert(QStringLiteral("source"), row.source);
      object.insert(QStringLiteral("dest"), row.dest);
      object.insert(QStringLiteral("transfers"), double(row.transfers));
      object.insert(QStringLiteral("errors"), double(row.errors));
      object.insert(QStringLiteral("requestId"), row.requestId);
      object.insert(QStringLiteral("taskName"), row.taskName);
      object.insert(QStringLiteral("kind"), row.kind);
      object.insert(QStringLiteral("startedBy"), row.transferMode);
      object.insert(QStringLiteral("state"), row.state);
      object.insert(QStringLiteral("exitCode"), row.exitCode);
      object.insert(QStringLiteral("bytes"), double(row.bytes));
      // A rehearsal. The size beside it is what would have been sent.
      object.insert(QStringLiteral("dryRun"), row.dryRun);

      // When, as well as what. A history that does not say when is a list
      // of things that happened at some point.
      object.insert(QStringLiteral("startedAt"), stamp(row.startedAt));
      object.insert(QStringLiteral("finishedAt"), stamp(row.finishedAt));
      array.append(object);
    }
    *body = toJson(array);
    return 200;
  }

  *body = problem(QStringLiteral("no such endpoint"));
  return 404;
}
