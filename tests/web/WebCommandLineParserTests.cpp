#include <QtTest/QtTest>
#include "TestRegistry.h"

#include "WebAuthMethods.h"
#include "WebCommandLineParser.h"
#include "WebConfig.h"

#include <QFileInfo>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>

namespace {

void setScopedConfigPaths(const QString &userPath, const QString &systemPath)
{
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, userPath);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, systemPath);
}

} // namespace

class WebCommandLineParserTests : public QObject
{
    Q_OBJECT

private slots:
    void configDefaultsDisableHttps();
    void configPersistsTlsSettings();
    void configPersistsProductionPortSettings();
    void systemScopeFallbackProvidesSettings();
    void explicitConfigFileOverridesScopedSettingsAndWritesBack();
    void environmentConfigFilePathIsResolved();
    void commandLineConfigFileOverridesEnvironmentPath();
    void configFileOptionIsParsed();
    void commandLineOverridesProductionPortSettings();
    void commandLineOverridesTlsSettings();
    void commandLineOverridesSecuritySettings();
    void authMethodsNormalizeAliasesConflictsAndDefaults();
    void commandLineOverridesAuthMethods();
};

void WebCommandLineParserTests::configDefaultsDisableHttps()
{
    QTemporaryDir userDir;
    QTemporaryDir systemDir;
    QVERIFY(userDir.isValid());
    QVERIFY(systemDir.isValid());
    setScopedConfigPaths(userDir.path(), systemDir.path());

    WebConfig config;
    QCOMPARE(config.httpsEnabled(), false);
    QCOMPARE(config.allowInsecureHttp(), false);
    QVERIFY(config.tlsCertificateFile().isEmpty());
    QVERIFY(config.tlsPrivateKeyFile().isEmpty());
    QVERIFY(config.sharedMemorySecret().isEmpty());
}

void WebCommandLineParserTests::configPersistsTlsSettings()
{
    QTemporaryDir userDir;
    QTemporaryDir systemDir;
    QVERIFY(userDir.isValid());
    QVERIFY(systemDir.isValid());
    setScopedConfigPaths(userDir.path(), systemDir.path());

    {
        WebConfig config;
        config.setHttpsEnabled(true);
        config.setTlsCertificateFile(QStringLiteral("settings-cert.pem"));
        config.setTlsPrivateKeyFile(QStringLiteral("settings-key.pem"));
        config.sync();
    }

    WebConfig config;
    QCOMPARE(config.httpsEnabled(), true);
    QCOMPARE(config.tlsCertificateFile(), QStringLiteral("settings-cert.pem"));
    QCOMPARE(config.tlsPrivateKeyFile(), QStringLiteral("settings-key.pem"));
}

void WebCommandLineParserTests::configPersistsProductionPortSettings()
{
    QTemporaryDir userDir;
    QTemporaryDir systemDir;
    QVERIFY(userDir.isValid());
    QVERIFY(systemDir.isValid());
    setScopedConfigPaths(userDir.path(), systemDir.path());

    {
        WebConfig config;
        config.setHttpPort(80);
        config.setHttpsPort(443);
        config.setHttpEnabled(false);
        config.setRedirectHttpToHttps(false);
        config.setForwardedHeadersEnabled(true);
        config.setTrustedProxies({ QStringLiteral("127.0.0.1"), QStringLiteral("10.0.0.0/8") });
        config.setPublicHttpsHost(QStringLiteral("booking.example"));
        config.setTlsReloadIntervalSeconds(60);
        config.sync();
    }

    WebConfig config;
    QCOMPARE(config.httpPort(), 80);
    QCOMPARE(config.httpsPort(), 443);
    QCOMPARE(config.httpEnabled(), false);
    QCOMPARE(config.redirectHttpToHttps(), false);
    QCOMPARE(config.forwardedHeadersEnabled(), true);
    QCOMPARE(config.trustedProxies().size(), 2);
    QCOMPARE(config.publicHttpsHost(), QStringLiteral("booking.example"));
    QCOMPARE(config.tlsReloadIntervalSeconds(), 60);
}

