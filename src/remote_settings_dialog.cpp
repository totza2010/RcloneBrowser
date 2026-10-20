#include "remote_settings_dialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHash>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QString firstLineOf(const QString &help) {
  return help.section(QLatin1Char('\n'), 0, 0).trimmed();
}

bool signsIn(const Provider &provider) { return SignsInWithBrowser(provider); }

} // namespace

RemoteSettingsDialog::RemoteSettingsDialog(const QString &remote,
                                           QWidget *parent)
    : QDialog(parent), mRemote(remote), mCreating(remote.isEmpty()) {
  setWindowTitle(mCreating ? QStringLiteral("New remote")
                           : QStringLiteral("Settings of %1:").arg(remote));
  resize(720, 620);

  auto *layout = new QVBoxLayout(this);
  mPages = new QStackedWidget(this);
  layout->addWidget(mPages);

  // ---- the form --------------------------------------------------------
  auto *formPage = new QWidget(mPages);
  auto *formLayout = new QVBoxLayout(formPage);
  formLayout->setContentsMargins(0, 0, 0, 0);

  auto *head = new QFormLayout();
  mName = new QLineEdit(formPage);
  mName->setObjectName(QStringLiteral("remoteName"));
  mName->setText(remote);
  mName->setPlaceholderText(QStringLiteral("films, backup, nas…"));
  head->addRow(QStringLiteral("Name"), mName);
  mKind = new QComboBox(formPage);
  mKind->setObjectName(QStringLiteral("remoteKind"));
  mKind->setEditable(true);
  mKind->setInsertPolicy(QComboBox::NoInsert);
  mKind->completer()->setFilterMode(Qt::MatchContains);
  mKind->completer()->setCompletionMode(QCompleter::PopupCompletion);
  head->addRow(QStringLiteral("Kind"), mKind);
  mKind->setVisible(mCreating);
  head->labelForField(mKind)->setVisible(mCreating);
  formLayout->addLayout(head);

  mAbout = new QLabel(QStringLiteral("Reading what rclone has…"), formPage);
  mAbout->setWordWrap(true);
  formLayout->addWidget(mAbout);

  // Signing in, for a kind that does it through a web browser: done by
  // rclone on any machine with one, and what it prints pasted here -- the
  // same way the page does it, and the only way that also works for a
  // program in a container.
  mSignIn = new QGroupBox(mCreating ? QStringLiteral("Sign in")
                                    : QStringLiteral("Sign in again (only if it has stopped working)"),
                          formPage);
  auto *signInLayout = new QVBoxLayout(mSignIn);
  auto *steps = new QLabel(
      QStringLiteral("1. On any computer with a web browser and rclone, run "
                     "the command below.  2. Sign in on the page it opens.  "
                     "3. Paste everything it prints here."),
      mSignIn);
  steps->setWordWrap(true);
  signInLayout->addWidget(steps);
  auto *commandRow = new QHBoxLayout();
  mAuthorize = new QLabel(mSignIn);
  mAuthorize->setTextInteractionFlags(Qt::TextSelectableByMouse);
  mAuthorize->setStyleSheet(QStringLiteral("font-family: monospace;"));
  auto *copy = new QPushButton(QStringLiteral("Copy"), mSignIn);
  connect(copy, &QPushButton::clicked, this, [this]() {
    QApplication::clipboard()->setText(mAuthorize->text());
  });
  commandRow->addWidget(mAuthorize, 1);
  commandRow->addWidget(copy);
  signInLayout->addLayout(commandRow);
  mToken = new QPlainTextEdit(mSignIn);
  mToken->setPlaceholderText(QStringLiteral("{\"access_token\":\"…\",\"token_type\":\"Bearer\",…}"));
  mToken->setMaximumHeight(90);
  signInLayout->addWidget(mToken);
  mSignIn->setVisible(false);
  formLayout->addWidget(mSignIn);

  auto *scroll = new QScrollArea(formPage);
  scroll->setWidgetResizable(true);
  mFieldsBox = new QWidget(scroll);
  auto *fieldsLayout = new QVBoxLayout(mFieldsBox);
  mForm = new QFormLayout();
  fieldsLayout->addLayout(mForm);
  mAdvanced = new QGroupBox(QStringLiteral("Advanced options"), mFieldsBox);
  mAdvanced->setCheckable(true);
  mAdvanced->setChecked(false);
  auto *advancedInner = new QWidget(mAdvanced);
  mAdvancedForm = new QFormLayout(advancedInner);
  auto *advancedLayout = new QVBoxLayout(mAdvanced);
  advancedLayout->addWidget(advancedInner);
  advancedInner->setVisible(false);
  connect(mAdvanced, &QGroupBox::toggled, advancedInner, &QWidget::setVisible);
  mAdvanced->setVisible(false);
  fieldsLayout->addWidget(mAdvanced);
  fieldsLayout->addStretch();
  scroll->setWidget(mFieldsBox);
  formLayout->addWidget(scroll, 1);
  mPages->addWidget(formPage);

  // ---- rclone's question ----------------------------------------------
  auto *questionPage = new QWidget(mPages);
  auto *questionLayout = new QVBoxLayout(questionPage);
  questionLayout->addWidget(new QLabel(QStringLiteral("<b>rclone asks</b>"), questionPage));
  mQuestionName = new QLabel(questionPage);
  mQuestionName->setStyleSheet(QStringLiteral("font-family: monospace;"));
  questionLayout->addWidget(mQuestionName);
  mQuestionHelp = new QLabel(questionPage);
  mQuestionHelp->setWordWrap(true);
  mQuestionHelp->setTextInteractionFlags(Qt::TextSelectableByMouse);
  questionLayout->addWidget(mQuestionHelp);
  mQuestionError = new QLabel(questionPage);
  mQuestionError->setWordWrap(true);
  mQuestionError->setStyleSheet(QStringLiteral("color: #c0392b;"));
  questionLayout->addWidget(mQuestionError);
  mQuestionChoice = new QComboBox(questionPage);
  questionLayout->addWidget(mQuestionChoice);
  mQuestionText = new QLineEdit(questionPage);
  questionLayout->addWidget(mQuestionText);
  questionLayout->addStretch();
  mPages->addWidget(questionPage);

  // ---- the buttons -----------------------------------------------------
  mError = new QLabel(this);
  mError->setWordWrap(true);
  mError->setStyleSheet(QStringLiteral("color: #c0392b;"));
  layout->addWidget(mError);

  auto *buttons = new QHBoxLayout();
  mDelete = new QPushButton(QStringLiteral("Delete this remote…"), this);
  mDelete->setVisible(!mCreating);
  buttons->addWidget(mDelete);
  buttons->addStretch();
  mClose = new QPushButton(QStringLiteral("Cancel"), this);
  mSave = new QPushButton(QStringLiteral("Save"), this);
  mSave->setDefault(true);
  mSave->setEnabled(false);
  buttons->addWidget(mClose);
  buttons->addWidget(mSave);
  layout->addLayout(buttons);

  connect(mClose, &QPushButton::clicked, this, &QDialog::reject);
  connect(mSave, &QPushButton::clicked, this, [this]() {
    if (mQuestion.isEmpty()) {
      save();
    } else {
      answer();
    }
  });
  connect(mDelete, &QPushButton::clicked, this, [this]() { deleteRemote(false); });
  connect(mKind, &QComboBox::currentIndexChanged, this, [this]() { chooseType(); });

  whenBackendsKnown();
}

