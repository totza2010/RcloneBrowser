#pragma once

// View (L3): the runs that have already happened.
//
// The Jobs tab shows what this process is running now and nothing else, which
// is why closing the application used to take every record of the night's
// work with it. This reads the table S13 writes -- see docs/PLAN.md 6.8.
//
// Deliberately read-only apart from deleting: a run is a fact about what
// happened and nothing here should be able to rewrite it.

#include "run_history.h"

#include <QList>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

class HistoryWidget : public QWidget {
  Q_OBJECT

public:
  explicit HistoryWidget(QWidget *parent = nullptr);

  // Reads the table again. Called when a job ends and when the tab is opened,
  // so what is on screen is never older than the last thing that happened.
  void refresh();

private:
  void openSelectedLog();
  void clearHistory();
  void applyFilter();
  void updateButtons();

  const JobRunRecord *selectedRecord() const;

  QTableWidget *mTable = nullptr;
  QLineEdit *mFilter = nullptr;
  QPushButton *mOpenLog = nullptr;
  QPushButton *mClear = nullptr;
  QLabel *mSummary = nullptr;

  QList<JobRunRecord> mRows;
};
