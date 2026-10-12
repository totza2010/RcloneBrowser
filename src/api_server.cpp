#include "api_server.h"
#include "api_auth.h"
#include "app_core.h"
#include "directory_listing.h"
#include "debug_log.h"
#include "http_message.h"
#include "job_options.h"
#include "job_queue.h"
#include "job_registry.h"
#include "list_of_job_options.h"
#include "remote_ops.h"
#include "remote_registry.h"
#include "run_history.h"
#include "running_job.h"
#include "schedule.h"
#include "scheduler_store.h"
#include "utils.h"

#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QHostAddress>
#include <QSharedPointer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>

// The resource is compiled into rbcore, and a static library's resources are
// not registered unless something asks for them. Left out, the daemon serves
// a blank page and nothing says why.
static void InitWebUiResources() { Q_INIT_RESOURCE(webui); }

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
  object.insert(QStringLiteral("dryRun"), job.isDryRun());
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

// A moment, with its offset from UTC. Without one, "10:52" means a different
// instant to every reader in a different time zone, and a program reading
// this has no window to look at to guess which.
QString stamp(qint64 msSinceEpoch) {
  if (msSinceEpoch <= 0) {
    return QString();
  }
  const QDateTime local = QDateTime::fromMSecsSinceEpoch(msSinceEpoch);
  return local.toOffsetFromUtc(local.offsetFromUtc()).toString(Qt::ISODate);
}

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
      QStringLiteral("/api/v1/events")};
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
  const QString offered = request.header(QStringLiteral("authorization"));
  if (!offered.startsWith(QLatin1String("Bearer "))) {
    return false;
  }

  const QString presented = offered.mid(7);
  if (presented.isEmpty()) {
    return false;
  }

  // The token, for anything unattended. Compared against the stored one, and
  // an empty stored token is refused rather than matched: "none set" must
  // never come out as "none required".
  const QString expected = token();
  if (!expected.isEmpty() && presented == expected) {
    return true;
  }

  // Or a session, for a person who signed in.
  return ApiAuth::Sessions::instance().accepts(presented);
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

  RunningJob *job = nullptr;
  if (question == QLatin1String("size")) {
    job = RemoteOps::Size(remote, path, QStringList(), true);
  } else if (question == QLatin1String("about")) {
    job = RemoteOps::About(remote, true);
  } else {
    job = RemoteOps::Tree(remote, path);
  }
  qCDebug(rbApp) << "API asking" << question << "of" << remote << path;

  // Everything rclone prints, because here the output is the answer. Capped,
  // because a tree of a large remote is not something to hold whole.
  const int cap = 2 * 1024 * 1024;
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
  // Once a connection is an event stream it is ours to write to, not to read
  // from. Anything the client sends down it afterwards is dropped rather
  // than answered into the middle of the stream.
  if (socket->property("stream").toBool()) {
    socket->readAll();
    return;
  }

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
  static const QStringList open = {QStringLiteral("/api/v1/ping"),
                                   QStringLiteral("/api/v1/auth"),
                                   QStringLiteral("/api/v1/login")};

  if (!open.contains(asked.path) && !mayAsk(asked)) {
    qCDebug(rbApp) << "refused an API request from somebody not signed in"
                   << request.method << asked.path;
    const QString why = QStringLiteral("a token or a session is required");
    reply(FormatHttpResponse(401, problem(why)));
    return;
  }

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
  }

  // Nor are these: they run rclone and the answer is what it prints.
  {
    const QStringList parts = segmentsOf(asked.path);
    static const QStringList questions = {QStringLiteral("size"),
                                          QStringLiteral("about"),
                                          QStringLiteral("tree")};
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
        QStringLiteral("cleanup"), QStringLiteral("dedupe")};
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

  // tasks/{id}/run and tasks/{id}/enqueue
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
      StartTask(task, QStringLiteral("api"), requestId, wantsDryRun(request));
      *body = startedJob(requestId, QStringLiteral("now"));
      return 202;
    }
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
      // A rehearsal. The size beside it is what would have been sent.
      object.insert(QStringLiteral("dryRun"), row.dryRun);

      // When, as well as what. A history that does not say when is a list
      // of things that happened at some point.
      const auto stamp = [](qint64 ms) {
        return ms > 0 ? QDateTime::fromMSecsSinceEpoch(ms).toString(Qt::ISODate)
                      : QString();
      };
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
