#include "quick_item_service.h"

#include <QSqlQuery>
#include <QVariant>

#include "date_utils.h"
#include "product_repository.h"

namespace app::data {

QuickItemService::QuickItemService(Database& db)
    : m_db(db)
{
}

QVector<DueRestock> QuickItemService::getDueRestocks(const QDate& today)
{
    QVector<DueRestock> due;
    ProductRepository products(m_db);
    const QString todayIso = today.toString(Qt::ISODate);

    for (const core::Product& product : products.findQuickItems()) {
        const QDate lastRestocked =
            QDate::fromString(product.lastRestockedAt.left(10), Qt::ISODate);
        const int cycleDays = product.restockDays > 0 ? product.restockDays : 1;
        if (lastRestocked.isValid() && lastRestocked.daysTo(today) < cycleDays) {
            continue;
        }

        const QString since = product.lastRestockedAt.trimmed().isEmpty()
                                  ? QString()
                                  : widenToDayStart(product.lastRestockedAt);
        QSqlQuery sold(m_db.handle());
        QString sql = QStringLiteral(
            "SELECT COALESCE(SUM(si.quantity), 0) FROM sale_items si "
            "JOIN sales s ON s.id = si.sale_id WHERE si.product_id = ?");
        if (!since.isEmpty()) {
            sql += QStringLiteral(" AND s.created_at >= ?");
        }
        sold.prepare(sql);
        sold.addBindValue(product.id);
        if (!since.isEmpty()) {
            sold.addBindValue(since);
        }
        if (!sold.exec() || !sold.next()) {
            m_db.recordError(sold.lastError(), QStringLiteral("QuickItemService::getDueRestocks"));
            continue;
        }

        DueRestock restock;
        restock.productId = product.id;
        restock.productName = product.name;
        restock.quantitySold = sold.value(0).toLongLong();
        due.push_back(restock);

        QSqlQuery update(m_db.handle());
        update.prepare(QStringLiteral(
            "UPDATE products SET last_restocked_at = ? WHERE id = ?"));
        update.addBindValue(todayIso);
        update.addBindValue(product.id);
        if (!update.exec()) {
            m_db.recordError(update.lastError(),
                             QStringLiteral("QuickItemService::getDueRestocks"));
        }
    }
    return due;
}

} // namespace app::data
