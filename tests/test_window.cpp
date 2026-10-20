#include "api_auth.h"
#include "api_keys_dialog.h"
#include "app_core.h"
#include "app_settings.h"
#include "backup_dialog.h"
#include "scheduler_store.h"
#include "webhooks.h"
#include "webhooks_dialog.h"
#include "database.h"
#include "job_options.h"
#include "job_queue.h"
#include "list_of_job_options.h"
#include "main_window.h"
#include "check_dialog.h"
#include "dedupe_dialog.h"
#include "job_registry.h"
#include "job_widget.h"
#include "mount_dialog.h"
#include "remote_settings_dialog.h"
#include "running_job.h"
#include "serve_dialog.h"
#include "transfer_dialog.h"
#include "progress_dialog.h"
#include "remote_widget.h"
#include "utils.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QSpinBox>
#include <QPushButton>
#include <QTableWidget>
#include <QInputDialog>
#include <QTreeView>
#include <QListWidget>
#include <QMessageBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QtTest>

#include <memory>

// The window, clicked as a person would: the same buttons, the same boxes
// asking "are you sure?", answered one way and then the other. The web page
// has tests/ui/check_ui.py for this; this is the window's.
//
// What the API tests cannot see is here: a button that does nothing, a
// question that is never asked, a list that is not redrawn.
class TestWindow : public QObject {
  Q_OBJECT

private:
  static QString appDir() {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return QCoreApplication::applicationDirPath();
#else
    return QDir(QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME")))
        .filePath("rclone-browser");
#endif
  }

  static QString iniPath() {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return QDir(appDir()).filePath(
        QFileInfo(QCoreApplication::applicationFilePath()).baseName() + ".ini");
#else
    return QDir(appDir()).filePath("rclone-browser.ini");
#endif
  }

  std::unique_ptr<QTemporaryDir> mScratch;
  std::unique_ptr<QTemporaryDir> mFolder;
  std::unique_ptr<MainWindow> mWindow;

  template <typename T> T *find(const char *name) {
    T *found = mWindow->findChild<T *>(QLatin1String(name));
    if (found == nullptr) {
      qWarning() << "the window has no" << name;
    }
    return found;
  }

  // A task that never runs here: the queue is paused throughout.
  JobOptions *makeTask(const QString &name) {
    auto *task = new JobOptions(false);
    task->description = name;
    task->operation = JobOptions::Copy;
    task->jobType = JobOptions::Upload;
    task->source = mFolder->path();
    task->dest = QStringLiteral("fake:somewhere");
    task->isFolder = true;
    task->uniqueId = QUuid::createUuid();
    ListOfJobOptions::getInstance()->Persist(task);
    return task;
  }

  // The next box the window asks with is answered with `which`; what it
  // asked comes back. A box nobody answers would hang the test, so after
  // ten seconds whatever is open is closed, and the question is "".
  struct Asked {
    QString text;
    bool seen = false;
    bool given_up = false; // the test it was for has ended
  };
  // Every box still being waited for. A wait outlives its test otherwise,
  // and answers the next test's box for it.
  QList<std::shared_ptr<Asked>> mWaiting;
  std::shared_ptr<Asked> answerNextBox(QMessageBox::StandardButton which) {
    auto asked = std::make_shared<Asked>();
    mWaiting << asked;
    auto *timer = new QTimer(this);
    auto *started = new QElapsedTimer;
    started->start();
    timer->setInterval(50);
    connect(timer, &QTimer::timeout, this, [=]() {
      if (asked->given_up) {
        timer->stop();
        timer->deleteLater();
        delete started;
        return;
      }
      if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
        asked->text = box->text();
        asked->seen = true;
        if (QAbstractButton *button = box->button(which)) {
          button->click();
        } else {
          box->reject();
        }
      } else if (started->elapsed() < 10000) {
        return;
      } else if (QWidget *other = QApplication::activeModalWidget()) {
        other->close();
      }
      timer->stop();
      timer->deleteLater();
      delete started;
    });
    timer->start();
    return asked;
  }

  // The next QInputDialog is answered with `text` (or Cancel when null);
  // what it asked comes back.
  std::shared_ptr<Asked> typeIntoNextBox(const QString &text) {
    auto asked = std::make_shared<Asked>();
    mWaiting << asked;
    auto *timer = new QTimer(this);
    auto *started = new QElapsedTimer;
    started->start();
    timer->setInterval(50);
    connect(timer, &QTimer::timeout, this, [=]() {
      if (asked->given_up) {
        timer->stop();
        timer->deleteLater();
        delete started;
        return;
      }
      if (auto *box = qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
        asked->text = box->labelText();
        asked->seen = true;
        if (text.isNull()) {
          box->reject();
        } else {
          box->setTextValue(text);
          box->accept();
        }
      } else if (started->elapsed() < 10000) {
        return;
      } else if (QWidget *other = QApplication::activeModalWidget()) {
        other->close();
      }
      timer->stop();
      timer->deleteLater();
      delete started;
    });
    timer->start();
    return asked;
  }

