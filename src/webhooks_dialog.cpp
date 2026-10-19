#include "webhooks_dialog.h"
#include "webhooks.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

QString describeFormat(const QString &format) {
  if (format == QLatin1String("json")) {
    return QStringLiteral("JSON — the whole event");
  }
  if (format == QLatin1String("text")) {
    return QStringLiteral("Text — one line (ntfy, Gotify)");
  }
  if (format == QLatin1String("slack")) {
    return QStringLiteral("Slack");
  }
  return QStringLiteral("Discord");
}

} // namespace

WebhooksDialog::WebhooksDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(QStringLiteral("Webhooks"));
  resize(820, 360);

  auto *intro = new QLabel(
      QStringLiteral("Addresses told when a job ends — a phone through ntfy, a "
                     "chat channel, Sonarr, a script — however the job was "
                     "started. The same list the web page's API tab shows."),
      this);
  intro->setWordWrap(true);

  mTable = new QTableWidget(0, 4, this);
  mTable->setHorizontalHeaderLabels(
      {QStringLiteral("Where"), QStringLiteral("As"), QStringLiteral("When"),
       QStringLiteral("Last")});
  mTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  mTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  mTable->setSelectionMode(QAbstractItemView::SingleSelection);
  mTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  mTable->verticalHeader()->hide();

  mNote = new QLabel(this);
  mNote->setWordWrap(true);

  auto *add = new QPushButton(QStringLiteral("Add…"), this);
  auto *change = new QPushButton(QStringLiteral("Change…"), this);
  auto *remove = new QPushButton(QStringLiteral("Remove"), this);
  auto *test = new QPushButton(QStringLiteral("Test"), this);
  auto *row = new QHBoxLayout();
  row->addWidget(add);
  row->addWidget(change);
  row->addWidget(remove);
  row->addWidget(test);
  row->addStretch();

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto *layout = new QVBoxLayout(this);
  layout->addWidget(intro);
  layout->addWidget(mTable, 1);
  layout->addLayout(row);
  layout->addWidget(mNote);
  layout->addWidget(buttons);

  QObject::connect(add, &QPushButton::clicked, this, [this]() { edit(QString()); });
  QObject::connect(change, &QPushButton::clicked, this, [this]() {
    if (!chosenId().isEmpty()) {
      edit(chosenId());
    }
  });
  QObject::connect(mTable, &QTableWidget::cellDoubleClicked, this,
                   [this]() { edit(chosenId()); });
  QObject::connect(remove, &QPushButton::clicked, this, [this]() {
    const Webhook *hook = Webhooks::instance().find(chosenId());
    if (hook == nullptr) {
      return;
    }
    if (QMessageBox::question(this, QStringLiteral("Webhooks"),
                              QStringLiteral("Stop telling \"%1\" when jobs end?")
                                  .arg(hook->name)) == QMessageBox::Yes) {
      Webhooks::instance().remove(chosenId());
    }
  });
  QObject::connect(test, &QPushButton::clicked, this, [this, test]() {
    const QString id = chosenId();
    if (id.isEmpty()) {
      return;
    }
    test->setEnabled(false);
    mNote->setText(QStringLiteral("Sending a made-up event…"));
    QPointer<WebhooksDialog> self(this);
    QPointer<QPushButton> button(test);
    Webhooks::instance().test(id, [self, button](int status, const QString &why) {
      if (button) {
        button->setEnabled(true);
      }
      if (!self) {
        return;
      }
      self->mNote->setText(why.isEmpty()
                               ? QStringLiteral("It answered %1.").arg(status)
                               : QStringLiteral("It did not take it: %1").arg(why));
      self->draw();
    });
  });

  // Changed here or from the page: the list is drawn again either way.
  QObject::connect(&Webhooks::instance(), &Webhooks::changed, this,
                   &WebhooksDialog::draw);
  draw();
}

QString WebhooksDialog::chosenId() const {
  const QList<QTableWidgetItem *> chosen = mTable->selectedItems();
  return chosen.isEmpty() ? QString()
                          : mTable->item(chosen.first()->row(), 0)
                                ->data(Qt::UserRole)
                                .toString();
}

