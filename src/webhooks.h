#pragma once

// Orchestration (L1): telling other systems when a job ends, without their
// having to ask. The event stream needs somebody holding a connection open;
// Sonarr, a phone (ntfy), a chat channel or a script would rather be told.
//
// Each webhook is an address this program POSTs to when a job ends. Heard
// from JobRegistry, so a job ends the same way to a webhook whether the
// window, the page, the queue or a schedule started it. See
// docs/WEB-API-PLAN.md A6.

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <functional>

class QNetworkAccessManager;
class RunningJob;

struct Webhook {
  QString id;
  QString name;
  QString url;
  // Only when a job fails, not when it finishes or is stopped.
  bool failuresOnly = false;
  // Every job -- a folder made, a link fetched, a mount ending -- not only
  // transfers.
  bool everyJob = false;
  // "json": the event, as below. "text": one line, for ntfy and the like.
  // "slack" / "discord": that line, the way those two want it.
  QString format = QStringLiteral("json");
  // With one, each POST carries X-Rb-Signature: sha256=<HMAC of the body>,
  // so whoever receives it can tell it came from here. Never said back.
  QString secret;

  // How the last delivery went, since the program started: the HTTP status,
  // or why there was none. Not stored.
  QString lastResult;
  qint64 lastAt = 0;
};

class Webhooks : public QObject {
  Q_OBJECT

public:
  static Webhooks &instance();

  // Starts listening to JobRegistry. Call once, after it exists.
  void install();

  QList<Webhook> list() const;
  const Webhook *find(const QString &id) const;

  // Adds one and returns its id, or an empty id with why not.
  QString add(const Webhook &hook, QString *why);
  // Changes everything but the id. An empty secret keeps the one there was
  // unless `clearSecret`.
  bool update(const QString &id, const Webhook &hook, bool clearSecret,
              QString *why);
  bool remove(const QString &id);

  // Sends a made-up event, and says how it went: the HTTP status, 0 and why
  // when there was none.
  void test(const QString &id,
            std::function<void(int status, const QString &why)> done);

  // Why this is not an address to send to, or empty.
  static QString WhyNotHook(const Webhook &hook);
  static QStringList Formats();

  // What is sent, by format: the body and its type.
  static QByteArray Body(const QString &format, const QJsonObject &event,
                         QByteArray *contentType);
  // The event for a job that has ended: {"event", "at", "summary", "job"}.
  static QJsonObject EventFor(const RunningJob &job);
  // "job.finished" | "job.failed" | "job.stopped", from the job's status.
  static QString EventName(const QString &finalStatus);
  // One line a person reads: "Finished: Copy of a → b · 3 files · 1.2 GB".
  static QString Summary(const QJsonObject &event);

signals:
  void changed();

private:
  Webhooks() = default;
  void load();
  void save();
  void deliver(const Webhook &hook, const QJsonObject &event,
               std::function<void(int status, const QString &why)> done);
  void jobEnded(RunningJob *job);

  QList<Webhook> mHooks;
  bool mLoaded = false;
  bool mInstalled = false;
  QNetworkAccessManager *mNetwork = nullptr;
};
