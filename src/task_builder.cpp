#include "task_builder.h"
#include "debug_log.h"
#include "job_options.h"
#include "list_of_job_options.h"

#include <QtGlobal>

namespace {

// "tgdrive:some/path" -> "tgdrive". A local path yields nothing, which is
// right: there is no remote to name it after.
QString remotePartOf(const QString &path) {
  const int colon = path.indexOf(QLatin1Char(':'));
  if (colon <= 0) {
    return QString();
  }

#if defined(Q_OS_WIN)
  // "D:/films" is a drive, not a remote called D. rclone draws the line the
  // same way -- a single letter before the colon on Windows is a drive -- and
  // without this a task saved to a local disk would be named after it.
  if (colon == 1 && path.at(0).isLetter()) {
    return QString();
  }
#endif

  return path.left(colon);
}

} // namespace

QString TaskBuilder::Explain(Problem problem) {
  switch (problem) {
  case Problem::None:
    return QString();
  case Problem::NoName:
    return QStringLiteral("Please enter task name to save.");
  case Problem::NoSource:
    return QStringLiteral("Invalid task, source is required.");
  case Problem::NoDestination:
    return QStringLiteral("Invalid task, destination is required.");
  }
  return QString();
}

TaskBuilder::Problem TaskBuilder::Check(bool isDownload, const QString &name,
                                        const QString &source,
                                        const QString &dest) {
  if (name.trimmed().isEmpty()) {
    return Problem::NoName;
  }
  // Only the side the user types. The other one is the path being browsed.
  if (isDownload) {
    if (dest.trimmed().isEmpty()) {
      return Problem::NoDestination;
    }
  } else {
    if (source.trimmed().isEmpty()) {
      return Problem::NoSource;
    }
  }
  return Problem::None;
}

TaskBuilder::Problem TaskBuilder::Check(const JobOptions &task) {
  // The side the user types is the near one, and it is stored as typed: a
  // download keeps the remote on source ("tgdrive:path") and the plain local
  // path on dest, an upload the other way round. So the same two fields the
  // dialog looks at are these two, with nothing to unpick.
  return Check(task.jobType == JobOptions::JobType::Download, task.description,
               task.source, task.dest);
}

QString TaskBuilder::RemoteOf(const JobOptions &task) {
  return task.jobType == JobOptions::JobType::Download
             ? remotePartOf(task.source)
             : remotePartOf(task.dest);
}

QString TaskBuilder::AutoName(const QString &remote, const QDateTime &now) {
  return QStringLiteral("_tmp_%1_%2_%3")
      .arg(now.date().toString(QStringLiteral("ddMMMyyyy")),
           now.time().toString(QStringLiteral("HHmmss")), remote);
}

QString TaskBuilder::AutoName(const JobOptions &task, const QDateTime &now) {
  return AutoName(RemoteOf(task), now);
}

QString TaskBuilder::Create(JobOptions *task, const QDateTime &now,
                            Problem *problem) {
  const auto refuse = [problem](Problem what) {
    if (problem != nullptr) {
      *problem = what;
    }
    qCDebug(rbTask) << "refused because" << Explain(what);
    return QString();
  };

  if (task == nullptr) {
    return refuse(Problem::NoName);
  }

  // Named before checking, or a task with no name would be refused for not
  // having one when the whole point is that one is made up.
  if (task->description.trimmed().isEmpty()) {
    task->description = AutoName(*task, now);
    qCDebug(rbTask) << "auto name=" << task->description;
  }

  const Problem found = Check(*task);
  if (found != Problem::None) {
    return refuse(found);
  }

  // Persist() answers "was this new", not "did it save" -- reading it as
  // success would report every edit of an existing task as a failure. What
  // was stored is said by ListOfJobOptions itself, which is where every
  // caller passes, not only this one.
  ListOfJobOptions::getInstance()->Persist(task);

  if (problem != nullptr) {
    *problem = Problem::None;
  }
  return task->uniqueId.toString();
}
