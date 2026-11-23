#include "theme.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QFile>
#include <QHash>
#include <QPalette>
#include <QStyleFactory>

#include "theme_tokens.h"

static void initMahaliResources()
{
    Q_INIT_RESOURCE(app);
}

namespace app::ui {

namespace {
QString g_activeTheme = QStringLiteral("light");
bool g_fontsLoaded = false;
bool g_fusionSelected = false;

bool isKnownThemeKey(const QString& key)
{
    return key == QStringLiteral("light") || key == QStringLiteral("dark")
        || isCustomThemeKey(key);
}

QString stylesheetPath(const QString& key)
{
    if (key == QStringLiteral("dark")) {
        return themesDarkPath();
    }
    if (isCustomThemeKey(key)) {
        return QStringLiteral(":/mahali/themes/%1.qss").arg(key);
    }
    return themesLightPath();
}

const themeTokens::ThemeColors& colorsFor(const QString& key)
{
    if (key == QStringLiteral("dark")) {
        return themeTokens::darkColors;
    }
    if (key == QStringLiteral("ramadan")) {
        return themeTokens::ramadanColors;
    }
    if (key == QStringLiteral("adha")) {
        return themeTokens::adhaColors;
    }
    if (key == QStringLiteral("fitr")) {
        return themeTokens::fitrColors;
    }
    return themeTokens::lightColors;
}

QString loadThemeStylesheet(const QString& path, const themeTokens::ThemeColors& colors)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    QString stylesheet = QString::fromUtf8(file.readAll());
    const auto hex = [](quint32 rgba) { return QColor::fromRgba(rgba).name(); };
    stylesheet.replace(QStringLiteral("@WINDOW@"), hex(colors.window));
    stylesheet.replace(QStringLiteral("@SURFACE@"), hex(colors.surface));
    stylesheet.replace(QStringLiteral("@BORDER@"), hex(colors.border));
    stylesheet.replace(QStringLiteral("@TEXT@"), hex(colors.text));
    stylesheet.replace(QStringLiteral("@TEXT_SECONDARY@"), hex(colors.textSecondary));
    stylesheet.replace(QStringLiteral("@MUTED@"), hex(colors.muted));
    stylesheet.replace(QStringLiteral("@ACCENT@"), hex(colors.accent));
    stylesheet.replace(QStringLiteral("@ACCENT_SOFT@"), hex(colors.accentSoft));
    stylesheet.replace(QStringLiteral("@POSITIVE@"), hex(colors.positive));
    stylesheet.replace(QStringLiteral("@NEGATIVE@"), hex(colors.negative));
    stylesheet.replace(QStringLiteral("@WARNING@"), hex(colors.warning));
    stylesheet.replace(QStringLiteral("@POSITIVE_SOFT@"), hex(colors.positiveSoft));
    stylesheet.replace(QStringLiteral("@NEGATIVE_SOFT@"), hex(colors.negativeSoft));
    stylesheet.replace(QStringLiteral("%1"), QString::number(themeTokens::fontBodyPt));
    stylesheet.replace(QStringLiteral("%2"), QString::number(themeTokens::fontHeadingPt));
    stylesheet.replace(QStringLiteral("%3"), QString::number(themeTokens::fontNotePt));
    return stylesheet;
}

const QString& cachedThemeStylesheet(const QString& key)
{
    static QHash<QString, QString> cache;
    const auto found = cache.constFind(key);
    if (found != cache.constEnd()) {
        return found.value();
    }

    QString stylesheet;
    if (isCustomThemeKey(key)) {
        stylesheet = loadThemeStylesheet(themesLightPath(), colorsFor(key));
        stylesheet += QStringLiteral("\n");
        stylesheet += loadThemeStylesheet(stylesheetPath(key), colorsFor(key));
    } else {
        stylesheet = loadThemeStylesheet(stylesheetPath(key), colorsFor(key));
    }
    cache.insert(key, stylesheet);
    return cache[key];
}

void loadApplicationFont(QApplication& app)
{
    if (g_fontsLoaded) {
        return;
    }
    const int regularId = QFontDatabase::addApplicationFont(
        QStringLiteral(":/mahali/fonts/NotoSansArabic-Regular.ttf"));
    QFontDatabase::addApplicationFont(
        QStringLiteral(":/mahali/fonts/NotoSansArabic-Bold.ttf"));
    if (regularId >= 0) {
        const QStringList families = QFontDatabase::applicationFontFamilies(regularId);
        if (!families.isEmpty()) {
            QFont font(families.first());
            font.setPointSize(themeTokens::fontBodyPt);
            app.setFont(font);
        }
    }
    g_fontsLoaded = true;
}

void applyPalette(const QString& key, QApplication& app)
{
    const themeTokens::ThemeColors& colors = colorsFor(key);
    const auto color = [](quint32 rgba) { return QColor::fromRgba(rgba); };

    QPalette palette = app.palette();
    palette.setColor(QPalette::Window, color(colors.window));
    palette.setColor(QPalette::WindowText, color(colors.text));
    palette.setColor(QPalette::Base, color(colors.surface));
    palette.setColor(QPalette::AlternateBase, color(colors.window));
    palette.setColor(QPalette::Text, color(colors.text));
    palette.setColor(QPalette::Button, color(colors.surface));
    palette.setColor(QPalette::ButtonText, color(colors.text));
    palette.setColor(QPalette::Mid, color(colors.border));
    palette.setColor(QPalette::Highlight, color(colors.accent));
    palette.setColor(QPalette::HighlightedText, QColor(Qt::white));
    palette.setColor(QPalette::PlaceholderText, color(colors.muted));
    app.setPalette(palette);
}
} // namespace

QString themesLightPath()
{
    return QStringLiteral(":/mahali/themes/light.qss");
}

QString themesDarkPath()
{
    return QStringLiteral(":/mahali/themes/dark.qss");
}

QString themePath(const QString& key)
{
    return stylesheetPath(isKnownThemeKey(key) ? key : QStringLiteral("light"));
}

bool isCustomThemeKey(const QString& key)
{
    return key == QStringLiteral("ramadan") || key == QStringLiteral("adha")
        || key == QStringLiteral("fitr");
}

QString activeTheme()
{
    return g_activeTheme;
}

void applyTheme(const QString& key, QApplication& app)
{
    initMahaliResources();
    const QString selected = isKnownThemeKey(key) ? key : QStringLiteral("light");
    g_activeTheme = selected;
    if (!g_fusionSelected) {
        app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        g_fusionSelected = true;
    }
    loadApplicationFont(app);
    applyPalette(selected, app);
    app.setStyleSheet(cachedThemeStylesheet(selected));
}

bool applyCustomTheme(const QString& themeKey, QApplication& app)
{
    if (!isCustomThemeKey(themeKey)) {
        return false;
    }
    applyTheme(themeKey, app);
    return true;
}

bool applyCustomTheme(const QString& themeKey)
{
    if (QApplication* app = qobject_cast<QApplication*>(QApplication::instance())) {
        return applyCustomTheme(themeKey, *app);
    }
    return false;
}

} // namespace app::ui
