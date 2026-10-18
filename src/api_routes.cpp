#include "api_routes.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QRegularExpression>

namespace {

using ApiRoutes::Route;

Route r(const char *method, const char *path, const char *tag,
        const char *summary, const char *about, const char *query = "",
        const char *body = "", const char *answers = "200: done",
        bool open = false) {
  Route route;
  route.method = QLatin1String(method);
  route.path = QString::fromUtf8(path);
  route.tag = QLatin1String(tag);
  route.summary = QString::fromUtf8(summary);
  route.about = QString::fromUtf8(about);
  route.query = QString::fromUtf8(query);
  route.body = QString::fromUtf8(body);
  route.answers = QString::fromUtf8(answers);
  route.open = open;
  return route;
}

// Sections, in the order a reader meets them.
const char *const kSystem = "System";
const char *const kSignIn = "Signing in";
const char *const kTasks = "Tasks";
const char *const kQueue = "Queue";
const char *const kJobs = "Jobs";
const char *const kSchedules = "Schedules";
const char *const kHistory = "History";
const char *const kRemotes = "Remotes";
const char *const kLocal = "This machine";
const char *const kStreams = "Streams";

const char *const kTaskBody =
    R"({"type":"upload","name":"nightly","source":"/media/films","dest":"gdrive:films","operation":"sync","syncTiming":"after","compare":"checksum","transfers":"4","bandwidth":"2M","excluded":"*.tmp\n*.part","extra":"--fast-list"})";

QList<Route> build() {
  return {
      // ---- system ----------------------------------------------------------
      r("GET", "/api/v1/ping", kSystem, "Whether the server is there",
        "Says nothing else, so it can be asked without a token: it tells "
        "\"the server is there and my token is wrong\" from \"there is no "
        "server\".",
        "", "", "200: it is there", true),
      r("GET", "/api/v1/openapi.json", kSystem,
        "This description of the API, as OpenAPI 3.1",
        "Made from the program's own list of routes, so it is the list the "
        "server answers by.",
        "", "", "200: the description", true),
      r("GET", "/api/v1/system", kSystem, "The state of the program",
        "rclone's version, whether the queue and the scheduler are running, "
        "how many jobs are running and how many are queued."),
      r("GET", "/api/v1/rclone/flags", kSystem,
        "The flags a task may be given",
        "What the rclone on this machine says it accepts, less the ones a "
        "task may not be given through the API. Each says which kind of "
        "remote it is for, empty for any. \"loaded\": false means rclone has "
        "not answered yet, not that there are none."),
      r("POST", "/api/v1/shutdown", kSystem, "Stop a daemon",
        "Stops the transfers, records what became of them and exits. Refused "
        "when the program is running as a window.",
        "", "", "202: stopping; 409: this is a window, or it is already "
                "stopping"),
      r("GET", "/api/v1/events", kSystem, "Things as they happen",
        "A server-sent event stream: hello, job, queue, schedule, tasks. "
        "Read with fetch() rather than EventSource, so the token can go in "
        "the Authorization header rather than the address."),

      // ---- signing in ------------------------------------------------------
      r("GET", "/api/v1/auth", kSignIn, "What kind of lock there is",
        "Whether an administrator password is set, so a sign-in page knows "
        "which form to show. Says nothing about what is behind it.",
        "", "", "200: the shape of the lock", true),
      r("POST", "/api/v1/login", kSignIn, "Sign in with the password",
        "Gives back a session, which is then used exactly as the token is: "
        "Authorization: Bearer <session>. Five wrong answers lock it for a "
        "while.",
        "", R"({"user":"admin","password":"…"})",
        "200: a session; 401: wrong; 409: no password is set; 429: locked "
        "for a while",
        true),
      r("POST", "/api/v1/logout", kSignIn, "End this session", "", "", "",
        "204: ended"),
      r("POST", "/api/v1/password", kSignIn,
        "Set or change the administrator password",
        "At least 8 characters. Everybody signed in elsewhere is signed out.",
        "", R"({"current":"…","new":"…"})",
        "200: changed; 400: too short; 403: the current one is wrong"),

      // ---- tasks -----------------------------------------------------------
      r("GET", "/api/v1/tasks", kTasks, "Every saved task",
        "Each with every field, in words: \"operation\": \"sync\" rather than "
        "a number."),
      r("GET", "/api/v1/tasks/defaults", kTasks,
        "What a new task starts from",
        "The values the window's transfer dialog comes up with.",
        "type: upload or download"),
      r("GET", "/api/v1/tasks/{id}", kTasks, "One task", "", "", "",
        "200: the task; 404: no such task"),
      r("POST", "/api/v1/tasks", kTasks, "Make a task",
        "Held to the rules the window's dialog keeps, and named "
        "_tmp_<date>_<time>_<remote> when no name is given. The remote has "
        "to be one there is and what an upload sends has to exist. Extra "
        "flags have to be ones this rclone has, and some -- those that run a "
        "program or move rclone's own files -- are refused whatever rclone "
        "says.",
        "", kTaskBody,
        "201: the task; 400: a field that cannot be (bad_field, bad_flag, "
        "incomplete, no_such_remote, no_such_path); 403: outside the folders "
        "the API may use"),
      r("PUT", "/api/v1/tasks/{id}", kTasks, "Change a task",
        "Only what is sent is changed. A request that is wrong changes "
        "nothing.",
        "", R"({"bandwidth":"1M","name":"renamed"})",
        "200: the task; 400: see making one; 403: outside the folders the "
        "API may use; 404: no such task"),
      r("DELETE", "/api/v1/tasks/{id}", kTasks, "Delete a task", "", "", "",
        "200: deleted; 404: no such task; 409: it is running, or a schedule "
        "runs it"),
      r("POST", "/api/v1/tasks/preview", kTasks,
        "The command a task would run",
        "Saves nothing. Secrets are taken out of what is shown.", "",
        R"({"type":"upload","source":"/media/films","dest":"gdrive:films","dryRun":true})",
        "200: the command; 400: a field or a flag that cannot be"),
      r("POST", "/api/v1/tasks/{id}/run", kTasks, "Run a task now",
        "Refused while the same task is already running: two rclones would "
        "be writing one destination.",
        "dryRun: 1 to say what would happen and do none of it",
        R"({"dryRun":false})",
        "202: started, with its requestId; 404: no such task; 409: it is "
        "already running; 503: the program is stopping"),
      r("POST", "/api/v1/tasks/{id}/enqueue", kTasks,
        "Put a task at the back of the queue", "",
        "dryRun: 1 to make it a rehearsal", R"({"dryRun":false})",
        "202: queued, with its requestId; 404: no such task"),

      // ---- queue -----------------------------------------------------------
      r("GET", "/api/v1/queue", kQueue, "What is waiting, in order",
        "The first is the one running, when one is."),
      r("POST", "/api/v1/queue/start", kQueue, "Start the queue",
        "Each task starts when the one before it ends."),
      r("POST", "/api/v1/queue/pause", kQueue, "Pause the queue",
        "It keeps what is in it and starts nothing."),
      r("POST", "/api/v1/queue/clear", kQueue,
        "Take everything waiting out of the queue",
        "The one running is left to finish. The tasks themselves are kept."),
      r("POST", "/api/v1/queue/{requestId}/move", kQueue,
        "Put a waiting entry somewhere else in the line",
        "The one running stays first: it cannot be moved and nothing can go "
        "above it.",
        "", R"({"to":"top"})",
        "200: where it is now; 400: \"to\" is not a place; 404: not in the "
        "queue; 409: that place is the running one's, or there is no such "
        "place"),
      r("DELETE", "/api/v1/queue/{requestId}", kQueue,
        "Take one waiting entry out", "", "", "",
        "204: taken out; 404: not in the queue; 409: it is running -- stop "
        "the job instead"),

      // ---- jobs ------------------------------------------------------------
      r("GET", "/api/v1/jobs", kJobs, "Jobs running and recently finished",
        "With progress while they run. A stream's job says where it can be "
        "opened."),
      r("GET", "/api/v1/jobs/{requestId}", kJobs, "One job", "", "", "",
        "200: the job; 404: no such job"),
      r("GET", "/api/v1/jobs/{requestId}/log", kJobs, "What a job printed",
        "Plain text, while it runs or long after.",
        "tail: how many lines from the end", "",
        "200: the log; 404: no such job, or it wrote no log"),
      r("POST", "/api/v1/jobs/{requestId}/stop", kJobs, "Stop a job",
        "A mount is unmounted; a stream stops being served.", "", "",
        "202: stopping; 404: no such job; 409: it has already finished"),

      // ---- schedules -------------------------------------------------------
      r("GET", "/api/v1/schedules", kSchedules, "Every schedule",
        "In words, with when each next comes due by the clock of the machine "
        "the program runs on."),
      r("POST", "/api/v1/schedules", kSchedules, "Put a task on a schedule",
        "mode is daily (days and time) or cron (five fields). runAs is now or "
        "queue. Something that could never come due is refused.",
        "",
        R"({"taskId":"{…}","name":"nightly","mode":"daily","days":["mon","tue","wed","thu","fri"],"time":"06:30","runAs":"queue","active":true})",
        "201: the schedule; 400: a field that cannot be, or no such task"),
      r("PUT", "/api/v1/schedules/{id}", kSchedules, "Change a schedule",
        "Only what is sent is changed.", "", R"({"time":"18:05"})",
        "200: the schedule; 400: a field that cannot be; 404: no such "
        "schedule"),
      r("DELETE", "/api/v1/schedules/{id}", kSchedules, "Delete a schedule",
        "A run it already started goes on. The task is kept.", "", "",
        "200: deleted; 404: no such schedule"),
      r("POST", "/api/v1/schedules/{id}/enable", kSchedules,
        "Switch a schedule on", "", "", "",
        "200: the schedule; 404: no such schedule"),
      r("POST", "/api/v1/schedules/{id}/disable", kSchedules,
        "Switch a schedule off", "", "", "",
        "200: the schedule; 404: no such schedule"),
      r("POST", "/api/v1/schedules/{id}/run", kSchedules,
        "Run a schedule's task now",
        "The road the clock takes, so the same rules hold: not while its last "
        "run is still going.",
        "", "", "202: started; 404: no such schedule; 409: why not"),
      r("POST", "/api/v1/scheduler/start", kSchedules, "Start the scheduler",
        "The switch for every schedule at once."),
      r("POST", "/api/v1/scheduler/pause", kSchedules, "Pause the scheduler",
        "Every schedule is kept and none starts anything."),

      // ---- history ---------------------------------------------------------
      r("GET", "/api/v1/history", kHistory, "Past runs, newest first",
        "When each began and ended, what it moved, and whether it was a "
        "rehearsal.",
        "limit: how many"),

      // ---- remotes ---------------------------------------------------------
      r("GET", "/api/v1/remotes", kRemotes, "Every remote",
        "With its kind, and when it was last uploaded to and downloaded "
        "from."),
      r("GET", "/api/v1/remotes/{name}/ls", kRemotes, "What is in a folder",
        "", "path: the folder, empty for the top", "",
        "200: the entries; 404: no such remote; 413: too many to list; 502: "
        "rclone could not list it"),
      r("GET", "/api/v1/remotes/{name}/size", kRemotes,
        "How big a folder is", "", "path: the folder", "",
        "200: count and bytes; 502: rclone's own answer"),
      r("GET", "/api/v1/remotes/{name}/about", kRemotes,
        "How full the remote is", "", "", "",
        "200: total, used, free; 502: rclone's own answer"),
      r("GET", "/api/v1/remotes/{name}/tree", kRemotes,
        "What is inside a folder, as a tree", "", "path: the folder", "",
        "200: the tree as text; 502: rclone's own answer"),
      r("GET", "/api/v1/remotes/{name}/check", kRemotes,
        "Compare two folders",
        "A difference is an answer, not a failure.",
        "path: this folder; toRemote: the other remote, this one when "
        "empty; to: the other folder; oneWay: 1 for only what is missing "
        "there; sizeOnly: 1 to compare sizes only; download: 1 to compare "
        "the contents",
        "", "200: what matched and what did not; 400: refused"),
      r("GET", "/api/v1/remotes/{name}/export", kRemotes,
        "A listing to save",
        "Every file, with its size and when it was changed.",
        "path: the folder; format: txt for names only; maxDepth: how deep",
        "", "200: the listing"),
      r("POST", "/api/v1/remotes/{name}/mkdir", kRemotes, "Make a folder",
        "", "", R"({"path":"films","name":"2026"})",
        "200: made; 400: not a name; 502: rclone's own answer"),
      r("POST", "/api/v1/remotes/{name}/rename", kRemotes,
        "Rename a file or a folder", "A name, not a path.", "",
        R"({"path":"films/a.mkv","name":"b.mkv"})",
        "200: renamed; 400: not a name; 502: rclone's own answer"),
      r("POST", "/api/v1/remotes/{name}/delete", kRemotes,
        "Delete a file or a folder",
        "A folder takes everything inside it. The whole remote cannot be "
        "deleted this way.",
        "", R"({"path":"films/old","isDir":true})",
        "200: deleted; 400: refused; 502: rclone's own answer"),
      r("POST", "/api/v1/remotes/{name}/link", kRemotes,
        "A public link to a file or a folder", "", "",
        R"({"path":"films/a.mkv"})",
        "200: the link; 400: refused; 502: the backend cannot"),
      r("POST", "/api/v1/remotes/{name}/cleanup", kRemotes,
        "Empty the remote's trash and old versions", "", "", "",
        "200: done; 502: the backend cannot"),
      r("POST", "/api/v1/remotes/{name}/dedupe", kRemotes,
        "Settle duplicate files",
        "mode is one of rclone's: interactive is not offered.", "",
        R"({"path":"films","mode":"newest","dryRun":true})",
        "200: done; 400: not a mode"),
      r("POST", "/api/v1/remotes/{name}/copy", kRemotes,
        "Copy a file or a folder to another folder",
        "A folder lands under its own name.", "",
        R"({"path":"films/a","isDir":true,"toRemote":"backup","to":"films","dryRun":false})",
        "202: started; 400: refused"),
      r("POST", "/api/v1/remotes/{name}/move", kRemotes,
        "Move a file or a folder to another folder",
        "A folder lands under its own name.", "",
        R"({"path":"films/a","isDir":true,"toRemote":"backup","to":"films","dryRun":false})",
        "202: started; 400: refused"),
      r("POST", "/api/v1/remotes/{name}/mount", kRemotes,
        "Mount a folder on the machine the program runs on",
        "Stopping the job unmounts it.", "",
        R"({"path":"films","mountPoint":"X:","readOnly":true,"cache":"writes"})",
        "202: started; 400: refused"),
      r("POST", "/api/v1/remotes/{name}/upload", kRemotes,
        "Upload from the machine the program runs on",
        "From a folder the API may use. A folder lands under its own name. "
        "To keep the transfer as a task, make a task instead.",
        "", R"({"from":"/media/films/a.mkv","to":"films","dryRun":false})",
        "202: started; 400: refused; 403: outside the folders the API may "
        "use"),
      r("POST", "/api/v1/remotes/{name}/download", kRemotes,
        "Download to the machine the program runs on",
        "Into a folder the API may use.", "",
        R"({"path":"films/a.mkv","isDir":false,"to":"/media/films","dryRun":false})",
        "202: started; 400: refused; 403: outside the folders the API may "
        "use"),
      r("POST", "/api/v1/remotes/{name}/stream", kStreams,
        "Serve a file or a folder for a player",
        "Gives back a path of this same server, under a part made at random "
        "for this stream. A player opens it with nothing else; anyone with "
        "the address can play it until the job is stopped.",
        "", R"({"path":"films/a.mkv","isDir":false})",
        "202: started, with the path; 409: refused"),

      // ---- this machine ----------------------------------------------------
      r("GET", "/api/v1/local/ls", kLocal,
        "What is in a folder of the machine the program runs on",
        "As far as the API may look: RB_LOCAL_ROOTS, which in the Docker "
        "image is /media. Empty path is the list of places to start from.",
        "path: the folder", "",
        "200: the entries; 403: not a folder the API may use"),
      r("GET", "/api/v1/local/stat", kLocal,
        "Whether a path of that machine is there, and what it is", "",
        "path: the path", "",
        "200: exists and isDir; 403: not a folder the API may use"),

      // ---- streams ---------------------------------------------------------
      r("GET", "/stream/{key}/{name}", kStreams,
        "A file being streamed",
        "The address POST /remotes/{name}/stream gave. No token: a player "
        "cannot send one, and the random part is the permission. Range is "
        "honoured, so a player can seek.",
        "", "",
        "200: the file; 206: part of it; 404: no such stream, or it has "
        "stopped; 502: it is still starting",
        true),

      // ---- keys ------------------------------------------------------------
      r("GET", "/api/v1/keys", kSignIn, "The API keys there are",
        "Names, levels, when made and when last used. Never the keys "
        "themselves."),
      r("POST", "/api/v1/keys", kSignIn, "Make an API key",
        "For another program. The key is in the answer and nowhere else: it "
        "is stored only as a hash, so it cannot be shown again. Levels: "
        "read, control (and run, queue, stop), write (and change tasks, "
        "schedules and files), admin (everything).",
        "", R"({"name":"home assistant","scope":"control"})",
        "201: the key, once; 400: no name, a name already taken, or no such "
        "level"),
      r("DELETE", "/api/v1/keys/{id}", kSignIn, "Take a key back",
        "From the next request on, it opens nothing.", "", "",
        "200: taken back; 404: no such key"),
  };
}

// How much each route asks of a key. Looking is "read"; changing anything
// is "write" unless it is listed below as only setting things going or
// stopping them ("control"), as harmless ("read"), or as the owner's
// business ("admin").
QString scopeOf(const Route &route) {
  if (route.open) {
    return QString();
  }
  if (route.method == QLatin1String("GET")) {
    return route.path.startsWith(QLatin1String("/api/v1/keys"))
               ? QStringLiteral("admin")
               : QStringLiteral("read");
  }
  static const QStringList control = {
      QStringLiteral("POST /api/v1/tasks/{id}/run"),
      QStringLiteral("POST /api/v1/tasks/{id}/enqueue"),
      QStringLiteral("POST /api/v1/queue/start"),
      QStringLiteral("POST /api/v1/queue/pause"),
      QStringLiteral("POST /api/v1/queue/clear"),
      QStringLiteral("POST /api/v1/queue/{requestId}/move"),
      QStringLiteral("DELETE /api/v1/queue/{requestId}"),
      QStringLiteral("POST /api/v1/jobs/{requestId}/stop"),
      QStringLiteral("POST /api/v1/schedules/{id}/enable"),
      QStringLiteral("POST /api/v1/schedules/{id}/disable"),
      QStringLiteral("POST /api/v1/schedules/{id}/run"),
      QStringLiteral("POST /api/v1/scheduler/start"),
      QStringLiteral("POST /api/v1/scheduler/pause"),
      QStringLiteral("POST /api/v1/remotes/{name}/stream"),
      QStringLiteral("POST /api/v1/remotes/{name}/mount")};
  static const QStringList read = {
      QStringLiteral("POST /api/v1/tasks/preview"),
      QStringLiteral("POST /api/v1/logout")};
  static const QStringList admin = {
      QStringLiteral("POST /api/v1/shutdown"),
      QStringLiteral("POST /api/v1/password"),
      QStringLiteral("POST /api/v1/keys"),
      QStringLiteral("DELETE /api/v1/keys/{id}")};

  const QString key = route.method + QLatin1Char(' ') + route.path;
  if (admin.contains(key)) {
    return QStringLiteral("admin");
  }
  if (control.contains(key)) {
    return QStringLiteral("control");
  }
  if (read.contains(key)) {
    return QStringLiteral("read");
  }
  return QStringLiteral("write");
}

const QList<Route> &routes() {
  static const QList<Route> all = []() {
    QList<Route> list = build();
    for (Route &route : list) {
      route.scope = scopeOf(route);
    }
    return list;
  }();
  return all;
}

// "a: b; c: d" -> [(a, b), (c, d)]
QList<QPair<QString, QString>> pairs(const QString &text) {
  QList<QPair<QString, QString>> out;
  for (const QString &part : text.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
    const int colon = part.indexOf(QLatin1Char(':'));
    if (colon < 0) {
      continue;
    }
    out.append({part.left(colon).trimmed(), part.mid(colon + 1).trimmed()});
  }
  return out;
}

QMutex gUnlistedLock;
QStringList gUnlisted;

} // namespace