void RemoteSettingsDialog::whenBackendsKnown() {
  RcloneConfigRegistry &config = RcloneConfigRegistry::instance();
  if (!config.isLoaded()) {
    config.ensureLoaded();
    // Asked once, and the form is made when the answer lands. Without it
    // nothing could be told about which settings are secret.
    connect(&config, &RcloneConfigRegistry::providersLoaded, this,
            [this]() { whenBackendsKnown(); }, Qt::SingleShotConnection);
    QTimer::singleShot(15000, this, [this]() {
      if (!RcloneConfigRegistry::instance().isLoaded()) {
        mAbout->setText(QStringLiteral("rclone did not say what kinds of remote "
                                       "it has, so nothing can be shown or changed "
                                       "here. Is rclone set up in Preferences?"));
      }
    });
    return;
  }

  if (mCreating) {
    const QSignalBlocker quiet(mKind);
    mKind->clear();
    mKind->addItem(QString(), QString());
    for (const Provider &provider : config.providers()) {
      mKind->addItem(provider.description.isEmpty()
                         ? provider.name
                         : provider.name + QStringLiteral(" — ") + provider.description,
                     provider.name);
    }
    mKind->setCurrentIndex(0);
    mAbout->setText(QStringLiteral("Choose the kind of remote."));
    return;
  }
  readRemote();
}

