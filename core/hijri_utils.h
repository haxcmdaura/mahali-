#pragma once

#include <QDate>
#include <QString>

#include <optional>

namespace app::core {

QString getCurrentHijriOccasion(const QDate& gregorianDate);

QString formatHijriDate(const QDate& gregorianDate);

std::optional<QDate> hijriOccasionStart(int hijriYear, const QString& occasionKey);

std::optional<QDate> hijriOccasionEnd(int hijriYear, const QString& occasionKey);

} // namespace app::core