  // Every rclone command the fake was given since the last look, one per
  // line, its arguments separated by tabs (FAKE_RCLONE_LOG).
  QStringList sent() {
    QFile file(mLog);
    QStringList lines;
    if (file.open(QIODevice::ReadOnly)) {
      lines = QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
      file.close();
      file.remove();
    }
    return lines;
  }
  static bool anyStartsWith(const QStringList &lines, const QString &start) {
    for (const QString &line : lines) {
      if (line.startsWith(start)) {
        return true;
      }
    }
    return false;
  }

  RemoteWidget *mRemote = nullptr;
  QString mLog;

  QTreeView *tree() { return mRemote->findChild<QTreeView *>(QStringLiteral("tree")); }
  QToolButton *remoteButton(const char *name) {
    return mRemote->findChild<QToolButton *>(QLatin1String(name));
  }

  // The row of the folder being shown whose name is `name`, chosen alone.
  bool choose(const QString &name) {
    QTreeView *view = tree();
    QAbstractItemModel *model = view->model();
    const QModelIndex top = model->index(0, 0, view->rootIndex());
    for (const QModelIndex &parent : {view->rootIndex(), top}) {
      for (int r = 0; r < model->rowCount(parent); ++r) {
        const QModelIndex at = model->index(r, 0, parent);
        if (model->data(at).toString() == name) {
          view->selectionModel()->select(
              at, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
          view->setCurrentIndex(at);
          return true;
        }
      }
    }
    return false;
  }

  // The next modal box of any kind is handed to `answer`, which fills it in
  // and closes it. A box that never comes is not waited for past ten
  // seconds; `seen` says whether it came.
  std::shared_ptr<Asked> onNextBox(std::function<void(QWidget *)> answer) {
    auto asked = std::make_shared<Asked>();
    mWaiting << asked;
    auto *timer = new QTimer(this);
    auto *started = new QElapsedTimer;
    started->start();
    timer->setInterval(50);
    connect(timer, &QTimer::timeout, this, [=]() {
      if (asked->given_up) {
        timer->stop();
        timer->deleteLater();
        delete started;
        return;
      }
      if (QWidget *box = QApplication::activeModalWidget()) {
        asked->text = box->windowTitle();
        asked->seen = true;
        answer(box);
      } else if (started->elapsed() < 10000) {
        return;
      }
      timer->stop();
      timer->deleteLater();
      delete started;
    });
    timer->start();
    return asked;
  }

  // A button by what it says, as a person finds it.
  template <typename Parent> static QAbstractButton *buttonSaying(Parent *in, const QString &text) {
    for (QAbstractButton *button : in->template findChildren<QAbstractButton *>()) {
      if (button->text().remove('&') == text) {
        return button;
      }
    }
    return nullptr;
  }

  static int rowSaying(QTableWidget *table, const QString &text) {
    for (int r = 0; r < table->rowCount(); ++r) {
      for (int c = 0; c < table->columnCount(); ++c) {
        if (table->item(r, c) && table->item(r, c)->text().contains(text)) {
          return r;
        }
      }
    }
    return -1;
  }

  static QStringList rows(QListWidget *list) {
    QStringList texts;
    for (int i = 0; i < list->count(); ++i) {
      texts << list->item(i)->text();
    }
    return texts;
  }

private slots:
  // Each test starts with nobody waiting to answer a box.
  void init() {
    for (const auto &asked : mWaiting) {
      asked->given_up = true;
    }
    mWaiting.clear();
  }

  void initTestCase() {
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    mScratch.reset(new QTemporaryDir);
    QVERIFY(mScratch->isValid());
    qputenv("XDG_CONFIG_HOME", mScratch->path().toLocal8Bit());
    QVERIFY(QDir().mkpath(appDir()));
#endif
    mFolder.reset(new QTemporaryDir);
    QVERIFY(mFolder->isValid());

    QFile ini(iniPath());
    QVERIFY(ini.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ini.close();
    QVERIFY2(IsPortableMode(), "portable mode did not take effect");

    Database::setPath(QDir(appDir()).filePath(QStringLiteral("window-test.db")));
    QFile::remove(Database::path());
    QVERIFY2(Database::connection().isOpen(), qPrintable(Database::lastError()));

    GetSettings()->setValue(QStringLiteral("Settings/queueStatus"), "false");
    GetSettings()->setValue(QStringLiteral("Settings/rclone"), QStringLiteral(RB_FAKE_RCLONE));
    // Nothing the window would otherwise ask about on its first opening.
    GetSettings()->setValue(QStringLiteral("Settings/checkRcloneUpdates"), false);
    GetSettings()->setValue(QStringLiteral("Settings/checkRcloneBrowserUpdates"), false);
    GetSettings()->sync();

    mLog = QDir(mFolder->path()).filePath(QStringLiteral("rclone-calls.txt"));
    qputenv("FAKE_RCLONE_LOG", QFile::encodeName(mLog));

    AppCore::instance().start();
    mWindow.reset(new MainWindow);
    mWindow->show();
    QVERIFY(QTest::qWaitForWindowExposed(mWindow.get()));
  }

  void cleanupTestCase() {
    mWindow.reset();
    const QString path = Database::path();
    Database::closeForThread();
    QFile::remove(path);
    QFile::remove(iniPath());
  }

  // rclone's remotes, in the window's list.
  void theWindowListsTheRemotes() {
    auto *remotes = find<QListWidget>("remotes");
    QVERIFY(remotes != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(rows(remotes).join(' ').contains(QLatin1String("fake")), 10000);
  }

  // The queue's Clean: asked first; No keeps everything, Yes empties it --
  // the list on screen and the queue itself.
  void cleaningTheQueueAsksAndEmptiesIt() {
    JobQueue::instance().pause();
    JobOptions *a = makeTask(QStringLiteral("window test a"));
    JobOptions *b = makeTask(QStringLiteral("window test b"));
    JobQueue::instance().enqueue(a->uniqueId.toString(), false);
    JobQueue::instance().enqueue(b->uniqueId.toString(), false);

    auto *list = find<QListWidget>("queueListWidget");
    auto *clean = find<QToolButton>("buttonPurgeQueue");
    QVERIFY(list != nullptr && clean != nullptr);
    QTRY_COMPARE(list->count(), 2);
    QVERIFY2(clean->isEnabled(), "Clean is greyed out with two waiting");

    auto no = answerNextBox(QMessageBox::No);
    clean->click();
    QTRY_VERIFY(no->seen);
    QVERIFY2(no->text.contains(QLatin1String("remove all")), qPrintable(no->text));
    QCOMPARE(list->count(), 2);
    QCOMPARE(JobQueue::instance().count(), 2);

    auto yes = answerNextBox(QMessageBox::Yes);
    clean->click();
    QTRY_VERIFY(yes->seen);
    QTRY_COMPARE(list->count(), 0);
    QCOMPARE(JobQueue::instance().count(), 0);

    ListOfJobOptions::getInstance()->Forget(a);
    ListOfJobOptions::getInstance()->Forget(b);
  }

  // Remove takes the chosen row out, whether the queue is running or not --
  // as the page's Remove does.
  void removeTakesOneOutOfAPausedQueue() {
    JobQueue::instance().pause();
    JobOptions *a = makeTask(QStringLiteral("window test keep"));
    JobOptions *b = makeTask(QStringLiteral("window test remove"));
    JobQueue::instance().enqueue(a->uniqueId.toString(), false);
    JobQueue::instance().enqueue(b->uniqueId.toString(), false);

    auto *list = find<QListWidget>("queueListWidget");
    auto *remove = find<QToolButton>("buttonRemoveFromQueue");
    QVERIFY(list != nullptr && remove != nullptr);
    QTRY_COMPARE(list->count(), 2);

    list->setCurrentRow(1);
    QVERIFY2(remove->isEnabled(), "Remove is greyed out with a row chosen");
    remove->click();
    QTRY_COMPARE_WITH_TIMEOUT(JobQueue::instance().count(), 1, 3000);
    QTRY_COMPARE(list->count(), 1);
    QVERIFY(list->item(0)->text().contains(QLatin1String("keep")));

    JobQueue::instance().clear();
    ListOfJobOptions::getInstance()->Forget(a);
    ListOfJobOptions::getInstance()->Forget(b);
  }

  // Open: the remote's own tab, listing what rclone says is there.
  void openingARemoteListsItsFiles() {
    auto *remotes = find<QListWidget>("remotes");
    auto *tabs = find<QTabWidget>("tabs");
    auto *open = find<QAbstractButton>("open");
    QVERIFY(remotes != nullptr && tabs != nullptr && open != nullptr);
    QTRY_VERIFY(remotes->count() > 0);
    remotes->setCurrentRow(0);
    remotes->item(0)->setSelected(true);
    const int before = tabs->count();
    open->click();
    QTRY_COMPARE(tabs->count(), before + 1);
    mRemote = qobject_cast<RemoteWidget *>(tabs->widget(tabs->count() - 1));
    QVERIFY2(mRemote != nullptr, "the new tab is not a remote");
    tabs->setCurrentWidget(mRemote);
    QVERIFY(tree() != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(choose(QStringLiteral("item-1")), 10000);
  }

  // New folder: asks for the name, and rclone is told to make it.
  void newFolderAsksForANameAndMakesIt() {
    QVERIFY(mRemote != nullptr);
    sent();
    QVERIFY(choose(QStringLiteral("item-0")));
    auto asked = typeIntoNextBox(QStringLiteral("made-by-test"));
    remoteButton("buttonMkdir")->click();
    QTRY_VERIFY(asked->seen);
    QVERIFY2(asked->text.contains(QLatin1String("Create folder")), qPrintable(asked->text));
    QStringList lines;
    QTRY_VERIFY((lines += sent(), anyStartsWith(lines, QStringLiteral("mkdir\t"))));
    QVERIFY2(lines.join('\n').contains(QLatin1String("made-by-test")), qPrintable(lines.join(" | ")));

    // Cancel: nothing is made.
    auto cancelled = typeIntoNextBox(QString());
    remoteButton("buttonMkdir")->click();
    QTRY_VERIFY(cancelled->seen);
    QTest::qWait(300);
    QVERIFY(!anyStartsWith(sent(), QStringLiteral("mkdir")));
  }

  // Rename: asks with the old name, and rclone moves it to the new one.
  void renameAsksAndMoves() {
    QVERIFY(mRemote != nullptr);
    sent();
    QVERIFY(choose(QStringLiteral("item-1")));
    auto asked = typeIntoNextBox(QStringLiteral("renamed-by-test"));
    remoteButton("buttonRename")->click();
    QTRY_VERIFY(asked->seen);
    QVERIFY2(asked->text.contains(QLatin1String("New name")), qPrintable(asked->text));
    QStringList lines;
    QTRY_VERIFY((lines += sent(), anyStartsWith(lines, QStringLiteral("moveto\t"))));
    QVERIFY2(lines.join('\n').contains(QLatin1String("renamed-by-test")), qPrintable(lines.join(" | ")));
  }

  // Delete: asked first; No sends nothing, Yes deletes.
  void deleteAsksFirst() {
    QVERIFY(mRemote != nullptr);
    sent();
    QTRY_VERIFY(choose(QStringLiteral("item-3")));

    auto no = answerNextBox(QMessageBox::No);
    remoteButton("buttonPurge")->click();
    QTRY_VERIFY(no->seen);
    QVERIFY2(no->text.contains(QLatin1String("delete")), qPrintable(no->text));
    QTest::qWait(300);
    QVERIFY(!anyStartsWith(sent(), QStringLiteral("deletefile")));

    QTRY_VERIFY(choose(QStringLiteral("item-3")));
    auto yes = answerNextBox(QMessageBox::Yes);
    remoteButton("buttonPurge")->click();
    QTRY_VERIFY(yes->seen);
    QStringList lines;
    QTRY_VERIFY((lines += sent(), anyStartsWith(lines, QStringLiteral("deletefile\t"))));
    // Whatever box the deleting showed is closed once it is done.
    for (QWidget *w : QApplication::topLevelWidgets()) {
      if (w->inherits("DeleteProgressDialog") && w->isVisible()) {
        w->close();
      }
    }
  }

  // Size, Tree and Info: each asks rclone and shows the answer in a box.
  void sizeTreeAndInfoAskRclone() {
    QVERIFY(mRemote != nullptr);
    const struct {
      const char *button;
      const char *command;
    } asks[] = {{"buttonSize", "size\t"}, {"buttonTree", "tree\t"}, {"buttonInfo", "about\t"}};
    for (const auto &ask : asks) {
      sent();
      QToolButton *button = remoteButton(ask.button);
      QVERIFY2(button != nullptr, ask.button);
      // A folder still being read has these greyed out, and they are looked
      // at again only when the choice changes -- so it is chosen again, as a
      // person would click it again, until it is ready.
      QTRY_VERIFY2((tree()->selectionModel()->clearSelection(),
                    choose(QStringLiteral("item-0")) && button->isEnabled()),
                   ask.button);
      button->click();
      QStringList lines;
      QTRY_VERIFY2((lines += sent(), anyStartsWith(lines, QLatin1String(ask.command))),
                   ask.button);
      ProgressDialog *shown = nullptr;
      QTRY_VERIFY([&]() {
        for (QWidget *w : QApplication::topLevelWidgets()) {
          if (auto *p = qobject_cast<ProgressDialog *>(w); p && p->isVisible()) {
            shown = p;
          }
        }
        return shown != nullptr;
      }());
      shown->close();
      shown->deleteLater();
      QTest::qWait(100);
    }
  }

  // API keys: made with a name and a level, shown once, listed; taken back
  // only after asking.
  void apiKeysAreMadeAndTakenBack() {
    ApiKeysDialog dialog(mWindow.get());
    dialog.show();
    auto *name = dialog.findChild<QLineEdit *>(QStringLiteral("keyName"));
    auto *scope = dialog.findChild<QComboBox *>(QStringLiteral("keyScope"));
    auto *made = dialog.findChild<QLineEdit *>(QStringLiteral("keyMade"));
    auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("keys"));
    QVERIFY(name && scope && made && table);

    name->setText(QStringLiteral("window test key"));
    scope->setCurrentIndex(scope->findData(QStringLiteral("read")));
    buttonSaying(&dialog, QStringLiteral("Make a key"))->click();
    QVERIFY2(made->text().size() > 20, "no key shown");
    const int row = rowSaying(table, QStringLiteral("window test key"));
    QVERIFY2(row >= 0, "the key is not listed");
    QCOMPARE(table->item(row, 1)->text(), QStringLiteral("read"));

    table->selectRow(row);
    auto no = answerNextBox(QMessageBox::No);
    buttonSaying(&dialog, QStringLiteral("Take back"))->click();
    QTRY_VERIFY(no->seen);
    QVERIFY(rowSaying(table, QStringLiteral("window test key")) >= 0);

    table->selectRow(rowSaying(table, QStringLiteral("window test key")));
    auto yes = answerNextBox(QMessageBox::Yes);
    buttonSaying(&dialog, QStringLiteral("Take back"))->click();
    QTRY_VERIFY(yes->seen);
    QTRY_COMPARE(rowSaying(table, QStringLiteral("window test key")), -1);
    for (const ApiAuth::Key &key : ApiAuth::Keys::instance().list()) {
      QVERIFY(key.name != QLatin1String("window test key"));
    }
  }

  // Webhooks: added through its form, listed, removed only after asking.
  void webhooksAreAddedAndRemoved() {
    WebhooksDialog dialog(mWindow.get());
    dialog.show();
    auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("hooks"));
    QVERIFY(table != nullptr);

    auto form = onNextBox([](QWidget *box) {
      box->findChild<QLineEdit *>(QStringLiteral("hookUrl"))
          ->setText(QStringLiteral("http://127.0.0.1:9/window-test"));
      box->findChild<QLineEdit *>(QStringLiteral("hookName"))
          ->setText(QStringLiteral("window test hook"));
      box->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    });
    buttonSaying(&dialog, QStringLiteral("Add…"))->click();
    QTRY_VERIFY(form->seen);
    QTRY_VERIFY(rowSaying(table, QStringLiteral("window test hook")) >= 0);

    table->selectRow(rowSaying(table, QStringLiteral("window test hook")));
    auto no = answerNextBox(QMessageBox::No);
    buttonSaying(&dialog, QStringLiteral("Remove"))->click();
    QTRY_VERIFY(no->seen);
    QVERIFY2(no->text.contains(QLatin1String("window test hook")), qPrintable(no->text));
    QVERIFY(rowSaying(table, QStringLiteral("window test hook")) >= 0);

    table->selectRow(rowSaying(table, QStringLiteral("window test hook")));
    auto yes = answerNextBox(QMessageBox::Yes);
    buttonSaying(&dialog, QStringLiteral("Remove"))->click();
    QTRY_VERIFY(yes->seen);
    QTRY_COMPARE(rowSaying(table, QStringLiteral("window test hook")), -1);
    QVERIFY(Webhooks::instance().list().isEmpty());
  }

  // A backup: a folder, a place, a time -- a task and a schedule that runs it.
  void aBackupIsATaskAndASchedule() {
    BackupDialog dialog(mWindow.get());
    dialog.show();
    auto *source = dialog.findChild<QLineEdit *>(QStringLiteral("backupSource"));
    auto *dest = dialog.findChild<QLineEdit *>(QStringLiteral("backupDest"));
    auto *name = dialog.findChild<QLineEdit *>(QStringLiteral("backupName"));
    QVERIFY(source && dest && name);
    source->setText(mFolder->path());
    dest->setText(QStringLiteral("fake:backups/window-test"));
    name->setText(QStringLiteral("window test backup"));

    const int schedulesBefore = SchedulerStore::instance().count();
    auto told = answerNextBox(QMessageBox::Ok);
    dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    QTRY_VERIFY(told->seen);
    QVERIFY2(told->text.contains(QLatin1String("a task now")), qPrintable(told->text));
    QTRY_COMPARE(dialog.result(), int(QDialog::Accepted));

    JobOptions *task = nullptr;
    for (JobOptions *one : ListOfJobOptions::getInstance()->getTasks()) {
      if (one->description == QLatin1String("window test backup")) {
        task = one;
      }
    }
    QVERIFY2(task != nullptr, "no task made");
    QCOMPARE(task->dest, QStringLiteral("fake:backups/window-test"));
    QCOMPARE(SchedulerStore::instance().count(), schedulesBefore + 1);
    const Schedule made = SchedulerStore::instance().schedules().last();
    QCOMPARE(made.taskId, task->uniqueId.toString());

    SchedulerStore::instance().remove(made.id);
    ListOfJobOptions::getInstance()->Forget(task);
  }

  // The transfer form: what is ticked and typed is what rclone is told.
  void theTransferFormBecomesTheCommand() {
    TransferDialog dialog(false, false, QStringLiteral("fake"), QDir(QStringLiteral("films")), true,
                          QStringLiteral("fake"), QStringLiteral("main"), false, QStringList(),
                          mWindow.get());
    dialog.show();
    auto *source = dialog.findChild<QLineEdit *>(QStringLiteral("textSource"));
    auto *copy = dialog.findChild<QRadioButton *>(QStringLiteral("rbCopy"));
    auto *exclude = dialog.findChild<QPlainTextEdit *>(QStringLiteral("textExclude"));
    auto *bandwidth = dialog.findChild<QLineEdit *>(QStringLiteral("textBandwidth"));
    auto *noTraverse = dialog.findChild<QCheckBox *>(QStringLiteral("noTraverse"));
    QVERIFY(source && copy && exclude && bandwidth && noTraverse);

    source->setText(mFolder->path());
    copy->setChecked(true);
    exclude->setPlainText(QStringLiteral("*.nfo"));
    bandwidth->setText(QStringLiteral("2M"));
    noTraverse->setChecked(true);

    const QStringList args = dialog.getOptions();
    const QString line = args.join(' ');
    QVERIFY2(args.value(0) == QLatin1String("copy"), qPrintable(line));
    QVERIFY2(args.contains(QStringLiteral("--no-traverse")), qPrintable(line));
    QVERIFY2(line.contains(QLatin1String("--bwlimit 2M")), qPrintable(line));
    QVERIFY2(line.contains(QLatin1String("--exclude *.nfo")), qPrintable(line));
    QVERIFY2(args.contains(QDir::toNativeSeparators(mFolder->path())) ||
                 args.contains(mFolder->path()),
             qPrintable(line));
    QVERIFY2(line.contains(QLatin1String("fake:films")), qPrintable(line));
  }

  // The mount form: the same, for a mount -- and the script it names.
  void theMountFormBecomesTheCommand() {
    MountDialog dialog(QStringLiteral("fake"), QDir(QStringLiteral("films")), QStringLiteral("fake"),
                       QStringLiteral("main"), mWindow.get());
    dialog.show();
    auto *readOnly = dialog.findChild<QCheckBox *>(QStringLiteral("cb_readOnly"));
    auto *cache = dialog.findChild<QComboBox *>(QStringLiteral("combob_cacheLevel"));
    auto *volume = dialog.findChild<QLineEdit *>(QStringLiteral("le_volumeName"));
    auto *script = dialog.findChild<QLineEdit *>(QStringLiteral("le_mountScript"));
    QVERIFY(readOnly && cache && volume && script);

    readOnly->setChecked(true);
    // Each level's flag is its tooltip; the words shown are not the flag.
    const int writes = cache->findData(QStringLiteral("--vfs-cache-mode writes"), Qt::ToolTipRole);
    QVERIFY2(writes >= 0, "no cache level called writes");
    cache->setCurrentIndex(writes);
    volume->setText(QStringLiteral("Films"));
    script->setText(QDir(mFolder->path()).filePath(QStringLiteral("after.cmd")));

    const QString line = dialog.getOptions().join(' ');
    QVERIFY2(line.contains(QLatin1String("--read-only")), qPrintable(line));
    QVERIFY2(line.contains(QLatin1String("--vfs-cache-mode writes")), qPrintable(line));
    QVERIFY2(line.contains(QLatin1String("Films")), qPrintable(line));
    QCOMPARE(dialog.getScript(), script->text());
  }

  // Check and Dedupe: what is ticked is what rclone is told.
  void checkAndDedupeBecomeCommands() {
    CheckDialog check(QStringLiteral("fake"), QDir(QStringLiteral("films")), QStringLiteral("fake"),
                      mWindow.get());
    check.findChild<QCheckBox *>(QStringLiteral("oneWay"))->setChecked(true);
    check.findChild<QCheckBox *>(QStringLiteral("sizeOnly"))->setChecked(true);
    const QString line = check.getOptions().join(' ');
    QVERIFY2(line.contains(QLatin1String("check")), qPrintable(line));
    QVERIFY2(line.contains(QLatin1String("--one-way")), qPrintable(line));
    QVERIFY2(line.contains(QLatin1String("--size-only")), qPrintable(line));

    DedupeDialog dedupe(QStringLiteral("fake"), QDir(QStringLiteral("films")), QStringLiteral("fake"),
                        mWindow.get());
    dedupe.findChild<QCheckBox *>(QStringLiteral("cb_newest"))->setChecked(true);
    const QString again = dedupe.getOptions().join(' ');
    QVERIFY2(again.contains(QLatin1String("dedupe")), qPrintable(again));
    QVERIFY2(again.contains(QLatin1String("newest")), qPrintable(again));
  }

  // Serve: the protocol, the port and the login chosen are what it serves
  // with -- and the password is kept for rclone's environment.
  void theServeFormIsWhatIsServed() {
    ServeDialog dialog(QStringLiteral("fake:films"), mWindow.get());
    dialog.show();
    auto *protocol = dialog.findChild<QComboBox *>(QStringLiteral("serveProtocol"));
    auto *port = dialog.findChild<QSpinBox *>(QStringLiteral("servePort"));
    auto *user = dialog.findChild<QLineEdit *>(QStringLiteral("serveUser"));
    auto *password = dialog.findChild<QLineEdit *>(QStringLiteral("servePassword"));
    QVERIFY(protocol && port && user && password);
    const int sftp = protocol->findData(QStringLiteral("sftp"));
    QVERIFY2(sftp >= 0, "no sftp to choose");
    protocol->setCurrentIndex(sftp);
    port->setValue(18922);
    user->setText(QStringLiteral("someone"));
    password->setText(QStringLiteral("a secret"));
    const RemoteOps::ServeOptions chosen = dialog.options();
    QCOMPARE(chosen.protocol, QStringLiteral("sftp"));
    QCOMPARE(int(chosen.port), 18922);
    QCOMPARE(chosen.user, QStringLiteral("someone"));
    QCOMPARE(chosen.password, QStringLiteral("a secret"));
  }

  // A remote from the window: made, changed, deleted -- each the rclone
  // command it should be, against the fake (no real rclone.conf is touched).
  void aRemoteIsMadeChangedAndDeletedFromTheWindow() {
    {
      RemoteSettingsDialog made(QString(), mWindow.get());
      made.show();
      auto *kind = made.findChild<QComboBox *>(QStringLiteral("remoteKind"));
      auto *name = made.findChild<QLineEdit *>(QStringLiteral("remoteName"));
      QVERIFY(kind && name);
      QTRY_VERIFY_WITH_TIMEOUT(kind->findText(QStringLiteral("fake"), Qt::MatchStartsWith) >= 0, 10000);
      kind->setCurrentIndex(kind->findText(QStringLiteral("fake"), Qt::MatchStartsWith));
      QLineEdit *token = nullptr;
      QTRY_VERIFY((token = made.findChild<QLineEdit *>(QStringLiteral("access_token"))) != nullptr);
      name->setText(QStringLiteral("made-in-window"));
      token->setText(QStringLiteral("tok-123"));
      sent();
      buttonSaying(&made, QStringLiteral("Save"))->click();
      QStringList lines;
      QTRY_VERIFY((lines += sent(), anyStartsWith(lines, QStringLiteral("config\tcreate\tmade-in-window\tfake"))));
      QVERIFY2(lines.join('\n').contains(QLatin1String("tok-123")), qPrintable(lines.join(" | ")));
      // Done when the box has closed itself: what it started has answered.
      QTRY_VERIFY_WITH_TIMEOUT(!made.isVisible(), 10000);
    }
    {
      RemoteSettingsDialog changed(QStringLiteral("fake"), mWindow.get());
      changed.show();
      QLineEdit *chunk = nullptr;
      QTRY_VERIFY_WITH_TIMEOUT((chunk = changed.findChild<QLineEdit *>(QStringLiteral("chunk_size"))) != nullptr,
                               10000);
      chunk->setText(QStringLiteral("2Gi")); // the fake has 1Gi: the same would change nothing
      sent();
      buttonSaying(&changed, QStringLiteral("Save"))->click();
      QStringList lines;
      QTRY_VERIFY((lines += sent(), anyStartsWith(lines, QStringLiteral("config\tupdate\tfake"))));
      QVERIFY2(lines.join('\n').contains(QLatin1String("chunk_size")), qPrintable(lines.join(" | ")));
      // The token that was set is not sent again, unasked.
      QVERIFY2(!lines.join('\n').contains(QLatin1String("access_token")), qPrintable(lines.join(" | ")));
      QTRY_VERIFY_WITH_TIMEOUT(!changed.isVisible(), 10000);
    }
    {
      RemoteSettingsDialog doomed(QStringLiteral("fake"), mWindow.get());
      doomed.show();
      QAbstractButton *drop = nullptr;
      QTRY_VERIFY((drop = buttonSaying(&doomed, QStringLiteral("Delete this remote…"))) != nullptr &&
                  drop->isEnabled());
      sent();
      auto no = answerNextBox(QMessageBox::No);
      drop->click();
      QTRY_VERIFY(no->seen);
      QTest::qWait(300);
      QVERIFY(!anyStartsWith(sent(), QStringLiteral("config\tdelete")));
      // Yes to whatever it asks: whether to, and whether even though tasks use it.
      auto yes = answerNextBox(QMessageBox::Yes);
      drop->click();
      QTRY_VERIFY(yes->seen);
      auto again = answerNextBox(QMessageBox::Yes);
      QStringList lines;
      QTRY_VERIFY_WITH_TIMEOUT((lines += sent(), anyStartsWith(lines, QStringLiteral("config\tdelete\tfake"))),
                               15000);
      QTRY_VERIFY_WITH_TIMEOUT(!doomed.isVisible(), 10000);
    }
  }

  // Save, then Cancel straight away: the box goes while rclone is still
  // making the remote. That took the window down: the process was the box's
  // child, and its last word reached a box already half taken apart.
  void cancellingWhileSavingDoesNotTakeTheWindowDown() {
    auto *box = new RemoteSettingsDialog(QString(), mWindow.get());
    box->show();
    auto *kind = box->findChild<QComboBox *>(QStringLiteral("remoteKind"));
    QTRY_VERIFY_WITH_TIMEOUT(kind->findText(QStringLiteral("fake"), Qt::MatchStartsWith) >= 0, 10000);
    kind->setCurrentIndex(kind->findText(QStringLiteral("fake"), Qt::MatchStartsWith));
    QLineEdit *token = nullptr;
    QTRY_VERIFY((token = box->findChild<QLineEdit *>(QStringLiteral("access_token"))) != nullptr);
    box->findChild<QLineEdit *>(QStringLiteral("remoteName"))->setText(QStringLiteral("cancelled-mid-save"));
    token->setText(QStringLiteral("tok"));
    sent();
    buttonSaying(box, QStringLiteral("Save"))->click();
    buttonSaying(box, QStringLiteral("Cancel"))->click();
    delete box; // as the window's dialog.exec() leaves it: gone at once
    // Whatever rclone was doing finishes without anyone to tell; the window
    // is still here. (Cancelled before rclone was asked to make it, nothing
    // is made: the steps after the one under way are not taken.)
    QTest::qWait(2000);
    QVERIFY(mWindow->isVisible());
  }

  // Preferences: what is typed there is what is kept.
  void preferencesAreKept() {
    auto *menuItem = mWindow->findChild<QAction *>(QStringLiteral("preferences"));
    QVERIFY(menuItem != nullptr);
    auto filled = onNextBox([](QWidget *box) {
      box->findChild<QLineEdit *>(QStringLiteral("defaultRcloneOptions"))
          ->setText(QStringLiteral("--fast-list"));
      box->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    menuItem->trigger();
    QTRY_VERIFY(filled->seen);
    QCOMPARE(GetSettings()->value(QStringLiteral("Settings/defaultRcloneOptions")).toString(),
             QStringLiteral("--fast-list"));
    GetSettings()->setValue(QStringLiteral("Settings/defaultRcloneOptions"), QString());
  }

  // A job's card: Stop asks first; No leaves it running, Yes stops it.
  void aJobsCardStopsItAfterAsking() {
    qputenv("FAKE_RCLONE_SLEEP_MS", "20000");
    JobOptions *task = makeTask(QStringLiteral("window test slow"));
    RunningJob *job = StartTask(task, QStringLiteral("window"), QUuid::createUuid().toString());
    QVERIFY(job != nullptr);
    JobWidget *card = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT([&]() {
      for (JobWidget *one : mWindow->findChildren<JobWidget *>()) {
        if (one->getRequestId() == job->requestId()) {
          card = one;
        }
      }
      return card != nullptr && job->isRunning();
    }(), 10000);
    // Where a person would see it: the Jobs tab.
    find<QTabWidget>("tabs")->setCurrentIndex(1);
    auto *stop = card->findChild<QToolButton *>(QStringLiteral("cancel"));
    QVERIFY(stop != nullptr);
    QVERIFY2(card->isRunning, qPrintable(QStringLiteral("the card says it is not running; the job is %1")
                                             .arg(job->isRunning() ? "running" : job->finalStatus())));
    QVERIFY2(stop->isVisible() && stop->isEnabled(), "its Stop button cannot be pressed");

    QSignalSpy pressed(stop, &QToolButton::clicked);
    auto no = answerNextBox(QMessageBox::No);
    QTest::mouseClick(stop, Qt::LeftButton);
    QVERIFY2(pressed.count() == 1, "the button did not say it was clicked");
    QTRY_VERIFY(no->seen);
    QVERIFY2(no->text.contains(QLatin1String("stop")), qPrintable(no->text));
    QTest::qWait(300);
    QVERIFY(job->isRunning());

    auto yes = answerNextBox(QMessageBox::Yes);
    stop->click();
    QTRY_VERIFY(yes->seen);
    QTRY_VERIFY_WITH_TIMEOUT(!job->isRunning(), 10000);
    qunsetenv("FAKE_RCLONE_SLEEP_MS");
    ListOfJobOptions::getInstance()->Forget(task);
  }

  // Deleting a task: asked first, with its name; No keeps it, Yes deletes it.
  void deletingATaskAsksFirst() {
    JobOptions *task = makeTask(QStringLiteral("window test doomed"));
    const QString id = task->uniqueId.toString();
    // The list is drawn from the tasks when they change.
    QMetaObject::invokeMethod(mWindow.get(), "listTasks");

    auto *list = find<QListWidget>("tasksListWidget");
    auto *remove = find<QToolButton>("buttonDeleteTask");
    QVERIFY(list != nullptr && remove != nullptr);
    QListWidgetItem *row = nullptr;
    QTRY_VERIFY((row = [&]() -> QListWidgetItem * {
      for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->text().contains(QLatin1String("doomed"))) {
          return list->item(i);
        }
      }
      return nullptr;
    }()) != nullptr);

    list->clearSelection();
    row->setSelected(true);
    list->setCurrentItem(row);
    QVERIFY2(remove->isEnabled(), "Delete is greyed out with a task chosen");

    auto no = answerNextBox(QMessageBox::No);
    remove->click();
    QTRY_VERIFY(no->seen);
    QVERIFY2(no->text.contains(QLatin1String("doomed")), qPrintable(no->text));
    QVERIFY(ListOfJobOptions::getInstance()->find(id) != nullptr);

    auto yes = answerNextBox(QMessageBox::Yes);
    remove->click();
    QTRY_VERIFY(yes->seen);
    QTRY_VERIFY(ListOfJobOptions::getInstance()->find(id) == nullptr);
  }
};

QTEST_MAIN(TestWindow)
#include "test_window.moc"
