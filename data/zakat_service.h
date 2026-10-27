#pragma once

#include <QDate>
#include <QString>

#include "database.h"

namespace app::data {

class ZakatService {
public:
    explicit ZakatService(Database& db);

    bool markPaid(int year,
                  long long nisabCents,
                  long long baseCents,
                  long long dueCents,
                  long long goldPriceCents,
                  const QDate& paidDate);
    QString lastError() const;

private:
    Database& m_db;
    QString m_lastError;
};

} // namespace app::data
