#pragma once

#include <optional>
#include <QHash>
#include <QString>
#include <QVector>
#include <QDebug>

#include "core/line_total.h"
#include "core/occasion.h"
#include "core/sale_item.h"
#include "occasion_offer_repository.h"
#include "product_repository.h"
#include "stock_movement_repository.h"

namespace app::data {

// The unit words and the overflow arithmetic live in core so the purchase path
// can share them without depending on this header. The names stay in this
// namespace so every existing call site keeps reading the same.
using core::UnitKind;
using core::toString;
using core::fromString;

namespace detail {
using core::detail::productFits;
using core::detail::sumFits;
} // namespace detail

// Shared accounting rules for recording a sale or customer debt. Both the
// Windows-side SaleService (server apply) and the device-side DeviceLedgerService
// (offline record) funnel through these same functions so money math never
// diverges between the two machines.

inline QVector<core::SaleItem> resolveSaleItems(ProductRepository& products,
                                                 const QVector<core::SaleItem>& items, bool allowOversold,
                                                 QString* error,
                                                 const OccasionOfferRepository* offers = nullptr,
                                                 const std::optional<core::Occasion>& occasion = std::nullopt)
{
    QVector<core::SaleItem> resolved = items;
    // A product can sit on more than one line: six pieces and a carton of six
    // are two lines but twelve pieces off the same shelf. The stock check has to
    // be against the sum, not against each line on its own.
    QHash<int, long long> requiredPieces;
    QHash<int, long long> shelfPieces;
    QHash<int, QString> productNames;
    for (core::SaleItem& item : resolved) {
        const auto product = products.findById(item.productId);
        if (!product.has_value()) {
            if (error) {
                *error = QStringLiteral("unknown product id %1").arg(item.productId);
            }
            return {};
        }
        // The unit and the piece count are settled before the price below, because
        // the price of a carton is derived from the same count the pieces are
        // counted in. Filling piecesConsumed here rather than at the repository
        // insert is what makes the money path (cogsCentsFor, insertSaleStockMovements)
        // and the stored row agree by construction, which is the entire reason
        // resolveSaleItems exists. Setting it at the insert site is too late: both
        // of those run on this resolved vector before the row is ever built.

        // An empty unitKind can only come from a caller that predates this field;
        // treating it as a piece line is what was true of every line before
        // Phase 2B, and refusing it would break every existing path that has not
        // yet been taught to set it.
        if (item.unitKind.isEmpty()) {
            item.unitKind = toString(UnitKind::Piece);
        }
        const std::optional<UnitKind> unitKind = fromString(item.unitKind);
        if (!unitKind) {
            if (error) {
                *error = QStringLiteral("unknown unit '%1' for product %2")
                             .arg(item.unitKind, product->name);
            }
            return {};
        }

        // The product is the authority on whether it can be weighed or boxed.
        // A weight price is quoted per kilogram while a piece price is not, so
        // letting the two cross would divide a piece sale by a thousand or bill
        // a kilogram line as one gram.
        if (product->soldByWeight && *unitKind != UnitKind::Weight) {
            if (error) {
                *error = QStringLiteral("product %1 is sold by weight and must use the weight unit, not '%2'")
                             .arg(product->name, item.unitKind);
            }
            return {};
        }
        if (!product->soldByWeight && *unitKind == UnitKind::Weight) {
            if (error) {
                *error = QStringLiteral("product %1 is not sold by weight and cannot use the weight unit")
                             .arg(product->name);
            }
            return {};
        }

        const long long claimedPiecesConsumed = item.piecesConsumed;

        if (*unitKind == UnitKind::Package) {
            // The carton price is the piece price times the count, and the caller
            // is allowed to know it already. When it does not, we compute it here
            // so a package line is always priced. Only ever the price of ONE carton:
            // totalCentsFor multiplies by quantity afterwards, so multiplying here
            // too would charge the customer for twice what they took.
            if (item.unitPriceCents <= 0) {
                // Same overflow shape as the COGS multiplication, so the same guard.
                if (!detail::productFits(product->salePriceCents,
                                         product->piecesPerPackage)) {
                    if (error) {
                        *error = QStringLiteral("package price overflows for product %1")
                                     .arg(product->name);
                    }
                    return {};
                }
                item.unitPriceCents = product->salePriceCents * product->piecesPerPackage;
            }
            // How many pieces leave the shelf. Guarded for the same reason and with
            // the same check: a carton of a million pieces would otherwise wrap into
            // a small positive number and take that many pieces off the shelf.
            if (!detail::productFits(item.quantity, product->piecesPerPackage)) {
                if (error) {
                    *error = QStringLiteral("package piece count overflows for product %1")
                                 .arg(product->name);
                }
                return {};
            }
            item.piecesConsumed = item.quantity * product->piecesPerPackage;
        } else {
            // A weight line is not a carton: its quantity is grams and is already
            // the figure that leaves the shelf. A piece line's quantity is too.
            item.piecesConsumed = item.quantity;
        }

        if (claimedPiecesConsumed != 0 && claimedPiecesConsumed != item.piecesConsumed) {
            // The definition of the product (pieces_per_package) is the authority
            // here; the device's claim only needs to be observed. We warn without
            // changing arithmetic because silently trusting either side would hide
            // a real definition drift between machines (D33 / TASKS G9).
            qWarning() << "sync drift: product" << item.productId
                       << "claimed piecesConsumed" << claimedPiecesConsumed
                       << "derived" << item.piecesConsumed;
        }

        if (item.unitPriceCents <= 0) {
            item.unitPriceCents = product->salePriceCents;
        }
        item.unitCostCents = product->costPriceCents;
        item.originalPriceCents = item.unitPriceCents;

        if (offers && occasion) {
            const std::optional<core::OccasionOffer> offer =
                offers->findByOccasionAndProduct(occasion->id, item.productId);
            if (offer) {
                item.hasOffer = true;
                item.bonusPieces = offer->bonusPieces;
                if (!item.priceOverridden) {
                    if (offer->offerPriceCents && *offer->offerPriceCents >= 0) {
                        item.unitPriceCents = *offer->offerPriceCents;
                    } else if (offer->discountPercent && *offer->discountPercent > 0) {
                        const long long percent = qBound(0LL, *offer->discountPercent, 100LL);
                        if (!detail::productFits(item.originalPriceCents, percent)) {
                            if (error) {
                                *error = QStringLiteral("offer discount overflows for product %1")
                                             .arg(product->name);
                            }
                            return {};
                        }
                        item.unitPriceCents =
                            item.originalPriceCents - (item.originalPriceCents * percent / 100);
                    }
                }
                if (item.bonusPieces > 0) {
                    if (!detail::sumFits(item.piecesConsumed, item.bonusPieces)) {
                        if (error) {
                            *error = QStringLiteral("offer bonus pieces overflow for product %1")
                                         .arg(product->name);
                        }
                        return {};
                    }
                    item.piecesConsumed += item.bonusPieces;
                }
            }
        }

        if (!allowOversold) {
            // The shelf is counted in pieces. For a package line that is the
            // piecesConsumed computed above, not quantity, which is cartons.
            const auto previous = requiredPieces.constFind(item.productId);
            if (previous == requiredPieces.cend()) {
                requiredPieces.insert(item.productId, item.piecesConsumed);
                shelfPieces.insert(item.productId, product->quantity);
                productNames.insert(item.productId, product->name);
            } else {
                if (!detail::sumFits(*previous, item.piecesConsumed)) {
                    if (error) {
                        *error = QStringLiteral("stock quantity overflows for product %1")
                                     .arg(product->name);
                    }
                    return {};
                }
                const long long total = *previous + item.piecesConsumed;
                requiredPieces[item.productId] = total;
            }
        }
    }

    if (!allowOversold) {
        for (auto required = requiredPieces.cbegin(); required != requiredPieces.cend(); ++required) {
            if (shelfPieces.value(required.key()) < required.value()) {
                if (error) {
                    *error = QStringLiteral("insufficient stock for product %1")
                                 .arg(productNames.value(required.key()));
                }
                return {};
            }
        }
    }
    return resolved;
}

// nullopt when the money does not fit. A sold-by-weight line is typed in
// kilograms, so quantity is a decimal the operator enters rather than a count
// of items, and a long enough line can push the multiplication past the range
// of the column it is about to be written to. Callers must refuse the sale
// rather than record a number.
//
// quantity is CORRECT here, and this is the only money function where it is. The
// price on a line is quoted per unit sold, not per piece: a carton of 24 sold at
// 1450 is 1450 of revenue, not 1450 x 24, because 1450 is what the customer was
// charged for the carton and that is the whole of what came in. Multiplying by
// pieces_consumed instead would bill the customer 24 times over for one line.
//
// The split with cogsCentsFor below is the reason both numbers are kept on the
// row. Revenue is about what the unit sold for and is counted in units; cost is
// about what the goods cost and is counted in pieces, because the piece is what
// the shop bought. The two use different units because they are answers to
// different questions, so a line that carried only one count could not answer both.
inline std::optional<long long> totalCentsFor(const QVector<core::SaleItem>& items)
{
    long long total = 0;
    for (const core::SaleItem& item : items) {
        if (!detail::productFits(item.unitPriceCents, item.quantity)) {
            return std::nullopt;
        }
        long long term = item.unitPriceCents * item.quantity;
        // A weight line stores grams in quantity and a per-kilogram price in
        // unitPriceCents. The thousand that separates them is divided out
        // here, once, so no other reader of a sale line has to remember it.
        if (item.unitKind == QStringLiteral("weight")) {
            term /= 1000;
        }
        // Each term being in range is not enough: the running sum can leave it on
        // its own once enough lines are added together.
        if (!detail::sumFits(total, term)) {
            return std::nullopt;
        }
        total += term;
    }
    return total;
}

// The cost of the goods, counted in PIECES rather than in units sold. This is the
// one money figure that must not use quantity: a unit price is quoted per piece and
// a carton price per carton, but costPriceCents is always what one piece cost the
// shop. Multiplying it by quantity would under-count the cost of a carton sale by a
// factor of pieces_per_package -- 24 pieces of goods gone for the price of one --
// and a sale that under-counts its own cost reports a profit the shop did not make.
// The profit and loss statement is built from this figure, so the error would not
// stay on one row: it would walk straight into the day's margin.
//
// Same guard as totalCentsFor, and for the same reason: cost is multiplied by a
// count exactly as price is, so a line long enough to overflow the price overflows
// the cost too. Reported apart from the total because cost is what the profit and
// loss statement is built from, and a wrapped figure there turns a loss into a
// margin. The operands change; the checks do not.
inline std::optional<long long> cogsCentsFor(const QVector<core::SaleItem>& items)
{
    long long cogs = 0;
    for (const core::SaleItem& item : items) {
        if (!detail::productFits(item.unitCostCents, item.piecesConsumed)) {
            return std::nullopt;
        }
        long long term = item.unitCostCents * item.piecesConsumed;
        if (item.unitKind == QStringLiteral("weight")) {
            term /= 1000;
        }
        if (!detail::sumFits(cogs, term)) {
            return std::nullopt;
        }
        cogs += term;
    }
    return cogs;
}

inline bool insertSaleStockMovements(StockMovementRepository& movements,
                                     const QVector<core::SaleItem>& items)
{
    for (const core::SaleItem& item : items) {
        core::StockMovement movement;
        movement.productId = item.productId;
        // piecesConsumed is already the figure that leaves the shelf: pieces
        // for a piece or package line, grams for a weight line. No conversion.
        // Stock is counted in pieces, so it takes piecesConsumed and not quantity.
        // The two agree on a piece sale and differ by a factor of pieces_per_package
        // on a carton sale, and stock is the figure that has to be right: counting a
        // carton off the shelf as one piece would leave 23 pieces on the shelf that
        // are not there, and the daily stock count would then disagree with the till.
        movement.delta = -item.piecesConsumed;
        movement.reason = QStringLiteral("sale");
        movement.createdAt = QDateTime::currentDateTime();
        if (movements.insert(movement) == 0) {
            return false;
        }
    }
    return true;
}

} // namespace app::data