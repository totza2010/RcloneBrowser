#include "script_editor_dialog.h"
#include "utils.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>

// TEST: (V-10) Mount dialog -> ปุ่ม "Edit..." -> เขียน script -> Save
// ต้องได้ไฟล์ใน <config>/mount-scripts/ · ช่อง path เติมให้อัตโนมัติ · RC port
// ถูกตั้งให้เอง · บน Linux/macOS ไฟล์ต้องมี exec bit · กด Edit ซ้ำต้องโหลดของเดิมมาแก้ได้
QString ScriptEditorDialog::scriptsDir() {
  return GetConfigDir().filePath("mount-scripts");
}

QString ScriptEditorDialog::scriptExtension() {
#ifdef Q_OS_WIN
  return ".cmd";
#else
  return ".sh";
#endif
}

QString ScriptEditorDialog::templateFor(const QString &name) {
  // The argument list is fixed by MountWidget; spell it out so nobody has to
  // go looking for it in the tooltip.
#ifdef Q_OS_WIN
  return QStringLiteral(
             "@echo off\r\n"
             "rem %1 - runs after the mount succeeds\r\n"
             "rem\r\n"
             "rem   %%1  rclone executable\r\n"
             "rem   %%2  remote control port\r\n"
             "rem   %%3  remote control user\r\n"
             "rem   %%4  remote control password\r\n"
             "rem   %%5  mount point\r\n"
             "rem\r\n"
             "rem The script is killed when the mount is unmounted.\r\n"
             "\r\n"
             "echo Mounted at %%5\r\n")
      .arg(name);
#else
  return QStringLiteral("#!/bin/sh\n"
                        "# %1 - runs after the mount succeeds\n"
                        "#\n"
                        "#   $1  rclone executable\n"
                        "#   $2  remote control port\n"
                        "#   $3  remote control user\n"
                        "#   $4  remote control password\n"
                        "#   $5  mount point\n"
                        "#\n"
                        "# The script is killed when the mount is unmounted.\n"
                        "\n"
                        "echo \"Mounted at $5\"\n")
      .arg(name);
#endif
}

ScriptEditorDialog::ScriptEditorDialog(const QString &existingPath,
                                       const QString &suggestedName,
                                       QWidget *parent)
    : QDialog(parent) {
  setWindowTitle("Post mount script");
  resize(640, 480);

  auto *layout = new QVBoxLayout(this);

  layout->addWidget(new QLabel("Name:", this));
  mName = new QLineEdit(this);
  layout->addWidget(mName);

  mPathLabel = new QLabel(this);
  mPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(mPathLabel);

  mEditor = new QPlainTextEdit(this);
  mEditor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  mEditor->setLineWrapMode(QPlainTextEdit::NoWrap);
  mEditor->setTabChangesFocus(false);
  layout->addWidget(mEditor, 1);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  layout->addWidget(buttons);

  QString baseName = suggestedName.trimmed();
  QString body;

  const QFileInfo existing(existingPath);
  if (!existingPath.isEmpty() && existing.isFile()) {
    QFile file(existingPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      QTextStream in(&file);
      body = in.readAll();
      baseName = existing.completeBaseName();
    }
  }

  if (baseName.isEmpty()) {
    baseName = "mount-script";
  }
  if (body.isEmpty()) {
    body = templateFor(baseName);
  }

  mName->setText(baseName);
  mEditor->setPlainText(body);

  auto updatePath = [this]() {
    mPathLabel->setText(
        "Saved to: " +
        QDir::toNativeSeparators(QDir(scriptsDir())
                                    .filePath(mName->text().trimmed() +
                                              scriptExtension())));
  };
  updatePath();
  QObject::connect(mName, &QLineEdit::textChanged, this, updatePath);

  QObject::connect(buttons, &QDialogButtonBox::accepted, this,
                   &ScriptEditorDialog::save);
  QObject::connect(buttons, &QDialogButtonBox::rejected, this,
                   &QDialog::reject);
}

void ScriptEditorDialog::save() {
  const QString name = mName->text().trimmed();
  if (name.isEmpty()) {
    QMessageBox::warning(this, "Post mount script", "Enter a name first.");
    return;
  }
  // The name becomes a filename; keep it to something every platform accepts
  // and make sure it cannot climb out of the scripts directory.
  static const QString invalid = R"(\/:*?"<>|)";
  for (const QChar &c : name) {
    if (invalid.contains(c) || c == '.') {
      QMessageBox::warning(this, "Post mount script",
                           "The name cannot contain any of  \\ / : * ? \" < > | .");
      return;
    }
  }

  QDir dir(scriptsDir());
  if (!dir.exists() && !dir.mkpath(".")) {
    QMessageBox::critical(this, "Post mount script",
                          "Could not create\n\n" +
                              QDir::toNativeSeparators(scriptsDir()));
    return;
  }

  const QString path = dir.filePath(name + scriptExtension());

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text |
                 QIODevice::Truncate)) {
    QMessageBox::critical(this, "Post mount script",
                          "Could not write\n\n" +
                              QDir::toNativeSeparators(path) + "\n\n" +
                              file.errorString());
    return;
  }
  {
    QTextStream out(&file);
    out << mEditor->toPlainText();
  }
  file.close();

#ifndef Q_OS_WIN
  // MountDialog rejects scripts that are not executable, and forgetting chmod
  // is the usual reason a hand-written script does not work. On Windows the
  // extension decides, so there is nothing to set.
  if (!file.setPermissions(file.permissions() | QFileDevice::ExeOwner |
                           QFileDevice::ExeUser)) {
    QMessageBox::warning(this, "Post mount script",
                         "The script was saved but could not be marked "
                         "executable:\n\n" +
                             QDir::toNativeSeparators(path));
  }
#endif

  mSavedPath = path;
  accept();
}
