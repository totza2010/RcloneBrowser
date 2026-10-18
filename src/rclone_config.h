#pragma once

// Orchestration (L1): what a remote is made of, read from rclone itself.
//
// Remotes were set up in a terminal ("rclone config"), and all this program
// knew of one was its name and its kind. That rules out the web page and the
// daemon entirely: there is no terminal there to open.
//
// rclone describes every backend as data ("rclone config providers"): each
// option with its help, its type, its default, and whether it is required,
// a password, sensitive, or advanced. So a form for any backend -- including
// one a fork adds, teldrive among them -- can be made from that, rather than
// written by hand once per backend.
//
// This is the reading half: the backends there are, and one remote's
// settings with every secret taken out. See docs/REMOTE-CONFIG-UI.md, steps
// R1 and R2.

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>

#include <functional>

struct ProviderOption {
  QString name;     // "access_token", as it is written in rclone.conf
  QString help;     // rclone's own words; the first line is the short one
  QString type;     // "string", "bool", "SizeSuffix", "Duration", ...
  QString defaultValue;
  bool required = false;
  bool isPassword = false; // stored obscured; rclone hides it
  bool sensitive = false;  // rclone hides it when it shows a config
  bool advanced = false;
  bool exclusive = false;  // only one of the examples may be given
  int hide = 0;            // rclone's own: non-zero is not offered
  QList<QPair<QString, QString>> examples; // value, what it means
};

struct Provider {
  QString name;        // "drive", "teldrive"
  QString description; // "Google Drive"
  QString prefix;      // how its flags begin
  QList<ProviderOption> options;
};

// Reads the output of "rclone config providers". A backend that cannot be
// read is left out rather than half described.
QList<Provider> ParseProviders(const QByteArray &json);

const Provider *FindProvider(const QList<Provider> &providers,
                             const QString &type);

// Whether a key, by its name alone, is a secret: for keys the provider does
// not describe, and as a second line behind the provider's own word.
bool LooksSecret(const QString &key);

// One remote's settings, as they may be shown: each setting it has, in the
// order the backend lists its options, with the value of anything that is a
// password, sensitive, or looks like a secret left out -- said to be set,
// and nothing more. Keys the backend does not describe are listed last.
// With no `provider` -- the backends not read, or a kind rclone does not
// describe -- every value is left out.
//
// "Sensitive" is rclone's word and is followed even where it seems
// cautious (a teldrive host is marked so): it is what "rclone config
// redacted" hides too, and a second opinion here would be a second rule.
QJsonArray DescribeSettings(const QJsonObject &section,
                            const Provider *provider);

// ---- changing remotes (R3) ---------------------------------------------------
//
// Always through rclone ("rclone config create / delete"), never by writing
// the file: rclone knows how passwords are obscured and how an encrypted
// configuration is opened, and a second writer of its file would be a
// second idea of its format.

// Why a name cannot be a remote's, or empty when it can. rclone's own rule:
// letters, digits, and _ - . + @ and space, not starting with - or a space
// and not ending with a space.
QString WhyNotARemoteName(const QString &name);

// What became of a change to a remote. `code` is for a machine:
// "no_such_remote", "bad_name", "taken", "in_use", "used_by_tasks",
// "config_unreadable", "rclone_refused".
struct RemoteChange {
  bool ok = false;
  QString code;
  QString why;
  QStringList tasks; // the tasks it concerns: changed, or in the way
  // Done, but rclone would have gone on to ask this: an option's name.
  // rclone asks a remote that signs in with a browser whether to refresh
  // its token after a change, and that is not something to answer here.
  QString asks;
};

// Whether a kind of remote signs in through a web browser (OAuth): it has a
// "token" that rclone gets by sending somebody to a sign-in page. Making
// one of these needs that step, which is R5; changing one does not.
bool SignsInWithBrowser(const Provider &provider);

// What is wrong with a set of settings for this backend, or empty. Only
// options the backend has, because rclone itself takes any key and writes
// it into the file; no new lines in a value, because the file is lines; true
// or false for a yes-or-no option; and, when `creating`, every option the
// backend calls required.
QString WhyNotSettings(const Provider &provider, const QJsonObject &settings,
                       bool creating);

// Makes a remote: "rclone config create <name> <type> k=v ...
// --non-interactive --obscure", passwords given as typed and obscured by
// rclone. Refused for a name that is taken or not a name, a kind rclone does
// not have, one that signs in with a browser, or settings that cannot be.
// The backends must have been read (RcloneConfigRegistry::isLoaded).
void CreateRemote(QObject *context, const QString &name, const QString &type,
                  const QJsonObject &settings,
                  std::function<void(const RemoteChange &)> done);

// Changes some of a remote's settings: "rclone config update". Only what is
// given changes; a secret that is not given is kept as it is.
void UpdateRemote(QObject *context, const QString &name,
                  const QJsonObject &settings,
                  std::function<void(const RemoteChange &)> done);

// Renames a remote. rclone has no command for it, so it is made again under
// the new name with exactly what it has -- passwords passed as they are
// stored, already obscured, and tokens as they are -- and then the old one
// is deleted. Tried on a copy of a real configuration: drive (with its OAuth
// token), sftp and teldrive each came out identical.
//
// Refused while a job is using it. Saved tasks that name it are changed to
// the new name, and said in `tasks`: otherwise renaming a remote would be
// a quiet way to break every task on it.
void RenameRemote(QObject *context, const QString &from, const QString &to,
                  std::function<void(const RemoteChange &)> done);

// Deletes a remote from rclone.conf. Its files are not touched -- they are
// on the remote, not in the configuration. Refused while a job is using it,
// and while saved tasks name it unless `evenIfTasksUseIt`: those tasks would
// be left pointing at nothing.
void DeleteRemote(QObject *context, const QString &name, bool evenIfTasksUseIt,
                  std::function<void(const RemoteChange &)> done);

// The backends, asked of rclone once per process and kept, as the flag list
// is. Empty until the answer lands.
class RcloneConfigRegistry : public QObject {
  Q_OBJECT

public:
  static RcloneConfigRegistry &instance();

  const QList<Provider> &providers() const { return mProviders; }
  bool isLoaded() const { return !mProviders.isEmpty(); }
  void ensureLoaded();

  // Every remote's section of rclone.conf, as "rclone config dump" gives it
  // -- secrets included, so what is handed to `done` must never be shown or
  // logged as it is. Run fresh each time: the file can change underneath.
  void readDump(QObject *context,
                std::function<void(bool ok, const QJsonObject &dump,
                                   const QString &why)> done);

signals:
  void providersLoaded();

private:
  explicit RcloneConfigRegistry(QObject *parent = nullptr);

  QList<Provider> mProviders;
  bool mInFlight = false;
};
