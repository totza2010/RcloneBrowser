#pragma once

// Orchestration (L1): a saved task, as something other than this program can
// read and write.
//
// A task is stored as JobOptions writes it: forty-odd fields, the ones that
// are choices among a few things held as numbers pinned to the order of
// items in a dialog. That is right for storage and wrong to hand to another
// program -- "operation: 3" means nothing to whoever reads it, and the
// number that means "sync" would become a promise.
//
// So this is the task in words: "sync", "upload", "checksum". It is what the
// API speaks and what the web page's form is built on, and it is the same
// set of fields the window's transfer dialog has. See docs/WEB-API-PLAN.md.

#include <QJsonObject>
#include <QString>

class JobOptions;

namespace TaskJson {

// The whole task.
QJsonObject Write(const JobOptions &task);

// Puts what `json` says onto `task`, leaving alone whatever it does not
// mention -- so the same function fills in a new task and changes an
// existing one. False with *why when a field is not something it can be: a
// word that is not one of the choices, a number where text belongs.
//
// What it will not write: the kind of task (a transfer cannot be turned into
// a mount by editing it), and the fields that belong to a mount.
bool Read(const QJsonObject &json, JobOptions *task, QString *why);

// What a new transfer starts from before anybody chooses anything: the
// numbers the window's dialog comes up with. In one place, so the page's
// form and a task made by another program begin where the window's would.
QJsonObject Defaults(bool isDownload);

} // namespace TaskJson
