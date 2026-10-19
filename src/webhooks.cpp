#include "webhooks.h"
#include "debug_log.h"
#include "job_json.h"
#include "job_registry.h"
#include "job_stats.h"
#include "running_job.h"
#include "utils.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageAuthenticationCode>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSettings>
#include <QUrl>
#include <QUuid>

namespace {

const char *const kStored = "Settings/webhooks";

// Long enough for a slow receiver, short enough that a dead address does not
// keep a request open for minutes.
const int kTimeoutMs = 15000;

} // namespace

Webhooks &Webhooks::instance() {
  static Webhooks *hooks = new Webhooks();
  return *hooks;
}

void Webhooks::install() {
  if (mInstalled) {
    return;
  }
  mInstalled = true;
  QObject::connect(&JobRegistry::instance(), &JobRegistry::jobFinished, this,
                   &Webhooks::jobEnded);
}

QStringList Webhooks::Formats() {
  return {QStringLiteral("json"), QStringLiteral("text"),
          QStringLiteral("slack"), QStringLiteral("discord")};
}

QString Webhooks::WhyNotHook(const Webhook &hook) {
  const QUrl url(hook.url.trimmed(), QUrl::StrictMode);
  if (!url.isValid() || url.host().isEmpty() ||
      (url.scheme() != QLatin1String("http") &&
       url.scheme() != QLatin1String("https"))) {
    return QStringLiteral("give an http:// or https:// address");
  }
  if (!Formats().contains(hook.format)) {
    return QStringLiteral("a format is one of: %1")
        .arg(Formats().join(QStringLiteral(", ")));
  }
  return QString();
}

void Webhooks::load() {
  if (mLoaded) {
    return;
  }
  mLoaded = true;
  mHooks.clear();
  const QJsonArray stored =
      QJsonDocument::fromJson(
          GetSettings()->value(kStored).toString().toUtf8())
          .array();
  for (const QJsonValue &value : stored) {
    const QJsonObject object = value.toObject();
    Webhook hook;
    hook.id = object.value(QStringLiteral("id")).toString();
    hook.name = object.value(QStringLiteral("name")).toString();
    hook.url = object.value(QStringLiteral("url")).toString();
    hook.failuresOnly = object.value(QStringLiteral("failuresOnly")).toBool();
    hook.everyJob = object.value(QStringLiteral("everyJob")).toBool();
    hook.format =
        object.value(QStringLiteral("format")).toString(QStringLiteral("json"));
    hook.secret = object.value(QStringLiteral("secret")).toString();
    // One that cannot be read is left out rather than sent to half an
    // address.
    if (!hook.id.isEmpty() && WhyNotHook(hook).isEmpty()) {
      mHooks.append(hook);
    }
  }
}

void Webhooks::save() {
  QJsonArray stored;
  for (const Webhook &hook : mHooks) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), hook.id);
    object.insert(QStringLiteral("name"), hook.name);
    object.insert(QStringLiteral("url"), hook.url);
    object.insert(QStringLiteral("failuresOnly"), hook.failuresOnly);
    object.insert(QStringLiteral("everyJob"), hook.everyJob);
    object.insert(QStringLiteral("format"), hook.format);
    object.insert(QStringLiteral("secret"), hook.secret);
    stored.append(object);
  }
  GetSettings()->setValue(
      kStored,
      QString::fromUtf8(QJsonDocument(stored).toJson(QJsonDocument::Compact)));
  GetSettings()->sync();
  emit changed();
}

QList<Webhook> Webhooks::list() const {
  const_cast<Webhooks *>(this)->load();
  return mHooks;
}

const Webhook *Webhooks::find(const QString &id) const {
  const_cast<Webhooks *>(this)->load();
  for (const Webhook &hook : mHooks) {
    if (hook.id == id) {
      return &hook;
    }
  }
  return nullptr;
}

