#include <QtTest/QtTest>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "data/backup_service.h"
#include "data/database.h"

using namespace app;

class BackupServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void dailyBackupRunsAtMostOncePer24Hours();
    void dailyBackupKeepsLatestFourteen();
    void backupFailureIsReportedWithoutThrowing();

private:
    static QStringList backupsIn(const QString& directory);
    static QString clockStamp(const QDateTime& time);

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

QTEST_GUILESS_MAIN(BackupServiceTest)
#include "tst_backup.moc"
