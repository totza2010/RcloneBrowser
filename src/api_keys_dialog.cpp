#include "api_keys_dialog.h"
#include "api_auth.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

ApiKeysDialog::ApiKeysDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(QStringLiteral("API keys"));
  resize(720, 420);

  auto *intro = new QLabel(
      QStringLiteral(
          "Give a program a key of its own instead of the token: it can do "
          "only as much as its level, and it can be taken back without "
          "changing anything for anyone else. read looks · control also "
          "runs, queues and stops · write also changes tasks, schedules and "
          "files · admin everything. The same keys the web page shows."),
      this);
  intro->setWordWrap(true);

  mTable = new QTableWidget(0, 4, this);
  mTable->setHorizontalHeaderLabels({QStringLiteral("Name"),
                                     QStringLiteral("Level"),
                                     QStringLiteral("Made"),
                                     QStringLiteral("Last used")});
  mTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  mTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  mTable->setSelectionMode(QAbstractItemView::SingleSelection);
  mTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  mTable->verticalHeader()->hide();

  auto *takeBackButton = new QPushButton(QStringLiteral("Take back"), this);
  QObject::connect(takeBackButton, &QPushButton::clicked, this,
                   &ApiKeysDialog::takeBack);

  mName = new QLineEdit(this);
  mName->setPlaceholderText(
      QStringLiteral("what it is for: home assistant, sonarr…"));
  mName->setMaxLength(40);
  mScope = new QComboBox(this);
  for (const char *scope : {"read", "control", "write", "admin"}) {
    mScope->addItem(QString::fromLatin1(scope), QString::fromLatin1(scope));
  }
  mScope->setCurrentIndex(1); // control: what most programs that ask need
  auto *makeButton = new QPushButton(QStringLiteral("Make a key"), this);
  QObject::connect(makeButton, &QPushButton::clicked, this,
                   &ApiKeysDialog::make);
  QObject::connect(mName, &QLineEdit::returnPressed, this,
                   &ApiKeysDialog::make);

  auto *makeRow = new QHBoxLayout();
  makeRow->addWidget(mName, 1);
  makeRow->addWidget(mScope);
  makeRow->addWidget(makeButton);

  // The key just made: here once, and nowhere else afterwards.
  mMadeNote = new QLabel(this);
  mMadeNote->setWordWrap(true);
  mMade = new QLineEdit(this);
  mMade->setReadOnly(true);
  auto *copy = new QPushButton(QStringLiteral("Copy"), this);
  QObject::connect(copy, &QPushButton::clicked, this, [this]() {
    QApplication::clipboard()->setText(mMade->text());
  });
  auto *madeRow = new QHBoxLayout();
  madeRow->addWidget(mMade, 1);
  madeRow->addWidget(copy);
  mMadeNote->hide();
  mMade->hide();
  copy->hide();
  QObject::connect(mMade, &QLineEdit::textChanged, copy,
                   [copy](const QString &text) { copy->setVisible(!text.isEmpty()); });

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto *layout = new QVBoxLayout(this);
  layout->addWidget(intro);
  layout->addWidget(mTable, 1);
  auto *tableButtons = new QHBoxLayout();
  tableButtons->addStretch();
  tableButtons->addWidget(takeBackButton);
  layout->addLayout(tableButtons);
  layout->addLayout(makeRow);
  layout->addWidget(mMadeNote);
  layout->addLayout(madeRow);
  layout->addWidget(buttons);

  draw();
}

void ApiKeysDialog::draw() {
  const QList<ApiAuth::Key> keys = ApiAuth::Keys::instance().list();
  mTable->setRowCount(keys.size());
  const auto when = [](const QDateTime &at) {
    return at.isValid() ? at.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                        : QStringLiteral("never");
  };
  for (int i = 0; i < keys.size(); ++i) {
    const ApiAuth::Key &key = keys.at(i);
    auto *name = new QTableWidgetItem(key.name);
    name->setData(Qt::UserRole, key.id);
    mTable->setItem(i, 0, name);
    mTable->setItem(i, 1, new QTableWidgetItem(ApiAuth::ScopeName(key.scope)));
    mTable->setItem(i, 2, new QTableWidgetItem(when(key.created)));
    mTable->setItem(i, 3, new QTableWidgetItem(when(key.lastUsed)));
  }
}

void ApiKeysDialog::make() {
  ApiAuth::Key made;
  QString why;
  const QString key = ApiAuth::Keys::instance().create(
      mName->text(), ApiAuth::ScopeNamed(mScope->currentData().toString()),
      &made, &why);
  if (key.isEmpty()) {
    QMessageBox::warning(this, windowTitle(), why);
    return;
  }
  mName->clear();
  mMadeNote->setText(
      QStringLiteral("<b>%1</b> — copy it now: it is not kept, and cannot be "
                     "shown again.")
          .arg(made.name.toHtmlEscaped()));
  mMadeNote->show();
  mMade->setText(key);
  mMade->show();
  mMade->selectAll();
  mMade->setFocus();
  draw();
}

void ApiKeysDialog::takeBack() {
  const QList<QTableWidgetItem *> chosen = mTable->selectedItems();
  if (chosen.isEmpty()) {
    return;
  }
  QTableWidgetItem *name = mTable->item(chosen.first()->row(), 0);
  if (QMessageBox::question(
          this, windowTitle(),
          QStringLiteral("Take back the key \"%1\"?\n\nWhatever uses it is "
                         "refused from its next request.")
              .arg(name->text())) != QMessageBox::Yes) {
    return;
  }
  ApiAuth::Keys::instance().remove(name->data(Qt::UserRole).toString());
  draw();
}