QString Webhooks::add(const Webhook &given, QString *why) {
  load();
  Webhook hook = given;
  hook.url = hook.url.trimmed();
  hook.name = hook.name.trimmed();
  const QString problem = WhyNotHook(hook);
  if (!problem.isEmpty()) {
    if (why != nullptr) {
      *why = problem;
    }
    return QString();
  }
  hook.id = QUuid::createUuid().toString(QUuid::Id128).left(12);
  if (hook.name.isEmpty()) {
    hook.name = QUrl(hook.url).host();
  }
  hook.lastResult.clear();
  hook.lastAt = 0;
  mHooks.append(hook);
  save();
  return hook.id;
}

bool Webhooks::update(const QString &id, const Webhook &given,
                      bool clearSecret, QString *why) {
  load();
  for (Webhook &hook : mHooks) {
    if (hook.id != id) {
      continue;
    }
    Webhook changed = given;
    changed.id = id;
    changed.url = changed.url.trimmed();
    changed.name = changed.name.trimmed();
    if (changed.name.isEmpty()) {
      changed.name = QUrl(changed.url).host();
    }
    if (changed.secret.isEmpty() && !clearSecret) {
      changed.secret = hook.secret;
    }
    const QString problem = WhyNotHook(changed);
    if (!problem.isEmpty()) {
      if (why != nullptr) {
        *why = problem;
      }
      return false;
    }
    changed.lastResult = hook.lastResult;
    changed.lastAt = hook.lastAt;
    hook = changed;
    save();
    return true;
  }
  if (why != nullptr) {
    *why = QStringLiteral("there is no such webhook");
  }
  return false;
}

bool Webhooks::remove(const QString &id) {
  load();
  for (int i = 0; i < mHooks.size(); ++i) {
    if (mHooks.at(i).id == id) {
      mHooks.removeAt(i);
      save();
      return true;
    }
  }
  return false;
}

QString Webhooks::EventName(const QString &finalStatus) {
  if (finalStatus == QLatin1String("error")) {
    return QStringLiteral("job.failed");
  }
  if (finalStatus == QLatin1String("stopped")) {
    return QStringLiteral("job.stopped");
  }
  return QStringLiteral("job.finished");
}

QJsonObject Webhooks::EventFor(const RunningJob &job) {
  QJsonObject event;
  event.insert(QStringLiteral("event"), EventName(job.finalStatus()));
  event.insert(QStringLiteral("at"), JobJson::Stamp(QDateTime::currentDateTime()));
  event.insert(QStringLiteral("job"), JobJson::Write(job));
  event.insert(QStringLiteral("summary"), Summary(event));
  return event;
}

QString Webhooks::Summary(const QJsonObject &event) {
  const QString name = event.value(QStringLiteral("event")).toString();
  const QJsonObject job = event.value(QStringLiteral("job")).toObject();
  const QString how = name == QLatin1String("job.failed")    ? "Failed"
                      : name == QLatin1String("job.stopped") ? "Stopped"
                      : name == QLatin1String("test")        ? "Test"
                                                             : "Finished";
  QStringList parts{QStringLiteral("%1: %2").arg(
      how, job.value(QStringLiteral("info")).toString())};
  const QJsonObject stats = job.value(QStringLiteral("stats")).toObject();
  const qint64 transfers =
      qint64(stats.value(QStringLiteral("transfers")).toDouble());
  const qint64 bytes = qint64(stats.value(QStringLiteral("bytes")).toDouble());
  const qint64 errors = qint64(stats.value(QStringLiteral("errors")).toDouble());
  if (transfers > 0) {
    parts << QStringLiteral("%1 files").arg(transfers);
  }
  if (bytes > 0) {
    parts << FormatBytes(bytes);
  }
  if (errors > 0) {
    parts << QStringLiteral("%1 errors").arg(errors);
  }
  return parts.join(QStringLiteral(" · "));
}

