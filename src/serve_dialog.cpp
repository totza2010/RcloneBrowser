#include "serve_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

QString describe(const QString &protocol) {
  if (protocol == QLatin1String("webdav")) {
    return QStringLiteral("WebDAV — file managers, Windows, macOS, phones");
  }
  if (protocol == QLatin1String("http")) {
    return QStringLiteral("HTTP — a browser, read only by nature");
  }
  return protocol.toUpper();
}

} // namespace

ServeDialog::ServeDialog(const QString &what, QWidget *parent)
    : QDialog(parent) {
  setWindowTitle(QStringLiteral("Serve %1").arg(what));

  auto *intro = new QLabel(
      QStringLiteral("Offers this folder to other programs on a port of its "
                     "own, until you stop it in Jobs."),
      this);
  intro->setWordWrap(true);

  mProtocol = new QComboBox(this);
  for (const QString &protocol : RemoteOps::ServeProtocols()) {
    mProtocol->addItem(describe(protocol), protocol);
  }
  mPort = new QSpinBox(this);
  mPort->setRange(1, 65535);
  mPort->setValue(RemoteOps::DefaultServePort(mProtocol->currentData().toString()));
  QObject::connect(mProtocol, &QComboBox::currentIndexChanged, this, [this]() {
    mPort->setValue(
        RemoteOps::DefaultServePort(mProtocol->currentData().toString()));
  });

  mEveryone = new QCheckBox(
      QStringLiteral("Other machines may reach it too — needs a user and a "
                     "password"),
      this);
  mReadOnly = new QCheckBox(QStringLiteral("Read only"), this);
  mReadOnly->setChecked(true);
  mUser = new QLineEdit(this);
  mPassword = new QLineEdit(this);
  mPassword->setEchoMode(QLineEdit::Password);

  auto *form = new QFormLayout();
  form->addRow(QStringLiteral("As"), mProtocol);
  form->addRow(QStringLiteral("Port"), mPort);
  form->addRow(QString(), mEveryone);
  form->addRow(QString(), mReadOnly);
  form->addRow(QStringLiteral("User"), mUser);
  form->addRow(QStringLiteral("Password"), mPassword);

  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Serve"));
  QObject::connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto *layout = new QVBoxLayout(this);
  layout->addWidget(intro);
  layout->addLayout(form);
  layout->addWidget(buttons);
}

RemoteOps::ServeOptions ServeDialog::options() const {
  RemoteOps::ServeOptions options;
  options.protocol = mProtocol->currentData().toString();
  options.port = quint16(mPort->value());
  options.everyone = mEveryone->isChecked();
  options.readOnly = mReadOnly->isChecked();
  options.user = mUser->text();
  options.password = mPassword->text();
  return options;
}
