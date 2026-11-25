#include <QtTest/QtTest>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

#include "data/backup_service.h"
#include "data/database.h"
#include "data/schema_migrations.h"

using namespace app;

class BackupServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void dailyBackupRunsAtMostOncePer24Hours();
    void dailyBackupKeepsLatestFourteen();
    void backupFailureIsReportedWithoutThrowing();
    void restoreReplacesDatabaseWithValidatedBackup();
    void restoreRejectsCorruptBackupAndLeavesCurrentIntact();
    void restoreRejectsNewerSchemaAndLeavesCurrentIntact();
    void stagedRestoreAppliesBeforeNextOpen();

private:
    static QStringList backupsIn(const QString& directory);
    static QString clockStamp(const QDateTime& time);
    static QByteArray readFile(const QString& path);
    static QString productName(const QString& path);
    static bool setUserVersion(const QString& path, int version);

    QTemporaryDir m_dir;
    QString m_dbPath;
};

void BackupServiceTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_dbPath = m_dir.filePath(QStringLiteral("backup-service.sqlite"));
    QFile::remove(m_dbPath);
    data::Database db(m_dbPath);
}

QStringList BackupServiceTest::backupsIn(const QString& directory)
{
    return QDir(directory).entryList(QStringList() << QStringLiteral("mahali-*.sqlite"),
                                     QDir::Files, QDir::Name);
}

QString BackupServiceTest::clockStamp(const QDateTime& time)
{
    return time.toString(QStringLiteral("yyyyMMdd-HHmmss"));
}

void BackupServiceTest::dailyBackupRunsAtMostOncePer24Hours()
{
    QTemporaryDir work;
    QVERIFY(work.isValid());
    const QString backupDir = work.filePath(QStringLiteral("backups"));
    data::Database db(m_dbPath);

    QDateTime now(QDate(2026, 1, 1), QTime(9, 0), Qt::UTC);
    data::BackupService service(db, backupDir, [&now]() { return now; });

    const data::BackupService::Result first = service.runDailyBackup();
    QVERIFY2(first.ok, qPrintable(first.error));
    QVERIFY(first.created);
    QVERIFY(QFile::exists(first.path));
    QCOMPARE(backupsIn(backupDir).size(), 1);

    now = QDateTime(QDate(2026, 1, 1), QTime(18, 0), Qt::UTC);
    const data::BackupService::Result sameDay = service.runDailyBackup();
    QVERIFY2(sameDay.ok, qPrintable(sameDay.error));
    QVERIFY(!sameDay.created);
    QCOMPARE(backupsIn(backupDir).size(), 1);

    now = QDateTime(QDate(2026, 1, 2), QTime(8, 59), Qt::UTC);
    const data::BackupService::Result justUnderADay = service.runDailyBackup();
    QVERIFY2(justUnderADay.ok, qPrintable(justUnderADay.error));
    QVERIFY(!justUnderADay.created);
    QCOMPARE(backupsIn(backupDir).size(), 1);

    now = QDateTime(QDate(2026, 1, 2), QTime(9, 0), Qt::UTC);
    const data::BackupService::Result nextDay = service.runDailyBackup();
    QVERIFY2(nextDay.ok, qPrintable(nextDay.error));
    QVERIFY(nextDay.created);
    QCOMPARE(backupsIn(backupDir).size(), 2);
}

void BackupServiceTest::dailyBackupKeepsLatestFourteen()
{
    QTemporaryDir work;
    QVERIFY(work.isValid());
    const QString backupDir = work.filePath(QStringLiteral("backups"));
    data::Database db(m_dbPath);

    QDateTime now(QDate(2026, 1, 1), QTime(12, 0), Qt::UTC);
    data::BackupService service(db, backupDir, [&now]() { return now; });

    for (int day = 0; day < 20; ++day) {
        now = QDateTime(QDate(2026, 1, 1).addDays(day), QTime(12, 0), Qt::UTC);
        const data::BackupService::Result result = service.runDailyBackup();
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(result.created);
    }

    const QStringList files = backupsIn(backupDir);
    QCOMPARE(files.size(), data::BackupService::kRetentionCount);
    QCOMPARE(files.first(),
             QStringLiteral("mahali-%1.sqlite").arg(clockStamp(
                 QDateTime(QDate(2026, 1, 7), QTime(12, 0), Qt::UTC))));
    QCOMPARE(files.last(),
             QStringLiteral("mahali-%1.sqlite").arg(clockStamp(
                 QDateTime(QDate(2026, 1, 20), QTime(12, 0), Qt::UTC))));
}

