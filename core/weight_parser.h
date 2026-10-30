#pragma once

#include <optional>

#include <QString>

namespace app::core {

// Ten tonnes is far beyond any single purchase line a grocery will write, and
// keeping the cap small also keeps the gram total far from overflowing.
inline constexpr long long kMaxPurchaseWeightKg = 10000;
inline constexpr long long kMaxPurchaseWeightGrams = kMaxPurchaseWeightKg * 1000;

// Kilograms as typed, converted to integer grams without floating point. A
// comma, a dot and the Arabic decimal separator all mean the same decimal
// point; unlike money, there is no thousands separator here.
inline std::optional<long long> parseWeightGrams(const QString& text)
{
    QString cleaned = text.trimmed();
    if (cleaned.isEmpty()) {
        return std::nullopt;
    }

    const QString arabicDigits = QStringLiteral("٠١٢٣٤٥٦٧٨٩");
    for (int i = 0; i < 10; ++i) {
        cleaned.replace(arabicDigits.at(i), QChar(QLatin1Char(static_cast<char>('0' + i))));
    }
    cleaned.replace(QChar(0x066B), QLatin1Char('.'));

    QString wholePart;
    QString fractionPart;
    bool seenSeparator = false;
    for (const QChar& ch : cleaned) {
        if (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) {
            (seenSeparator ? fractionPart : wholePart).append(ch);
        } else if (ch == QLatin1Char('.') || ch == QLatin1Char(',')) {
            // One separator, and it is always the decimal point: "1,000" is one
            // kilogram, not a thousand.
            if (seenSeparator) {
                return std::nullopt;
            }
            seenSeparator = true;
        } else {
            // Signs, currency words and stray characters are refused rather
            // than stripped, so "-1" can never become a positive weight.
            return std::nullopt;
        }
    }

    if (wholePart.isEmpty()) {
        wholePart = QStringLiteral("0");
    }
    bool wholeOk = false;
    const long long wholeKg = wholePart.toLongLong(&wholeOk);
    if (!wholeOk || wholeKg > kMaxPurchaseWeightKg) {
        return std::nullopt;
    }

    if (fractionPart.size() > 3) {
        // Extra decimal places are allowed only when they add nothing: 2.5000
        // is 2.5 kg, but 2.5001 cannot be represented in grams.
        for (int i = 3; i < fractionPart.size(); ++i) {
            if (fractionPart.at(i) != QLatin1Char('0')) {
                return std::nullopt;
            }
        }
        fractionPart.truncate(3);
    }
    while (fractionPart.size() < 3) {
        fractionPart.append(QLatin1Char('0'));
    }
    bool fractionOk = false;
    const long long fractionGrams = fractionPart.toLongLong(&fractionOk);
    if (!fractionOk) {
        return std::nullopt;
    }

    const long long grams = wholeKg * 1000 + fractionGrams;
    if (grams <= 0 || grams > kMaxPurchaseWeightGrams) {
        return std::nullopt;
    }
    return grams;
}

} // namespace app::core
