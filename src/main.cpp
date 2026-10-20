#include "database.h"
#include "debug_log.h"
#include "headless.h"
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
