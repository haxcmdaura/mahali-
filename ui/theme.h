#pragma once

#include <QString>

class QApplication;

namespace app::ui {

QString themesLightPath();
QString themesDarkPath();
QString themePath(const QString& key);

bool isCustomThemeKey(const QString& key);

// Returns the current theme key.
QString activeTheme();

// Reads :/mahali/themes/<key>.qss and applies it to the given QApplication.
// Falls back to the light theme when <key> is unknown.
void applyTheme(const QString& key, QApplication& app);

bool applyCustomTheme(const QString& themeKey, QApplication& app);
bool applyCustomTheme(const QString& themeKey);

QString occasionGreeting(const QString& themeKey, const QString& occasionName = QString());

} // namespace app::ui
