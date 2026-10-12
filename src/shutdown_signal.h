#pragma once

// Core (L0): being told to stop by the operating system.
//
// A daemon is stopped by a signal, not by a key press: "docker stop" sends
// SIGTERM, waits ten seconds and then kills. With nothing listening, that is
// simply death -- rclone is left running or cut off mid-file, the history
// says "running" for a job that is not, and the queue has no idea what
// happened to its head entry.
//
// This turns the signal into an ordinary Qt signal, delivered on the main
// thread, so the rest of the program can stop the way it would if somebody
// had asked politely. What stopping means is AppCore's business; this only
// carries the message.
//
// A signal handler may do almost nothing safely, and on Windows the
// notification arrives on a thread of its own, so neither can touch the
// application directly. Both hand over to the event loop and return.

#include <QObject>
#include <QString>

class QSocketNotifier;

class ShutdownSignal : public QObject {
  Q_OBJECT

public:
  static ShutdownSignal &instance();

  // Starts listening. Safe to call more than once.
  void install();

  // Says that stopping is finished. On Windows the system kills the process
  // as soon as the console handler returns for a window being closed or a
  // session ending, so the handler waits for this -- up to the few seconds
  // the system allows -- before it lets go.
  void finished();

  // Passes the request on, once. Public only because the Windows console
  // handler is a free function on another thread and has to reach it.
  void deliver(const QString &by);

signals:
  // Asked to stop, and by what: "SIGTERM", "Ctrl+C", "console closed". Emitted
  // once; a second signal while the first is being dealt with is ignored,
  // because the answer to "stop" twice is still just stopping.
  void requested(const QString &by);

private:
  explicit ShutdownSignal(QObject *parent = nullptr);

  bool mInstalled = false;
  bool mDelivered = false;
  QSocketNotifier *mNotifier = nullptr;
};
