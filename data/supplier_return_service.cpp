#include "supplier_return_service.h"

#include <QDateTime>
#include <QMap>
#include <QSqlQuery>
#include <QVariant>

#include "core/line_total.h"
#include "date_utils.h"

namespace app::data {

SupplierReturnService::SupplierReturnService(Database& db,
                                             SupplierReturnRepository& returns,
                                             SupplierReturnItemRepository& returnItems,
                                             SupplierRepository& suppliers,
                                             PurchaseRepository& purchases,
                                             PurchaseItemRepository& purchaseItems,
                                             ProductRepository& products,
                                             StockMovementRepository& stockMovements)
    : m_db(db)
    , m_returns(returns)
    , m_returnItems(returnItems)
    , m_suppliers(suppliers)
    , m_purchases(purchases)
    , m_purchaseItems(purchaseItems)
    , m_products(products)
    , m_stockMovements(stockMovements)
{
}

namespace {

// The repositories already recorded the driver message, so it is preferred; this
// only speaks up when the call failed without saying why, which would otherwise
// reach the operator as a blank reason.
QString stepError(const Database& db, const QString& step)
{
    const QString reason = db.lastError();
    if (!reason.isEmpty()) {
        return reason;
    }
    return QStringLiteral("SupplierReturnService failed at %1").arg(step);
}

QString normalizedUnitKind(const QString& value)
{
    return value.isEmpty() ? QStringLiteral("piece") : value;
}

// A purchase line is identified for return validation by the product, the unit
// it was counted in and the price the invoice charged. Quantity alone would let
// a second line of the same product at another price be returned at either
// price, and the supplier would be credited an amount nobody invoiced.
QString purchaseLineKey(int productId, const QString& unitKind, long long unitPriceCents)
{
    return QStringLiteral("%1|%2|%3").arg(productId).arg(unitKind).arg(unitPriceCents);
}

} // namespace

SupplierReturnResult SupplierReturnService::recordLinkedReturn(int supplierId,
                                                                int purchaseId,
                                                                const QVector<core::SupplierReturnItem>& items,
                                                                const QString& returnedAt,
                                                                const QString& note)
{
    SupplierReturnResult result;

    // A. The checks that cost nothing, before the transaction is opened.
    if (!m_suppliers.findById(supplierId).has_value()) {
        result.error = QStringLiteral("supplier not found");
        return result;
    }

    // Crediting one supplier for goods returned against another supplier's
    // invoice would settle the wrong debt, so the two have to agree first.
    const std::optional<core::Purchase> purchase = m_purchases.findById(purchaseId);
    if (!purchase.has_value()) {
        result.error = QStringLiteral("purchase %1 not found").arg(purchaseId);
        return result;
    }
    if (purchase->supplierId != supplierId) {
        result.error = QStringLiteral("purchase does not belong to supplier");
        return result;
    }

    if (items.isEmpty()) {
        result.error = QStringLiteral("return items are empty");
        return result;
    }
    for (const core::SupplierReturnItem& item : items) {
        if (item.quantity <= 0) {
            result.error = QStringLiteral("quantity must be positive");
            return result;
        }
        if (item.unitPriceCents <= 0) {
            result.error = QStringLiteral("unit price must be positive");
            return result;
        }
    }

    // B. The quantity checks need the invoice lines and the quantity already
    // returned against them, so they run inside the write transaction. A check
    // outside it could pass twice for two returns recorded at the same moment.
    if (!m_db.beginWriteTransaction()) {
        result.error = stepError(m_db, QStringLiteral("opening the transaction"));
        return result;
    }

    const QVector<core::PurchaseItem> purchaseLines = m_purchaseItems.findByPurchase(purchaseId);
    if (purchaseLines.isEmpty()) {
        m_db.rollback();
        result.error = QStringLiteral("purchase has no lines to return against");
        return result;
    }

    // What the invoice allows to come back, per product, unit and price. The
    // price is part of the key because the same product can be invoiced twice at
    // two prices; returning at either price would credit an amount the supplier
    // never charged.
    QMap<QString, long long> purchased;
    for (const core::PurchaseItem& line : purchaseLines) {
        if (!line.productId.has_value() || *line.productId <= 0) {
            continue;
        }
        const QString unitKind = normalizedUnitKind(line.unitKind);
        if (!core::fromString(unitKind).has_value()) {
            m_db.rollback();
            result.error = QStringLiteral("purchase line %1 has an unknown unit '%2'")
                               .arg(line.id)
                               .arg(line.unitKind);
            return result;
        }
        const QString key = purchaseLineKey(*line.productId, unitKind, line.unitPriceCents);
        if (!core::detail::sumFits(purchased.value(key, 0), line.quantity)) {
            m_db.rollback();
            result.error = QStringLiteral("purchased quantity overflows for a return line");
            return result;
        }
        purchased[key] += line.quantity;
    }

    QVector<core::SupplierReturnItem> lines;
    lines.reserve(items.size());
    QMap<QString, long long> requestedInThisReturn;
    long long amountCents = 0;

    for (core::SupplierReturnItem line : items) {
        if (line.quantity <= 0) {
            m_db.rollback();
            result.error = QStringLiteral("quantity must be positive");
            return result;
        }
        if (line.unitPriceCents <= 0) {
            m_db.rollback();
            result.error = QStringLiteral("unit price must be positive");
            return result;
        }

        const QString unitKind = normalizedUnitKind(line.unitKind);
        const std::optional<core::UnitKind> kind = core::fromString(unitKind);
        if (!kind.has_value()) {
            m_db.rollback();
            result.error = QStringLiteral("unknown unit '%1'").arg(line.unitKind);
            return result;
        }
        line.unitKind = unitKind;

        // The line total belongs to the service, not to the caller: a weight
        // line's price is per kilogram, so quantity * price without the thousand
        // is a credit a thousand times too large.
        const std::optional<long long> total =
            core::lineTotalCents(line.quantity, line.unitPriceCents, *kind);
        if (!total.has_value()) {
            m_db.rollback();
            result.error = QStringLiteral("return line total overflows");
            return result;
        }
        line.totalCents = *total;

        if (line.productId.has_value() && *line.productId > 0) {
            const std::optional<core::Product> product = m_products.findById(*line.productId);
            if (!product.has_value()) {
                m_db.rollback();
                result.error =
                    QStringLiteral("return line references product %1, which does not exist")
                        .arg(*line.productId);
                return result;
            }

            // A purchase row written before weight units existed cannot say
            // whether its quantity was grams or kilograms. Guessing either way
            // moves the wrong stock, so the row is refused until it is repaired.
            if (product->soldByWeight && *kind != core::UnitKind::Weight) {
                m_db.rollback();
                result.error =
                    QStringLiteral("product '%1' is sold by weight, but this purchase line is not a weight line")
                        .arg(product->name);
                return result;
            }
            if (!product->soldByWeight && *kind == core::UnitKind::Weight) {
                m_db.rollback();
                result.error =
                    QStringLiteral("product '%1' is not sold by weight").arg(product->name);
                return result;
            }

            const QString key = purchaseLineKey(*line.productId, unitKind, line.unitPriceCents);
            if (!purchased.contains(key)) {
                m_db.rollback();
                result.error =
                    QStringLiteral("return line does not match any line on this purchase");
                return result;
            }

            QSqlQuery alreadyQuery(m_db.handle());
            alreadyQuery.prepare(
                QStringLiteral("SELECT COALESCE(SUM(ri.quantity), 0) "
                               "FROM supplier_return_items ri "
                               "JOIN supplier_returns sr ON sr.id = ri.return_id "
                               "WHERE sr.purchase_id = ? AND ri.product_id = ? "
                               "AND ri.unit_price_cents = ? "
                               "AND COALESCE(NULLIF(ri.unit_kind, ''), 'piece') = ?"));
            alreadyQuery.addBindValue(purchaseId);
            alreadyQuery.addBindValue(*line.productId);
            alreadyQuery.addBindValue(line.unitPriceCents);
            alreadyQuery.addBindValue(unitKind);
            if (!alreadyQuery.exec() || !alreadyQuery.next()) {
                m_db.rollback();
                result.error = alreadyQuery.lastError().isValid()
                    ? QStringLiteral("could not read previous returns: %1")
                          .arg(alreadyQuery.lastError().text())
                    : QStringLiteral("could not read previous returns");
                return result;
            }
            const long long previous = alreadyQuery.value(0).toLongLong();
            const long long cap = purchased.value(key);
            if (!core::detail::sumFits(
                    previous, requestedInThisReturn.value(key, 0))) {
                m_db.rollback();
                result.error = QStringLiteral("returned quantity overflows");
                return result;
            }
            const long long alreadyReturned =
                previous + requestedInThisReturn.value(key, 0);
            if (alreadyReturned > cap || line.quantity > cap - alreadyReturned) {
                m_db.rollback();
                result.error =
                    QStringLiteral("cannot return %1: only %2 remain returnable on this line")
                        .arg(line.quantity)
                        .arg(cap - alreadyReturned);
                return result;
            }
            if (!core::detail::sumFits(
                    requestedInThisReturn.value(key, 0), line.quantity)) {
                m_db.rollback();
                result.error = QStringLiteral("returned quantity overflows");
                return result;
            }
            requestedInThisReturn[key] += line.quantity;

            long long pieces = line.quantity;
            if (*kind == core::UnitKind::Package) {
                if (product->piecesPerPackage <= 0) {
                    m_db.rollback();
                    result.error =
                        QStringLiteral("product '%1' has no pieces per package")
                            .arg(product->name);
                    return result;
                }
                if (!core::detail::productFits(line.quantity, product->piecesPerPackage)) {
                    m_db.rollback();
                    result.error = QStringLiteral("returned piece count overflows");
                    return result;
                }
                pieces = line.quantity * product->piecesPerPackage;
            }
            line.piecesConsumed = pieces;
        } else {
            // No product on the line means no shelf to move. The credit still
            // has to be right, which is what the line total above guarantees.
            line.piecesConsumed = 0;
        }

        if (!core::detail::sumFits(amountCents, line.totalCents)) {
            m_db.rollback();
            result.error = QStringLiteral("return total overflows");
            return result;
        }
        amountCents += line.totalCents;
        lines.append(line);
    }

    // C.
    core::SupplierReturn r;
    r.supplierId = supplierId;
    r.purchaseId = purchaseId;
    r.amountCents = amountCents;
    r.returnedAt = returnedAt.isEmpty() ? nowIso() : returnedAt;
    // A linked return always names its lines, and goods that went back to the
    // supplier are no longer on the shelves.
    r.removeFromStock = true;
    r.note = note;
    r.createdAt = nowIso();

    // D.
    const int returnId = m_returns.insert(r);
    if (returnId == 0) {
        m_db.rollback();
        result.error = stepError(m_db, QStringLiteral("inserting the supplier return"));
        return result;
    }

    // E. One line at a time, inside the transaction this service owns.
    for (core::SupplierReturnItem& line : lines) {
        line.returnId = returnId;
    }
    for (const core::SupplierReturnItem& line : lines) {
        if (m_returnItems.insert(line) == 0) {
            m_db.rollback();
            result.error = stepError(m_db, QStringLiteral("inserting a return item"));
            return result;
        }
    }

    // F. The goods leave the shelves by the same mechanism that put them there,
    // so the level after the return is the level the trigger has just written.
    // The average cost is deliberately left alone: the goods were bought at the
    // price that was paid for them, so removing them changes the level and not
    // the cost. Recomputing it here would rewrite the cost of everything still
    // on the shelf, which is what the purchase average is for.
    for (const core::SupplierReturnItem& line : lines) {
        if (!line.productId.has_value() || *line.productId <= 0
            || line.piecesConsumed <= 0) {
            continue;
        }
        const int productId = *line.productId;

        core::StockMovement movement;
        movement.productId = productId;
        // Cartons leave as the pieces they contain, not as the number of boxes:
        // the shelf counts pieces.
        movement.delta = -line.piecesConsumed;
        movement.reason = QStringLiteral("supplier_return");
        movement.reference = QStringLiteral("Supplier Return #%1").arg(returnId);
        movement.createdAt = QDateTime::currentDateTime();
        if (m_stockMovements.insert(movement) == 0) {
            m_db.rollback();
            result.error = stepError(m_db, QStringLiteral("inserting a stock movement"));
            return result;
        }

        // Read after the movement landed, because that is the insert the trigger
        // reacted to. Going below zero is allowed and reported, not refused.
        const std::optional<core::Product> product = m_products.findById(productId);
        if (product.has_value() && product->quantity < 0 && result.warning.isEmpty()) {
            result.warning = QStringLiteral("Stock is negative for %1").arg(product->name);
        }
    }

    // Z. Database::commit() rolls the transaction back itself when it fails, so
    // the connection stays usable for the next return.
    if (!m_db.commit()) {
        result.error = m_db.lastError();
        return result;
    }

    result.ok = true;
    result.returnId = returnId;
    return result;
}

SupplierReturnResult SupplierReturnService::recordGeneralReturn(int supplierId,
                                                                 long long amountCents,
                                                                 const QString& returnedAt,
                                                                 const QString& note)
{
    SupplierReturnResult result;

    // A. A credit that is not positive is not a credit, and would read as money
    // owed to the supplier once the balance subtracts it.
    if (!m_suppliers.findById(supplierId).has_value()) {
        result.error = QStringLiteral("supplier not found");
        return result;
    }
    if (amountCents <= 0) {
        result.error = QStringLiteral("amount must be positive");
        return result;
    }

    // B.
    if (!m_db.beginWriteTransaction()) {
        result.error = stepError(m_db, QStringLiteral("opening the transaction"));
        return result;
    }

    // C. No invoice and no lines, so the stock is left alone: there is nothing
    // here that says which shelf the goods came off.
    core::SupplierReturn r;
    r.supplierId = supplierId;
    r.purchaseId = std::nullopt;
    r.amountCents = amountCents;
    r.returnedAt = returnedAt.isEmpty() ? nowIso() : returnedAt;
    r.removeFromStock = false;
    r.note = note;
    r.createdAt = nowIso();

    // D.
    const int returnId = m_returns.insert(r);
    if (returnId == 0) {
        m_db.rollback();
        result.error = stepError(m_db, QStringLiteral("inserting the supplier return"));
        return result;
    }

    // Z.
    if (!m_db.commit()) {
        result.error = m_db.lastError();
        return result;
    }

    result.ok = true;
    result.returnId = returnId;
    return result;
}

} // namespace app::data