QByteArray Webhooks::Body(const QString &format, const QJsonObject &event,
                          QByteArray *contentType) {
  const QString line = event.value(QStringLiteral("summary")).toString();
  if (format == QLatin1String("text")) {
    *contentType = "text/plain; charset=utf-8";
    return line.toUtf8();
  }
  *contentType = "application/json";
  if (format == QLatin1String("slack")) {
    return QJsonDocument(QJsonObject{{QStringLiteral("text"), line}})
        .toJson(QJsonDocument::Compact);
  }
  if (format == QLatin1String("discord")) {
    return QJsonDocument(QJsonObject{{QStringLiteral("content"), line}})
        .toJson(QJsonDocument::Compact);
  }
  return QJsonDocument(event).toJson(QJsonDocument::Compact);
}

void Webhooks::jobEnded(RunningJob *job) {
  if (job == nullptr) {
    return;
  }
  load();
  if (mHooks.isEmpty()) {
    return;
  }
  const QJsonObject event = EventFor(*job);
  const bool failed =
      event.value(QStringLiteral("event")).toString() == QLatin1String("job.failed");
  for (const Webhook &hook : mHooks) {
    if (!hook.everyJob && job->kind() != JobKind::Transfer) {
      continue;
    }
    if (hook.failuresOnly && !failed) {
      continue;
    }
    deliver(hook, event, nullptr);
  }
}

void Webhooks::test(const QString &id,
                    std::function<void(int, const QString &)> done) {
  const Webhook *hook = find(id);
  if (hook == nullptr) {
    done(0, QStringLiteral("there is no such webhook"));
    return;
  }
  QJsonObject job;
  job.insert(QStringLiteral("info"), QStringLiteral("a test from RcloneBrowser"));
  job.insert(QStringLiteral("kind"), QStringLiteral("transfer"));
  job.insert(QStringLiteral("state"), QStringLiteral("finished"));
  QJsonObject event;
  event.insert(QStringLiteral("event"), QStringLiteral("test"));
  event.insert(QStringLiteral("at"), JobJson::Stamp(QDateTime::currentDateTime()));
  event.insert(QStringLiteral("job"), job);
  event.insert(QStringLiteral("summary"), Summary(event));
  deliver(*hook, event, done);
}

void Webhooks::deliver(const Webhook &hook, const QJsonObject &event,
                       std::function<void(int, const QString &)> done) {
  if (mNetwork == nullptr) {
    mNetwork = new QNetworkAccessManager(this);
  }
  QByteArray type;
  const QByteArray body = Body(hook.format, event, &type);

  QNetworkRequest request{QUrl(hook.url)};
  request.setHeader(QNetworkRequest::ContentTypeHeader, type);
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    QByteArray("RcloneBrowser"));
  request.setRawHeader("X-Rb-Event",
                       event.value(QStringLiteral("event")).toString().toUtf8());
  if (!hook.secret.isEmpty()) {
    request.setRawHeader(
        "X-Rb-Signature",
        "sha256=" + QMessageAuthenticationCode::hash(
                        body, hook.secret.toUtf8(), QCryptographicHash::Sha256)
                        .toHex());
  }
  request.setTransferTimeout(kTimeoutMs);
  // An address that sends this somewhere else is followed only to the same
  // kind of address: a POST that turned into a GET would deliver nothing.
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);

  const QString id = hook.id;
  const QString what = event.value(QStringLiteral("event")).toString();
  QNetworkReply *reply = mNetwork->post(request, body);
  QPointer<Webhooks> self(this);
  QObject::connect(reply, &QNetworkReply::finished, this, [=]() {
    reply->deleteLater();
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString why = reply->error() == QNetworkReply::NoError
                            ? QString()
                            : reply->errorString();
    const QString result =
        status != 0 ? QString::number(status) : QStringLiteral("no answer: %1").arg(why);
    qCDebug(rbApp) << "webhook" << id << what << "->" << result;
    if (self) {
      for (Webhook &kept : self->mHooks) {
        if (kept.id == id) {
          kept.lastResult = result;
          kept.lastAt = QDateTime::currentMSecsSinceEpoch();
        }
      }
    }
    if (done) {
      done(status, status != 0 && status < 300 ? QString()
                   : status != 0               ? QStringLiteral("it answered %1").arg(status)
                                               : why);
    }
  });
}
