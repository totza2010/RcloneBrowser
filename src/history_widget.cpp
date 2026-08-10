#include "history_widget.h"

#include "job_registry.h"
#include "job_stats.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

enum Column {
  ColStarted = 0,
  ColName,
  ColKind,
  ColResult,
  ColDuration,
  ColTransferred,
  ColLog,
  ColumnCount,
};

// The words the rest of the application uses, in the form a person reads.
QString resultText(const JobRunRecord &row, bool liveHere) {
  if (row.isRunning()) {
    return liveHere ? QObject::tr("Running")
                    : QObject::tr("No ending recorded");
  }
  if (row.state == QStringLiteral("finished")) {
    return QObject::tr("Finished");
  }
  if (row.state == QStringLiteral("unmounted")) {
    return QObject::tr("Unmounted");
  }
  if (row.state == QStringLiteral("stopped")) {
    return QObject::tr("Stopped");
  }
  if (row.state == QStringLiteral("error")) {
    return row.exitCode > 0 ? QObject::tr("Error (exit %1)").arg(row.exitCode)
                            : QObject::tr("Error");
  }
  return row.state.isEmpty() ? QObject::tr("Unknown") : row.state;
}

QString nameText(const JobRunRecord &row) {
  if (!row.taskName.isEmpty()) {
    return row.taskName;
  }
  if (!row.info.isEmpty()) {
    return row.info;
  }
  if (!row.source.isEmpty() || !row.dest.isEmpty()) {
    return row.source + QStringLiteral(" -> ") + row.dest;
  }
  return row.requestId;
}

QTableWidgetItem *cell(const QString &text) {
  auto *item = new QTableWidgetItem(text);
  item->setFlags(item->flags() & ~Qt::ItemIsEditable);
  return item;
}

} // namespace

HistoryWidget::HistoryWidget(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);

  auto *bar = new QHBoxLayout;
  mFilter = new QLineEdit(this);
  mFilter->setPlaceholderText(tr("Filter by task, path or result"));
  mFilter->setClearButtonEnabled(true);
  bar->addWidget(mFilter, 1);

  auto *refreshButton = new QPushButton(tr("Refresh"), this);
  mOpenLog = new QPushButton(tr("Open log"), this);
  mClear = new QPushButton(tr("Clear history..."), this);
  bar->addWidget(refreshButton);
  bar->addWidget(mOpenLog);
  bar->addWidget(mClear);
  layout->addLayout(bar);

  mTable = new QTableWidget(0, ColumnCount, this);
  mTable->setHorizontalHeaderLabels(
      {tr("Started"), tr("Job"), tr("Kind"), tr("Result"), tr("Duration"),
       tr("Transferred"), tr("Log")});
  mTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  mTable->setSelectionMode(QAbstractItemView::SingleSelection);
  mTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  mTable->verticalHeader()->setVisible(false);
  mTable->horizontalHeader()->setSectionResizeMode(ColName,
                                                   QHeaderView::Stretch);
  mTable->setSortingEnabled(false); // the query already orders by time
  layout->addWidget(mTable, 1);

  mSummary = new QLabel(this);
  layout->addWidget(mSummary);

  QObject::connect(refreshButton, &QPushButton::clicked, this,
                   &HistoryWidget::refresh);
  QObject::connect(mOpenLog, &QPushButton::clicked, this,
                   &HistoryWidget::openSelectedLog);
  QObject::connect(mClear, &QPushButton::clicked, this,
                   &HistoryWidget::clearHistory);
  QObject::connect(mFilter, &QLineEdit::textChanged, this,
                   [this]() { applyFilter(); });
  QObject::connect(mTable, &QTableWidget::itemSelectionChanged, this,
                   [this]() { updateButtons(); });
  QObject::connect(mTable, &QTableWidget::itemDoubleClicked, this,
                   [this]() { openSelectedLog(); });

  // A job that has just ended belongs here immediately: the card and the row
  // otherwise disagree until something happens to refresh this.
  QObject::connect(&JobRegistry::instance(), &JobRegistry::jobFinished, this,
                   [this](RunningJob *) { refresh(); });

  refresh();
}

