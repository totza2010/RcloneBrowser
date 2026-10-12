#include "shutdown_signal.h"
#include "debug_log.h"

#include <QCoreApplication>
#include <QMetaObject>

#if defined(Q_OS_WIN)
#include <QSemaphore>
#include <windows.h>
#else
#include <QSocketNotifier>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

#if defined(Q_OS_WIN)

// Released by finished(), waited on by the console handler.
QSemaphore gDone;

BOOL WINAPI consoleHandler(DWORD event) {
  const char *by = nullptr;
  bool mustWait = false;

  switch (event) {
  case CTRL_C_EVENT:
    by = "Ctrl+C";
    break;
  case CTRL_BREAK_EVENT:
    by = "Ctrl+Break";
    break;
  case CTRL_CLOSE_EVENT:
    by = "console closed";
    mustWait = true;
    break;
  case CTRL_LOGOFF_EVENT:
  case CTRL_SHUTDOWN_EVENT:
    by = "session ending";
    mustWait = true;
    break;
  default:
    return FALSE;
  }

  // This runs on a thread the system made for the purpose, so the request is
  // posted to the main thread rather than acted on here.
  const QString reason = QString::fromLatin1(by);
  QMetaObject::invokeMethod(
      &ShutdownSignal::instance(),
      [reason]() { ShutdownSignal::instance().deliver(reason); },
      Qt::QueuedConnection);

  // For a closing console or an ending session the process is killed the
  // moment this returns. Five seconds is what the system allows; waiting
  // longer changes nothing.
  if (mustWait) {
    gDone.tryAcquire(1, 4500);
  }
  return TRUE;
}

#else

// The write end is used from the signal handler and the read end from the
// event loop. write() is one of the few things a handler is allowed to call.
int gPipe[2] = {-1, -1};

void signalHandler(int number) {
  const char byte = static_cast<char>(number);
  const ssize_t ignored = ::write(gPipe[0], &byte, 1);
  (void)ignored;
}

#endif

} // namespace

ShutdownSignal &ShutdownSignal::instance() {
  static ShutdownSignal signal;
  return signal;
}

ShutdownSignal::ShutdownSignal(QObject *parent) : QObject(parent) {}

void ShutdownSignal::deliver(const QString &by) {
  if (mDelivered) {
    return;
  }
  mDelivered = true;
  qCDebug(rbApp) << "asked to stop by" << by;
  emit requested(by);
}

void ShutdownSignal::install() {
  if (mInstalled) {
    return;
  }
  mInstalled = true;

#if defined(Q_OS_WIN)
  SetConsoleCtrlHandler(consoleHandler, TRUE);
#else
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, gPipe) != 0) {
    qCDebug(rbApp) << "could not listen for signals; stopping will be abrupt";
    return;
  }

  mNotifier = new QSocketNotifier(gPipe[1], QSocketNotifier::Read, this);
  QObject::connect(mNotifier, &QSocketNotifier::activated, this, [this]() {
    char byte = 0;
    const ssize_t got = ::read(gPipe[1], &byte, 1);
    if (got != 1) {
      return;
    }
    deliver(byte == SIGTERM  ? QStringLiteral("SIGTERM")
            : byte == SIGINT ? QStringLiteral("SIGINT")
                             : QStringLiteral("SIGHUP"));
  });

  struct sigaction action;
  action.sa_handler = signalHandler;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_RESTART;
  sigaction(SIGTERM, &action, nullptr);
  sigaction(SIGINT, &action, nullptr);
  sigaction(SIGHUP, &action, nullptr);
#endif
}

void ShutdownSignal::finished() {
#if defined(Q_OS_WIN)
  gDone.release();
#endif
}
