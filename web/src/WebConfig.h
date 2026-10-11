#ifndef WEBCONFIG_H
#define WEBCONFIG_H

#include <QStringList>
#include <QVariant>

#include "LayeredIniSettings.h"

class WebConfig final
{
public:
    WebConfig();
    explicit WebConfig(const QString &explicitFilePath);

    using LookupResult = LayeredIniSettings::LookupResult;

    static const char *configFileEnvironmentVariable();
    static QString environmentConfigFilePath();
    static QString resolveExplicitConfigFilePath(const QString &commandLineConfigFile);

    QString fileName() const;
    QString userScopeFileName() const;
    QString systemScopeFileName() const;
    QString explicitFileName() const;
    void sync();

    LookupResult lookupValue(const QString &key, const QVariant &defaultValue = QVariant()) const;
    LookupResult lookupAnyValue(const QStringList &keys,
                                const QVariant &defaultValue = QVariant()) const;

    QString bindAddress() const;
    void setBindAddress(const QString &address);

    quint16 httpPort() const;
    void setHttpPort(quint16 port);
    quint16 httpsPort() const;
    void setHttpsPort(quint16 port);
    bool httpEnabled() const;
    void setHttpEnabled(bool enabled);
    bool redirectHttpToHttps() const;
    void setRedirectHttpToHttps(bool enabled);

    QString sharedMemoryKey() const;
    void setSharedMemoryKey(const QString &key);

    QString sharedMemorySecret() const;
    void setSharedMemorySecret(const QString &secret);

    QString logFile() const;
    void setLogFile(const QString &filePath);

    bool httpsEnabled() const;
    void setHttpsEnabled(bool enabled);

    bool externalHttpsEnabled() const;
    void setExternalHttpsEnabled(bool enabled);

    bool allowInsecureHttp() const;
    void setAllowInsecureHttp(bool allowed);

    bool forwardedHeadersEnabled() const;
    void setForwardedHeadersEnabled(bool enabled);
    QStringList trustedProxies() const;
    void setTrustedProxies(const QStringList &proxies);
    QString publicHttpsHost() const;
    void setPublicHttpsHost(const QString &host);

    QString tlsCertificateFile() const;
    void setTlsCertificateFile(const QString &filePath);

    QString tlsPrivateKeyFile() const;
    void setTlsPrivateKeyFile(const QString &filePath);

    int tlsReloadIntervalSeconds() const;
    void setTlsReloadIntervalSeconds(int seconds);

    int threadCount() const;
    void setThreadCount(int count);

    QString storageFile() const;
    void setStorageFile(const QString &filePath);

    QString pageTitle() const;
    void setPageTitle(const QString &title);

    QStringList authMethods() const;
    void setAuthMethods(const QStringList &methods);

private:
    LayeredIniSettings m_settings;
};

#endif // WEBCONFIG_H
