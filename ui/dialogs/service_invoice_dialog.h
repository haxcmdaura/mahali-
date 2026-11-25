#pragma once

#include <QWidget>

#include "core/service_invoice.h"

namespace app::data {
class Database;
}

namespace app::ui {

// Saves a new or edited service invoice and returns its id, or 0 on cancel/failure.
int showServiceInvoiceDialog(QWidget* parent, app::data::Database& db,
                             const core::ServiceInvoice& initial = {});

}
