#include "occasion_service.h"

#include "audit_log_repository.h"
#include "core/hijri_utils.h"

#include <QDate>

namespace app::data {

namespace {
// The one setting that says which occasion is running. Kept here so the key is
// written in one place: a second spelling of it would leave an occasion running
// that nothing can turn off.
const char* kActiveOccasionKey = "active_occasion_id";
const char* kModeKey = "occasion_mode";
const char* kAutoMode = "auto";
const char* kManualMode = "manual";
const char* kDisabledMode = "disabled";

std::optional<QDate> datePart(const QString& value)
{
    const QDate date = QDate::fromString(value.trimmed().left(10), Qt::ISODate);
    if (!date.isValid()) {
        return std::nullopt;
    }
    return date;
}

bool sameThemeKey(const QString& left, const QString& right)
{
    return left.trimmed().toLower() == right.trimmed().toLower();
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

QString OccasionService::mode() const
{
    const QString stored = m_settings.value(QString::fromLatin1(kModeKey))
                               .value_or(QString::fromLatin1(kAutoMode));
    if (stored == QString::fromLatin1(kManualMode)
        || stored == QString::fromLatin1(kDisabledMode)) {
        return stored;
    }
    return QString::fromLatin1(kAutoMode);
}

std::optional<core::Occasion> OccasionService::current() const
{
    return current(QDate::currentDate());
}

std::optional<core::Occasion> OccasionService::current(const QDate& date) const
{
    const QString currentMode = mode();
    if (currentMode == QString::fromLatin1(kDisabledMode)) {
        return std::nullopt;
    }
    if (currentMode == QString::fromLatin1(kManualMode)) {
        const std::optional<QString> stored =
            m_settings.value(QString::fromLatin1(kActiveOccasionKey));
        if (!stored.has_value()) {
            return std::nullopt;
        }
        bool ok = false;
        const int id = stored->toInt(&ok);
        if (!ok || id <= 0) {
            return std::nullopt;
        }
        return m_occasions.findById(id);
    }

    const QString hijriKey = core::getCurrentHijriOccasion(date);
    if (!hijriKey.isEmpty()) {
        for (const core::Occasion& occasion : m_occasions.findActive()) {
            if (sameThemeKey(occasion.themeKey, hijriKey)) {
                return occasion;
            }
        }
    }

    const QString today = date.toString(Qt::ISODate);
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
    m_settings.set(QString::fromLatin1(kModeKey), QString::fromLatin1(kManualMode));

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
    return stored.has_value() && stored->toInt() == occasionId
        && mode() == QString::fromLatin1(kManualMode);
}

bool OccasionService::setAutoMode()
{
    if (!m_db.beginWriteTransaction()) {
        return false;
    }

    m_settings.set(QString::fromLatin1(kModeKey), QString::fromLatin1(kAutoMode));
    m_settings.remove(QString::fromLatin1(kActiveOccasionKey));

    if (m_audit.record(QStringLiteral("occasion_mode_auto"),
                       QStringLiteral("occasion mode set to auto"))
        == 0) {
        m_db.rollback();
        return false;
    }

    if (!m_db.commit()) {
        return false;
    }
    return true;
}

bool OccasionService::disable()
{
    if (!m_db.beginWriteTransaction()) {
        return false;
    }

    m_settings.set(QString::fromLatin1(kModeKey), QString::fromLatin1(kDisabledMode));
    m_settings.remove(QString::fromLatin1(kActiveOccasionKey));

    if (m_audit.record(QStringLiteral("occasion_mode_disabled"),
                       QStringLiteral("occasion mode set to disabled"))
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
