#include "zakat_service.h"

#include <QSqlError>

#include "setting_repository.h"
#include "zakat_history_repository.h"
#include "core/zakat_notifier.h"

namespace app::data {

ZakatService::ZakatService(Database& db) : m_db(db) {}

bool ZakatService::markPaid(int year,
                            long long nisabCents,
                            long long baseCents,
                            long long dueCents,
                            long long goldPriceCents,
                            const QDate& paidDate)
{
    if (!m_db.beginWriteTransaction()) {
        m_lastError = m_db.lastError().isEmpty()
            ? QStringLiteral("could not start zakat transaction")
            : m_db.lastError();
        return false;
    }

    SettingRepository settings(m_db);
    settings.set(QStringLiteral("nisab_cents"), QString::number(nisabCents));
    settings.set(QStringLiteral("zakat_paid_year"), QString::number(year));
    settings.set(QStringLiteral("zakat_paid_cents"), QString::number(dueCents));
    settings.set(QStringLiteral("zakat_paid_date"), paidDate.toString(Qt::ISODate));

    ZakatHistoryRepository history(m_db);
    if (!history.insertOrIgnore(year, nisabCents, baseCents, dueCents, goldPriceCents)) {
        m_db.rollback();
        m_lastError = m_db.lastError().isEmpty()
            ? QStringLiteral("could not update zakat history")
            : m_db.lastError();
        return false;
    }
    if (!history.markPaid(year, dueCents, paidDate.toString(Qt::ISODate))) {
        m_db.rollback();
        m_lastError = m_db.lastError().isEmpty()
            ? QStringLiteral("could not mark zakat year as paid")
            : m_db.lastError();
        return false;
    }

    core::markZakatYearAsSeen(m_db, year);

    if (!m_db.commit()) {
        m_db.rollback();
        m_lastError = m_db.lastError();
        return false;
    }
    return true;
}

QString ZakatService::lastError() const
{
    return m_lastError;
}

} // namespace app::data
