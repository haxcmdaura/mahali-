#include "backup_service.h"

#include "database.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

#include <utility>

namespace app::data {

BackupService::BackupService(Database& db, QString backupDirectory, Clock clock)
    : m_db(db)
    , m_backupDirectory(backupDirectory.isEmpty() ? defaultBackupDirectory()
                                                   : std::move(backupDirectory))
    , m_clock(std::move(clock))
{
    if (!m_clock) {
        m_clock = []() { return QDateTime::currentDateTimeUtc(); };
    }
}

QString BackupService::defaultBackupDirectory()
{
    const QString dataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(dataDir).filePath(QStringLiteral("backups"));
}

QString BackupService::backupDirectory() const
{
    return m_backupDirectory;
}

namespace {

const QString kBackupNameFormat = QStringLiteral("yyyyMMdd-HHmmss");
const QString kBackupFilePattern = QStringLiteral("mahali-*.sqlite");

QStringList backupFiles(const QDir& directory)
{
    return directory.entryList(QStringList() << kBackupFilePattern,
                               QDir::Files, QDir::Name);
}

QDateTime backupTime(const QString& filePath)
{
    const QString name = QFileInfo(filePath).completeBaseName();
    if (!name.startsWith(QStringLiteral("mahali-"))) {
        return {};
    }
    const QDateTime parsed =
        QDateTime::fromString(name.mid(7), kBackupNameFormat);
    if (!parsed.isValid()) {
        return {};
    }
    return QDateTime(parsed.date(), parsed.time(), Qt::UTC);
}

} // namespace

BackupService::Result BackupService::runDailyBackup()
{
    Result result;

    QDir directory(m_backupDirectory);
    if (!directory.exists() && !QDir().mkpath(m_backupDirectory)) {
        result.error = QStringLiteral("could not create backup directory: %1")
                           .arg(m_backupDirectory);
        return result;
    }

    const QDateTime now = m_clock().toUTC();
    if (!now.isValid()) {
        result.error = QStringLiteral("backup clock returned an invalid time");
        return result;
    }

    const QStringList before = backupFiles(directory);
    for (int i = before.size() - 1; i >= 0; --i) {
        const QDateTime latest = backupTime(directory.filePath(before.at(i)));
        if (!latest.isValid()) {
            continue;
        }
        if (latest.secsTo(now) < kMinimumIntervalHours * 60 * 60) {
            result.ok = true;
            return result;
        }
        break;
    }

    const QString path = directory.filePath(
        QStringLiteral("mahali-%1.sqlite").arg(now.toString(kBackupNameFormat)));
    if (!m_db.backupTo(path)) {
        result.error = m_db.lastError();
        return result;
    }

    result.created = true;
    result.path = path;

    const QStringList after = backupFiles(directory);
    const int excess = after.size() - kRetentionCount;
    for (int i = 0; i < excess; ++i) {
        const QString oldPath = directory.filePath(after.at(i));
        if (!QFile::remove(oldPath)) {
            result.error = QStringLiteral("could not remove old backup: %1").arg(oldPath);
            return result;
        }
    }

    result.ok = true;
    return result;
}

} // namespace app::data
