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

} // namespace RemoteOps