void HistoryWidget::refresh() {
  const QString wasSelected =
      selectedRecord() != nullptr ? selectedRecord()->requestId : QString();

  mRows = RunHistory::recent(500);

  mTable->setRowCount(0);
  mTable->setRowCount(mRows.size());

  for (int i = 0; i < mRows.size(); ++i) {
    const JobRunRecord &row = mRows[i];
    const bool liveHere = JobRegistry::instance().find(row.requestId) != nullptr;

    mTable->setItem(i, ColStarted,
                    cell(QDateTime::fromMSecsSinceEpoch(row.startedAt)
                             .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    mTable->setItem(i, ColName, cell(nameText(row)));
    mTable->setItem(i, ColKind, cell(row.kind));

    QTableWidgetItem *result = cell(resultText(row, liveHere));
    if (row.state == QStringLiteral("error")) {
      result->setForeground(Qt::red);
    } else if (row.isRunning() && !liveHere) {
      // Not a failure and not a success: the process that was running this
      // never got to say how it ended.
      result->setForeground(Qt::darkYellow);
    }
    mTable->setItem(i, ColResult, result);

    const qint64 seconds =
        row.finishedAt > row.startedAt
            ? (row.finishedAt - row.startedAt) / 1000
            : 0;
    mTable->setItem(i, ColDuration,
                    cell(row.finishedAt == 0 ? QString()
                                             : FormatSeconds(seconds)));

    QString moved;
    if (row.bytes > 0 || row.totalBytes > 0) {
      moved = FormatBytes(row.bytes);
      if (row.totalBytes > 0 && row.totalBytes != row.bytes) {
        moved += QStringLiteral(" / ") + FormatBytes(row.totalBytes);
      }
    }
    mTable->setItem(i, ColTransferred, cell(moved));

    // The file name alone: the full path is long, identical for every row,
    // and available in the tooltip when it is actually wanted.
    QTableWidgetItem *log =
        cell(row.logPath.isEmpty() ? QString()
                                   : QFileInfo(row.logPath).fileName());
    log->setToolTip(row.logPath);
    mTable->setItem(i, ColLog, log);

    // The row carries its own index, so filtering by hiding rows cannot make
    // the buttons act on the wrong run.
    mTable->item(i, ColStarted)->setData(Qt::UserRole, i);

    if (!wasSelected.isEmpty() && row.requestId == wasSelected) {
      mTable->selectRow(i);
    }
  }

  mTable->resizeColumnsToContents();
  mTable->horizontalHeader()->setSectionResizeMode(ColName,
                                                   QHeaderView::Stretch);
  applyFilter();
}

void HistoryWidget::applyFilter() {
  const QString needle = mFilter->text().trimmed();
  int shown = 0;

  for (int i = 0; i < mRows.size(); ++i) {
    const JobRunRecord &row = mRows[i];
    const bool match =
        needle.isEmpty() ||
        nameText(row).contains(needle, Qt::CaseInsensitive) ||
        row.source.contains(needle, Qt::CaseInsensitive) ||
        row.dest.contains(needle, Qt::CaseInsensitive) ||
        row.state.contains(needle, Qt::CaseInsensitive) ||
        row.kind.contains(needle, Qt::CaseInsensitive);
    mTable->setRowHidden(i, !match);
    if (match) {
      ++shown;
    }
  }

  if (mRows.isEmpty()) {
    mSummary->setText(tr("No runs recorded yet. Every job started from here or "
                         "with --run-task is added when it begins."));
  } else if (needle.isEmpty()) {
    mSummary->setText(tr("%n run(s)", nullptr, mRows.size()));
  } else {
    mSummary->setText(tr("%1 of %2 runs").arg(shown).arg(mRows.size()));
  }
  updateButtons();
}

const JobRunRecord *HistoryWidget::selectedRecord() const {
  const QList<QTableWidgetItem *> selected = mTable->selectedItems();
  if (selected.isEmpty()) {
    return nullptr;
  }
  const int index =
      mTable->item(selected.first()->row(), ColStarted)->data(Qt::UserRole)
          .toInt();
  if (index < 0 || index >= mRows.size()) {
    return nullptr;
  }
  return &mRows[index];
}

void HistoryWidget::updateButtons() {
  const JobRunRecord *row = selectedRecord();
  mOpenLog->setEnabled(row != nullptr && !row->logPath.isEmpty());
  mClear->setEnabled(!mRows.isEmpty());
}

void HistoryWidget::openSelectedLog() {
  const JobRunRecord *row = selectedRecord();
  if (row == nullptr || row->logPath.isEmpty()) {
    return;
  }
  if (!QFileInfo::exists(row->logPath)) {
    // The retention policy deletes logs before rows can outlive them, but a
    // file removed by hand would otherwise open nothing and say nothing.
    QMessageBox::information(
        this, tr("Log"),
        tr("The log file is gone:\n%1").arg(row->logPath));
    return;
  }
  QDesktopServices::openUrl(QUrl::fromLocalFile(row->logPath));
}

void HistoryWidget::clearHistory() {
  if (QMessageBox::question(
          this, tr("Clear history"),
          tr("Delete every recorded run, and the log files they point at?\n\n"
             "Runs that have not ended are kept."),
          QMessageBox::Yes | QMessageBox::No,
          QMessageBox::No) != QMessageBox::Yes) {
    return;
  }

  RunHistory::clear();
  refresh();
}
