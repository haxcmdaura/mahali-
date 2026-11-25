#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QtTest/QtTest>

#include "core/update_checker.h"
#include "core/update_downloader.h"
#include "core/update_installer.h"
#include "core/version.h"

using namespace app;

// Phase: the update-notification data layer. Only the pure version logic is
// covered here; UpdateChecker's GitHub round-trip needs a network and stays out
// of the suite, but the URL and the tag comparison it relies on are checked.
class UpdateTest : public QObject
{
    Q_OBJECT

private slots:
    void version_parse();
    void version_compare();
    void current_version_matches_macro();
    void release_url_is_the_one_the_installer_publishes_to();
    void downloader_urls_the_published_installer();
    void downloader_start_with_http_error_fails();
    void update_script_contains_checks();
    void update_script_refuses_unsafe_paths();
};

void UpdateTest::downloader_urls_the_published_installer()
{
    // The asset the Windows workflow uploads, reached through the "latest"
    // alias so a new release needs no change here.
    QCOMPARE(app::core::UpdateDownloader::defaultUrl().toString(),
             QStringLiteral("https://github.com/haxcmdaura/mahali-/releases/"
                            "latest/download/Mahali-Setup.zip"));

    // The batch script the restart step runs unpacks exactly this name, so the
    // two must not drift apart silently.
    QVERIFY(app::core::UpdateDownloader::defaultDestination()
                .endsWith(QStringLiteral("Mahali-Setup.zip")));
    QVERIFY(app::core::UpdateDownloader::defaultDestination().startsWith(QDir::tempPath()));
}

void UpdateTest::downloader_start_with_http_error_fails()
{
    app::core::UpdateDownloader downloader;

    // A local server answers 404 and closes. This walks the HTTP error path
    // without DNS, GitHub, or an operating-system-specific connection-refused
    // delay: on a Windows runner, port 1 can sit in connect() long enough to
    // trip the test watchdog.
    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost, 0), qPrintable(server.errorString()));
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server]() {
        while (QTcpSocket* socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
                socket->readAll();
                socket->write("HTTP/1.1 404 Not Found\r\n"
                              "Content-Type: text/plain\r\n"
                              "Content-Length: 0\r\n"
                              "Connection: close\r\n\r\n");
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket,
                             &QObject::deleteLater);
        }
    });

    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(QStringLiteral("127.0.0.1"));
    url.setPort(server.serverPort());
    url.setPath(QStringLiteral("/Mahali-Setup.zip"));
    downloader.setUrl(url);

    const QString destination =
        QDir(QDir::tempPath()).filePath(QStringLiteral("tst-update-download.zip"));
    downloader.setDestination(destination);

    QSignalSpy progress(&downloader, &app::core::UpdateDownloader::progress);
    QSignalSpy finishedSpy(&downloader, &app::core::UpdateDownloader::finished);
    QSignalSpy failedSpy(&downloader, &app::core::UpdateDownloader::failed);

    QElapsedTimer timer;
    timer.start();
    downloader.start();

    QVERIFY2(failedSpy.wait(10000), "an HTTP error must fail, not hang");
    QVERIFY2(timer.elapsed() < 10000, "the failure took too long to surface");

    // An HTTP error is not a download: nothing may be reported as finished,
    // and no half-written file may be left for the restart step to find and
    // unpack.
    QVERIFY(finishedSpy.isEmpty());
    for (const QList<QVariant>& reported : progress) {
        QCOMPARE(reported.at(0).toInt(), 0);
    }
    QVERIFY2(!QFile::exists(destination), "a failed download left a file behind");
}

void UpdateTest::update_script_contains_checks()
{
    const QString appDir = QStringLiteral("C:/Mahali");
    const QString archive = app::core::UpdateDownloader::defaultDestination();
    const QString script = app::core::buildWindowsInstallBatch(
        appDir, archive, QStringLiteral("mahali-desktop.exe"));

    QVERIFY(!script.isEmpty());

    // Both failure paths stop the script. Without them a corrupt archive left
    // the script copying nothing and restarting the app, so the user saw a
    // restart and no update.
    QVERIFY2(script.contains(QStringLiteral("if not exist")),
             "a failed extraction is not detected");
    QVERIFY(script.contains(QStringLiteral("exit /b 1")));

    // The app is killed before anything is unpacked, twice: an installation that
    // lingers in its shutdown keeps the exe and the Qt DLLs locked, which is
    // what made the copy fail where a manual copy succeeded.
    const QString taskkill = QStringLiteral("taskkill /F /IM mahali-desktop.exe >nul 2>&1");
    QCOMPARE(script.count(taskkill), 2);

    // robocopy, not xcopy: it retries a locked file instead of giving up on the
    // first one, which is the whole difference between a shop that updates and a
    // shop that does not. 0-7 are success for robocopy, so "errorlevel 1" would
    // have called every clean copy a failure; only 8 and above is a real one.
    QVERIFY2(script.contains(QStringLiteral("robocopy")), "the copy still uses xcopy");
    QVERIFY2(!script.contains(QStringLiteral("xcopy")), "xcopy is still in the script");
    QVERIFY2(script.contains(QStringLiteral("/R:5")), "a locked file is not retried");
    QVERIFY2(script.contains(QStringLiteral("if %ERRORLEVEL% GEQ 8")),
             "a failed copy is not detected");

    // A copy that reports success but leaves the executable behind would end in a
    // restart that starts nothing, so the file itself is checked after the copy.
    QVERIFY2(script.contains(QStringLiteral("if not exist \"") + QDir::toNativeSeparators(appDir)
                             + QStringLiteral("\\mahali-desktop.exe\"")),
             "the installed executable is not verified after the copy");

    // Paths come from the downloader, not from the environment. %TEMP% on one
    // side and QDir::tempPath() on the other is how a script ends up unpacking
    // a file that was never downloaded.
    QVERIFY2(!script.contains(QStringLiteral("%TEMP%")),
             "the script still leaves a path to the environment");
    QVERIFY2(script.contains(QDir::toNativeSeparators(QDir::tempPath())),
             "the script does not point at the temp directory the app uses");

    // The archive path must be the one the downloader writes, still quoted.
    const QString nativeArchive = QDir::toNativeSeparators(archive);
    QVERIFY2(script.contains(QStringLiteral("\"") + nativeArchive + QStringLiteral("\"")),
             "the archive path is missing or unquoted in the script");

    // The executable the script launches is the one it also checks for, so the
    // extraction test and the restart cannot disagree.
    QVERIFY(script.contains(QStringLiteral("\"") + QDir::toNativeSeparators(appDir)
                            + QStringLiteral("\\mahali-desktop.exe\"")));
}

