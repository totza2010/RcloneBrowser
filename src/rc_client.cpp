#include "rc_client.h"

#include <QNetworkReply>
#include <QUrl>

namespace {
// Enough attempts to ride out a busy moment, few enough that a job whose
// remote control never came up falls back to output parsing promptly.
constexpr int kMaxConsecutiveFailures = 6;
} // namespace

RcClient::RcClient(QObject *parent) : QObject(parent) {
  mTimer.setSingleShot(false);
  QObject::connect(&mTimer, &QTimer::timeout, this, &RcClient::poll);
}

void RcClient::start(quint16 port, const QString &user,
                     const QString &password, int intervalMs) {
  if (port == 0) {
    return;
  }
  mPort = port;
  mUser = user;
  mPassword = password;
  mConsecutiveFailures = 0;
  mGaveUp = false;

  mTimer.setInterval(intervalMs);
  mTimer.start();
  poll(); // don't make the first reading wait a whole interval
}

void RcClient::stop() {
  mTimer.stop();
  if (mInFlight) {
    mInFlight->abort();
  }
}

QNetworkRequest RcClient::request(const QString &path) const {
  QNetworkRequest req(
      QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(mPort).arg(path)));
  req.setHeader(QNetworkRequest::ContentTypeHeader,
                QStringLiteral("application/json"));
  // Sent up front rather than waiting for a 401 challenge, which would double
  // every request.
  const QByteArray credentials =
      (mUser + QLatin1Char(':') + mPassword).toUtf8().toBase64();
  req.setRawHeader("Authorization", "Basic " + credentials);
  req.setTransferTimeout(3000);
  return req;
}

void RcClient::poll() {
  // Skip rather than queue: on a slow reply the next tick would otherwise pile
  // requests onto a job that is already busy.
  if (mInFlight) {
    return;
  }

  QNetworkReply *reply = mNetwork.post(request("/core/stats"), QByteArray("{}"));
  mInFlight = reply;

  QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
      // Once it has worked, a hiccup is not a reason to abandon the endpoint.
      if (!mEverSucceeded && ++mConsecutiveFailures >= kMaxConsecutiveFailures &&
          !mGaveUp) {
        mGaveUp = true;
        mTimer.stop();
        emit unavailable();
      }
      return;
    }

    const JobStats stats = JobStats::fromCoreStats(reply->readAll());
    if (!stats.valid) {
      return;
    }
    mEverSucceeded = true;
    mConsecutiveFailures = 0;
    emit statsReceived(stats);
  });
}

void RcClient::requestQuit() {
  if (mPort == 0) {
    return;
  }
  QNetworkReply *reply =
      mNetwork.post(request("/core/quit"), QByteArray("{}"));
  QObject::connect(reply, &QNetworkReply::finished, reply,
                   &QNetworkReply::deleteLater);
}
