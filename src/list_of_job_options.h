#pragma once
#include "job_options.h"
#include <qfile.h>

class ListOfJobOptions : public QObject {
  Q_OBJECT

protected:
  ~ListOfJobOptions() = default;
  ListOfJobOptions();

public:
  static ListOfJobOptions *getInstance();
  bool Persist(JobOptions *jo);
  bool Forget(JobOptions *jo);
  QList<JobOptions *> &getTasks() { return tasks; }

  // Finding a task by what identifies it, without walking a list widget.
  //
  // The lookup used to be written out at each call site as a loop over
  // ui.tasksListWidget, which is why nothing outside the window could find a
  // task -- the data was in memory the whole time, but only reachable through
  // the GUI. See docs/API.md S1.
  //
  // Both return nullptr when there is no match; the id form accepts the
  // string form of the UUID as well, because that is how it is written to
  // queue.conf and scheduler.conf.
  JobOptions *find(const QUuid &id) const;
  JobOptions *find(const QString &id) const;

  // Tasks are named by their description, which the user types and which
  // nothing enforces to be unique. First match wins, and callers that care
  // should say so -- "--run-task" reports the ambiguity rather than picking.
  JobOptions *findByName(const QString &description) const;
  int countByName(const QString &description) const;

signals:
  void tasksListUpdated();

private:
  static ListOfJobOptions *SavedJobOptions;
  static const QString persistenceFileName;
  static bool RestoreFromUserData(ListOfJobOptions &dataIn);
  static QFile *GetPersistenceFile(QIODevice::OpenModeFlag mode);

  QList<JobOptions *> tasks;
  bool PersistToUserData();
};
