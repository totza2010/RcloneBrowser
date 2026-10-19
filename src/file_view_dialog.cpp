#include "file_view_dialog.h"
#include "file_reader.h"
#include "job_stats.h"
#include "utils.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFontDatabase>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

FileViewDialog::FileViewDialog(const QString &remote, const QString &path,
                               qint64 size, QWidget *parent)
    : QDialog(parent), mRemote(remote), mPath(path), mSize(size) {
  const QString name = path.section(QLatin1Char('/'), -1);
  setWindowTitle(QStringLiteral("%1:%2").arg(remote, path));
  resize(900, 700);

  mView = FileReader::ViewOf(name);
  mNote = new QLabel(QStringLiteral("Reading %1…").arg(name), this);
  mNote->setWordWrap(true);
  mPages = new QStackedWidget(this);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto *layout = new QVBoxLayout(this);
  layout->addWidget(mNote);
  layout->addWidget(mPages, 1);
  layout->addWidget(buttons);

  // As the page does: all of a picture or a PDF, the start of anything else.
  const qint64 most = 64LL * 1024 * 1024;
  const bool whole =
      mView == QLatin1String("image") || mView == QLatin1String("pdf");
  mReader = new FileReader(remote, path, whole ? most : 2LL * 1024 * 1024, this);
  QObject::connect(mReader, &FileReader::finished, this, &FileViewDialog::present);
  QObject::connect(mReader, &FileReader::failed, this,
                   [this](const QString &reason) {
                     mNote->setText(QStringLiteral("rclone could not read it: %1")
                                        .arg(reason));
                   });

  if (mView == QLatin1String("media")) {
    mNote->setText(QStringLiteral("A film or a song is played rather than "
                                  "shown: use Stream."));
    return;
  }
  mReader->start();
}

void FileViewDialog::present(const QByteArray &data, bool truncated) {
  const QString name = mPath.section(QLatin1Char('/'), -1);
  const bool whole =
      mView == QLatin1String("image") || mView == QLatin1String("pdf");
  if (whole && truncated) {
    mNote->setText(QStringLiteral("Too large to show here. Download it to "
                                  "open it."));
    return;
  }

  if (mView == QLatin1String("image")) {
    QImage image;
    if (image.loadFromData(data)) {
      auto *picture = new QLabel();
      picture->setAlignment(Qt::AlignCenter);
      picture->setPixmap(QPixmap::fromImage(image));
      auto *scroll = new QScrollArea();
      scroll->setAlignment(Qt::AlignCenter);
      scroll->setWidget(picture);
      mPages->addWidget(scroll);
      mNote->setText(QStringLiteral("%1 · %2 × %3")
                         .arg(name)
                         .arg(image.width())
                         .arg(image.height()));
      return;
    }
    // A kind of picture this build cannot draw (HEIC, AVIF without the
    // plugin): the machine may have something that can.
  }

  if (mView == QLatin1String("text") || mView.isEmpty()) {
    auto *text = new QPlainTextEdit();
    text->setReadOnly(true);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text->setPlainText(QString::fromUtf8(data));
    mPages->addWidget(text);
    QString note = mView.isEmpty() ? QStringLiteral("Shown as text, whatever it is. ")
                                   : QString();
    if (truncated) {
      note += mSize > 0
                  ? QStringLiteral("The first %1 of %2 — Download it for the "
                                   "rest.")
                        .arg(FormatBytes(data.size()), FormatBytes(mSize))
                  : QStringLiteral("The first %1 — Download it for the rest.")
                        .arg(FormatBytes(data.size()));
    }
    mNote->setText(note.isEmpty() ? name : note);
    return;
  }

  // A PDF, or a picture this build cannot draw: there is no viewer for it
  // here, and the machine has one.
  mNote->setText(QStringLiteral("Opened in the program this machine uses for "
                                "%1 files.")
                     .arg(name.section(QLatin1Char('.'), -1).toUpper()));
  openOutside(data);
}

void FileViewDialog::openOutside(const QByteArray &data) {
  // A folder of its own each time, so two files of the same name do not
  // overwrite each other while both are open.
  const QString folder =
      QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
          .filePath(QStringLiteral("rclonebrowser-view/") +
                    QUuid::createUuid().toString(QUuid::Id128));
  QDir().mkpath(folder);
  QFile file(QDir(folder).filePath(mPath.section(QLatin1Char('/'), -1)));
  if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
    mNote->setText(QStringLiteral("It could not be set down to open: %1")
                       .arg(file.errorString()));
    return;
  }
  file.close();
  QDesktopServices::openUrl(QUrl::fromLocalFile(file.fileName()));
}
