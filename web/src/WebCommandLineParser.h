#ifndef WEBCOMMANDLINEPARSER_H
#define WEBCOMMANDLINEPARSER_H

#include <QString>
#include <QStringList>

class QCoreApplication;
class WebConfig;

class WebCommandLineParser final
{
public:
    WebCommandLineParser();
    ~WebCommandLineParser();

    void process(const QCoreApplication &app);
    void process(const QStringList &arguments);
    QString configFilePath() const;

    QString bindAddress(const WebConfig &config) const;
    quint16 httpPort(const WebConfig &config) const;
    quint16 httpsPort(const WebConfig &config) const;
    bool httpEnabled(const WebConfig &config) const;
    bool redirectHttpToHttps(const WebConfig &config) const;
    QString sharedMemoryKey(const WebConfig &config) const;
    QString sharedMemorySecret(const WebConfig &config) const;
    QString logFilePath(const WebConfig &config) const;
    bool httpsEnabled(const WebConfig &config) const;
    bool externalHttpsEnabled(const WebConfig &config) const;
    bool allowInsecureHttp(const WebConfig &config) const;
    bool forwardedHeadersEnabled(const WebConfig &config) const;
    QStringList trustedProxies(const WebConfig &config) const;
    QString publicHttpsHost(const WebConfig &config) const;
    QString tlsCertificateFile(const WebConfig &config) const;
    QString tlsPrivateKeyFile(const WebConfig &config) const;
    int tlsReloadIntervalSeconds(const WebConfig &config) const;
    int threadCount(const WebConfig &config) const;
    QString storageFile(const WebConfig &config) const;
    QString pageTitle(const WebConfig &config) const;
    QStringList authMethods(const WebConfig &config) const;

private:
    class Private;
    Private *d;
};

#endif // WEBCOMMANDLINEPARSER_H
