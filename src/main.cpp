#include "database.h"
#include "debug_log.h"
#include "api_server.h"
#include "app_core.h"
#include "main_window.h"
#include "shutdown_signal.h"
#include "task_runner.h"
#include "utils.h"
#include "window_language.h"

#include <QLockFile>
#include <QTextStream>

#ifdef Q_OS_WIN
// required by messageHandler
#include "stdio.h"
// required by AttachToParentConsole
#include <windows.h>
#endif

#ifdef Q_OS_WIN
// redirect debug to stderr so we can run "app.exe > log.txt 2>&1" on Windows
void messageHandler(QtMsgType type, const QMessageLogContext &context,
                    const QString &msg) {
  QByteArray localMsg = msg.toLocal8Bit();
  const char *file = context.file ? context.file : "";
  const char *function = context.function ? context.function : "";
  switch (type) {
  case QtDebugMsg:
    fprintf(stderr, "Debug: %s (%s:%u, %s)\n", localMsg.constData(), file,
            context.line, function);
    break;
  case QtInfoMsg:
    fprintf(stderr, "Info: %s (%s:%u, %s)\n", localMsg.constData(), file,
            context.line, function);
    break;
  case QtWarningMsg:
    fprintf(stderr, "Warning: %s (%s:%u, %s)\n", localMsg.constData(), file,
            context.line, function);
    break;
  case QtCriticalMsg:
    fprintf(stderr, "Critical: %s (%s:%u, %s)\n", localMsg.constData(), file,
            context.line, function);
    break;
  case QtFatalMsg:
    fprintf(stderr, "Fatal: %s (%s:%u, %s)\n", localMsg.constData(), file,
            context.line, function);
    break;
  }
}
#endif

