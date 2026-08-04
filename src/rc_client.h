#pragma once

// Core (L0): polls one rclone job's remote control for progress.
// Qt Core and Qt Network only -- see docs/ARCHITECTURE.md.

#include "job_stats.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QNetworkReply;

class RcClient : public QObject {
  Q_OBJECT

public:
  explicit RcClient(QObject *parent = nullptr);

  // Starts polling once rclone has announced its port. Safe to call again if
  // the port changes.
  // One second is enough for a progress display and halves the log lines
  // rclone writes about being polled when the user runs at -vv or above.
  void start(quint16 port, const QString &user, const QString &password,
             int intervalMs = 1000);
  void stop();

  bool isRunning() const { return mTimer.isActive(); }
  quint16 port() const { return mPort; }

  // Asks rclone to stop the job and quit. Used instead of killing the process
  // so in-flight writes get a chance to finish.
  void requestQuit();

signals:
  void statsReceived(const JobStats &stats);

  // Emitted once if the endpoint never answers. Callers should fall back to
  // reading the process output.
  void unavailable();

private:
  void poll();
  QNetworkRequest request(const QString &path) const;

  QNetworkAccessManager mNetwork;
  QTimer mTimer;
  QPointer<QNetworkReply> mInFlight;

  quint16 mPort = 0;
  QString mUser;
  QString mPassword;

  int mConsecutiveFailures = 0;
  bool mEverSucceeded = false;
  bool mGaveUp = false;
};