void RemoteSettingsDialog::readRemote() {
  RcloneConfigRegistry::instance().readDump(
      this, [this](bool ok, const QJsonObject &dump, const QString &why) {
        if (!ok) {
          mAbout->setText(why);
          return;
        }
        if (!dump.contains(mRemote)) {
          mAbout->setText(QStringLiteral("There is no remote called %1 any more.").arg(mRemote));
          return;
        }
        // Everything the file has is in `section`, secrets included; only
        // what DescribeSettings makes of it is shown.
        const QJsonObject section = dump.value(mRemote).toObject();
        mType = section.value(QStringLiteral("type")).toString();
        const Provider *provider =
            FindProvider(RcloneConfigRegistry::instance().providers(), mType);
        if (provider == nullptr) {
          mAbout->setText(QStringLiteral("This rclone does not describe %1 remotes, "
                                         "so their settings cannot be shown or changed "
                                         "here; use rclone config.").arg(mType));
          return;
        }
        buildForm(*provider, DescribeSettings(section, provider));
      });
}

void RemoteSettingsDialog::chooseType() {
  mType = mKind->currentData().toString();
  const Provider *provider =
      FindProvider(RcloneConfigRegistry::instance().providers(), mType);
  if (provider == nullptr) {
    buildForm(Provider(), QJsonArray());
    mSave->setEnabled(false);
    mAbout->setText(QStringLiteral("Choose the kind of remote."));
    return;
  }
  buildForm(*provider, QJsonArray());
}

QWidget *RemoteSettingsDialog::makeInput(const ProviderOption &option,
                                         const QString &value, bool isSet) {
  QWidget *input = nullptr;
  if (option.type == QLatin1String("bool")) {
    auto *box = new QCheckBox(mFieldsBox);
    box->setChecked(value.isEmpty() ? option.defaultValue == QLatin1String("true")
                                    : value == QLatin1String("true"));
    input = box;
  } else if (option.exclusive && !option.examples.isEmpty()) {
    auto *choice = new QComboBox(mFieldsBox);
    if (!option.required) {
      choice->addItem(option.defaultValue.isEmpty()
                          ? QStringLiteral("not set")
                          : QStringLiteral("default (%1)").arg(option.defaultValue),
                      QString());
    }
    for (const auto &example : option.examples) {
      choice->addItem(example.second.isEmpty()
                          ? example.first
                          : example.first + QStringLiteral(" — ") + firstLineOf(example.second),
                      example.first);
    }
    const int at = choice->findData(value);
    choice->setCurrentIndex(at < 0 ? 0 : at);
    input = choice;
  } else {
    auto *line = new QLineEdit(mFieldsBox);
    if (option.isPassword || option.sensitive) {
      // Never filled in: what is set stays out of the window as it stays out
      // of the page. Typing replaces it; leaving it empty keeps it.
      line->setEchoMode(QLineEdit::Password);
      line->setPlaceholderText(isSet ? QStringLiteral("set — type to replace it")
                                     : QString());
    } else {
      line->setText(value);
      line->setPlaceholderText(option.defaultValue);
      if (!option.examples.isEmpty()) {
        QStringList values;
        for (const auto &example : option.examples) {
          values << example.first;
        }
        line->setCompleter(new QCompleter(values, line));
      }
    }
    input = line;
  }
  input->setToolTip(option.help);
  // Named as rclone names the option: what a test fills in, and what a
  // screen reader can tell apart.
  input->setObjectName(option.name);
  return input;
}

