#include "occasion_service.h"

#include "audit_log_repository.h"

#include <QDate>

namespace app::data {

namespace {
// The one setting that says which occasion is running. Kept here so the key is
// written in one place: a second spelling of it would leave an occasion running
// that nothing can turn off.
const char* kActiveOccasionKey = "active_occasion_id";

std::optional<QDate> datePart(const QString& value)
{
    const QDate date = QDate::fromString(value.trimmed().left(10), Qt::ISODate);
    if (!date.isValid()) {
        return std::nullopt;
    }
    return date;
}
} // namespace

OccasionService::OccasionService(Database& db,
                                 OccasionRepository& occasions,
                                 SettingRepository& settings)
    : m_db(db)
    , m_occasions(occasions)
    , m_settings(settings)
    , m_audit(db)
{
}

std::optional<core::Occasion> OccasionService::current() const
{
    const QString today = QDate::currentDate().toString(Qt::ISODate);

    const std::optional<QString> stored =
        m_settings.value(QString::fromLatin1(kActiveOccasionKey));
    if (stored.has_value()) {
        bool ok = false;
        const int id = stored->toInt(&ok);
        if (ok && id > 0) {
            const std::optional<core::Occasion> manual = m_occasions.findById(id);
            if (manual.has_value() && manual->active && isWithinRange(*manual, today)) {
                return manual;
            }
        }
    }

    const QVector<core::Occasion> open = m_occasions.findCurrent(today);
    for (const core::Occasion& occasion : open) {
        if (isWithinRange(occasion, today)) {
            return occasion;
        }
    }
    return std::nullopt;
}

bool OccasionService::activate(int occasionId)
{
    const std::optional<core::Occasion> occasion = m_occasions.findById(occasionId);
    if (!occasion.has_value()) {
        return false;
    }
    if (!occasion->active) {
        return false;
    }

    if (!m_db.beginWriteTransaction()) {
        return false;
    }

    m_settings.set(QString::fromLatin1(kActiveOccasionKey), QString::number(occasionId));

    // Inside the transaction: the setting and its audit entry commit or roll
    // back together. An activation that survives a rollback would show an
    // occasion running that never actually ran.
    if (m_audit.record(QStringLiteral("occasion_activate"),
                       QStringLiteral("occasion %1 (%2)")
                           .arg(occasionId)
                           .arg(occasion->name))
        == 0) {
        m_db.rollback();
        return false;
    }

    if (!m_db.commit()) {
        return false;
    }

    const std::optional<QString> stored =
        m_settings.value(QString::fromLatin1(kActiveOccasionKey));
    return stored.has_value() && stored->toInt() == occasionId;
}

bool OccasionService::deactivate()
{
    if (!m_db.beginWriteTransaction()) {
        return false;
    }

    m_settings.remove(QString::fromLatin1(kActiveOccasionKey));

    if (m_audit.record(QStringLiteral("occasion_deactivate"),
                       QStringLiteral("occasion cleared"))
        == 0) {
        m_db.rollback();
        return false;
    }

    if (!m_db.commit()) {
        return false;
    }
    return true;
}

bool OccasionService::isWithinRange(const core::Occasion& o, const QString& nowIso) const
{
    const std::optional<QDate> start = datePart(o.startsAt);
    const std::optional<QDate> end = datePart(o.endsAt);
    const std::optional<QDate> now = datePart(nowIso);
    if (!start.has_value() || !end.has_value() || !now.has_value()) {
        return false;
    }
    if (*now < *start) {
        return false;
    }
    if (*now > *end) {
        return false;
    }
    return true;
}

} // namespace app::data
