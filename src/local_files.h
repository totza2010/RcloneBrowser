#pragma once

// Orchestration (L1): the folders of this machine, as far as the API may see
// them.
//
// A browser cannot choose a file on the machine the program runs on: its own
// file dialog shows the machine the browser is on, which is a different one
// as soon as the program is in a container or on a server. So uploading and
// downloading from the web page need the program to say what is on its own
// disk -- and "show me any folder on this machine" is exactly the request an
// API should not grant without being told how far it goes.
//
// How far is a list of roots. In a container that is the folders mounted in
// from the host (RB_LOCAL_ROOTS, /media by default in the image): the
// boundary is what somebody chose to hand the container, which is the user's
// own answer to this question. With no list -- the program run on a desk,
// by the person whose disk it is -- there is no boundary, and the roots are
// the drives or the filesystem root.
//
// Every path is resolved before it is judged, links included, so a link
// inside a root that points outside it does not lead out.

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace LocalFiles {

struct Entry {
  QString name;
  QString path; // absolute, with forward slashes
  bool isDir = false;
  qint64 size = 0;
  QDateTime modified;
};

// The folders the API may look inside. Never empty: with no list configured
// it is the drives (Windows) or "/" (elsewhere).
QStringList Roots();

// Whether a list was configured at all. False means the whole machine.
bool IsRestricted();

// The path with links and ".." resolved, if it is inside one of the roots;
// an empty string if it is not, or cannot be resolved. A path that does not
// exist yet is judged by the folder it would be in, so a destination that is
// about to be created can be named.
QString Allowed(const QString &path);

// What is in a folder. False with *why when the folder is outside the roots,
// is not a folder, or cannot be read. An empty path lists the roots
// themselves, which is where a chooser starts.
bool List(const QString &path, QList<Entry> *entries, QString *why);

} // namespace LocalFiles
