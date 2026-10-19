#pragma once

// Orchestration (L1): the things done to a remote's files from its tab.
//
// Making a folder, renaming, deleting and the rest were each written inside
// the click handler of the button that asked for them: a QProcess built on
// the spot, handed to a dialog, and gone when the dialog closed. So they left
// no row in the history, wrote no log, and -- the reason this file exists --
// could not be asked for by anything that was not that button. The web page
// could show a remote's folders and do nothing whatever to them.
//
// Each operation here is one function that starts a job through the registry,
// the same road a transfer takes. The window calls it and so does the API,
// which is the rule this project now works to: what one view can do, the
// other can. See docs/WEB-API-PLAN.md.
//
// Moved over one at a time. What is here is what has been moved; the rest
// still lives in remote_widget.cpp and is listed in the plan.

#include "running_job.h"

#include <QList>
#include <QString>
#include <QStringList>

class JobOptions;
class RunningJob;

namespace RemoteOps {

// One file or folder on a remote: what a command is to act on.
struct Item {
  QString remote;
  QString path;
  bool isFolder = false;
};

// Why this cannot be the name of one new file or folder, or an empty string
// when it can.
//
// One name, not a path: a slash in it would make somewhere else, and ".."
// would make it somewhere above where the caller was looking. The window
// never checked, because whoever typed it was looking at the folder they
// were in; a request arriving over HTTP is not.
QString WhyNotAName(const QString &name);

// "parent/name", and just "name" at the root of the remote.
QString Join(const QString &parent, const QString &name);

// "remote:path", the form rclone takes. Slashes at either end of the path are
// dropped, so "films", "/films" and "films/" are one folder.
QString Target(const QString &remote, const QString &path);

// Makes a folder called `name` inside `parent` (empty for the root of the
// remote). Returns the job, which ends like any other: finished when the
// folder is there, error with rclone's own words in its output when not.
//
// Returns nullptr without starting anything when the name is not usable;
// *why then says what is wrong with it.
RunningJob *MakeFolder(const QString &remote, const QString &parent,
                       const QString &name, QString *why = nullptr);

// The folder a path is in, and its own name: "a/b/c" is "a/b" and "c". The
// root of a remote has neither.
QString ParentOf(const QString &path);
QString NameOf(const QString &path);

// Gives the file or folder at `path` a new name, in the folder it is already
// in. Moving it somewhere else is a different command and a different
// question to be asked -- which is why this takes a name and not a path.
//
// Returns nullptr without starting anything when the name is not usable, is
// the name it already has, or the path is the root of the remote, which has
// no name to change.
RunningJob *Rename(const QString &remote, const QString &path,
                   const QString &newName, QString *why = nullptr);

// Deletes one file, or one folder and everything in it.
//
// Whether it is a folder has to be said, and the two are different commands
// on purpose. A file is removed with "deletefile", which refuses a folder --
// so being told "file" about something that is really a folder ends in an
// error from rclone, not in a folder emptied by a request that thought it
// was removing one thing. The window used "delete" here, which given a
// folder quietly removes every file under it.
//
// A folder is removed with "purge", contents and all. There is no undo for
// either beyond whatever the backend's own trash does.
//
// Refuses the root of a remote outright. Purging a path that is empty is
// purging everything, and nobody asks for that by leaving a field blank.
RunningJob *Delete(const Item &item, QString *why = nullptr);

// ---- finding things out, which changes nothing ----------------------------

// How many files are under a path and how much they come to.
//
// `filters` narrows it to some of what is there, which is how the window
// sizes several selected items at once. With `json` rclone answers in JSON,
// for a caller that wants the numbers rather than a sentence about them.
RunningJob *Size(const QString &remote, const QString &path,
                 const QStringList &filters = QStringList(),
                 bool json = false);

// What the backend says about its own space: total, used, free. Not every
// backend can say; rclone reports that itself when it cannot.
RunningJob *About(const QString &remote, bool json = false);

// The folders under a path, drawn as a tree. Folders only, as the window has
// always asked for it.
RunningJob *Tree(const QString &remote, const QString &path);

// ---- the ones that change something other than a file's name or place ------

// Makes a public link to a file or folder and prints it. This changes who
// can reach the thing: anybody holding the link can. Refuses the root of a
// remote, for the reason Delete does -- a path left empty is not a request
// to share everything.
RunningJob *Link(const QString &remote, const QString &path,
                 QString *why = nullptr);

// Empties the trash, or removes old versions, whichever the backend means by
// it. What goes this way does not come back.
RunningJob *Cleanup(const QString &remote);

// The ways rclone can settle files that share a name. "skip" removes
// identical copies and leaves the rest alone; the others each pick a
// survivor, and "rename" keeps everything under new names.
QStringList DedupeModes();

// Settles duplicates under a path. `mode` is one of DedupeModes(); anything
// else is refused rather than passed on, because rclone's own default when
// it does not recognise a mode is to ask at a terminal nobody is sitting at.
//
// `extra` is flags typed into the window's dialog. A request that did not
// come from somebody at the keyboard passes none.
RunningJob *Dedupe(const QString &remote, const QString &path,
                   const QString &mode, bool dryRun,
                   const QStringList &extra = QStringList(),
                   QString *why = nullptr);

// ---- copying and moving, which are transfers -------------------------------
//
// These run as long as a transfer runs and are jobs of that kind: they get a
// card, report progress, and count as the last time the destination was
// written to.

// Copies or moves `sourcePath` to `destPath`, each given whole. `args` is
// whatever else the command should carry. This is the form the window uses,
// because it has already worked out both ends -- including, for several
// selected items at once, a parent folder and filters naming them.
RunningJob *CopyBetween(bool move, const QString &sourceRemote,
                        const QString &sourcePath, const QString &destRemote,
                        const QString &destPath, const QStringList &args,
                        const QString &info = QString());

// Copies or moves one file or folder *into* a folder, which is how somebody
// choosing a destination thinks of it. A folder arrives under its own name
// inside the destination; a file arrives in it.
//
// Refused before anything runs: the root of a remote as the thing to move,
// a folder into itself or into something inside itself, and a destination
// that is where the thing already is.
RunningJob *CopyInto(bool move, const Item &what, const QString &destRemote,
                     const QString &destFolder, bool dryRun,
                     QString *why = nullptr);

// ---- comparing, and listing to a file ---------------------------------------

struct CheckOptions {
  bool oneWay = false;   // only that everything on the first side is on the second
  bool sizeOnly = false; // compare sizes and not hashes
  bool download = false; // compare by downloading both, for backends with no common hash
};

// Compares two folders and reports what differs. Changes nothing.
//
// rclone ends this with an error when the two differ, which makes a
// difference look like a failure. ReadCheckOutput() tells them apart.
// Refused when both sides are the same folder: that always matches, and
// asking is more likely a slip than a question.
RunningJob *Check(const QString &remote, const QString &path,
                  const QString &otherRemote, const QString &otherPath,
                  const CheckOptions &options, QString *why = nullptr);

struct CheckResult {
  int differences = -1; // -1: rclone never said, so the check did not finish
  int matching = -1;
  bool completed() const { return differences >= 0; }
  bool matched() const { return differences == 0; }
};

// What a check found, from what rclone printed.
CheckResult ReadCheckOutput(const QStringList &lines);

struct ExportOptions {
  int maxDepth = 0; // 0 for all the way down
};

// Lists every file under a path, for writing out as a file of its own.
RunningJob *ListForExport(const QString &remote, const QString &path,
                          const ExportOptions &options);

// One line of that listing -- "  300000 2026-10-12 14:16:41.398206300 a.bin"
// -- taken apart. False for a line that is not one, which is how the notices
// rclone prints among them are left out.
bool ParseLslLine(const QString &line, qint64 *size, QString *modified,
                  QString *name);

// The line of the exported file for one entry: the name alone, or
// "name","modified",size. One function, so the file the window saves and the
// file the page downloads are the same file.
QString ExportRow(const QString &name, const QString &modified, qint64 size,
                  bool namesOnly);

// ---- mounting ----------------------------------------------------------------

struct MountOptions {
  bool readOnly = false;
  // "off", "minimal", "writes" or "full": how much rclone keeps on disk so
  // that programs which seek and rewrite can work. Anything else is "off".
  QString cache;
  // The name the mounted drive is shown by (--volname); empty: rclone's own.
  QString volumeName;
  // More rclone options, written as a task's extra options are. Whoever
  // takes them from outside checks them first (ExtraFlags).
  QString extra;
};

// The mount kept as a task, as the window's Mount dialog can: named, and
// started when the program starts when `autoStart`. Not started here --
// StartTask() does that, as for any mount task. Null with why not.
JobOptions *MountTask(const QString &remote, const QString &path,
                      const QString &mountPoint, const MountOptions &options,
                      const QString &name, bool autoStart,
                      QString *why = nullptr);

// Makes a folder of a remote appear as a folder (or a drive) on this
// machine, at `mountPoint`. The job runs for as long as the mount is up, and
// stopping the job is unmounting.
//
// It is given a remote-control port of its own, because on Windows that is
// the only way to ask a mount to come down. The login for it never appears
// among the arguments.
//
// Refused when no mount point is named. Whether the place can actually be
// mounted on is rclone's to say: it differs by platform and by what is
// installed, and its own message is better than a guess made here.
RunningJob *Mount(const QString &remote, const QString &path,
                  const QString &mountPoint, const MountOptions &options,
                  QString *why = nullptr);

// ---- serving ------------------------------------------------------------------
//
// A remote, or a folder of one, offered to other programs -- a file manager,
// a TV, another machine's backup tool -- the way rclone serves it: "rclone
// serve http|webdav|ftp|sftp". Unlike a stream it is not passed through the
// API's port: FTP and SFTP are not HTTP, and the programs that speak WebDAV
// expect a server of their own. So it has a port of its own, and who can
// reach that port is the owner's choice.

struct ServeOptions {
  QString protocol;      // "http" | "webdav" | "ftp" | "sftp"
  quint16 port = 0;      // 0: DefaultServePort(protocol)
  bool everyone = false; // every address of this machine, not only itself
  bool readOnly = true;
  QString user;     // with a password, who has to log in
  QString password; // handed to rclone in its environment, never its arguments
};

// The protocols Serve() takes, in the order they are offered.
QStringList ServeProtocols();

// The port a protocol is offered on when none is named. Not the protocol's
// own (80, 21, 22): those need more rights than this program has, and are
// often taken.
quint16 DefaultServePort(const QString &protocol);

// Offers a folder of a remote over `options.protocol` until the job is
// stopped. Refused when the protocol is not one of ServeProtocols(), the port
// is in use, or other machines could reach it without a password -- a remote
// served to a network with nobody asked to log in is everything in it given
// to whoever is on that network.
RunningJob *Serve(const QString &remote, const QString &path,
                  const ServeOptions &options, QString *why = nullptr);

// The addresses a running serve can be opened at from this machine and, when
// it is for everyone, from others: one per address this machine has.
QStringList ServeUrls(const RunningJob::ServeAddress &address);

// ---- between this machine and a remote --------------------------------------
//
// The command is put together by the same code a saved task uses, so an
// upload from the page carries the default options an upload from the window
// would. Whether the path on this machine is one the caller may use is not
// decided here; see local_files.h.

// Copies a file or folder of this machine into a folder of a remote. A folder
// arrives under its own name, as with CopyInto().
RunningJob *Upload(const QString &localPath, bool isFolder,
                   const QString &remote, const QString &remoteFolder,
                   bool dryRun);

// Moves a file that arrived from somewhere else -- the browser's machine,
// through the API -- from where it was set down into a folder of a remote,
// as a transfer job like any other: progress, a history row, a Stop. Moved
// rather than copied, so the staged copy is gone once it is up.
RunningJob *UploadReceived(const QString &stagedFile, const QString &remote,
                           const QString &remoteFolder);

// Where files sent from a browser are set down before they go up:
// RB_UPLOAD_STAGING, or a folder under the system's temporary one. They
// need room there for as long as each is being sent and moved.
QString UploadStagingFolder();

// Copies a file or folder of a remote into a folder of this machine.
RunningJob *Download(const Item &what, const QString &localFolder,
                     bool dryRun, QString *why = nullptr);

// ---- streaming ---------------------------------------------------------------
//
// The window streams by piping "rclone cat" into a player it starts itself,
// which only means anything on the machine the window is on. From anywhere
// else the file has to be given an address a player can open, so this serves
// it over HTTP ("rclone serve http") for as long as the job runs.
//
// rclone listens on this machine alone, on whatever port is free. Nobody
// outside is meant to reach that port: the API passes requests on to it
// (ApiServer::relayStream), so a stream is at the same address as everything
// else and there is no second port to open, publish or put behind a proxy.
//
// A player cannot send a header, so the address has to be enough by itself:
// the file is served under a path made at random for that one stream, which
// stops meaning anything when the job is stopped. That is a secret in a URL,
// which the API's own token is never allowed to be -- the difference is that
// this one opens one file, for one sitting.

// The first part of every stream's path, which is how the API tells a
// request for a stream from a request for anything else.
extern const char *const kStreamPrefix; // "/stream/"

// Serves a file, or a folder, of a remote. For a file only that file is
// served, not the folder it is in. The job says where: streamPath() is the
// path to ask the API for, streamPort() the port rclone itself is on.
RunningJob *Stream(const Item &what, QString *why = nullptr);

} // namespace RemoteOps
