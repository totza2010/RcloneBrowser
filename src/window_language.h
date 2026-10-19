#pragma once

// The language of the window's words. Translations are compiled in from
// translations/rclonebrowser_<language>.ts (see docs/TRANSLATING.md); with
// none, the window is in English, the language it is written in.
//
// Only the words: the locale stays UK English whatever is chosen, because
// numbers and dates read back by the program must keep one spelling (see
// main.cpp and issue #96).

#include <QStringList>

class QCoreApplication;

namespace WindowLanguage {

// "en" and every language there is a translation for.
QStringList Available();

// What was chosen in Preferences: a language from Available(), or empty for
// the one this machine is set to (when there is a translation for it).
QString Chosen();
void Choose(const QString &language);

// A language's own name for itself: "English", "ไทย".
QString NameOf(const QString &language);

// Loads the chosen translation into the application. Call once, after the
// application exists and before any window is made.
void Install(QCoreApplication *app);

} // namespace WindowLanguage
