#pragma once

#include <QDialog>

class QPlainTextEdit;
class QLineEdit;
class QLabel;

// Lets the user write a post-mount script in place instead of creating the
// file, marking it executable and browsing for it by hand. Saved scripts live
// under <config dir>/mount-scripts/ and are made executable on save.
class ScriptEditorDialog : public QDialog {
  Q_OBJECT

public:
  // existingPath may be empty (new script) or point at a file to load.
  // suggestedName seeds the filename for a new script.
  ScriptEditorDialog(const QString &existingPath, const QString &suggestedName,
                     QWidget *parent = nullptr);

  // Absolute path of the saved script. Only valid after accept().
  QString scriptPath() const { return mSavedPath; }

  // Directory scripts written by this dialog are stored in.
  static QString scriptsDir();

  // Default file extension for the current platform (".cmd" or ".sh").
  static QString scriptExtension();

private:
  void save();
  static QString templateFor(const QString &name);

  QLineEdit *mName = nullptr;
  QPlainTextEdit *mEditor = nullptr;
  QLabel *mPathLabel = nullptr;
  QString mSavedPath;
};
