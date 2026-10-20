#pragma once

// The program without a window: what it does when asked on the command line
// (--run-task, --list-tasks, --daemon), and opening the API and the page.
// Shared by the window's main() and by the build that has no window at all
// (RB_BUILD_GUI=OFF -- the web-only image), which is why it lives apart from
// main.cpp and needs nothing but the core.

#include <QString>

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

CommandLine ReadCommandLine(int argc, char *argv[]);

// The headless run: --run-task, --list-tasks, --help, --daemon. Returns the
// exit code.
int RunHeadless(int argc, char *argv[], const CommandLine &cmd);

// Opens the API and the page it serves; the address, or empty with why not.
QString StartApi(QString *error);

// Where the one-copy-at-a-time lock file is kept.
QString InstanceLockPath();
