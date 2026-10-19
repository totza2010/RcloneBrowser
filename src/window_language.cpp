#include "window_language.h"
#include "utils.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace WindowLanguage {

QStringList Available() {
  QStringList languages{QStringLiteral("en")};
  // qt_add_translations puts them here: rclonebrowser_<language>.qm.
  const QStringList found = QDir(QStringLiteral(":/i18n")).entryList(
      {QStringLiteral("rclonebrowser_*.qm")}, QDir::Files, QDir::Name);
  for (QString name : found) {
    name.remove(0, int(qstrlen("rclonebrowser_")));
    name.chop(3);
    if (!name.isEmpty() && !languages.contains(name)) {
      languages << name;
    }
  }
  return languages;
}

QString Chosen() {
  return GetSettings()->value(QStringLiteral("Settings/language")).toString();
}

void Choose(const QString &language) {
  GetSettings()->setValue(QStringLiteral("Settings/language"), language);
}

QString NameOf(const QString &language) {
  const QLocale locale(language);
  const QString own = locale.nativeLanguageName();
  if (own.isEmpty() || locale.language() == QLocale::C) {
    return language;
  }
  return own.left(1).toUpper() + own.mid(1);
}

void Install(QCoreApplication *app) {
  const QStringList available = Available();
  QString language = Chosen();
  if (language.isEmpty()) {
    // The machine's own, when there is a translation for it.
    const QString system = QLocale::system().name(); // "th_TH"
    for (const QString &candidate : {system, system.section('_', 0, 0)}) {
      if (available.contains(candidate)) {
        language = candidate;
        break;
      }
    }
  }
  if (language.isEmpty() || language == QLatin1String("en") ||
      !available.contains(language)) {
    return; // English is what the program is written in
  }

  auto *ours = new QTranslator(app);
  if (ours->load(QStringLiteral(":/i18n/rclonebrowser_%1.qm").arg(language))) {
    app->installTranslator(ours);
  }
  // Qt's own words -- OK, Cancel, the file dialog -- when Qt has them.
  auto *qt = new QTranslator(app);
  if (qt->load(QStringLiteral("qtbase_%1").arg(language),
               QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
    app->installTranslator(qt);
  }
}

} // namespace WindowLanguage
