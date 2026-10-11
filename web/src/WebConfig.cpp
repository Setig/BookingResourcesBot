#include "WebConfig.h"

#include "WebAuthMethods.h"

#include <QByteArray>
#include <QThread>
#include <QtGlobal>

namespace {

constexpr char kWebConfigFileEnvVar[] = "BOOKING_RESOURCES_BOT_WEB_CONFIG_FILE";

} // namespace

WebConfig::WebConfig()
    : WebConfig(QString())
{
    // do nothing
}

WebConfig::WebConfig(const QString &explicitFilePath)
    : m_settings(QStringLiteral(APP_NAME), QStringLiteral("web"), explicitFilePath)
{
    // do nothing
}

const char *WebConfig::configFileEnvironmentVariable()
{
    return kWebConfigFileEnvVar;
}

QString WebConfig::environmentConfigFilePath()
{
    return QString::fromLocal8Bit(qgetenv(configFileEnvironmentVariable())).trimmed();
}

QString WebConfig::resolveExplicitConfigFilePath(const QString &commandLineConfigFile)
{
    const QString trimmedCommandLinePath = commandLineConfigFile.trimmed();
    if (!trimmedCommandLinePath.isEmpty())
        return trimmedCommandLinePath;

    return environmentConfigFilePath();
}

QString WebConfig::fileName() const
{
    return m_settings.fileName();
}

QString WebConfig::userScopeFileName() const
{
    return m_settings.userScopeFileName();
}

QString WebConfig::systemScopeFileName() const
{
    return m_settings.systemScopeFileName();
}

QString WebConfig::explicitFileName() const
{
    return m_settings.explicitFileName();
}

void WebConfig::sync()
{
    m_settings.sync();
}

WebConfig::LookupResult WebConfig::lookupValue(const QString &key, const QVariant &defaultValue) const
{
    return m_settings.lookup(key, defaultValue);
}

WebConfig::LookupResult WebConfig::lookupAnyValue(const QStringList &keys,
                                                  const QVariant &defaultValue) const
{
    return m_settings.lookupAny(keys, defaultValue);
}

QString WebConfig::bindAddress() const
{
    return m_settings.value(QStringLiteral("web/bind_address"), QStringLiteral("127.0.0.1"))
            .toString();
}

void WebConfig::setBindAddress(const QString &address)
{
    m_settings.setValue(QStringLiteral("web/bind_address"), address);
}

quint16 WebConfig::httpPort() const
{
    return static_cast<quint16>(m_settings.value(QStringLiteral("web/http_port"), 8080).toUInt());
}

void WebConfig::setHttpPort(quint16 port)
{
    m_settings.setValue(QStringLiteral("web/http_port"), port);
}

quint16 WebConfig::httpsPort() const
{
    return static_cast<quint16>(m_settings.value(QStringLiteral("web/https_port"), 8443).toUInt());
}

void WebConfig::setHttpsPort(quint16 port)
{
    m_settings.setValue(QStringLiteral("web/https_port"), port);
}

bool WebConfig::httpEnabled() const
{
    return m_settings.value(QStringLiteral("web/http_enabled"), true).toBool();
}

void WebConfig::setHttpEnabled(bool enabled)
{
    m_settings.setValue(QStringLiteral("web/http_enabled"), enabled);
}

bool WebConfig::redirectHttpToHttps() const
{
    return m_settings.value(QStringLiteral("web/redirect_http_to_https"), true).toBool();
}

void WebConfig::setRedirectHttpToHttps(bool enabled)
{
    m_settings.setValue(QStringLiteral("web/redirect_http_to_https"), enabled);
}

QString WebConfig::sharedMemoryKey() const
{
    return m_settings
            .value(QStringLiteral("web/shared_memory_key"), QStringLiteral("booking-resources-web"))
            .toString();
}

void WebConfig::setSharedMemoryKey(const QString &key)
{
    m_settings.setValue(QStringLiteral("web/shared_memory_key"), key);
}

QString WebConfig::sharedMemorySecret() const
{
    return m_settings.value(QStringLiteral("web/shared_memory_secret")).toString();
}

void WebConfig::setSharedMemorySecret(const QString &secret)
{
    m_settings.setValue(QStringLiteral("web/shared_memory_secret"), secret);
}

QString WebConfig::logFile() const
{
    return m_settings.value(QStringLiteral("log/file")).toString();
}

void WebConfig::setLogFile(const QString &filePath)
{
    m_settings.setValue(QStringLiteral("log/file"), filePath);
}