void WebhooksDialog::draw() {
  const QString kept = chosenId();
  const QList<Webhook> hooks = Webhooks::instance().list();
  mTable->setRowCount(hooks.size());
  for (int i = 0; i < hooks.size(); ++i) {
    const Webhook &hook = hooks.at(i);
    auto *where = new QTableWidgetItem(
        QStringLiteral("%1 — %2").arg(hook.name, hook.url));
    where->setData(Qt::UserRole, hook.id);
    mTable->setItem(i, 0, where);
    mTable->setItem(i, 1, new QTableWidgetItem(hook.format));
    QString when = hook.failuresOnly ? QStringLiteral("when one fails")
                                     : QStringLiteral("when one ends");
    when += hook.everyJob ? QStringLiteral(" · every job")
                          : QStringLiteral(" · transfers");
    if (!hook.secret.isEmpty()) {
      when += QStringLiteral(" · signed");
    }
    mTable->setItem(i, 2, new QTableWidgetItem(when));
    mTable->setItem(
        i, 3,
        new QTableWidgetItem(
            hook.lastResult.isEmpty()
                ? QStringLiteral("not yet")
                : QStringLiteral("%1 at %2").arg(
                      hook.lastResult,
                      QDateTime::fromMSecsSinceEpoch(hook.lastAt)
                          .toString(QStringLiteral("HH:mm:ss")))));
    if (hook.id == kept) {
      mTable->selectRow(i);
    }
  }
  if (hooks.isEmpty()) {
    mNote->setText(QStringLiteral("No webhooks yet."));
  }
}

void WebhooksDialog::edit(const QString &id) {
  const Webhook *existing = Webhooks::instance().find(id);
  Webhook hook = existing != nullptr ? *existing : Webhook();
  // Held as a yes or no: the list can change while the form is open (from
  // the page), and a pointer into it would not survive that.
  const bool exists = existing != nullptr;

  QDialog form(this);
  form.setWindowTitle(exists ? QStringLiteral("Change webhook")
                                          : QStringLiteral("Add webhook"));
  auto *url = new QLineEdit(hook.url, &form);
  url->setPlaceholderText(QStringLiteral("https://ntfy.sh/my-topic"));
  auto *name = new QLineEdit(hook.name, &form);
  name->setPlaceholderText(QStringLiteral("optional"));
  auto *format = new QComboBox(&form);
  for (const QString &one : Webhooks::Formats()) {
    format->addItem(describeFormat(one), one);
  }
  format->setCurrentIndex(qMax(0, format->findData(hook.format)));
  auto *failures = new QCheckBox(QStringLiteral("Only when one fails"), &form);
  failures->setChecked(hook.failuresOnly);
  auto *every =
      new QCheckBox(QStringLiteral("Every job, not only transfers"), &form);
  every->setChecked(hook.everyJob);
  auto *secret = new QLineEdit(&form);
  secret->setEchoMode(QLineEdit::Password);
  secret->setPlaceholderText(!hook.secret.isEmpty()
                                 ? QStringLiteral("kept unless changed")
                                 : QStringLiteral("optional: signs each one"));
  auto *clearSecret = new QCheckBox(QStringLiteral("No secret"), &form);
  clearSecret->setVisible(!hook.secret.isEmpty());

  auto *layout = new QFormLayout(&form);
  layout->addRow(QStringLiteral("Address"), url);
  layout->addRow(QStringLiteral("Name"), name);
  layout->addRow(QStringLiteral("As"), format);
  layout->addRow(QString(), failures);
  layout->addRow(QString(), every);
  layout->addRow(QStringLiteral("Secret"), secret);
  layout->addRow(QString(), clearSecret);
  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &form);
  layout->addRow(buttons);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &form, &QDialog::reject);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &form, [&]() {
    Webhook given;
    given.url = url->text();
    given.name = name->text();
    given.format = format->currentData().toString();
    given.failuresOnly = failures->isChecked();
    given.everyJob = every->isChecked();
    given.secret = secret->text();
    QString why;
    const bool done =
        exists
            ? Webhooks::instance().update(id, given, clearSecret->isChecked(), &why)
            : !Webhooks::instance().add(given, &why).isEmpty();
    if (!done) {
      QMessageBox::warning(&form, form.windowTitle(), why);
      return;
    }
    form.accept();
  });
  form.exec();
}