namespace {

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
void AttachToParentConsole() {
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

// The command line, read straight from argv.
//
// QCommandLineParser wants QCoreApplication::arguments(), and the whole point
// here is deciding which kind of application to create -- so the decision has
// to be made before either exists.
struct CommandLine {
  bool headless = false; // asked for something that needs no window
  bool listTasks = false;
  bool help = false;
  bool dryRun = false;
  bool daemon = false;
  QString task;
  QString unknownOption;
};

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

void PrintUsage(QTextStream &out) {
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

} // namespace

int main(int argc, char *argv[]) {

#ifdef Q_OS_WIN
  // redirect debug to stderr so we can run "app.exe > log.txt 2>&1" on Windows
  QLoggingCategory::defaultCategory()->setEnabled(QtDebugMsg, true);
  qInstallMessageHandler(messageHandler);
#endif

  // Before anything that would need a screen. The window path below opens
  // message boxes on failure, which is no use to a cron job.
  const CommandLine cmd = ReadCommandLine(argc, argv);
  if (cmd.headless) {
    return RunHeadless(argc, argv, cmd);
  }

  // set locale to UK english
  // would be great to let Qt manage it but it leads to issue like this one:
  // https://github.com/kapitainsky/RcloneBrowser/issues/96
  // maybe one day somebody looks into localizing RB and solves this better
  // From now on chaps - we speak english only
  QLocale l(QLocale::English, QLocale::UnitedKingdom);
  QLocale::setDefault(l);

  QApplication app(argc, argv);

  //  app.setApplicationDisplayName("Rclone Browser");
  app.setApplicationName("rclone-browser");
  app.setOrganizationName("rclone-browser");
  app.setWindowIcon(QIcon(":/icons/icon.png"));

  DebugLog::install();

// initialize SSL libraries
// see: https://github.com/linuxdeploy/linuxdeploy-plugin-qt/issues/57
#if defined(Q_OS_LINUX)
  QString currentDir = QDir::currentPath();
  QDir::setCurrent(QCoreApplication::applicationDirPath());
  QSslSocket::supportsSsl();
  QDir::setCurrent(currentDir);
#endif

  auto settings = GetSettings();

  // The words of the window, in the language chosen in Preferences. Before
  // any window is made: a translation installed later changes nothing that
  // is already on screen.
  WindowLanguage::Install(&app);

  // initialize proxy settings
  if (!(settings->contains("Settings/useProxy"))) {
    settings->setValue("Settings/useProxy", "false");
  };
  if (!(settings->contains("Settings/http_proxy"))) {
    settings->setValue("Settings/http_proxy", "");
  };
  if (!(settings->contains("Settings/https_proxy"))) {
    settings->setValue("Settings/https_proxy", "");
  };
  if (!(settings->contains("Settings/no_proxy"))) {
    settings->setValue("Settings/no_proxy", "");
  };

  if (settings->value("Settings/useProxy").toBool()) {
    qputenv("HTTP_PROXY", settings->value("Settings/http_proxy").toByteArray());
    qputenv("http_proxy", settings->value("Settings/http_proxy").toByteArray());
    qputenv("HTTPS_PROXY",
            settings->value("Settings/https_proxy").toByteArray());
    qputenv("https_proxy",
            settings->value("Settings/https_proxy").toByteArray());
    qputenv("NO_PROXY", settings->value("Settings/no_proxy").toByteArray());
    qputenv("no_proxy", settings->value("Settings/no_proxy").toByteArray());
  }

  // remmber darkMode state on app startup
  // during first run the darkModeIni key might not exist
  if (!(settings->contains("Settings/darkModeIni"))) {
    // if darkModeIni does not exist create new key
    settings->setValue("Settings/darkModeIni", "true");
  };

  // during first run the darkMode key might not exist
  if (!(settings->contains("Settings/darkMode"))) {
    // if darkMode does not exist create new key
    settings->setValue("Settings/darkMode", "true");
  };

  bool darkMode = settings->value("Settings/darkMode").toBool();

  settings->setValue("Settings/darkModeIni", darkMode);

  // during first run the iconSize key might not exist
  if (!(settings->contains("Settings/iconSize"))) {
    // if iconSize does not exist create new key
    settings->setValue("Settings/iconSize", "M");
  };

  // during first run the iconsLayout key might not exist
  if (!(settings->contains("Settings/iconsLayout"))) {
    // if iconsLayout does not exist create new key
    settings->setValue("Settings/iconsLayout", "tiles");
  };

  // during first run the iconsColour key might not exist
  if (!(settings->contains("Settings/iconsColour"))) {
    // if iconsColour does not exist create new key
    settings->setValue("Settings/iconsColour", "black");
  };

  // during first run the buttonStyle key might not exist
  if (!(settings->contains("Settings/buttonStyle"))) {
    // if buttonstyle does not exist create new key
    settings->setValue("Settings/buttonStyle", "icononly");
  };

  // during first run the fontSize key might not exist
  if (!(settings->contains("Settings/fontSize"))) {
    // if fontSize does not exist create new key
#ifdef Q_OS_WIN
    // on Windows Fussion mode uses too small fonts
    settings->setValue("Settings/fontSize", "1");
#else
    settings->setValue("Settings/fontSize", "0");
#endif
  };

  // during first run the buttonSize key might not exist
  if (!(settings->contains("Settings/buttonSize"))) {
    // if buttonSize does not exist create new key
    settings->setValue("Settings/buttonSize", "0");
  };

  // during first run the sortTask key might not exist
  if (!(settings->contains("Settings/sortTask"))) {
    // if sortTask does not exist create new key
    settings->setValue("Settings/sortTask", "false");
  };

  // during first run the remoteMode key might not exist
  if (!(settings->contains("Settings/remoteMode"))) {
    // if remoteMode does not exist create new key
    settings->setValue("Settings/remoteMode", "main");
  };

  // during first run the remoteType key might not exist
  if (!(settings->contains("Settings/remoteType"))) {
    // if remoteType does not exist create new key
    settings->setValue("Settings/remoteType", "main");
  };

  if (!(settings->contains("Settings/soundNotif"))) {
    settings->setValue("Settings/soundNotif", "false");
  };

  // true - scheduler runing, false - scheduler not runing
  if (!(settings->contains("Settings/schedulerStatus"))) {
    settings->setValue("Settings/schedulerStatus", "true");
  };

  // true - queue runing, false - queue not runing
  if (!(settings->contains("Settings/queueStatus"))) {
    settings->setValue("Settings/queueStatus", "true");
  };

  if (!(settings->contains("Settings/startMinimisedToTray"))) {
    settings->setValue("Settings/startMinimisedToTray", "false");
  };

  if (!(settings->contains("Settings/transferAutoName"))) {
    settings->setValue("Settings/transferAutoName", "false");
  };

  if (!(settings->contains("Settings/transferAddToQueue"))) {
    settings->setValue("Settings/transferAddToQueue", "false");
  };

  // preemptive content loading on/off
  if (!(settings->contains("Settings/preemptiveLoading"))) {
    settings->setValue("Settings/preemptiveLoading", "true");
  };

  // preemptive content loading level (0,1,2)
  if (!(settings->contains("Settings/preemptiveLoadingLevel"))) {
    settings->setValue("Settings/preemptiveLoadingLevel", "0");
  }

  // during first run the queueScript key might not exist
  if (!(settings->contains("Settings/queueScript"))) {
    settings->setValue("Settings/queueScript", "");
  };

  // script to run when transfer jobs start
  if (!(settings->contains("Settings/transferOnScript"))) {
    settings->setValue("Settings/transferOnScript", "");
  };

  // script to run when last transfer jobs finished
  if (!(settings->contains("Settings/transferOffScript"))) {
    settings->setValue("Settings/transferOffScript", "");
  };

  // during first run the queueScriptRun key might not exist
  if (!(settings->contains("Settings/queueScriptRun"))) {
    settings->setValue("Settings/queueScriptRun", "false");
  };

  if (!(settings->contains("Settings/jobStartScriptRun"))) {
    settings->setValue("Settings/jobStartScriptRun", "false");
  };

  if (!(settings->contains("Settings/jobLastFinishedScriptRun"))) {
    settings->setValue("Settings/jobLastFinishedScriptRun", "false");
  };

  // remember and re-use last transfer options
  if (!(settings->contains("Settings/rememberLastOptions"))) {
    settings->setValue("Settings/rememberLastOptions", "true");
  };

  // use ports (49152-65535) -
  // https://www.iana.org/assignments/service-names-port-numbers/service-names-port-numbers.xhtml
  // during first run the rcPortStartWin key might not exist
  if (!(settings->contains("Settings/rcPortStartWin"))) {
    // if rcPortStartWin does not exist create new key
    settings->setValue("Settings/rcPortStartWin", "49700");
  };

  // set application font size
  int fontsize = 0;
  fontsize = (settings->value("Settings/fontSize").toInt());

  QFont defaultFont = QApplication::font();
  defaultFont.setPointSize(defaultFont.pointSize() + fontsize);
  qApp->setFont(defaultFont);

  // enforce one instance of Rclone Browser per user
  QString tmpDir;
  QString applicationNameBase;
  QFileInfo applicationPath;
  QFileInfo applicationUserPath;

  // QString xdg_config_home = qgetenv("XDG_CONFIG_HOME");
  // qDebug() << QString("main.cpp $XDG_CONFIG_HOME: " + xdg_config_home);

  // QString APPIMAGE = qgetenv("APPIMAGE");
  // qDebug() << QString("main.cpp $APPIMAGE: " + APPIMAGE);

  QFileInfo appBundlePath;

  if (IsPortableMode()) {

    //  qDebug() << QString("isPortable is true");
    //  applicationPath = qApp->applicationFilePath();
#ifdef Q_OS_MACOS
    // on macOS excecutable file is located in
    // ./rclone-browser.app/Contents/MasOS/
    // to get actual bundle folder we have
    // to traverse three levels up
    applicationPath = qApp->applicationFilePath();
    tmpDir = applicationPath.absolutePath() + "/../../..";

    // get bundle name
    QFileInfo MacOSPath = applicationPath.dir().path();
    QFileInfo ContentsPath = MacOSPath.dir().path();
    appBundlePath = ContentsPath.dir().path();

#else
    // not macOS
#ifdef Q_OS_WIN
  QFileInfo applicationPath(qApp->applicationFilePath());
  QString tmpDir = applicationPath.absolutePath();
#else
    QString xdg_config_home = qgetenv("XDG_CONFIG_HOME");
    tmpDir = xdg_config_home + "/rclone-browser";
    // create ./rclone-browser folder
    if (!QDir(tmpDir).exists()) {
      QDir().mkdir(tmpDir);
    }
#endif
#endif
  } else {
    // not portable mode
    // get tmp folder from Qt  - OS dependend
    tmpDir = QDir::tempPath();
  }

  // check if tmpDir writable
  // as isWritable does weird things on Windows
  // we do this old fashioned way by creating temp file
  QTemporaryFile tempfile(tmpDir + "/rclone-browserXXXXXX.test");

  if (tempfile.open()) {
    tempfile.close();
    tempfile.remove();
  } else {
    // folder has no write access
    if (IsPortableMode()) {
      QMessageBox msgBox;
      msgBox.setIcon(QMessageBox::Warning);
      msgBox.setText("You need write "
                     "access to this folder:\n\n"
#ifdef Q_OS_MACOS
                     + appBundlePath.absolutePath() +
#else
#ifdef Q_OS_WIN
                     + tmpDir +
#else
                     + tmpDir.left(tmpDir.length() - 15) +
#endif
#endif
                     "\n\n"
#ifdef Q_OS_MACOS
                     "Or remove file:\n\n" +
                     appBundlePath.baseName() +
                     ".ini \n\nfrom the above folder "
#else
#ifdef Q_OS_WIN
                     "Or remove file:\n\n" +
                     applicationPath.baseName() +
                     ".ini \n\nfrom the above folder "
#else
                     "Or remove folder:\n\n" +
                     tmpDir.left(tmpDir.length() - 15) +
                     "\n\n"
#endif
#endif
                     "to disable portable mode.");
      msgBox.exec();
    } else {
      QMessageBox msgBox;
      msgBox.setIcon(QMessageBox::Warning);
      msgBox.setText("You need write "
                     "access to this folder: \n\n"
#ifdef Q_OS_MACOS
                     + tmpDir
#else

#ifdef Q_OS_WIN
                     + tmpDir
#else

                     + tmpDir.left(tmpDir.length() - 15)
#endif
#endif
      );
      msgBox.exec();
    }
    return static_cast<int>(
        0x80004004); // exit immediately if folder not writable
  }

  // qDebug() << QString("main.cpp tmpDir:  " + tmpDir);

  // The same file the daemon locks. See InstanceLockPath().
  QLockFile lockFile(InstanceLockPath());

  if (!lockFile.tryLock(100)) {
    // if already running display warning and quit
    QMessageBox msgBox;
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setText("Rclone Browser is already running -- as a window, or "
                   "in the background as a daemon."
                   "\r\n\nOnly one may run at a time. If it is running in "
                   "the background, its web interface is the way to reach "
                   "it.");
    msgBox.exec();
    return static_cast<int>(
        0x80004004); // exit immediately if another instance is running
  }

  // Before the window, not inside it. Reading the stored queue and the
  // schedules and starting the clock is the application running; a window is
  // one way of watching it. See docs/LAYER-SPLIT.md.
  AppCore::instance().start();

  // And the page, beside the window. Both are views of the core just
  // started: what one does the other sees, because neither is where anything
  // happens. A failure here is not a reason to refuse to open -- the window
  // works without it -- but it is said, in the log and in the Help menu.
  QString apiError;
  const bool apiUp = !StartApi(&apiError).isEmpty();
  if (!apiUp) {
    qCDebug(rbApp) << "the web interface is not available:" << apiError;
  }
  // Why, for the Help menu to say -- the window has no other way to know.
  app.setProperty("apiProblem", apiUp ? QString() : apiError);

  MainWindow w;
  w.show();

  // Not up, and not because it was switched off: most often its port was
  // taken when the program opened -- by another copy of it, closing as this
  // one opened. Said where it is seen, and tried again until it works,
  // rather than leaving the page unreachable for as long as the window is
  // open with nothing to say why. Found 2026-10-19: the page "would not
  // open", and the window was running without it.
  if (!apiUp && !apiError.contains(QLatin1String("switched off"))) {
    w.statusBar()->showMessage(
        QStringLiteral("The web page is not available: %1 -- trying again "
                       "every 15 seconds.")
            .arg(apiError));
    auto *retry = new QTimer(&app);
    retry->setInterval(15000);
    QObject::connect(retry, &QTimer::timeout, &app, [&w, &app, retry]() {
      if (ApiServer::instance().isListening()) {
        retry->stop();
        return;
      }
      QString again;
      const QString url = StartApi(&again);
      if (url.isEmpty()) {
        app.setProperty("apiProblem", again);
        return;
      }
      retry->stop();
      app.setProperty("apiProblem", QString());
      qCDebug(rbApp) << "the web interface is up after all, at" << url;
      w.statusBar()->showMessage(
          QStringLiteral("The web page is available at %1").arg(url), 20000);
    });
    retry->start();
    app.setProperty("apiRetrying", true);
  }

  // A window in a container is stopped the way a daemon is: by a signal,
  // with nobody there to answer "are you sure?". So this does not go through
  // the window's own closing, which asks -- it stops the core and leaves.
  // exit() rather than quit(), because quit() offers every window the chance
  // to refuse.
  ShutdownSignal::instance().install();
  QObject::connect(&ShutdownSignal::instance(), &ShutdownSignal::requested,
                   &app, [](const QString &by) {
                     qCDebug(rbApp) << "stopping the window, asked by" << by;
                     AppCore::instance().shutdown(8000);
                     ApiServer::instance().stop();
                     ShutdownSignal::instance().finished();
                     QCoreApplication::exit(0);
                   });

  const int code = app.exec();
  Database::closeForThread();
  return code;
}
