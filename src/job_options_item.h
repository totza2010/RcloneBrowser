#pragma once

// L3 (GUI) adapter for JobOptions -- see docs/ARCHITECTURE.md.
// Kept out of job_options.h so that JobOptions itself stays free of QtWidgets
// and can move into the headless core.

#include "job_options.h"

#include <QIcon>
#include <QListWidget>

class JobOptionsListWidgetItem : public QListWidgetItem {
public:
  JobOptionsListWidgetItem(JobOptions *jo, const QIcon &icon,
                           const QString &text, const QString &requestId)
      : QListWidgetItem(icon, text), mJobData(jo), mRequestId(requestId) {}

  void SetData(JobOptions *jo) { mJobData = jo; }
  JobOptions *GetData() { return mJobData; }
  QString GetRequestId() { return mRequestId; }

private:
  JobOptions *mJobData;
  QString mRequestId;
};