namespace ApiRoutes {

const QList<Route> &All() { return routes(); }

const Route *Match(const QString &method, const QString &path) {
  const QString asked = method == QLatin1String("HEAD") ? QStringLiteral("GET")
                                                        : method;
  const QStringList segments = path.split(QLatin1Char('/'));

  const Route *best = nullptr;
  int bestLiterals = -1;
  for (const Route &route : routes()) {
    if (route.method != asked) {
      continue;
    }
    const QStringList pattern = route.path.split(QLatin1Char('/'));
    if (pattern.size() != segments.size()) {
      continue;
    }
    int literals = 0;
    bool matches = true;
    for (int i = 0; i < pattern.size() && matches; ++i) {
      if (pattern.at(i).startsWith(QLatin1Char('{'))) {
        matches = !segments.at(i).isEmpty();
      } else {
        matches = pattern.at(i) == segments.at(i);
        ++literals;
      }
    }
    if (matches && literals > bestLiterals) {
      best = &route;
      bestLiterals = literals;
    }
  }
  return best;
}

QJsonObject OpenApi(const QString &version) {
  QJsonObject paths;
  static const QRegularExpression placeholder(QStringLiteral("\\{([^}]+)\\}"));

  for (const Route &route : routes()) {
    QJsonObject operation;
    operation.insert(QStringLiteral("tags"), QJsonArray{route.tag});
    operation.insert(QStringLiteral("summary"), route.summary);
    if (!route.about.isEmpty()) {
      operation.insert(QStringLiteral("description"), route.about);
    }
    // method + path, so a generated client gets names it can keep.
    QString id = route.method.toLower() + route.path;
    id.replace(QStringLiteral("/api/v1"), QString());
    id.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9]+")),
               QStringLiteral("_"));
    operation.insert(QStringLiteral("operationId"), id);
    if (route.open) {
      operation.insert(QStringLiteral("security"), QJsonArray());
    } else {
      // Not part of OpenAPI itself: the level a key needs, for whoever reads
      // this to decide what to give a program.
      operation.insert(QStringLiteral("x-scope"), route.scope);
    }

