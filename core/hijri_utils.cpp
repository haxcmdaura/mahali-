#include "hijri_utils.h"

#include <QCalendar>

namespace app::core {

namespace {

QCalendar hijriCalendar()
{
    return QCalendar(QCalendar::System::IslamicCivil);
}

QString normalizedKey(const QString& key)
{
    return key.trimmed().toLower();
}

} // namespace

QString getCurrentHijriOccasion(const QDate& gregorianDate)
{
    if (!gregorianDate.isValid()) {
        return {};
    }

    const QCalendar calendar = hijriCalendar();
    if (!calendar.isValid()) {
        return {};
    }

    const QCalendar::YearMonthDay parts = calendar.partsFromDate(gregorianDate);
    if (!parts.isValid() || parts.year == QCalendar::Unspecified) {
        return {};
    }

    if (parts.month == 9) {
        return QStringLiteral("ramadan");
    }
    if (parts.month == 10 && parts.day == 1) {
        return QStringLiteral("fitr");
    }
    if (parts.month == 12 && parts.day >= 10 && parts.day <= 13) {
        return QStringLiteral("adha");
    }
    return {};
}

QString formatHijriDate(const QDate& gregorianDate)
{
    if (!gregorianDate.isValid()) {
        return {};
    }

    const QCalendar calendar = hijriCalendar();
    if (!calendar.isValid()) {
        return {};
    }

    const QCalendar::YearMonthDay parts = calendar.partsFromDate(gregorianDate);
    if (!parts.isValid() || parts.year == QCalendar::Unspecified) {
        return {};
    }

    return QStringLiteral("%1-%2-%3")
        .arg(parts.year)
        .arg(parts.month, 2, 10, QLatin1Char('0'))
        .arg(parts.day, 2, 10, QLatin1Char('0'));
}

std::optional<QDate> hijriOccasionStart(int hijriYear, const QString& occasionKey)
{
    const QCalendar calendar = hijriCalendar();
    if (!calendar.isValid() || !calendar.isDateValid(hijriYear, 1, 1)) {
        return std::nullopt;
    }

    int month = 0;
    int day = 0;
    const QString key = normalizedKey(occasionKey);
    if (key == QStringLiteral("ramadan")) {
        month = 9;
        day = 1;
    } else if (key == QStringLiteral("fitr")) {
        month = 10;
        day = 1;
    } else if (key == QStringLiteral("adha")) {
        month = 12;
        day = 10;
    } else {
        return std::nullopt;
    }

    if (!calendar.isDateValid(hijriYear, month, day)) {
        return std::nullopt;
    }
    const QDate start = calendar.dateFromParts(hijriYear, month, day);
    if (!start.isValid()) {
        return std::nullopt;
    }
    return start;
}

std::optional<QDate> hijriOccasionEnd(int hijriYear, const QString& occasionKey)
{
    const QCalendar calendar = hijriCalendar();
    if (!calendar.isValid() || !calendar.isDateValid(hijriYear, 1, 1)) {
        return std::nullopt;
    }

    int month = 0;
    int day = 0;
    const QString key = normalizedKey(occasionKey);
    if (key == QStringLiteral("ramadan")) {
        month = 9;
        day = calendar.daysInMonth(month, hijriYear);
    } else if (key == QStringLiteral("fitr")) {
        month = 10;
        day = 1;
    } else if (key == QStringLiteral("adha")) {
        month = 12;
        day = 13;
    } else {
        return std::nullopt;
    }

    if (!calendar.isDateValid(hijriYear, month, day)) {
        return std::nullopt;
    }
    const QDate end = calendar.dateFromParts(hijriYear, month, day);
    if (!end.isValid()) {
        return std::nullopt;
    }
    return end;
}

} // namespace app::core
