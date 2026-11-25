#pragma once

#include <optional>
#include <vector>

#include "core/service_invoice.h"
#include "database.h"

namespace app::data {

class ServiceInvoiceRepository {
public:
    explicit ServiceInvoiceRepository(Database& db);

    std::vector<core::ServiceInvoice> findAll() const;
    std::optional<core::ServiceInvoice> findById(int id) const;
    int save(const core::ServiceInvoice& invoice);
    bool remove(int id);

private:
    Database& m_db;
};

} // namespace app::data
