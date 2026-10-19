#include "history_widget.h"

#include "job_registry.h"
#include "job_stats.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QTimer>
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
  mFilter->setPlaceholderText(tr("Find: a task, a folder, a remote"));
  mFilter->setClearButtonEnabled(true);
  bar->addWidget(mFilter, 1);

  // The same narrowing as the page's History tab, asked of the same search.
  mState = new QComboBox(this);
  mState->addItem(tr("Every result"), QString());
  for (const char *state : {"finished", "error", "stopped", "running"}) {
    mState->addItem(QString::fromLatin1(state), QString::fromLatin1(state));
  }
  bar->addWidget(mState);
  mKind = new QComboBox(this);
  mKind->addItem(tr("Every kind"), QString());
  for (const char *kind : {"transfer", "mount", "serve", "stream", "check",
                           "size", "link", "mkdir", "rename", "delete"}) {
    mKind->addItem(QString::fromLatin1(kind), QString::fromLatin1(kind));
  }
  bar->addWidget(mKind);

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

  auto *foot = new QHBoxLayout;
  mSummary = new QLabel(this);
  foot->addWidget(mSummary, 1);
  mOlder = new QPushButton(tr("Load older"), this);
  foot->addWidget(mOlder);
  layout->addLayout(foot);

  // Asked when typing stops, not at every letter: each is a query.
  mTyping = new QTimer(this);
  mTyping->setSingleShot(true);
  mTyping->setInterval(300);

  QObject::connect(refreshButton, &QPushButton::clicked, this,
                   &HistoryWidget::refresh);
  QObject::connect(mOpenLog, &QPushButton::clicked, this,
                   &HistoryWidget::openSelectedLog);
  QObject::connect(mClear, &QPushButton::clicked, this,
                   &HistoryWidget::clearHistory);
  QObject::connect(mFilter, &QLineEdit::textChanged, mTyping,
                   [this]() { mTyping->start(); });
  QObject::connect(mTyping, &QTimer::timeout, this, &HistoryWidget::refresh);
  QObject::connect(mState, &QComboBox::currentIndexChanged, this,
                   &HistoryWidget::refresh);
  QObject::connect(mKind, &QComboBox::currentIndexChanged, this,
                   &HistoryWidget::refresh);
  QObject::connect(mOlder, &QPushButton::clicked, this,
                   &HistoryWidget::loadOlder);
  QObject::connect(mTable, &QTableWidget::itemSelectionChanged, this,
                   [this]() { updateButtons(); });
  QObject::connect(mTable, &QTableWidget::itemDoubleClicked, this,
                   [this]() { openSelectedRun(); });

  // A job that has just ended belongs here immediately: the card and the row
  // otherwise disagree until something happens to refresh this.
  QObject::connect(&JobRegistry::instance(), &JobRegistry::jobFinished, this,
                   [this](RunningJob *) { refresh(); });

  refresh();
}

RunHistory::Search HistoryWidget::asked() const {
  RunHistory::Search search;
  search.limit = 200;
  search.text = mFilter->text().trimmed();
  search.state = mState->currentData().toString();
  search.kind = mKind->currentData().toString();
  return search;
}

void HistoryWidget::refresh() {
  mRows = RunHistory::search(asked());
  mAtTheEnd = mRows.size() < asked().limit;
  draw();
}

void HistoryWidget::loadOlder() {
  if (mRows.isEmpty()) {
    return;
  }
  RunHistory::Search search = asked();
  search.before = mRows.last().startedAt;
  const QList<JobRunRecord> older = RunHistory::search(search);
  mAtTheEnd = older.size() < search.limit;
  mRows += older;
  draw();
}

void HistoryWidget::draw() {
  const QString wasSelected =
      selectedRecord() != nullptr ? selectedRecord()->requestId : QString();

  mTable->setRowCount(0);
  mTable->setRowCount(mRows.size());

  for (int i = 0; i < mRows.size(); ++i) {
    const JobRunRecord &row = mRows[i];
    const bool liveHere = JobRegistry::instance().find(row.requestId) != nullptr;

    mTable->setItem(i, ColStarted,
                    cell(QDateTime::fromMSecsSinceEpoch(row.startedAt)
                             .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    mTable->setItem(i, ColName, cell(nameText(row)));
    // A rehearsal says so in the same column, because every figure on the
    // row means "would have" when it was one.
    mTable->setItem(i, ColKind,
                    cell(row.dryRun ? row.kind + QStringLiteral(" (dry run)")
                                    : row.kind));

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

  const RunHistory::Search search = asked();
  const bool narrowed = !search.text.isEmpty() || !search.state.isEmpty() ||
                        !search.kind.isEmpty();
  if (mRows.isEmpty()) {
    mSummary->setText(narrowed
                          ? tr("Nothing that matches.")
                          : tr("No runs recorded yet. Every job started from "
                               "here or with --run-task is added when it "
                               "begins."));
  } else {
    mSummary->setText(tr("%n run(s)", nullptr, mRows.size()) +
                      (mAtTheEnd ? QString() : tr(" — there are older ones")));
  }
  mOlder->setVisible(!mRows.isEmpty() && !mAtTheEnd);
  updateButtons();
}

// The page's run in full, here: everything kept about it.
void HistoryWidget::openSelectedRun() {
  const JobRunRecord *row = selectedRecord();
  if (row == nullptr) {
    return;
  }
  const auto at = [](qint64 ms) {
    return ms > 0 ? QDateTime::fromMSecsSinceEpoch(ms).toString(
                        QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                  : tr("still going");
  };
  QStringList lines;
  lines << tr("What: %1").arg(nameText(*row))
        << tr("From: %1").arg(row->source.isEmpty() ? QStringLiteral("—") : row->source)
        << tr("To: %1").arg(row->dest.isEmpty() ? QStringLiteral("—") : row->dest)
        << tr("Started by: %1%2")
               .arg(row->transferMode,
                    row->askedBy.isEmpty()
                        ? QString()
                        : tr(", asked by %1").arg(row->askedBy))
        << tr("Began: %1").arg(at(row->startedAt))
        << tr("Ended: %1").arg(at(row->finishedAt))
        << tr("How it went: %1%2")
               .arg(row->state, row->dryRun ? tr(" (dry run)") : QString())
        << tr("Moved: %1 · %2 files · %3 errors")
               .arg(FormatBytes(row->bytes))
               .arg(row->transfers)
               .arg(row->errors);
  QMessageBox box(QMessageBox::Information, tr("Run"), lines.join('\n'),
                  QMessageBox::Close, this);
  QPushButton *log = nullptr;
  if (!row->logPath.isEmpty() && QFileInfo::exists(row->logPath)) {
    log = box.addButton(tr("Open log"), QMessageBox::ActionRole);
  }
  box.exec();
  if (log != nullptr && box.clickedButton() == log) {
    openSelectedLog();
  }
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