void WebCommandLineParserTests::systemScopeFallbackProvidesSettings()
{
    QTemporaryDir userDir;
    QTemporaryDir systemDir;
    QVERIFY(userDir.isValid());
    QVERIFY(systemDir.isValid());
    setScopedConfigPaths(userDir.path(), systemDir.path());

    {
        QSettings systemSettings(QSettings::IniFormat,
                                 QSettings::SystemScope,
                                 QStringLiteral(WEB_APP_NAME),
                                 QStringLiteral("web"));
        systemSettings.setValue(QStringLiteral("web/shared_memory_secret"),
                                QStringLiteral("system-secret"));
        systemSettings.setValue(QStringLiteral("web/storage_file"),
                                QStringLiteral("/var/lib/system-web.sqlite"));
        systemSettings.sync();
    }

    WebConfig config;
    QCOMPARE(config.sharedMemorySecret(), QStringLiteral("system-secret"));
    QCOMPARE(config.storageFile(), QStringLiteral("/var/lib/system-web.sqlite"));
    QCOMPARE(config.lookupValue(QStringLiteral("web/shared_memory_secret")).source,
             LayeredIniSettings::ValueSource::SystemScope);
}

void WebCommandLineParserTests::explicitConfigFileOverridesScopedSettingsAndWritesBack()
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
                               QStringLiteral(WEB_APP_NAME),
                               QStringLiteral("web"));
        userSettings.setValue(QStringLiteral("web/shared_memory_secret"),
                              QStringLiteral("user-secret"));
        userSettings.sync();
    }

    {
        QSettings systemSettings(QSettings::IniFormat,
                                 QSettings::SystemScope,
                                 QStringLiteral(WEB_APP_NAME),
                                 QStringLiteral("web"));
        systemSettings.setValue(QStringLiteral("web/shared_memory_secret"),
                                QStringLiteral("system-secret"));
        systemSettings.sync();
    }

    const QString explicitConfigFile =
            explicitDir.path() + QLatin1String("/explicit-web-config.ini");
    {
        QSettings explicitSettings(explicitConfigFile, QSettings::IniFormat);
        explicitSettings.setValue(QStringLiteral("web/shared_memory_secret"),
                                  QStringLiteral("explicit-secret"));
        explicitSettings.sync();
    }

    WebConfig config(explicitConfigFile);
    QCOMPARE(config.sharedMemorySecret(), QStringLiteral("explicit-secret"));
    QCOMPARE(config.lookupValue(QStringLiteral("web/shared_memory_secret")).source,
             LayeredIniSettings::ValueSource::ExplicitFile);

    config.setStorageFile(QStringLiteral("explicit-storage.sqlite"));
    config.sync();

    QSettings explicitSettings(explicitConfigFile, QSettings::IniFormat);
    QCOMPARE(explicitSettings.value(QStringLiteral("web/storage_file")).toString(),
             QStringLiteral("explicit-storage.sqlite"));

    QSettings userSettings(QSettings::IniFormat,
                           QSettings::UserScope,
                           QStringLiteral(WEB_APP_NAME),
                           QStringLiteral("web"));
    QVERIFY(!userSettings.contains(QStringLiteral("web/storage_file")));

    QCOMPARE(config.fileName(), QFileInfo(explicitConfigFile).absoluteFilePath());
}

void WebCommandLineParserTests::environmentConfigFilePathIsResolved()
{
    const QByteArray previous = qgetenv(WebConfig::configFileEnvironmentVariable());
    QVERIFY(qputenv(WebConfig::configFileEnvironmentVariable(), QByteArray("env-web.ini")));
    QCOMPARE(WebConfig::environmentConfigFilePath(), QStringLiteral("env-web.ini"));

    if (previous.isEmpty())
        qunsetenv(WebConfig::configFileEnvironmentVariable());
    else
        QVERIFY(qputenv(WebConfig::configFileEnvironmentVariable(), previous));
}

