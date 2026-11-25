#pragma once

#include <optional>

namespace app::core {

struct OccasionOffer {
    int id = 0;
    int occasionId = 0;
    int productId = 0;
    std::optional<long long> offerPriceCents;
    std::optional<long long> discountPercent;
    long long bonusPieces = 0;
};

} // namespace app::core