void RemoteSettingsDialog::buildForm(const Provider &provider,
                                     const QJsonArray &current) {
  while (mForm->rowCount() > 0) {
    mForm->removeRow(0);
  }
  while (mAdvancedForm->rowCount() > 0) {
    mAdvancedForm->removeRow(0);
  }
  mFields.clear();
  mError->clear();

  QHash<QString, QString> now;
  QSet<QString> set;
  bool advancedSet = false;
  for (const QJsonValue &each : current) {
    const QJsonObject setting = each.toObject();
    const QString name = setting.value(QStringLiteral("name")).toString();
    set.insert(name);
    if (!setting.value(QStringLiteral("secret")).toBool()) {
      now.insert(name, setting.value(QStringLiteral("value")).toString());
    }
    advancedSet = advancedSet || setting.value(QStringLiteral("advanced")).toBool();
  }

  const bool browser = !provider.name.isEmpty() && signsIn(provider);
  mSignIn->setVisible(browser);
  mToken->clear();
  mAuthorize->setText(QStringLiteral("rclone authorize \"%1\"").arg(provider.name));
  mAbout->setText(provider.description.isEmpty() ? provider.name : provider.description);

  int advancedCount = 0;
  for (const ProviderOption &option : provider.options) {
    // The token is the paste box above; hidden options are rclone's own.
    if (option.hide != 0 || option.name == QLatin1String("token")) {
      continue;
    }
    Field field;
    field.option = option;
    field.before = now.value(option.name);
    field.wasSet = set.contains(option.name);
    field.input = makeInput(option, field.before, field.wasSet);

    auto *row = new QWidget(mFieldsBox);
    auto *rowLayout = new QVBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(1);
    rowLayout->addWidget(field.input);
    QString about = firstLineOf(option.help);
    if (!option.defaultValue.isEmpty()) {
      about += QStringLiteral(" · default ") + option.defaultValue;
    }
    auto *help = new QLabel(about, row);
    help->setWordWrap(true);
    help->setStyleSheet(QStringLiteral("color: gray; font-size: small;"));
    rowLayout->addWidget(help);

    const QString label = option.name + (option.required ? QStringLiteral(" *") : QString());
    if (option.advanced) {
      mAdvancedForm->addRow(label, row);
      ++advancedCount;
    } else {
      mForm->addRow(label, row);
    }
    mFields.append(field);
  }
  mAdvanced->setVisible(advancedCount > 0);
  mAdvanced->setTitle(QStringLiteral("Advanced options (%1)").arg(advancedCount));
  // Opened when one of them is set: a form that hid a setting the remote
  // has would be lying about it.
  mAdvanced->setChecked(advancedSet);
  mSave->setEnabled(!provider.name.isEmpty());
}

QJsonObject RemoteSettingsDialog::changedSettings() const {
  QJsonObject settings;
  if (mSignIn->isVisible() && !mToken->toPlainText().trimmed().isEmpty()) {
    settings.insert(QStringLiteral("token"), mToken->toPlainText().trimmed());
  }
  for (const Field &field : mFields) {
    QString value;
    if (auto *box = qobject_cast<QCheckBox *>(field.input)) {
      value = box->isChecked() ? QStringLiteral("true") : QStringLiteral("false");
      const QString was = !field.before.isEmpty()
                              ? field.before
                              : (field.option.defaultValue.isEmpty()
                                     ? QStringLiteral("false")
                                     : field.option.defaultValue);
      if (value == was) {
        continue;
      }
    } else if (auto *choice = qobject_cast<QComboBox *>(field.input)) {
      value = choice->currentData().toString();
      if (value == field.before) {
        continue;
      }
    } else if (auto *line = qobject_cast<QLineEdit *>(field.input)) {
      value = line->text();
      if (field.option.isPassword || field.option.sensitive) {
        if (value.isEmpty()) {
          continue; // left alone: kept as it is
        }
      } else if (mCreating ? value.isEmpty() : value == field.before) {
        continue;
      }
    }
    settings.insert(field.option.name, value);
  }
  return settings;
}

void RemoteSettingsDialog::setBusy(bool busy) {
  mSave->setEnabled(!busy);
  mDelete->setEnabled(!busy);
  if (busy) {
    mError->setText(QStringLiteral("Working…"));
  }
}

void RemoteSettingsDialog::save() {
  const QString name = mName->text().trimmed();
  const QJsonObject settings = changedSettings();
  setBusy(true);

  if (mCreating) {
    CreateRemote(this, name, mType, settings,
                 [this, name](const RemoteChange &change) {
                   if (change.ok) {
                     mRemote = name;
                     mCreating = false;
                   }
                   finished(change);
                 });
    return;
  }

  if (settings.isEmpty() && name == mRemote) {
    accept();
    return;
  }
  // The settings first, while it is still called what it is called; then
  // the name. The same order the page's request takes.
  const QString was = mRemote;
  UpdateRemote(this, was, settings, [this, was, name](const RemoteChange &changed) {
    if (!changed.ok || name.isEmpty() || name == was) {
      finished(changed);
      return;
    }
    RenameRemote(this, was, name, [this, name, changed](RemoteChange renamed) {
      if (renamed.ok) {
        mRemote = name;
        if (!renamed.tasks.isEmpty()) {
          QMessageBox::information(
              this, windowTitle(),
              QStringLiteral("These tasks now use %1:\n\n  %2")
                  .arg(name, renamed.tasks.join(QStringLiteral("\n  "))));
        }
      }
      if (renamed.question.isEmpty()) {
        renamed.question = changed.question;
      }
      finished(renamed);
    });
  });
}

