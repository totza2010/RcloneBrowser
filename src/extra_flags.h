#pragma once

// Orchestration (L1): which rclone flags a task may be given by something
// other than the person sitting at the machine.
//
// The window's "extra options" box is free text, and that is right for a
// window: whoever types into it could open a terminal just as easily. The
// API is not that. A task written through it is a command line written by
// whoever holds the secret, and two things follow.
//
// The first is about mistakes: a flag that is misspelt, or belongs to
// another build of rclone, fails when the task runs -- at night, on a
// schedule -- rather than when it is typed. So a flag has to be one the
// rclone on this machine has, which RcloneFlagRegistry already knows.
//
// The second is about harm. Some flags are not options for a transfer at
// all: they run a program, or say where rclone's own configuration and logs
// live. Those are refused whatever rclone thinks of them. And where the API
// has been told how far it may look on this machine (see local_files.h), the
// flags that read or write somewhere else on it are refused too -- otherwise
// they would be the way round that line.
//
// The list of what is refused is a judgment, not a proof. It is in one place
// so that it can be read and added to. See docs/WEB-API-PLAN.md.

#include <QHash>
#include <QList>
#include <QString>

struct RcloneFlag;

namespace ExtraFlags {

// Whether a flag, by its long name ("--config"), is one a task may not be
// given through the API. `restricted` is whether the API is held to a list
// of folders on this machine.
bool IsForbidden(const QString &name, bool restricted);

// What is wrong with `extra`, as a sentence that names the flag; empty when
// nothing is.
//
// `known` is what the rclone on this machine accepts. When it is empty --
// rclone could not be asked -- only what is forbidden is checked, on the
// same reasoning as everywhere else here: a question that could not be
// answered must not turn into a refusal of everything.
QString Check(const QString &extra, const QList<RcloneFlag> &known,
              bool restricted);

// Which kind of remote each flag is for: "--drive-chunk-size" -> "drive".
// Flags that are for no kind in particular are not in the answer.
//
// rclone lists every backend's flags in one group and tells them apart only
// by how the name begins, so that is what this reads. The local disk's are
// the exception -- "--copy-links", "--one-file-system" carry no prefix --
// and are recognised by being the only flag that begins the way they do,
// which no real backend's is: each has at least its own "-description".
QHash<QString, QString> Backends(const QList<RcloneFlag> &known);

// How the flags of a remote of this type begin. The type as "rclone
// listremotes" gives it, which is the prefix for all but a few.
QString FlagPrefixOfType(const QString &type);

// Whether a remote of this type is a layer over another remote (crypt,
// union, alias...). The flags of whatever is underneath apply as well, and
// what that is cannot be told from the type.
bool WrapsAnother(const QString &type);

} // namespace ExtraFlags