void WebCommandLineParserTests::commandLineConfigFileOverridesEnvironmentPath()
{
    const QByteArray previous = qgetenv(WebConfig::configFileEnvironmentVariable());
    QVERIFY(qputenv(WebConfig::configFileEnvironmentVariable(), QByteArray("env-web.ini")));
    QCOMPARE(WebConfig::resolveExplicitConfigFilePath(QStringLiteral("cli-web.ini")),
             QStringLiteral("cli-web.ini"));
    QCOMPARE(WebConfig::resolveExplicitConfigFilePath(QString()),
             QStringLiteral("env-web.ini"));

    if (previous.isEmpty())
        qunsetenv(WebConfig::configFileEnvironmentVariable());
    else
        QVERIFY(qputenv(WebConfig::configFileEnvironmentVariable(), previous));
}

void WebCommandLineParserTests::configFileOptionIsParsed()
{
    WebCommandLineParser parser;
    parser.process({ QStringLiteral("booking-resources-web"),
                     QStringLiteral("--config-file"),
                     QStringLiteral("custom-web.ini") });

    QCOMPARE(parser.configFilePath(), QStringLiteral("custom-web.ini"));
}

void WebCommandLineParserTests::commandLineOverridesProductionPortSettings()
{
    QTemporaryDir userDir;
    QTemporaryDir systemDir;
    QVERIFY(userDir.isValid());
    QVERIFY(systemDir.isValid());
    setScopedConfigPaths(userDir.path(), systemDir.path());

    WebConfig config;
    config.setHttpPort(8080);
    config.setHttpsPort(8443);

    WebCommandLineParser parser;
    parser.process({ QStringLiteral("booking-resources-web"),
                     QStringLiteral("--http-port"),
                     QStringLiteral("80"),
                     QStringLiteral("--https-port"),
                     QStringLiteral("443"),
                     QStringLiteral("--no-http"),
                     QStringLiteral("--redirect-http-to-https"),
                     QStringLiteral("--forwarded-headers"),
                     QStringLiteral("--trusted-proxy"),
                     QStringLiteral("127.0.0.1"),
                     QStringLiteral("--public-https-host"),
                     QStringLiteral("booking.example"),
                     QStringLiteral("--tls-reload-interval"),
                     QStringLiteral("30") });

    QCOMPARE(parser.httpPort(config), 80);
    QCOMPARE(parser.httpsPort(config), 443);
    QCOMPARE(parser.httpEnabled(config), false);
    QCOMPARE(parser.redirectHttpToHttps(config), true);
    QCOMPARE(parser.forwardedHeadersEnabled(config), true);
    QCOMPARE(parser.trustedProxies(config), QStringList({ QStringLiteral("127.0.0.1") }));
    QCOMPARE(parser.publicHttpsHost(config), QStringLiteral("booking.example"));
    QCOMPARE(parser.tlsReloadIntervalSeconds(config), 30);
}

void WebCommandLineParserTests::commandLineOverridesTlsSettings()
{
    QTemporaryDir userDir;
    QTemporaryDir systemDir;
    QVERIFY(userDir.isValid());
    QVERIFY(systemDir.isValid());
    setScopedConfigPaths(userDir.path(), systemDir.path());

    WebConfig config;
    config.setHttpsEnabled(true);
    config.setTlsCertificateFile(QStringLiteral("settings-cert.pem"));
    config.setTlsPrivateKeyFile(QStringLiteral("settings-key.pem"));

    WebCommandLineParser parser;
    parser.process({ QStringLiteral("booking-resources-web"),
                     QStringLiteral("--no-https"),
                     QStringLiteral("--tls-certificate-file"),
                     QStringLiteral("cli-cert.pem"),
                     QStringLiteral("--tls-private-key-file"),
                     QStringLiteral("cli-key.pem") });

    QCOMPARE(parser.httpsEnabled(config), false);
    QCOMPARE(parser.tlsCertificateFile(config), QStringLiteral("cli-cert.pem"));
    QCOMPARE(parser.tlsPrivateKeyFile(config), QStringLiteral("cli-key.pem"));

    WebCommandLineParser enablingParser;
    enablingParser.process({ QStringLiteral("booking-resources-web"), QStringLiteral("--https") });
    QCOMPARE(enablingParser.httpsEnabled(config), true);
    QCOMPARE(enablingParser.tlsCertificateFile(config), QStringLiteral("settings-cert.pem"));
    QCOMPARE(enablingParser.tlsPrivateKeyFile(config), QStringLiteral("settings-key.pem"));
}

