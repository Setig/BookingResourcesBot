#include <QtTest/QtTest>
#include "TestRegistry.h"

#include "ServerConfig.h"
#include "ServerCommandLineParser.h"

#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>

namespace {

void setScopedConfigPaths(const QString &userPath, const QString &systemPath)
{
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, userPath);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, systemPath);
}

} // namespace

class ServerConfigTests : public QObject
{
    Q_OBJECT

private slots:
    void systemScopeFallbackProvidesServerSettings();
    void explicitConfigFileOverridesScopedSettingsAndWritesBack();
    void legacyTelegramSettingsAreReadFromSystemScope();
    void environmentConfigFilePathIsResolved();
    void commandLineConfigFileOverridesEnvironmentPath();
    void configFileOptionIsParsed();
};

void ServerConfigTests::systemScopeFallbackProvidesServerSettings()
{
    QTemporaryDir userDir;
    QTemporaryDir systemDir;
    QVERIFY(userDir.isValid());
    QVERIFY(systemDir.isValid());
    setScopedConfigPaths(userDir.path(), systemDir.path());

    {
        QSettings systemSettings(QSettings::IniFormat,
                                 QSettings::SystemScope,
                                 QStringLiteral(APP_NAME),
                                 QStringLiteral("config"));
        systemSettings.setValue(QStringLiteral("database/file"),
                                QStringLiteral("/var/lib/server.sqlite"));
        systemSettings.setValue(QStringLiteral("web/shared_memory_secret"),
                                QStringLiteral("system-secret"));
        systemSettings.sync();
    }

    ServerConfig config;
    QCOMPARE(config.databaseFile(), QStringLiteral("/var/lib/server.sqlite"));
    QCOMPARE(config.webSharedMemorySecret(), QStringLiteral("system-secret"));
    QCOMPARE(config.lookupValue(QStringLiteral("database/file")).source,
             LayeredIniSettings::ValueSource::SystemScope);
}

void ServerConfigTests::explicitConfigFileOverridesScopedSettingsAndWritesBack()
{
    QTemporaryDir userDir;
    QTemporaryDir systemDir;
    QTemporaryDir explicitDir;
    QVERIFY(userDir.isValid());
    QVERIFY(systemDir.isValid());
    QVERIFY(explicitDir.isValid());
    setScopedConfigPaths(userDir.path(), systemDir.path());

    {
        QSettings userSettings(QSettings::IniFormat,
                               QSettings::UserScope,
                               QStringLiteral(APP_NAME),
                               QStringLiteral("config"));
        userSettings.setValue(QStringLiteral("database/file"),
                              QStringLiteral("user.sqlite"));
        userSettings.sync();
    }

    {
        QSettings systemSettings(QSettings::IniFormat,
                                 QSettings::SystemScope,
                                 QStringLiteral(APP_NAME),
                                 QStringLiteral("config"));
        systemSettings.setValue(QStringLiteral("database/file"),
                                QStringLiteral("system.sqlite"));
        systemSettings.sync();
    }

    const QString explicitConfigFile =
            explicitDir.path() + QLatin1String("/explicit-server-config.ini");
    {
        QSettings explicitSettings(explicitConfigFile, QSettings::IniFormat);
        explicitSettings.setValue(QStringLiteral("database/file"),
                                  QStringLiteral("explicit.sqlite"));
        explicitSettings.sync();
    }

    ServerConfig config(explicitConfigFile);
    QCOMPARE(config.databaseFile(), QStringLiteral("explicit.sqlite"));
    QCOMPARE(config.lookupValue(QStringLiteral("database/file")).source,
             LayeredIniSettings::ValueSource::ExplicitFile);

    config.setLogFile(QStringLiteral("server.log"));
    config.sync();

    QSettings explicitSettings(explicitConfigFile, QSettings::IniFormat);
    QCOMPARE(explicitSettings.value(QStringLiteral("log/file")).toString(),
             QStringLiteral("server.log"));

    QSettings userSettings(QSettings::IniFormat,
                           QSettings::UserScope,
                           QStringLiteral(APP_NAME),
                           QStringLiteral("config"));
    QVERIFY(!userSettings.contains(QStringLiteral("log/file")));

    QCOMPARE(config.fileName(), QFileInfo(explicitConfigFile).absoluteFilePath());
}

void ServerConfigTests::legacyTelegramSettingsAreReadFromSystemScope()
{
    QTemporaryDir userDir;
    QTemporaryDir systemDir;
    QVERIFY(userDir.isValid());
    QVERIFY(systemDir.isValid());
    setScopedConfigPaths(userDir.path(), systemDir.path());

    {
        QSettings systemSettings(QSettings::IniFormat,
                                 QSettings::SystemScope,
                                 QStringLiteral(APP_NAME),
                                 QStringLiteral("config"));
        systemSettings.setValue(QStringLiteral("telegram/token"),
                                QStringLiteral("legacy-token"));
        systemSettings.setValue(QStringLiteral("telegram/admin_ids"),
                                QVariantList({ QStringLiteral("100"), QStringLiteral("200") }));
        systemSettings.sync();
    }

    ServerConfig config;
    QCOMPARE(config.botToken(MessengerType::Telegram), QStringLiteral("legacy-token"));
    QCOMPARE(config.adminUserIds(MessengerType::Telegram),
             QStringList({ QStringLiteral("100"), QStringLiteral("200") }));
}

void ServerConfigTests::environmentConfigFilePathIsResolved()
{
    const QByteArray previous = qgetenv(ServerConfig::configFileEnvironmentVariable());
    QVERIFY(qputenv(ServerConfig::configFileEnvironmentVariable(), QByteArray("env-server.ini")));
    QCOMPARE(ServerConfig::environmentConfigFilePath(), QStringLiteral("env-server.ini"));

    if (previous.isEmpty())
        qunsetenv(ServerConfig::configFileEnvironmentVariable());
    else
        QVERIFY(qputenv(ServerConfig::configFileEnvironmentVariable(), previous));
}

void ServerConfigTests::commandLineConfigFileOverridesEnvironmentPath()
{
    const QByteArray previous = qgetenv(ServerConfig::configFileEnvironmentVariable());
    QVERIFY(qputenv(ServerConfig::configFileEnvironmentVariable(), QByteArray("env-server.ini")));
    QCOMPARE(ServerConfig::resolveExplicitConfigFilePath(QStringLiteral("cli-server.ini")),
             QStringLiteral("cli-server.ini"));
    QCOMPARE(ServerConfig::resolveExplicitConfigFilePath(QString()),
             QStringLiteral("env-server.ini"));

    if (previous.isEmpty())
        qunsetenv(ServerConfig::configFileEnvironmentVariable());
    else
        QVERIFY(qputenv(ServerConfig::configFileEnvironmentVariable(), previous));
}

void ServerConfigTests::configFileOptionIsParsed()
{
    ServerCommandLineParser parser;
    parser.process({ QStringLiteral("booking-resources-server"),
                     QStringLiteral("--config-file"),
                     QStringLiteral("custom-server.ini") });

    QCOMPARE(parser.configFilePath(), QStringLiteral("custom-server.ini"));
}

BOOKING_RESOURCES_REGISTER_TEST(ServerConfigTests)

#include "ServerConfigTests.moc"
