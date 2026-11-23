#include "occasion_offer_repository.h"

#include <QSqlQuery>

namespace app::data {

namespace {

const char* kOccasionOfferColumns =
    "id, occasion_id, product_id, offer_price_cents, discount_percent, bonus_pieces";

core::OccasionOffer offerFromQuery(const QSqlQuery& query)
{
    core::OccasionOffer offer;
    offer.id = query.value(0).toInt();
    offer.occasionId = query.value(1).toInt();
    offer.productId = query.value(2).toInt();
    if (!query.value(3).isNull()) {
        offer.offerPriceCents = query.value(3).toLongLong();
    }
    if (!query.value(4).isNull()) {
        offer.discountPercent = query.value(4).toLongLong();
    }
    if (!query.value(5).isNull()) {
        offer.bonusPieces = query.value(5).toLongLong();
    }
    return offer;
}

} // namespace

OccasionOfferRepository::OccasionOfferRepository(Database& db)
    : m_db(db)
{
}

std::optional<core::OccasionOffer> OccasionOfferRepository::findByOccasionAndProduct(int occasionId,
                                                                                    int productId) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM occasion_offers WHERE occasion_id = ? AND product_id = ? LIMIT 1")
                      .arg(QLatin1StringView(kOccasionOfferColumns)));
    query.addBindValue(occasionId);
    query.addBindValue(productId);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("OccasionOfferRepository::findByOccasionAndProduct"));
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }
    return offerFromQuery(query);
}

QVector<core::OccasionOffer> OccasionOfferRepository::findByOccasion(int occasionId) const
{
    QVector<core::OccasionOffer> offers;
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM occasion_offers WHERE occasion_id = ? ORDER BY product_id")
                      .arg(QLatin1StringView(kOccasionOfferColumns)));
    query.addBindValue(occasionId);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("OccasionOfferRepository::findByOccasion"));
        return offers;
    }
    while (query.next()) {
        offers.push_back(offerFromQuery(query));
    }
    return offers;
}

} // namespace app::data
