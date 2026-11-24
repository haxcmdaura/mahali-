#pragma once

#include <QString>

namespace app::core {

// A household utility bill: electricity, gas, or water. Kept apart from the
// shop's supplier purchases because it is a service paid on a due date, not
// stock received from a supplier.
struct ServiceInvoice {
    int id = 0;
    QString type;
    QString invoiceDate;
    long long amountCents = 0;
    QString dueDate;
    QString status = QStringLiteral("unpaid");
    QString notes;
};

} // namespace app::core