    QJsonArray parameters;
    auto found = placeholder.globalMatch(route.path);
    while (found.hasNext()) {
      const QString name = found.next().captured(1);
      QJsonObject parameter;
      parameter.insert(QStringLiteral("name"), name);
      parameter.insert(QStringLiteral("in"), QStringLiteral("path"));
      parameter.insert(QStringLiteral("required"), true);
      parameter.insert(QStringLiteral("schema"),
                       QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}});
      parameters.append(parameter);
    }
    for (const auto &query : pairs(route.query)) {
      QJsonObject parameter;
      parameter.insert(QStringLiteral("name"), query.first);
      parameter.insert(QStringLiteral("in"), QStringLiteral("query"));
      parameter.insert(QStringLiteral("description"), query.second);
      parameter.insert(QStringLiteral("schema"),
                       QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}});
      parameters.append(parameter);
    }
    if (!parameters.isEmpty()) {
      operation.insert(QStringLiteral("parameters"), parameters);
    }

    if (!route.body.isEmpty()) {
      QJsonObject media;
      media.insert(QStringLiteral("schema"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}});
      media.insert(QStringLiteral("example"),
                   QJsonDocument::fromJson(route.body.toUtf8()).object());
      QJsonObject body;
      body.insert(QStringLiteral("content"),
                  QJsonObject{{QStringLiteral("application/json"), media}});
      operation.insert(QStringLiteral("requestBody"), body);
    }

    QJsonObject responses;
    for (const auto &answer : pairs(route.answers)) {
      responses.insert(answer.first,
                       QJsonObject{{QStringLiteral("description"), answer.second}});
    }
    if (!route.open) {
      responses.insert(QStringLiteral("403"),
                       QJsonObject{{QStringLiteral("description"),
                                    QStringLiteral("a key whose level is "
                                                   "below ") +
                                        route.scope}});
      responses.insert(QStringLiteral("401"),
                       QJsonObject{{QStringLiteral("description"),
                                    QStringLiteral("no token or session, or a "
                                                   "wrong one")}});
    }
    operation.insert(QStringLiteral("responses"), responses);

    QJsonObject item = paths.value(route.path).toObject();
    item.insert(route.method.toLower(), operation);
    paths.insert(route.path, item);
  }

  QJsonObject info;
  info.insert(QStringLiteral("title"), QStringLiteral("Rclone Browser"));
  info.insert(QStringLiteral("version"), version);
  info.insert(QStringLiteral("description"),
              QStringLiteral(
                  "Controls Rclone Browser: its tasks, queue, schedules, jobs "
                  "and remotes. Every route but the few marked otherwise "
                  "needs Authorization: Bearer <token, session or key>. The "
                  "token is printed when the daemon starts; a session comes "
                  "from POST /api/v1/login; a key, from POST /api/v1/keys, "
                  "and may do only as much as its level (x-scope on each "
                  "route): read, control, write or admin."));

  QJsonObject bearer;
  bearer.insert(QStringLiteral("type"), QStringLiteral("http"));
  bearer.insert(QStringLiteral("scheme"), QStringLiteral("bearer"));

  QJsonObject spec;
  spec.insert(QStringLiteral("openapi"), QStringLiteral("3.1.0"));
  spec.insert(QStringLiteral("info"), info);
  spec.insert(QStringLiteral("paths"), paths);
  spec.insert(QStringLiteral("components"),
              QJsonObject{{QStringLiteral("securitySchemes"),
                           QJsonObject{{QStringLiteral("bearer"), bearer}}}});
  spec.insert(QStringLiteral("security"),
              QJsonArray{QJsonObject{{QStringLiteral("bearer"), QJsonArray()}}});
  return spec;
}

void NoteUnlisted(const QString &method, const QString &path) {
  QMutexLocker locker(&gUnlistedLock);
  const QString entry = method + QLatin1Char(' ') + path;
  if (!gUnlisted.contains(entry)) {
    gUnlisted.append(entry);
  }
}

QStringList Unlisted() {
  QMutexLocker locker(&gUnlistedLock);
  return gUnlisted;
}

} // namespace ApiRoutes
