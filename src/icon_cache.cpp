#include "icon_cache.h"
#include "item_model.h"
#if defined(Q_OS_MACOS)
#include "osx_helper.h"
#endif
#include <QFileIconProvider>
#include <QMimeDatabase>
#include <QPixmap>
#include <QIcon>
#if defined(Q_OS_WIN32)
#include <windows.h>
#include <QImage>

QPixmap pixmapFromHICON(HICON hIcon) {
    if (!hIcon) return QPixmap();

    ICONINFO iconInfo;
    if (!GetIconInfo(hIcon, &iconInfo)) return QPixmap();

    BITMAP bmpColor;
    GetObject(iconInfo.hbmColor, sizeof(BITMAP), &bmpColor);

    int w = bmpColor.bmWidth;
    int h = bmpColor.bmHeight;

    QImage img(w, h, QImage::Format_ARGB32);
    HDC hdc = CreateCompatibleDC(nullptr);
    HBITMAP oldBmp = (HBITMAP)SelectObject(hdc, iconInfo.hbmColor);

    BITMAPINFOHEADER bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = w;
    bi.biHeight = -h; // top-down
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    GetDIBits(hdc, iconInfo.hbmColor, 0, h, img.bits(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    SelectObject(hdc, oldBmp);
    DeleteDC(hdc);
    DeleteObject(iconInfo.hbmColor);
    DeleteObject(iconInfo.hbmMask);

    return QPixmap::fromImage(img);
}
#endif

IconCache::IconCache(QObject *parent) : QObject(parent) {
  mFileIcon = QFileIconProvider().icon(QFileIconProvider::File);

#if defined(Q_OS_WIN32)
  CoInitializeEx(NULL, COINIT_MULTITHREADED);
#endif

    QPixmapCache::setCacheLimit(20480);

  mThread.start();
  moveToThread(&mThread);
}

IconCache::~IconCache() {
  mThread.quit();
  mThread.wait();

#if defined(Q_OS_WIN32)
  CoUninitialize();
#endif
}

void IconCache::getIcon(Item *item, const QPersistentModelIndex &parent) {
  QString ext = QFileInfo(item->name).suffix();
  QIcon icon;
  auto it = mIcons.find(ext);
  if (it == mIcons.end()) {
#if defined(Q_OS_WIN32)
    SHFILEINFOW info;
    if (SHGetFileInfoW(reinterpret_cast<LPCWSTR>(("dummy." + ext).utf16()),
                       FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
                       SHGFI_ICON | SHGFI_USEFILEATTRIBUTES) &&
        info.hIcon) {
      icon = QIcon(pixmapFromHICON(info.hIcon));
      DestroyIcon(info.hIcon);
    }
#elif defined(Q_OS_MACOS)
    icon = osxGetIcon(ext.toUtf8().constData());
#else
    QMimeType mime = mMimeDatabase.mimeTypeForFile(
        item->name, QMimeDatabase::MatchExtension);
    if (mime.isValid()) {
      icon = QIcon::fromTheme(mime.iconName());
    }
#endif
    if (icon.isNull()) {
      icon = mFileIcon;
    }
    mIcons.insert(ext, icon);
  } else {
    icon = it.value();
  }

  emit iconReady(item, parent, icon);
}
