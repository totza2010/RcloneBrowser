#pragma once

#include <QDialog>

class FileReader;
class QLabel;
class QStackedWidget;

// Looks at a file of a remote: a picture or a page of text, here; a PDF, or
// anything else wanted, in the program this machine opens it with. Read
// through FileReader, as the page's viewer is, so both see the same bytes
// with the same limits. See FileReader::ViewOf().
class FileViewDialog : public QDialog {
  Q_OBJECT

public:
  FileViewDialog(const QString &remote, const QString &path, qint64 size,
                 QWidget *parent = nullptr);

private:
  void present(const QByteArray &data, bool truncated);
  void openOutside(const QByteArray &data);

  const QString mRemote;
  const QString mPath;
  const qint64 mSize;
  QString mView;
  FileReader *mReader;
  QLabel *mNote;
  QStackedWidget *mPages;
};
