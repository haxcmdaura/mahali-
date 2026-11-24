#include "service_invoice_repository.h"

#include <QSqlQuery>
#include <QVariant>

namespace app::data {

namespace {

const char* kColumns = "id, type, invoice_date, amount_cents, due_date, status, notes";

core::ServiceInvoice invoiceFromQuery(const QSqlQuery& query)
{
    core::ServiceInvoice invoice;
    invoice.id = query.value(0).toInt();
    invoice.type = query.value(1).toString();
    invoice.invoiceDate = query.value(2).toString();
    invoice.amountCents = query.value(3).toLongLong();
    invoice.dueDate = query.value(4).toString();
    invoice.status = query.value(5).toString();
    invoice.notes = query.value(6).toString();
    return invoice;
}

QVariant nullableText(const QString& value)
{
    return value.trimmed().isEmpty() ? QVariant() : QVariant(value);
}

QString statusOrDefault(const QString& status)
{
    return status.trimmed().isEmpty() ? QStringLiteral("unpaid") : status;
}

} // namespace

ServiceInvoiceRepository::ServiceInvoiceRepository(Database& db)
    : m_db(db)
{
}

std::vector<core::ServiceInvoice> ServiceInvoiceRepository::findAll() const
{
    std::vector<core::ServiceInvoice> invoices;
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM service_invoices "
                                 "ORDER BY invoice_date DESC, id DESC")
                      .arg(QLatin1StringView(kColumns)));
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("ServiceInvoiceRepository::findAll"));
        return invoices;
    }
    while (query.next()) {
        invoices.push_back(invoiceFromQuery(query));
    }
    return invoices;
}

std::optional<core::ServiceInvoice> ServiceInvoiceRepository::findById(int id) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM service_invoices WHERE id = ?")
                      .arg(QLatin1StringView(kColumns)));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return invoiceFromQuery(query);
}

int ServiceInvoiceRepository::save(const core::ServiceInvoice& invoice)
{
    QSqlQuery query(m_db.handle());
    if (invoice.id == 0) {
        query.prepare(QStringLiteral(
            "INSERT INTO service_invoices "
            "(type, invoice_date, amount_cents, due_date, status, notes) "
            "VALUES (?, ?, ?, ?, ?, ?)"));
        query.addBindValue(invoice.type);
        query.addBindValue(invoice.invoiceDate);
        query.addBindValue(invoice.amountCents);
        query.addBindValue(nullableText(invoice.dueDate));
        query.addBindValue(statusOrDefault(invoice.status));
        query.addBindValue(nullableText(invoice.notes));
        if (!query.exec()) {
            m_db.recordError(query.lastError(), QStringLiteral("ServiceInvoiceRepository::save"));
            return 0;
        }
        return query.lastInsertId().toInt();
    }

    query.prepare(QStringLiteral(
        "UPDATE service_invoices SET type = ?, invoice_date = ?, amount_cents = ?, "
        "due_date = ?, status = ?, notes = ? WHERE id = ?"));
    query.addBindValue(invoice.type);
    query.addBindValue(invoice.invoiceDate);
    query.addBindValue(invoice.amountCents);
    query.addBindValue(nullableText(invoice.dueDate));
    query.addBindValue(statusOrDefault(invoice.status));
    query.addBindValue(nullableText(invoice.notes));
    query.addBindValue(invoice.id);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("ServiceInvoiceRepository::save"));
        return 0;
    }
    return query.numRowsAffected() > 0 ? invoice.id : 0;
}

bool ServiceInvoiceRepository::remove(int id)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("DELETE FROM service_invoices WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("ServiceInvoiceRepository::remove"));
        return false;
    }
    return query.numRowsAffected() > 0;
}

} // namespace app::data
