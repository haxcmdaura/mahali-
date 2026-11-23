#pragma once

#include <optional>

#include "core/occasion.h"
#include "audit_log_repository.h"
#include "database.h"
#include "occasion_repository.h"
#include "setting_repository.h"

namespace app::data {

// Which occasion is running. The date window decides the default; settings hold
// a manual choice between occasions that are open today.
class OccasionService {
public:
    OccasionService(Database& db,
                    OccasionRepository& occasions,
                    SettingRepository& settings);

    // The occasion open today, preferring a manual choice that is also open today.
    std::optional<core::Occasion> current() const;

    // Chooses an occasion manually, writing its id into settings. The occasion has
    // to exist and be active: choosing a disabled one would stamp sales with an
    // event the shop has turned off.
    // Runs inside a transaction so the setting and the audit log commit together.
    // Returns false if the occasion does not exist, is disabled, or the audit
    // log write fails.
    bool activate(int occasionId);

    // Clears the manual choice by dropping the setting. Runs inside a
    // transaction so the setting and the audit log commit together. Returns false
    // if the audit log write fails.
    bool deactivate();

    // Whether the given date falls inside the occasion's inclusive calendar-date window.
    bool isWithinRange(const core::Occasion& o, const QString& nowIso) const;

private:
    Database& m_db;
    OccasionRepository& m_occasions;
    SettingRepository& m_settings;
    AuditLogRepository m_audit;
};

} // namespace app::data
