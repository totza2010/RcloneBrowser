#pragma once

// Core (L1): what makes a task a task.
//
// Two rules lived only inside TransferDialog: what a task must have before it
// is worth saving, and what to call one the user did not name. Both were
// written out more than once in that dialog, each copy paired with its own
// message box and its own call to focus a widget -- so the rule and the way
// of showing it were the same lines of code, and neither could be asked for
// without a window.
//
// That is the thing standing between here and an HTTP API: a request that
// creates a task has to be held to the same rules as the dialog, and there
// was no way to do that except to build a dialog. See docs/LAYER-SPLIT.md
// block 3.

#include <QDateTime>
#include <QString>

class JobOptions;

namespace TaskBuilder {

// What stops a task from being worth saving. One at a time, in the order
// the dialog has always checked them, because the answer is a message and a
// widget to put the cursor in.
enum class Problem {
  None,
  NoName,
  NoSource,      // an upload with nothing to upload
  NoDestination, // a download with nowhere to put it
};

// The sentence shown for a problem. Kept next to the rule so that a second
// caller -- the API -- says the same thing the dialog says, rather than
// inventing its own wording for the same refusal.
QString Explain(Problem problem);

// The rule, from the parts a dialog has in its hands before it has built
// anything.
//
// Note which side is checked: a download must say where it is going, and an
// upload must say what it is sending. The other side comes from the remote
// being browsed, so it cannot be empty.
Problem Check(bool isDownload, const QString &name, const QString &source,
              const QString &dest);

// The same rule for a task that already exists -- what the API will have.
Problem Check(const JobOptions &task);

// The name a task gets when the user did not type one:
// "_tmp_<ddMMMyyyy>_<HHmmss>_<remote>", which is what the dialog has always
// produced. `now` is passed in rather than read, so that a test can say what
// it expects.
QString AutoName(const QString &remote, const QDateTime &now);
QString AutoName(const JobOptions &task, const QDateTime &now);

// The remote a task is about: the far side, which is the source of a
// download and the destination of an upload. Without the trailing colon.
QString RemoteOf(const JobOptions &task);

// Names the task if it has none, checks it, and saves it. Returns the task's
// id, or an empty string with *problem saying why not.
//
// The three steps in the order the dialog does them, so that a caller with no
// window -- the API -- gets a task that is named, checked and stored the same
// way, rather than a nearly-right one. Saving an unchecked task is the failure
// this exists to prevent: a task with no destination sits in the list looking
// ordinary until the night it is due to run.
//
// `now` is what an automatic name is made from; pass the current time.
QString Create(JobOptions *task, const QDateTime &now,
               Problem *problem = nullptr);

} // namespace TaskBuilder