void BackupServiceTest::backupFailureIsReportedWithoutThrowing()
{
    QTemporaryDir work;
    QVERIFY(work.isValid());
    const QString blockerPath = work.filePath(QStringLiteral("blocked"));
    QFile blocker(blockerPath);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.write("not a directory");
    blocker.close();

    data::Database db(m_dbPath);
    QDateTime now(QDate(2026, 2, 1), QTime(12, 0), Qt::UTC);
    data::BackupService service(db, blockerPath + QStringLiteral("/backups"),
                                [&now]() { return now; });

    const data::BackupService::Result result = service.runDailyBackup();
    QVERIFY(!result.ok);
    QVERIFY(!result.created);
    QVERIFY(!result.error.isEmpty());
}

QByteArray BackupServiceTest::readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

QString BackupServiceTest::productName(const QString& path)
{
    data::Database db(path);
    QSqlQuery query(db.handle());
    if (!query.exec(QStringLiteral("SELECT name FROM products ORDER BY id LIMIT 1"))
        || !query.next()) {
        return {};
    }
    return query.value(0).toString();
}

bool BackupServiceTest::setUserVersion(const QString& path, int version)
{
    const QString connection =
        QStringLiteral("tst_backup_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(path);
        if (db.open()) {
            QSqlQuery query(db);
            ok = query.exec(QStringLiteral("PRAGMA user_version = %1").arg(version));
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return ok;
}

void BackupServiceTest::restoreReplacesDatabaseWithValidatedBackup()
{
    QTemporaryDir work;
    QVERIFY(work.isValid());
    const QString currentPath = work.filePath(QStringLiteral("current.sqlite"));
    const QString backupPath = work.filePath(QStringLiteral("backup.sqlite"));

    {
        data::Database db(currentPath);
        QSqlQuery insert(db.handle());
        QVERIFY(insert.prepare(QStringLiteral("INSERT INTO products (name) VALUES (?)")));
        insert.addBindValue(QStringLiteral("original"));
        QVERIFY(insert.exec());
        QVERIFY2(db.backupTo(backupPath), qPrintable(db.lastError()));
    }

    {
        data::Database db(currentPath);
        QSqlQuery update(db.handle());
        QVERIFY(update.exec(QStringLiteral("UPDATE products SET name = 'changed'")));
    }

    const QDateTime now(QDate(2026, 2, 2), QTime(12, 30), Qt::UTC);
    const data::BackupService::RestoreResult result =
        data::BackupService::restore(backupPath, currentPath, [&now]() { return now; });

    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.safetyCopyPath,
             currentPath + QStringLiteral(".before-restore-20260202-123000"));
    QVERIFY(QFile::exists(result.safetyCopyPath));
    QCOMPARE(productName(currentPath), QStringLiteral("original"));
    QCOMPARE(productName(result.safetyCopyPath), QStringLiteral("changed"));
}

void BackupServiceTest::stagedRestoreAppliesBeforeNextOpen()
{
    QTemporaryDir work;
    QVERIFY(work.isValid());
    const QString currentPath = work.filePath(QStringLiteral("current.sqlite"));
    const QString backupPath = work.filePath(QStringLiteral("backup.sqlite"));

    {
        data::Database db(currentPath);
        QSqlQuery insert(db.handle());
        QVERIFY(insert.prepare(QStringLiteral("INSERT INTO products (name) VALUES (?)")));
        insert.addBindValue(QStringLiteral("original"));
        QVERIFY(insert.exec());
        QVERIFY2(db.backupTo(backupPath), qPrintable(db.lastError()));
    }

    {
        data::Database db(currentPath);
        QSqlQuery update(db.handle());
        QVERIFY(update.exec(QStringLiteral("UPDATE products SET name = 'changed'")));

        QString error;
        QVERIFY2(data::BackupService::stageRestore(backupPath, currentPath, &error),
                 qPrintable(error));
    }

    QCOMPARE(productName(currentPath), QStringLiteral("changed"));
    const data::BackupService::PendingRestoreResult applied =
        data::BackupService::applyPendingRestore(currentPath);
    QVERIFY2(applied.ok, qPrintable(applied.error));
    QVERIFY(applied.applied);
    QVERIFY(QFile::exists(applied.safetyCopyPath));
    QCOMPARE(productName(currentPath), QStringLiteral("original"));
    QCOMPARE(productName(applied.safetyCopyPath), QStringLiteral("changed"));

    const data::BackupService::PendingRestoreResult again =
        data::BackupService::applyPendingRestore(currentPath);
    QVERIFY2(again.ok, qPrintable(again.error));
    QVERIFY(!again.applied);
}

void BackupServiceTest::restoreRejectsCorruptBackupAndLeavesCurrentIntact()
{
    QTemporaryDir work;
    QVERIFY(work.isValid());
    const QString currentPath = work.filePath(QStringLiteral("current.sqlite"));
    const QString badBackupPath = work.filePath(QStringLiteral("bad.sqlite"));

    {
        data::Database db(currentPath);
        QSqlQuery insert(db.handle());
        QVERIFY(insert.prepare(QStringLiteral("INSERT INTO products (name) VALUES (?)")));
        insert.addBindValue(QStringLiteral("keep me"));
        QVERIFY(insert.exec());
    }
    {
        QFile badBackup(badBackupPath);
        QVERIFY(badBackup.open(QIODevice::WriteOnly));
        badBackup.write("this is not a sqlite database");
    }

    const QByteArray before = readFile(currentPath);
    const QDateTime now(QDate(2026, 2, 3), QTime(12, 0), Qt::UTC);
    const data::BackupService::RestoreResult result =
        data::BackupService::restore(badBackupPath, currentPath, [&now]() { return now; });

    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(readFile(currentPath), before);
    QVERIFY(QDir(work.path())
                .entryList(QStringList() << QStringLiteral("*.before-restore-*"), QDir::Files)
                .isEmpty());
}

void BackupServiceTest::restoreRejectsNewerSchemaAndLeavesCurrentIntact()
{
    QTemporaryDir work;
    QVERIFY(work.isValid());
    const QString currentPath = work.filePath(QStringLiteral("current.sqlite"));
    const QString newerPath = work.filePath(QStringLiteral("newer.sqlite"));

    {
        data::Database db(currentPath);
        QSqlQuery insert(db.handle());
        QVERIFY(insert.prepare(QStringLiteral("INSERT INTO products (name) VALUES (?)")));
        insert.addBindValue(QStringLiteral("keep me"));
        QVERIFY(insert.exec());
        QVERIFY2(db.backupTo(newerPath), qPrintable(db.lastError()));
    }
    QVERIFY(setUserVersion(newerPath, data::kSchemaVersion + 1));

    const QByteArray before = readFile(currentPath);
    const QDateTime now(QDate(2026, 2, 4), QTime(12, 0), Qt::UTC);
    const data::BackupService::RestoreResult result =
        data::BackupService::restore(newerPath, currentPath, [&now]() { return now; });

    QVERIFY(!result.ok);
    QVERIFY2(result.error.contains(QStringLiteral("newer"), Qt::CaseInsensitive),
             qPrintable(result.error));
    QCOMPARE(readFile(currentPath), before);
    QVERIFY(QDir(work.path())
                .entryList(QStringList() << QStringLiteral("*.before-restore-*"), QDir::Files)
                .isEmpty());
}

QTEST_GUILESS_MAIN(BackupServiceTest)
#include "tst_backup.moc"
