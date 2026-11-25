#pragma once

#include <limits>
#include <optional>

#include <QString>

namespace app::core {

// The three sides of a product a line can be sold or bought as. Stored as text
// because every ledger written before this enum existed already carries these
// words; parsing them here is what keeps an unknown spelling from being read as
// an ordinary piece line.
enum class UnitKind { Piece, Package, Weight };

inline QString toString(UnitKind kind)
{
    switch (kind) {
    case UnitKind::Piece:
        return QStringLiteral("piece");
    case UnitKind::Package:
        return QStringLiteral("package");
    case UnitKind::Weight:
        return QStringLiteral("weight");
    }
    return QStringLiteral("piece");
}

// An empty value is the pre-enum piece line. Anything else has to be one of the
// three words above: a typo or an old free-form unit is refused rather than
// guessed at.
inline std::optional<UnitKind> fromString(const QString& text)
{
    if (text.isEmpty() || text == QStringLiteral("piece")) {
        return UnitKind::Piece;
    }
    if (text == QStringLiteral("package")) {
        return UnitKind::Package;
    }
    if (text == QStringLiteral("weight")) {
        return UnitKind::Weight;
    }
    return std::nullopt;
}

namespace detail {

// Whether a * b is representable as a long long. Signed overflow is undefined
// behaviour, and a wrapped amount is not a wrong amount, it is a plausible wrong
// amount of money.
//
// Done in unsigned magnitudes on purpose. The tempting shortcut
// `a < 0 && b < min / b` reads like a guard on the negative range and is not one:
// with a quantity of -2 and a cost of 5000, min / -2 comes out as a large
// positive number, so a comparison the wrong way round rejects every ordinary
// small figure and lets through the one that cannot be stored. A cancellation is
// exactly the case that needs this to be right — its lines are negative and
// perfectly ordinary.
inline bool productFits(long long a, long long b)
{
    // Two to the 63rd: the width of the negative range, one wider than max.
    const unsigned long long limit =
        static_cast<unsigned long long>(std::numeric_limits<long long>::max()) + 1ULL;
    const auto magnitude = [](long long value) -> unsigned long long {
        return value < 0 ? 0ULL - static_cast<unsigned long long>(value)
                          : static_cast<unsigned long long>(value);
    };
    const unsigned long long left = magnitude(a);
    const unsigned long long right = magnitude(b);
    if (left != 0 && right > limit / left) {
        return false;
    }
    const unsigned long long product = left * right;
    // The negative range reaches one further than the positive one, so the same
    // product can be too large to store either way round depending on the sign.
    return (a < 0) != (b < 0) ? product <= limit : product < limit;
}

// Whether a + b is representable, checked rather than performed.
inline bool sumFits(long long a, long long b)
{
    if (b > 0 && a > std::numeric_limits<long long>::max() - b) {
        return false;
    }
    return !(b < 0 && a < std::numeric_limits<long long>::min() - b);
}

} // namespace detail

// The total of one line, shared by the sale and purchase paths so the two can
// never round a weight line differently. quantity is in the unit the line is
// counted in: grams for a weight line, where the price is per kilogram.
// nullopt when the multiplication cannot be represented, even if dividing a
// weight line by a thousand would have brought it back into range: the same
// refusal totalCentsFor makes, for the same reason.
inline std::optional<long long> lineTotalCents(long long quantity, long long unitPriceCents, UnitKind kind)
{
    if (!detail::productFits(quantity, unitPriceCents)) {
        return std::nullopt;
    }
    long long total = quantity * unitPriceCents;
    if (kind == UnitKind::Weight) {
        total /= 1000;
    }
    return total;
}

} // namespace app::core
