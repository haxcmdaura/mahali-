#pragma once

namespace app::core {

struct ZakatInputs {
    long long stockValueCents = 0;
    long long cashCents = 0;
    long long receivablesCents = 0;
};

class ZakatCalculator {
public:
    static long long zakatBase(const ZakatInputs& in);

    static long long zakatBaseCents(long long cashSalesCents, long long customerDebtCents);

    // Zakat due is 2.5% of the zakat base. Computed in integer cents using
    // integer division (25/1000) to match existing behavior.
    static long long dueOn(long long baseCents);
};

inline long long ZakatCalculator::dueOn(long long baseCents)
{
    if (baseCents <= 0) {
        return 0;
    }
    return baseCents * 25 / 1000;
}

} // namespace app::core