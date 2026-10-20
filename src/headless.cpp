#include "headless.h"
#include "api_server.h"
#include "app_core.h"
#include "database.h"
#include "debug_log.h"
#include "shutdown_signal.h"
#include "task_runner.h"
#include "utils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QSettings>
#include <QTextStream>

#ifdef Q_OS_WIN
#include <windows.h>
#include <stdio.h>
#endif

#ifdef Q_OS_WIN
// Give the headless run somewhere to print on Windows.
//
// The executable is linked as a GUI application (add_executable(... WIN32)),
// which on Windows means it starts with no standard handles at all. Piping or
// redirecting gives it some, which is why "--list-tasks > out.txt" works while
// the same command typed in a terminal prints nothing.
//
// Only when there is no handle already: a pipe or a file redirection is a
// handle the caller chose, and reopening CONOUT$ over it would send the output
// to the console instead of where it was asked to go.
static void AttachToParentConsole() {
  const HANDLE existing = GetStdHandle(STD_OUTPUT_HANDLE);
  if (existing != nullptr && existing != INVALID_HANDLE_VALUE) {
    return;
  }
  if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
    return; // started from a shortcut or a service: nowhere to print
  }
  FILE *stream = nullptr;
  freopen_s(&stream, "CONOUT$", "w", stdout);
  freopen_s(&stream, "CONOUT$", "w", stderr);
}
#endif


CommandLine ReadCommandLine(int argc, char *argv[]) {
  CommandLine cmd;

  for (int i = 1; i < argc; ++i) {
    const QString arg = QString::fromLocal8Bit(argv[i]);

    if (arg == QLatin1String("--run-task")) {
      cmd.headless = true;
      if (i + 1 < argc) {
        cmd.task = QString::fromLocal8Bit(argv[++i]);
      }
    } else if (arg.startsWith(QLatin1String("--run-task="))) {
      cmd.headless = true;
      cmd.task = arg.section(QLatin1Char('='), 1);
    } else if (arg == QLatin1String("--list-tasks")) {
      cmd.headless = true;
      cmd.listTasks = true;
    } else if (arg == QLatin1String("--daemon")) {
      cmd.headless = true;
      cmd.daemon = true;
    } else if (arg == QLatin1String("--dry-run")) {
      cmd.dryRun = true;
    } else if (arg == QLatin1String("--help") || arg == QLatin1String("-h")) {
      cmd.headless = true;
      cmd.help = true;
    } else if (arg.startsWith(QLatin1Char('-'))) {
      // Only flagged when something else already asked for the command line;
      // starting the window with a stray argument stays harmless.
      cmd.unknownOption = arg;
    }
  }

  return cmd;
}

static void PrintUsage(QTextStream &out) {
  // The real file name, not a guess. The executable is "RcloneBrowser.exe" on
  // Windows and "rclone-browser" everywhere else (see src/CMakeLists.txt), so
  // a hardcoded name sends half the readers to a command that does not exist.
  const QString program =
      QFileInfo(QCoreApplication::applicationFilePath()).fileName();

  out << program << " [options]\n"
      << "\n"
      << "With no options the window opens as usual.\n"
      << "\n"
      << "  --list-tasks         list saved tasks as \"<id>  <operation>  "
         "<name>\"\n"
      << "  --run-task <name|id> run one saved task and exit with rclone's "
         "exit code\n"
      << "  --dry-run            with --run-task, pass --dry-run to rclone\n"
      << "  --daemon             run the queue, the schedules and the API "
         "with no window\n"
      << "  -h, --help           this text\n"
      << "\n"
      << "Exit codes: rclone's own (1-9) are passed through.\n"
      << "  64 usage   65 no such task   66 ambiguous name\n"
      << "  69 rclone would not start   70 rclone did not exit normally\n"
      << "  75 already running (--daemon)\n";
}

// Where the one-instance lock lives.
//
// One function, because the window and the daemon have to be reaching for the
// same file. They did not: only the window took a lock at all, so a daemon and
// a window could run side by side -- two clocks and two queues over one
// database, and a schedule that came due started twice.
QString InstanceLockPath() {
  QString dir = QDir::tempPath();
  if (IsPortableMode()) {
#if defined(Q_OS_WIN)
    dir = QCoreApplication::applicationDirPath();
#elif defined(Q_OS_MACOS)
    dir = QDir(QCoreApplication::applicationDirPath() + "/../../..")
              .absolutePath();
#else
    dir = QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME")) +
          "/rclone-browser";
#endif
  }

  // Per user, so two people on one machine each get their own.
  QString user = qEnvironmentVariable("USERNAME");
  if (user.isEmpty()) {
    user = qEnvironmentVariable("USER");
  }
  return dir + "/." + user + "RcloneBrowser_4q6RgLs2RpbJA.lock";
}

