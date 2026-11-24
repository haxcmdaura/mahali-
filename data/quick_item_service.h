#pragma once

#include <QDate>
#include <QString>
#include <QVector>

#include "database.h"

namespace app::data {

struct DueRestock {
    int productId = 0;
    QString productName;
    long long quantitySold = 0;
};

class QuickItemService {
public:
    explicit QuickItemService(Database& db);

    QVector<DueRestock> getDueRestocks(const QDate& today = QDate::currentDate());

private:
    Database& m_db;
};

} // namespace app::data
