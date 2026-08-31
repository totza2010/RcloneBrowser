#pragma once

// Orchestration (L2): the HTTP API.
//
// It speaks this program's language, not rclone's. The queue, the saved
// tasks and the schedules are things this application holds and "rclone rcd"
// knows nothing about, so proxying rclone would answer none of the questions
// a dashboard asks. See docs/API.md sections 11 and 13.
//
// Everything it reports comes from the same objects the window reads --
// AppCore, JobRegistry, JobQueue, SchedulerStore, RemoteRegistry -- which is
// the whole point of the layer split: there is one set of answers, not a
// window's and an API's.
//
// Security, from the first endpoint rather than added later:
//   - a token is required on every request except /api/v1/ping
//   - it listens on 127.0.0.1 unless somebody deliberately says otherwise
//   - command lines are redacted before they leave, because they carry
//     backend tokens and the remote-control password
//   - nothing here can change where rclone is or what scripts run: that
//     would turn a stolen token into "run any program on this machine"

#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;
struct HttpRequest;

class ApiServer : public QObject {
  Q_OBJECT

public:
  static ApiServer &instance();

  // Starts listening. Returns false and says why if it cannot -- a port
  // already taken is the usual reason, and a daemon that quietly did not
  // open its API would be worse than one that refused to start.
  bool start(const QString &address, quint16 port, QString *error = nullptr);
  void stop();

  bool isListening() const;
  quint16 port() const;

  // The token every request must carry. Made on first use and kept in the
  // settings: an API with no password on a machine somebody else can reach
  // is not something to leave as a default.
  static QString token();
  static void setToken(const QString &value);
  static QString ensureToken();

signals:
  void started(quint16 port);
  void stopped();

private:
  explicit ApiServer(QObject *parent = nullptr);

  void accept();
  void read(QTcpSocket *socket);
  void answer(QTcpSocket *socket, const HttpRequest &request);

  // The routes. Each returns a status and fills `body`; splitting them this
  // way keeps the socket handling out of the part worth reading.
  int route(const HttpRequest &request, QByteArray *body);

  QTcpServer *mServer = nullptr;
};