// Opens the API and the page it serves. The same call whether or not there
// is a window, because the page is a view of the core and so is the window:
// neither of them is what does the work, and either can be absent.
//
// Returns the address to open, or an empty string with *error saying why.
QString StartApi(QString *error) {
  // The environment wins over the stored settings, for one reason: a
  // container is configured by what it is started with, and "edit a file
  // inside the volume and restart" is not a way to say which of three modes
  // an image should run in. See docs/DOCKER-MODES.md.
  //
  //   RB_API_ENABLED   0 / false / off switches the page and the API off
  //   RB_API_ADDRESS   where to listen, 0.0.0.0 to be reachable from outside
  //   RB_API_PORT      the port
  const auto fromEnv = [](const char *name) {
    return qEnvironmentVariable(name).trimmed();
  };

  bool enabled = GetSettings()->value("Settings/apiEnabled", true).toBool();
  const QString wanted = fromEnv("RB_API_ENABLED").toLower();
  if (!wanted.isEmpty()) {
    enabled = !(wanted == QLatin1String("0") ||
                wanted == QLatin1String("false") ||
                wanted == QLatin1String("off") || wanted == QLatin1String("no"));
  }

  if (!enabled) {
    if (error != nullptr) {
      *error = QStringLiteral(
          "it is switched off (Settings/apiEnabled or RB_API_ENABLED)");
    }
    return QString();
  }

  // 127.0.0.1 unless somebody deliberately says otherwise, because opening
  // this to the network is a decision and not a default. See
  // docs/API.md section 13.
  QString address =
      GetSettings()
          ->value("Settings/apiAddress", QStringLiteral("127.0.0.1"))
          .toString();
  if (!fromEnv("RB_API_ADDRESS").isEmpty()) {
    address = fromEnv("RB_API_ADDRESS");
  }

  quint16 port =
      quint16(GetSettings()->value("Settings/apiPort", 19999).toUInt());
  bool readable = false;
  const uint asked = fromEnv("RB_API_PORT").toUInt(&readable);
  if (readable && asked > 0 && asked < 65536) {
    port = quint16(asked);
  }

  if (!ApiServer::instance().start(address, port, error)) {
    return QString();
  }
  return QStringLiteral("http://%1:%2/")
      .arg(address)
      .arg(ApiServer::instance().port());
}

// Everything the headless path needs that the window would otherwise set up.
//
// Deliberately short: no single-instance lock, so a task can be run from a
// terminal or a cron job while the window is open, and no writable-directory
// check, because that one reports failure through a message box.
int RunHeadless(int argc, char *argv[], const CommandLine &cmd) {
#ifdef Q_OS_WIN
  AttachToParentConsole();
#endif

  QCoreApplication app(argc, argv);
  app.setApplicationName("rclone-browser");
  app.setOrganizationName("rclone-browser");

  // After the names are set, because where the log goes depends on them.
  DebugLog::install();

  // Closed here rather than in main(): QtSql needs a QCoreApplication to be
  // alive to take a connection down, and this one dies with this function.
  // Every early return below leaves through this.
  struct CloseDatabase {
    ~CloseDatabase() { Database::closeForThread(); }
  } closeDatabase;

  QTextStream out(stdout);
  QTextStream err(stderr);

  if (cmd.help) {
    PrintUsage(out);
    return TaskRunner::Ok;
  }

  if (!cmd.unknownOption.isEmpty()) {
    err << "unknown option: " << cmd.unknownOption << "\n\n";
    PrintUsage(err);
    return TaskRunner::UsageError;
  }

  if (cmd.listTasks) {
    return TaskRunner::listTasks(out);
  }

  if (cmd.daemon) {
    // The same lock the window takes. Held for as long as this runs.
    QLockFile lockFile(InstanceLockPath());
    if (!lockFile.tryLock(100)) {
      err << "rclone-browser is already running -- as a window or as another "
             "daemon.\nOnly one may run at a time: two would each start the "
             "same schedule.\n";
      return TaskRunner::AlreadyRunning;
    }

    // The whole application, with nothing watching it. Every part of this is
    // the same code the window runs on: the queue keeps itself moving, the
    // clock looks for schedules that have come due, and the scripts fire at
    // the moments they are configured for.
    AppCore::instance().start();

    out << "rclone-browser running without a window. Ctrl+C to stop.\n";

    QString apiError;
    const QString url = StartApi(&apiError);
    if (!url.isEmpty()) {
      out << "web interface on " << url << "\n"
          << "API on " << url << "api/v1/\n"
          << "token: " << ApiServer::token() << "\n";
    } else {
      // Said, not swallowed. A daemon whose API quietly did not open looks
      // exactly like one that is working until something tries to reach it.
      err << "the API could not start: " << apiError << "\n";
      err.flush();
    }
    out.flush();

    // Stopping, by whichever road the request arrives: a signal from the
    // operating system ("docker stop" sends SIGTERM and kills ten seconds
    // later), Ctrl+C, or POST /shutdown from somewhere that can send neither.
    // One function, so that all three leave the same things behind.
    const auto stop = [&app, &out](const QString &by) {
      out << "stopping (" << by << ")...\n";
      out.flush();

      const int left = AppCore::instance().shutdown(8000);
      ApiServer::instance().stop();

      if (left > 0) {
        out << left << " job(s) had not stopped in time\n";
      } else {
        out << "stopped\n";
      }
      out.flush();

      ShutdownSignal::instance().finished();
      app.quit();
    };

    ShutdownSignal::instance().install();
    QObject::connect(&ShutdownSignal::instance(), &ShutdownSignal::requested,
                     &app, stop);

    ApiServer::instance().setShutdownAllowed(true);
    QObject::connect(&ApiServer::instance(), &ApiServer::shutdownRequested,
                     &app, [stop]() { stop(QStringLiteral("asked over the API")); });

    return app.exec();
  }

  if (cmd.task.isEmpty()) {
    err << "--run-task needs the name or id of a task\n\n";
    PrintUsage(err);
    return TaskRunner::UsageError;
  }

  SetRclone(GetSettings()->value("Settings/rclone").toString());
  SetRcloneConf(GetSettings()->value("Settings/rcloneConf").toString());

  // Deliberately not AppCore::start() here. --run-task does one thing and
  // exits; starting the clock and the queue would leave a scheduled run half
  // begun when the process ends a few seconds later. The daemon, when there
  // is one, is the mode that starts the core and stays.
  return TaskRunner::runTask(cmd.task, cmd.dryRun, out, err);
}

