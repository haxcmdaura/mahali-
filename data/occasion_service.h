#pragma once

#include <QDate>

#include <optional>

#include "core/occasion.h"
#include "audit_log_repository.h"
#include "database.h"
#include "occasion_repository.h"
#include "setting_repository.h"

namespace app::data {

// Which occasion is running. A mode setting selects automatic date windows, a
// manually forced occasion, or no occasion at all.
class OccasionService {
public:
    OccasionService(Database& db,
                    OccasionRepository& occasions,
                    SettingRepository& settings);

    QString mode() const;

    std::optional<core::Occasion> current() const;
    std::optional<core::Occasion> current(const QDate& date) const;

    // Chooses an occasion manually and switches the mode to manual. The occasion
    // has to exist; an inactive occasion is reactivated as part of the choice.
    // Runs inside a transaction so the settings and the audit log commit together.
    bool activate(int occasionId);

    bool setAutoMode();
    bool disable();

    // Whether the given date falls inside the occasion's inclusive calendar-date window.
    bool isWithinRange(const core::Occasion& o, const QString& nowIso) const;

private:
    Database& m_db;
    OccasionRepository& m_occasions;
    SettingRepository& m_settings;
    AuditLogRepository m_audit;
};

} // namespace app::data
