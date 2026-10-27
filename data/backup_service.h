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

    struct RestoreResult {
        bool ok = false;
        QString error;
        QString safetyCopyPath;
    };

    // Creates at most one automatic copy per kMinimumIntervalHours, then trims
    // the directory to kRetentionCount copies. A skipped run is ok, not failed.
    Result runDailyBackup();

    // Validates sourcePath, copies the current database to a timestamped
    // *.before-restore file, and only then replaces it. Static and path-based
    // because the current Database must already be closed: callers run this
    // before opening the database on the next start, not while it is live.
    static RestoreResult restore(const QString& sourcePath, const QString& databasePath,
                                 Clock clock = Clock());

    QString backupDirectory() const;
    static QString defaultBackupDirectory();

private:
    Database& m_db;
    QString m_backupDirectory;
    Clock m_clock;
};

} // namespace app::data
