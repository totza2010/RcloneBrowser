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

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;
class QTimer;
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

  // Whether this request may be answered at all: the token, or a session
  // handed out to somebody who knew the administrator password. One function
  // rather than a check per route, for the reason given in answer().
  static bool mayAsk(const HttpRequest &request);

  // Whether POST /shutdown is honoured. Off unless something says so: a
  // window is closed by whoever is sitting at it, and a page being able to
  // close it from somewhere else would be a surprise. The daemon switches it
  // on, because for a daemon there is nobody sitting anywhere.
  void setShutdownAllowed(bool allowed) { mShutdownAllowed = allowed; }

signals:
  void started(quint16 port);
  void stopped();

  // Somebody signed in asked for the program to stop. What that means is not
  // this class's business; main() connects it to the same road a signal from
  // the operating system takes.
  void shutdownRequested();

private:
  explicit ApiServer(QObject *parent = nullptr);

  void accept();
  void read(QTcpSocket *socket);
  void answer(QTcpSocket *socket, const HttpRequest &request);

  // The routes. Each returns a status and fills `body`; splitting them this
  // way keeps the socket handling out of the part worth reading.
  //
  // `contentType` is an out-parameter because one route -- a job's log --
  // answers with text rather than JSON, and a log delivered as a JSON string
  // is a log nobody can read in a browser.
  int route(const HttpRequest &request, QByteArray *body,
            QByteArray *contentType);

  // The reading half and the doing half, kept apart. A GET must never change
  // anything, and the surest way to keep that true is for the code that
  // answers one to have no way to.
  int get(const HttpRequest &request, QByteArray *body,
          QByteArray *contentType);
  int command(const HttpRequest &request, QByteArray *body);

  // The one connection that is not a question and an answer: it is held open
  // and written to as things happen. Polling /jobs once a second is what a
  // dashboard does when it has no other choice, and it means the news is
  // always up to a second old and the cost is paid whether anything happened
  // or not.
  // Everything that is not /api/, which is the Web UI and its 404s. Answered
  // without a token, deliberately: these are markup and script that say
  // nothing about this installation, and the page they make can ask the
  // daemon nothing until somebody types the token into it. A login page that
  // needs a login is a page nobody can reach.
  //
  // The files are a fixed list rather than a directory, so there is no path
  // from outside that turns into a file name.
  bool serveWebUi(const HttpRequest &request, QByteArray *body,
                  QByteArray *contentType, int *status);

  // Signing in, out, and changing the password. Kept apart from command()
  // because these are the only requests answered without being signed in
  // already, and that exception should be visible rather than buried among
  // the ordinary commands.
  int authCommand(const HttpRequest &request, QByteArray *body);

  // One directory of a remote. Unlike every other route this cannot answer
  // straight away -- rclone has to be run and waited for -- so it holds the
  // socket and writes when the answer arrives, the way the event stream
  // does. See docs/WEB-UI.md.
  void listDirectory(QTcpSocket *socket, const HttpRequest &request,
                     const QString &remote, const QString &path);

  void openEventStream(QTcpSocket *socket);
  void broadcast(const QString &event, const QJsonObject &data);

  // Connects to the things worth telling anyone about. Done once, when the
  // first listener arrives, rather than at start(): a daemon nobody is
  // watching should not be formatting JSON for nobody.
  void watchWhatHappens();

  QTcpServer *mServer = nullptr;
  QList<QTcpSocket *> mListeners;
  QTimer *mHeartbeat = nullptr;
  bool mWatching = false;
  bool mShutdownAllowed = false;
  bool mShutdownPending = false;
};