void WebCommandLineParserTests::commandLineOverridesSecuritySettings()
{
    QTemporaryDir userDir;
    QTemporaryDir systemDir;
    QVERIFY(userDir.isValid());
    QVERIFY(systemDir.isValid());
    setScopedConfigPaths(userDir.path(), systemDir.path());

    WebConfig config;
    config.setAllowInsecureHttp(false);
    config.setSharedMemorySecret(QStringLiteral("settings-secret"));

    WebCommandLineParser parser;
    parser.process({ QStringLiteral("booking-resources-web"),
                     QStringLiteral("--allow-insecure-http"),
                     QStringLiteral("--web-shared-memory-secret"),
                     QStringLiteral("cli-secret") });

    QCOMPARE(parser.allowInsecureHttp(config), true);
    QCOMPARE(parser.sharedMemorySecret(config), QStringLiteral("cli-secret"));
}

void WebCommandLineParserTests::authMethodsNormalizeAliasesConflictsAndDefaults()
{
    QCOMPARE(WebAuthMethods::defaults(),
             QStringList({ QStringLiteral("sign-without-registration") }));
    QCOMPARE(WebAuthMethods::normalizeList({}),
             QStringList({ QStringLiteral("sign-without-registration") }));
    QCOMPARE(WebAuthMethods::normalizeList({ QStringLiteral("unknown") }),
             QStringList({ QStringLiteral("sign-without-registration") }));
    QCOMPARE(WebAuthMethods::normalizeList({ QStringLiteral("web-profile"),
                                             QStringLiteral("local-password") }),
             QStringList({ QStringLiteral("sign-without-registration"),
                           QStringLiteral("log-in-with-registration") }));
    QCOMPARE(WebAuthMethods::normalizeList({ QStringLiteral("log-in-only"),
                                             QStringLiteral("log-in-with-registration") }),
             QStringList({ QStringLiteral("log-in-with-registration") }));
    QCOMPARE(WebAuthMethods::normalizeList({ QStringLiteral("sign-without-registration"),
                                             QStringLiteral("log-in-only") }),
             QStringList({ QStringLiteral("sign-without-registration"),
                           QStringLiteral("log-in-only") }));
    QVERIFY(WebAuthMethods::allowsLogin({ QStringLiteral("log-in-only") }));
    QVERIFY(!WebAuthMethods::allowsRegistration({ QStringLiteral("log-in-only") }));
    QVERIFY(WebAuthMethods::allowsRegistration({ QStringLiteral("log-in-with-registration") }));
    QVERIFY(WebAuthMethods::allowsSignWithoutRegistration({ QStringLiteral("web-profile") }));

    QTemporaryDir userDir;
    QTemporaryDir systemDir;
    QVERIFY(userDir.isValid());
    QVERIFY(systemDir.isValid());
    setScopedConfigPaths(userDir.path(), systemDir.path());

    WebConfig config;
    QCOMPARE(config.authMethods(), QStringList({ QStringLiteral("sign-without-registration") }));

    config.setAuthMethods({ QStringLiteral("web-profile"), QStringLiteral("local-password") });
    QCOMPARE(config.authMethods(),
             QStringList({ QStringLiteral("sign-without-registration"),
                           QStringLiteral("log-in-with-registration") }));
}

void WebCommandLineParserTests::commandLineOverridesAuthMethods()
{
    QTemporaryDir userDir;
    QTemporaryDir systemDir;
    QVERIFY(userDir.isValid());
    QVERIFY(systemDir.isValid());
    setScopedConfigPaths(userDir.path(), systemDir.path());

    WebConfig config;
    config.setAuthMethods({ QStringLiteral("sign-without-registration") });

    WebCommandLineParser parser;
    parser.process({ QStringLiteral("booking-resources-web"),
                     QStringLiteral("--web-auth-method"),
                     QStringLiteral("log-in-only") });
    QCOMPARE(parser.authMethods(config), QStringList({ QStringLiteral("log-in-only") }));
}

BOOKING_RESOURCES_REGISTER_TEST(WebCommandLineParserTests)

#include "WebCommandLineParserTests.moc"