bool WebConfig::httpsEnabled() const
{
    return m_settings.value(QStringLiteral("web/https_enabled"), false).toBool();
}

void WebConfig::setHttpsEnabled(bool enabled)
{
    m_settings.setValue(QStringLiteral("web/https_enabled"), enabled);
}

bool WebConfig::externalHttpsEnabled() const
{
    return m_settings.value(QStringLiteral("web/external_https_enabled"), false).toBool();
}

void WebConfig::setExternalHttpsEnabled(bool enabled)
{
    m_settings.setValue(QStringLiteral("web/external_https_enabled"), enabled);
}

bool WebConfig::allowInsecureHttp() const
{
    return m_settings.value(QStringLiteral("web/allow_insecure_http"), false).toBool();
}

void WebConfig::setAllowInsecureHttp(bool allowed)
{
    m_settings.setValue(QStringLiteral("web/allow_insecure_http"), allowed);
}

bool WebConfig::forwardedHeadersEnabled() const
{
    return m_settings.value(QStringLiteral("web/forwarded_headers_enabled"), false).toBool();
}

void WebConfig::setForwardedHeadersEnabled(bool enabled)
{
    m_settings.setValue(QStringLiteral("web/forwarded_headers_enabled"), enabled);
}

QStringList WebConfig::trustedProxies() const
{
    return m_settings.value(QStringLiteral("web/trusted_proxies")).toStringList();
}

void WebConfig::setTrustedProxies(const QStringList &proxies)
{
    m_settings.setValue(QStringLiteral("web/trusted_proxies"), proxies);
}

QString WebConfig::publicHttpsHost() const
{
    return m_settings.value(QStringLiteral("web/public_https_host")).toString();
}

void WebConfig::setPublicHttpsHost(const QString &host)
{
    m_settings.setValue(QStringLiteral("web/public_https_host"), host);
}

QString WebConfig::tlsCertificateFile() const
{
    return m_settings.value(QStringLiteral("web/tls_certificate_file")).toString();
}

void WebConfig::setTlsCertificateFile(const QString &filePath)
{
    m_settings.setValue(QStringLiteral("web/tls_certificate_file"), filePath);
}

QString WebConfig::tlsPrivateKeyFile() const
{
    return m_settings.value(QStringLiteral("web/tls_private_key_file")).toString();
}

void WebConfig::setTlsPrivateKeyFile(const QString &filePath)
{
    m_settings.setValue(QStringLiteral("web/tls_private_key_file"), filePath);
}

int WebConfig::tlsReloadIntervalSeconds() const
{
    return qMax(0, m_settings.value(QStringLiteral("web/tls_reload_interval_seconds"), 0).toInt());
}

void WebConfig::setTlsReloadIntervalSeconds(int seconds)
{
    m_settings.setValue(QStringLiteral("web/tls_reload_interval_seconds"), qMax(0, seconds));
}

int WebConfig::threadCount() const
{
    const int fallback = qMax(1, QThread::idealThreadCount());
    return qMax(1, m_settings.value(QStringLiteral("web/threads"), fallback).toInt());
}

void WebConfig::setThreadCount(int count)
{
    m_settings.setValue(QStringLiteral("web/threads"), qMax(1, count));
}

QString WebConfig::storageFile() const
{
    return m_settings.value(QStringLiteral("web/storage_file")).toString();
}

void WebConfig::setStorageFile(const QString &filePath)
{
    m_settings.setValue(QStringLiteral("web/storage_file"), filePath);
}

QString WebConfig::pageTitle() const
{
    const QString title =
            m_settings.value(QStringLiteral("web/page_title"), QStringLiteral("Booking Resources"))
                    .toString()
                    .trimmed();
    return title.isEmpty() ? QStringLiteral("Booking Resources") : title;
}

void WebConfig::setPageTitle(const QString &title)
{
    m_settings.setValue(QStringLiteral("web/page_title"), title);
}

QStringList WebConfig::authMethods() const
{
    const QVariant stored = m_settings.value(QStringLiteral("web/auth_methods"));
    if (!stored.isValid())
        return WebAuthMethods::defaults();

    const QStringList storedList = stored.toStringList();
    if (!storedList.isEmpty())
        return WebAuthMethods::normalizeList(storedList);
    return WebAuthMethods::normalizeList({ stored.toString() });
}

void WebConfig::setAuthMethods(const QStringList &methods)
{
    m_settings.setValue(QStringLiteral("web/auth_methods"),
                        WebAuthMethods::normalizeList(methods));
}
