#include "backup_dialog.h"
#include "backup_plan.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTimeEdit>
#include <QVBoxLayout>

BackupDialog::BackupDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(QStringLiteral("New backup"));

  auto *intro = new QLabel(
      QStringLiteral("A folder of this machine to a remote — or a remote to "
                     "a folder here — on a schedule. It is made into a task "
                     "and a schedule you can change afterwards like any "
                     "other."),
      this);
  intro->setWordWrap(true);

  mSource = new QLineEdit(this);
  mSource->setPlaceholderText(QStringLiteral("D:/Documents  or  gdrive:Documents"));
  auto *browse = new QPushButton(QStringLiteral("Folder…"), this);
  QObject::connect(browse, &QPushButton::clicked, this, [this]() {
    const QString folder = QFileDialog::getExistingDirectory(
        this, QStringLiteral("What to back up"), mSource->text());
    if (!folder.isEmpty()) {
      mSource->setText(folder);
    }
  });
  auto *sourceRow = new QHBoxLayout();
  sourceRow->addWidget(mSource, 1);
  sourceRow->addWidget(browse);

  mDest = new QLineEdit(this);
  mDest->setPlaceholderText(QStringLiteral("gdrive:Backups/documents"));

  mMirror = new QRadioButton(
      QStringLiteral("Mirror — kept the same as the source, so what is "
                     "deleted there goes here too"),
      this);
  auto *keep = new QRadioButton(
      QStringLiteral("Keep everything — nothing is ever deleted from the "
                     "backup"),
      this);
  mMirror->setChecked(true);

  mVersions = new QCheckBox(
      QStringLiteral("Keep old versions: what is changed or deleted in the "
                     "backup is moved to a dated folder beside it, not lost"),
      this);
  mVersions->setChecked(true);

  auto *daysRow = new QHBoxLayout();
  for (const char *day : {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"}) {
    auto *box = new QCheckBox(QString::fromLatin1(day), this);
    box->setChecked(true);
    mDays << box;
    daysRow->addWidget(box);
  }
  mTime = new QTimeEdit(QTime(2, 0), this);
  mTime->setDisplayFormat(QStringLiteral("HH:mm"));
  daysRow->addWidget(new QLabel(QStringLiteral("at"), this));
  daysRow->addWidget(mTime);
  daysRow->addStretch();

  mName = new QLineEdit(this);
  mName->setPlaceholderText(QStringLiteral("optional"));

  auto *form = new QFormLayout();
  form->addRow(QStringLiteral("What to back up"), sourceRow);
  form->addRow(QStringLiteral("Where to"), mDest);
  form->addRow(QString(), mMirror);
  form->addRow(QString(), keep);
  form->addRow(QString(), mVersions);
  form->addRow(QStringLiteral("When"), daysRow);
  form->addRow(QStringLiteral("Name"), mName);

  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Make the backup"));
  QObject::connect(buttons, &QDialogButtonBox::accepted, this, &BackupDialog::make);
  QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto *layout = new QVBoxLayout(this);
  layout->addWidget(intro);
  layout->addLayout(form);
  layout->addWidget(buttons);
}

void BackupDialog::make() {
  BackupPlan plan;
  plan.name = mName->text();
  plan.source = mSource->text();
  plan.dest = mDest->text();
  plan.mirror = mMirror->isChecked();
  plan.keepVersions = mVersions->isChecked();
  static const char *const week[] = {"mon", "tue", "wed", "thu",
                                     "fri", "sat", "sun"};
  for (int i = 0; i < mDays.size(); ++i) {
    if (mDays.at(i)->isChecked()) {
      plan.days << QString::fromLatin1(week[i]);
    }
  }
  if (plan.days.isEmpty()) {
    QMessageBox::warning(this, windowTitle(),
                         QStringLiteral("Choose at least one day."));
    return;
  }
  if (plan.days.size() == 7) {
    plan.days.clear();
  }
  plan.time = mTime->time().toString(QStringLiteral("HH:mm"));

  auto *task = new JobOptions(false);
  Schedule schedule;
  QString why;
  if (!Backup::Build(plan, task, &schedule, &why) ||
      !Backup::Save(task, &schedule, &why)) {
    delete task;
    QMessageBox::warning(this, windowTitle(), why);
    return;
  }
  QMessageBox::information(
      this, windowTitle(),
      QStringLiteral("\"%1\" is a task now, on a schedule. Run it from Tasks "
                     "to make the first backup straight away.")
          .arg(task->description));
  accept();
}
