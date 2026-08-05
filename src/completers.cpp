#include "completers.h"
#include "rclone_flags.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QFileSystemModel>
#include <QFontDatabase>
#include <QStandardItemModel>
#include <QKeyEvent>
#include <QTextBlock>

#include <functional>
#include <memory>

namespace {

// The flag name itself, kept apart from what the popup displays.
constexpr int kFlagNameRole = Qt::UserRole + 1;

// How wide the name column is padded to before the description starts. Long
// enough for "--multi-thread-chunk-size SizeSuffix" without wrapping.
constexpr int kNameColumn = 44;

// The word the cursor is inside, as a [start, end) range over the text.
QPair<int, int> WordUnderCursor(const QString &text, int cursor) {
  int start = cursor;
  while (start > 0 && !text.at(start - 1).isSpace()) {
    --start;
  }
  int end = cursor;
  while (end < text.size() && !text.at(end).isSpace()) {
    ++end;
  }
  return {start, end};
}

QStandardItemModel *BuildModel(QObject *parent) {
  auto *model = new QStandardItemModel(parent);

  for (const RcloneFlag &flag : RcloneFlagRegistry::instance().flags()) {
    QString label = flag.name;
    if (flag.takesValue()) {
      label += QLatin1Char(' ') + flag.type;
    }

    // The description rides along in the same string so it is searchable and
    // so no custom delegate is needed: typing "chunk" finds
    // --teldrive-chunk-size, and typing "bandwidth" finds --bwlimit.
    auto *item = new QStandardItem(label.leftJustified(kNameColumn) +
                                   QLatin1Char(' ') + flag.description);
    item->setData(flag.insertion(), kFlagNameRole);

    QString tip = flag.description;
    if (!flag.shortName.isEmpty()) {
      tip = flag.shortName + QStringLiteral(", ") + flag.name +
            QStringLiteral("\n") + tip;
    }
    item->setToolTip(tip + QStringLiteral("\n\nGroup: ") + flag.group);

    model->appendRow(item);
  }

  return model;
}

// Everything about a flag completer that does not depend on which kind of
// editor it is attached to.
QCompleter *MakeFlagCompleter(QWidget *edit) {
  auto *completer = new QCompleter(edit);
  completer->setWidget(edit);
  completer->setCompletionMode(QCompleter::PopupCompletion);
  completer->setCaseSensitivity(Qt::CaseInsensitive);
  // Contains, not starts-with: half the value is finding the flag whose name
  // you cannot remember. "--chunk" turning up both --teldrive-chunk-size and
  // --drive-chunk-size is the point.
  completer->setFilterMode(Qt::MatchContains);

  // The popup lines up two columns of text, which only works in a font where
  // every character is the same width.
  const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  completer->popup()->setFont(fixed);
  completer->popup()->setMinimumWidth(QFontMetrics(fixed).horizontalAdvance(
      QString(kNameColumn + 56, QLatin1Char('m'))));

  auto refresh = [completer, edit]() {
    completer->setModel(BuildModel(completer));
    // Rebuilding the model closes the popup; put it back if the user was in
    // the middle of typing when the query landed.
    if (edit->hasFocus() && !completer->completionPrefix().isEmpty()) {
      completer->complete();
    }
  };
  refresh();
  QObject::connect(&RcloneFlagRegistry::instance(),
                   &RcloneFlagRegistry::flagsChanged, edit, refresh);
  RcloneFlagRegistry::instance().ensureLoaded();

  return completer;
}

// Makes Enter pick the highlighted flag in a multi-line field.
//
// QCompleter offers the key to the editor before acting on it, and a
// QPlainTextEdit accepts Return -- so the key was swallowed as a newline and
// the popup did nothing. A QLineEdit does not accept it, which is why the
// single-line fields need none of this. Measured, not assumed: see V-15.
//
// Filters run last-installed-first, and QCompleter has already installed its
// own on the popup by the time this goes on, so this one gets there first.
// Only Return, Enter and Tab: the arrow keys never reach the editor, and
// QCompleter handles Escape itself.
class PopupCommitFilter : public QObject {
public:
  PopupCommitFilter(QCompleter *completer,
                    std::function<void(const QModelIndex &)> commit)
      : QObject(completer), mCompleter(completer), mCommit(std::move(commit)) {}

protected:
  bool eventFilter(QObject *, QEvent *event) override {
    if (event->type() != QEvent::KeyPress) {
      return false;
    }
    const int key = static_cast<QKeyEvent *>(event)->key();
    if (key != Qt::Key_Return && key != Qt::Key_Enter && key != Qt::Key_Tab) {
      return false;
    }

    const QModelIndex index = mCompleter->popup()->currentIndex();
    mCompleter->popup()->hide();
    if (index.isValid()) {
      mCommit(index);
    }
    return true;
  }

private:
  QCompleter *mCompleter;
  std::function<void(const QModelIndex &)> mCommit;
};

// True when the word being typed is a flag name and not a value.
//
// Once past the "=" the text is a path, a size or a duration, and none of
// those are flags -- the popup would just be in the way.
bool WantsCompletion(const QString &word) {
  return word.startsWith(QLatin1Char('-')) && !word.contains(QLatin1Char('='));
}

} // namespace

