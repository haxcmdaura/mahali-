#pragma once

#include <optional>

#include <QVector>

#include "core/occasion_offer.h"
#include "database.h"

namespace app::data {

class OccasionOfferRepository {
public:
    explicit OccasionOfferRepository(Database& db);

    std::optional<core::OccasionOffer> findByOccasionAndProduct(int occasionId, int productId) const;
    QVector<core::OccasionOffer> findByOccasion(int occasionId) const;

private:
    Database& m_db;
};

} // namespace app::data
