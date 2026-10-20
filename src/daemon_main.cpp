// main() of the build with no window (RB_BUILD_GUI=OFF): the web-only image.
//
// The same program as the window's --daemon, --run-task and --list-tasks,
// from the same code (headless.cpp) -- only there is no window to fall back
// to, so started with nothing asked of it, it is the daemon.

#include "headless.h"

int main(int argc, char *argv[]) {
  CommandLine cmd = ReadCommandLine(argc, argv);
  if (!cmd.headless) {
    cmd.headless = true;
    cmd.daemon = true;
  }
  return RunHeadless(argc, argv, cmd);
}