void InstallRcloneFlagCompleter(QLineEdit *edit) {
  QCompleter *completer = MakeFlagCompleter(edit);

  QObject::connect(edit, &QLineEdit::textEdited, edit, [completer, edit]() {
    const auto [start, end] =
        WordUnderCursor(edit->text(), edit->cursorPosition());
    const QString word = edit->text().mid(start, end - start);

    if (!WantsCompletion(word)) {
      completer->popup()->hide();
      return;
    }

    completer->setCompletionPrefix(word);
    if (completer->completionCount() == 0) {
      completer->popup()->hide();
      return;
    }
    completer->complete();
  });

  QObject::connect(
      completer, QOverload<const QModelIndex &>::of(&QCompleter::activated),
      edit, [edit](const QModelIndex &index) {
        const QString insertion = index.data(kFlagNameRole).toString();
        if (insertion.isEmpty()) {
          return;
        }

        const auto [start, end] =
            WordUnderCursor(edit->text(), edit->cursorPosition());

        QString text = edit->text();
        text.replace(start, end - start, insertion);
        edit->setText(text);
        // After a boolean flag the next thing typed is another flag, so leave
        // a space; after "--flag=" the cursor belongs right where the value
        // goes.
        edit->setCursorPosition(start + insertion.size());
        if (!insertion.endsWith(QLatin1Char('='))) {
          edit->insert(QStringLiteral(" "));
        }
      });
}

void InstallRcloneFlagCompleter(QPlainTextEdit *edit) {
  QCompleter *completer = MakeFlagCompleter(edit);

  // textChanged fires for our own insertion as well as for typing, and acting
  // on that would reopen the popup on the flag just chosen.
  auto inserting = std::make_shared<bool>(false);

  QObject::connect(edit, &QPlainTextEdit::textChanged, edit,
                   [completer, edit, inserting]() {
                     if (*inserting) {
                       return;
                     }

                     const QTextCursor cursor = edit->textCursor();
                     const QString line = cursor.block().text();
                     const auto [start, end] =
                         WordUnderCursor(line, cursor.positionInBlock());
                     const QString word = line.mid(start, end - start);

                     if (!WantsCompletion(word)) {
                       completer->popup()->hide();
                       return;
                     }

                     completer->setCompletionPrefix(word);
                     if (completer->completionCount() == 0) {
                       completer->popup()->hide();
                       return;
                     }

                     // Beside the caret rather than under the whole box: in a
                     // multi-line field those are nowhere near each other.
                     QRect rect = edit->cursorRect();
                     rect.setWidth(completer->popup()->minimumWidth());
                     completer->complete(rect);
                   });

  auto commit = [edit, inserting](const QModelIndex &index) {
    const QString insertion = index.data(kFlagNameRole).toString();
    if (insertion.isEmpty()) {
      return;
    }

    QTextCursor cursor = edit->textCursor();
    const QString line = cursor.block().text();
    const auto [start, end] = WordUnderCursor(line, cursor.positionInBlock());
    const int blockStart = cursor.block().position();

    *inserting = true;
    cursor.setPosition(blockStart + start);
    cursor.setPosition(blockStart + end, QTextCursor::KeepAnchor);
    // After a boolean flag the next thing typed is another flag, so leave a
    // space; after "--flag=" the cursor belongs where the value goes.
    cursor.insertText(insertion.endsWith(QLatin1Char('='))
                          ? insertion
                          : insertion + QStringLiteral(" "));
    edit->setTextCursor(cursor);
    *inserting = false;
  };

  // Clicking a row goes through activated; pressing Enter does not.
  QObject::connect(
      completer, QOverload<const QModelIndex &>::of(&QCompleter::activated),
      edit, commit);
  completer->popup()->installEventFilter(
      new PopupCommitFilter(completer, commit));
}

void InstallPathCompleter(QLineEdit *edit, PathKind kind) {
  auto *model = new QFileSystemModel(edit);
  model->setRootPath(QString());
  model->setFilter(kind == PathKind::Directory
                       ? QDir::Dirs | QDir::NoDotAndDotDot
                       : QDir::AllEntries | QDir::NoDotAndDotDot);

  auto *completer = new QCompleter(model, edit);
  // Paths are stored with the platform's own separators in these fields, so
  // the completer has to match that or nothing lines up on Windows.
  completer->setCaseSensitivity(
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
      Qt::CaseInsensitive
#else
      Qt::CaseSensitive
#endif
  );

  edit->setCompleter(completer);
}