void UpdateTest::update_script_refuses_unsafe_paths()
{
    const QString archive = app::core::UpdateDownloader::defaultDestination();

    // A quote or a percent sign would escape the quoting or be expanded as a
    // variable, so no script is produced at all. An empty result is the caller's
    // signal to tell the user rather than to run something unpredictable.
    QVERIFY(app::core::buildWindowsInstallBatch(
                QStringLiteral("C:/Mahali \"evil\""), archive,
                QStringLiteral("mahali-desktop.exe"))
                .isEmpty());
    QVERIFY(app::core::buildWindowsInstallBatch(
                QStringLiteral("C:/100% Mahali"), archive,
                QStringLiteral("mahali-desktop.exe"))
                .isEmpty());
    // An empty executable name would produce a script that checks for a
    // directory it cannot name, and then starts nothing.
    QVERIFY(app::core::buildWindowsInstallBatch(QStringLiteral("C:/Mahali"), archive,
                                                 QString())
                .isEmpty());
}

void UpdateTest::version_parse()
{
    const app::core::Version full = app::core::parseVersion(QStringLiteral("v1.0.0"));
    QCOMPARE(full.major, 1);
    QCOMPARE(full.minor, 0);
    QCOMPARE(full.patch, 0);

    const app::core::Version another = app::core::parseVersion(QStringLiteral("v2.3.4"));
    QCOMPARE(another.major, 2);
    QCOMPARE(another.minor, 3);
    QCOMPARE(another.patch, 4);

    // A missing patch component defaults to 0, and the "v" stays optional.
    const app::core::Version shortTag = app::core::parseVersion(QStringLiteral("1.5"));
    QCOMPARE(shortTag.major, 1);
    QCOMPARE(shortTag.minor, 5);
    QCOMPARE(shortTag.patch, 0);

    // Extra components are dropped rather than folded into the patch.
    const app::core::Version longTag = app::core::parseVersion(QStringLiteral("v1.2.3.4"));
    QCOMPARE(longTag.patch, 3);

    // A pre-release suffix is ignored, not misread as a number.
    const app::core::Version rc = app::core::parseVersion(QStringLiteral("v1.2.3-rc1"));
    QCOMPARE(rc.major, 1);
    QCOMPARE(rc.minor, 2);
    QCOMPARE(rc.patch, 3);

    // Junk never parses as a version a user could be "offered" an upgrade to.
    QCOMPARE(app::core::parseVersion(QString()).major, 0);
    QCOMPARE(app::core::parseVersion(QStringLiteral("v")).major, 0);
    QCOMPARE(app::core::parseVersion(QStringLiteral("latest")).major, 0);
}

void UpdateTest::version_compare()
{
    QVERIFY(app::core::isNewer({1, 0, 1}, {1, 0, 0}));
    QVERIFY(app::core::isNewer({2, 0, 0}, {1, 9, 9}));
    QVERIFY(!app::core::isNewer({1, 0, 0}, {1, 0, 0}));
    QVERIFY(!app::core::isNewer({1, 0, 0}, {1, 0, 1}));

    // A patch bump is the difference between "up to date" and an update prompt,
    // so it has to be caught. The probe sits one patch above the build.
    QVERIFY(app::core::isNewer({1, 0, 5}, app::core::currentVersion()));
}

void UpdateTest::current_version_matches_macro()
{
    // Keeps the checker and the .rc in step: the build version is 1.0.4.
    const app::core::Version current = app::core::currentVersion();
    QCOMPARE(current.major, 1);
    QCOMPARE(current.minor, 0);
    QCOMPARE(current.patch, 4);
    QCOMPARE(QStringLiteral(MAHALI_VERSION), QStringLiteral("1.0.4"));
}

void UpdateTest::release_url_is_the_one_the_installer_publishes_to()
{
    QCOMPARE(app::core::UpdateChecker::latestReleaseUrl().toString(),
             QStringLiteral("https://api.github.com/repos/haxcmdaura/mahali-/releases/latest"));
}

QTEST_MAIN(UpdateTest)
#include "tst_update.moc"
