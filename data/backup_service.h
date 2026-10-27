#pragma once

#include <QDateTime>
#include <QString>

#include <functional>

namespace app::data {

class Database;

// Owns the retention policy for the copies Database::backupTo writes. The clock
// is injected so the daily throttle and the rolling window are testable without
// waiting for real days to pass.
class BackupService {
public:
    using Clock = std::function<QDateTime()>;

    static constexpr int kRetentionCount = 14;
    static constexpr int kMinimumIntervalHours = 24;

    explicit BackupService(Database& db, QString backupDirectory = QString(),
                           Clock clock = Clock());

    struct Result {
        bool ok = false;
        bool created = false;
        QString path;
        QString error;
    };

    // Creates at most one automatic copy per kMinimumIntervalHours, then trims
    // the directory to kRetentionCount copies. A skipped run is ok, not failed.
    Result runDailyBackup();

    QString backupDirectory() const;
    static QString defaultBackupDirectory();

private:
    Database& m_db;
    QString m_backupDirectory;
    Clock m_clock;
};

} // namespace app::data
