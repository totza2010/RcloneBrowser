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

#include <QString>

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

} // namespace RemoteOps
