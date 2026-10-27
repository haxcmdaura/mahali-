#include "backup_service.h"

#include "database.h"
#include "schema_migrations.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStringList>
#include <QUuid>

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

struct BackupCheck {
    bool ok = false;
    int schemaVersion = 0;
    QString error;
};

BackupCheck checkBackup(const QString& path)
{
    BackupCheck result;
    const QString connection =
        QStringLiteral("mahali_backup_check_%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

    {
        QSqlDatabase source = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        source.setDatabaseName(path);
        source.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!source.open()) {
            result.error = QStringLiteral("could not open backup: %1")
                               .arg(source.lastError().text());
        } else {
            QSqlQuery query(source);
            if (!query.exec(QStringLiteral("PRAGMA quick_check"))) {
                result.error = QStringLiteral("backup quick_check could not run: %1")
                                   .arg(query.lastError().text());
            } else if (!query.next()) {
                result.error = QStringLiteral("backup quick_check returned no result");
            } else if (query.value(0).toString() != QLatin1String("ok")) {
                result.error = QStringLiteral("backup quick_check failed: %1")
                                   .arg(query.value(0).toString());
            } else {
                result.schemaVersion = readSchemaVersion(source);
                result.ok = true;
            }
            source.close();
        }
    }

    QSqlDatabase::removeDatabase(connection);
    return result;
}

QString uniqueSafetyPath(const QString& databasePath, const QString& stamp)
{
    const QString base = databasePath + QStringLiteral(".before-restore-") + stamp;
    const QStringList suffixes = {QString(), QStringLiteral("-wal"),
                                  QStringLiteral("-shm"), QStringLiteral("-journal")};
    for (int attempt = 0;; ++attempt) {
        const QString candidate = attempt == 0 ? base : base + QStringLiteral("-%1").arg(attempt);
        bool taken = false;
        for (const QString& suffix : suffixes) {
            if (QFileInfo::exists(candidate + suffix)) {
                taken = true;
                break;
            }
        }
        if (!taken) {
            return candidate;
        }
    }
}

} // namespace

BackupService::RestoreResult BackupService::restore(const QString& sourcePath,
                                                   const QString& databasePath,
                                                   Clock clock)
{
    RestoreResult result;

    if (!QFileInfo(sourcePath).isFile()) {
        result.error = QStringLiteral("backup does not exist: %1").arg(sourcePath);
        return result;
    }
    if (!QFileInfo(databasePath).isFile()) {
        result.error = QStringLiteral("current database does not exist: %1").arg(databasePath);
        return result;
    }
    if (QFileInfo(sourcePath).canonicalFilePath()
        == QFileInfo(databasePath).canonicalFilePath()) {
        result.error = QStringLiteral("backup is the current database");
        return result;
    }

    const BackupCheck check = checkBackup(sourcePath);
    if (!check.ok) {
        result.error = check.error;
        return result;
    }
    if (check.schemaVersion > kSchemaVersion) {
        result.error = QStringLiteral("backup schema version %1 is newer than this build's %2")
                           .arg(check.schemaVersion)
                           .arg(kSchemaVersion);
        return result;
    }

    if (!clock) {
        clock = []() { return QDateTime::currentDateTimeUtc(); };
    }
    const QDateTime now = clock().toUTC();
    if (!now.isValid()) {
        result.error = QStringLiteral("restore clock returned an invalid time");
        return result;
    }

    const QString safetyPath = uniqueSafetyPath(databasePath, now.toString(kBackupNameFormat));
    if (!QFile::copy(databasePath, safetyPath)) {
        result.error = QStringLiteral("could not create safety copy: %1").arg(safetyPath);
        return result;
    }

    const QStringList companionSuffixes = {QStringLiteral("-wal"), QStringLiteral("-shm"),
                                           QStringLiteral("-journal")};
    QStringList movedCompanions;
    auto rollback = [&]() {
        for (const QString& suffix : movedCompanions) {
            QFile::rename(safetyPath + suffix, databasePath + suffix);
        }
        QFile::remove(safetyPath);
    };

    for (const QString& suffix : companionSuffixes) {
        const QString from = databasePath + suffix;
        if (!QFileInfo::exists(from)) {
            continue;
        }
        if (!QFile::rename(from, safetyPath + suffix)) {
            rollback();
            result.error = QStringLiteral("could not move database companion to safety copy: %1")
                               .arg(from);
            return result;
        }
        movedCompanions.append(suffix);
    }

    QFile input(sourcePath);
    if (!input.open(QIODevice::ReadOnly)) {
        rollback();
        result.error = QStringLiteral("could not read backup: %1").arg(sourcePath);
        return result;
    }

    QSaveFile output(databasePath);
    if (!output.open(QIODevice::WriteOnly)) {
        rollback();
        result.error = QStringLiteral("could not replace database: %1")
                           .arg(output.errorString());
        return result;
    }
    while (!input.atEnd()) {
        const QByteArray chunk = input.read(1024 * 1024);
        if (chunk.isEmpty() && input.error() != QFileDevice::NoError) {
            output.cancelWriting();
            rollback();
            result.error = QStringLiteral("could not read backup: %1").arg(input.errorString());
            return result;
        }
        if (output.write(chunk) != chunk.size()) {
            output.cancelWriting();
            rollback();
            result.error = QStringLiteral("could not write restored database: %1")
                               .arg(output.errorString());
            return result;
        }
    }
    if (!output.commit()) {
        rollback();
        result.error = QStringLiteral("could not replace database: %1")
                           .arg(output.errorString());
        return result;
    }

    result.ok = true;
    result.safetyCopyPath = safetyPath;
    return result;
}

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