void RemoteSettingsDialog::finished(const RemoteChange &change) {
  setBusy(false);
  if (!change.ok) {
    mError->setText(change.why);
    if (!mQuestion.isEmpty()) {
      mQuestionError->setText(change.why);
    }
    return;
  }
  mError->clear();
  if (!change.question.isEmpty()) {
    showQuestion(change.question);
    return;
  }
  accept();
}

void RemoteSettingsDialog::showQuestion(const QJsonObject &question) {
  mQuestion = question;
  mPages->setCurrentIndex(1);
  mName->setEnabled(false);
  mDelete->setVisible(false);
  mSave->setText(QStringLiteral("Answer"));
  mClose->setText(QStringLiteral("Leave the rest"));

  mQuestionName->setText(question.value(QStringLiteral("name")).toString());
  mQuestionHelp->setText(question.value(QStringLiteral("help")).toString());
  mQuestionError->setText(question.value(QStringLiteral("error")).toString());

  const QJsonArray examples = question.value(QStringLiteral("examples")).toArray();
  const QString fallback = question.value(QStringLiteral("default")).toString();
  const bool choose = question.value(QStringLiteral("exclusive")).toBool() &&
                      !examples.isEmpty();
  mQuestionChoice->clear();
  mQuestionChoice->setVisible(choose);
  mQuestionText->setVisible(!choose);
  if (choose) {
    for (const QJsonValue &each : examples) {
      const QJsonObject e = each.toObject();
      const QString value = e.value(QStringLiteral("value")).toString();
      const QString help = firstLineOf(e.value(QStringLiteral("help")).toString());
      mQuestionChoice->addItem(help.isEmpty() ? value : value + QStringLiteral(" — ") + help,
                               value);
    }
    const int at = mQuestionChoice->findData(fallback);
    mQuestionChoice->setCurrentIndex(at < 0 ? 0 : at);
  } else {
    mQuestionText->setText(fallback);
    mQuestionText->setEchoMode(question.value(QStringLiteral("password")).toBool()
                                   ? QLineEdit::Password
                                   : QLineEdit::Normal);
    QStringList values;
    for (const QJsonValue &each : examples) {
      values << each.toObject().value(QStringLiteral("value")).toString();
    }
    mQuestionText->setCompleter(values.isEmpty() ? nullptr : new QCompleter(values, mQuestionText));
  }
}

void RemoteSettingsDialog::answer() {
  const QString result = mQuestionChoice->isVisible()
                             ? mQuestionChoice->currentData().toString()
                             : mQuestionText->text();
  setBusy(true);
  ContinueRemote(this, mRemote, mQuestion.value(QStringLiteral("state")).toString(),
                 result, [this](const RemoteChange &change) {
                   setBusy(false);
                   if (!change.ok) {
                     mQuestionError->setText(change.why);
                     return;
                   }
                   if (!change.question.isEmpty()) {
                     showQuestion(change.question);
                     return;
                   }
                   accept();
                 });
}

void RemoteSettingsDialog::deleteRemote(bool evenIfTasksUseIt) {
  if (!evenIfTasksUseIt &&
      QMessageBox::question(
          this, QStringLiteral("Delete remote"),
          QStringLiteral("Delete the remote %1: from rclone.conf?\n\nIts files are not "
                         "touched -- they are on the remote -- but this program and "
                         "rclone will no longer know how to reach them.")
              .arg(mRemote),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
    return;
  }
  setBusy(true);
  DeleteRemote(this, mRemote, evenIfTasksUseIt, [this](const RemoteChange &change) {
    setBusy(false);
    if (!change.ok && change.code == QLatin1String("used_by_tasks")) {
      if (QMessageBox::question(
              this, QStringLiteral("Delete remote"),
              QStringLiteral("These saved tasks use %1:\n\n  %2\n\nDelete the remote "
                             "anyway? The tasks are kept, and will not run until "
                             "changed.")
                  .arg(mRemote, change.tasks.join(QStringLiteral("\n  "))),
              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
        deleteRemote(true);
      }
      return;
    }
    if (!change.ok) {
      mError->setText(change.why);
      return;
    }
    accept();
  });
}
