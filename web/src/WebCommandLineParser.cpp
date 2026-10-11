#include "WebCommandLineParser.h"

#include "WebConfig.h"
#include "WebAuthMethods.h"

#include <QTextStream>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QtGlobal>

Q_LOGGING_CATEGORY(lcWebCmdLine, "booking.web.cmdline")

class WebCommandLineParser::Private
{
public:
    Private();

    QCommandLineParser parser;
    QCommandLineOption configFileOption;
    QCommandLineOption bindAddressOption;
    QCommandLineOption httpPortOption;
    QCommandLineOption httpsPortOption;
    QCommandLineOption noHttpOption;
    QCommandLineOption redirectHttpToHttpsOption;
    QCommandLineOption sharedMemoryKeyOption;
    QCommandLineOption sharedMemorySecretOption;
    QCommandLineOption logFileOption;
    QCommandLineOption httpsOption;
    QCommandLineOption noHttpsOption;
    QCommandLineOption externalHttpsOption;
    QCommandLineOption allowInsecureHttpOption;
    QCommandLineOption forwardedHeadersOption;
    QCommandLineOption trustedProxyOption;
    QCommandLineOption publicHttpsHostOption;
    QCommandLineOption tlsCertificateFileOption;
    QCommandLineOption tlsPrivateKeyFileOption;
    QCommandLineOption tlsReloadIntervalOption;
    QCommandLineOption threadsOption;
    QCommandLineOption storageFileOption;
    QCommandLineOption pageTitleOption;
    QCommandLineOption authMethodOption;
};

namespace {

QString lookupSourceDescription(const WebConfig::LookupResult &lookup)
{
    QString description = LayeredIniSettings::sourceName(lookup.source);
    if (!lookup.fileName.isEmpty())
        description += QStringLiteral(" %1").arg(lookup.fileName);
    return description;
}

} // namespace

WebCommandLineParser::Private::Private()
    : configFileOption({ QStringLiteral("config-file") },
                       QStringLiteral("Path to the INI configuration file."),
                       QStringLiteral("file"))
    , bindAddressOption({ QStringLiteral("bind-address") },
                        QStringLiteral("Address to bind the web server to."),
                        QStringLiteral("address"))
    , httpPortOption({ QStringLiteral("http-port") },
                     QStringLiteral("HTTP port to listen on."),
                     QStringLiteral("port"))
    , httpsPortOption({ QStringLiteral("https-port") },
                      QStringLiteral("HTTPS port to listen on."),
                      QStringLiteral("port"))
    , noHttpOption({ QStringLiteral("no-http") }, QStringLiteral("Disable the HTTP listener."))
    , redirectHttpToHttpsOption({ QStringLiteral("redirect-http-to-https") },
                                QStringLiteral("Redirect HTTP requests to HTTPS."))
    , sharedMemoryKeyOption({ QStringLiteral("web-shared-memory-key") },
                            QStringLiteral(
                                    "Shared memory key used to reach booking-resources-server."),
                            QStringLiteral("key"))
    , sharedMemorySecretOption({ QStringLiteral("web-shared-memory-secret") },
                               QStringLiteral("Shared secret used to authenticate web IPC frames."),
                               QStringLiteral("secret"))
    , logFileOption({ QStringLiteral("l"), QStringLiteral("log-file") },
                    QStringLiteral("Path to log file for redirecting stdout and stderr."),
                    QStringLiteral("file"))
    , httpsOption({ QStringLiteral("https") }, QStringLiteral("Enable HTTPS for the web server."))
    , noHttpsOption({ QStringLiteral("no-https") },
                    QStringLiteral("Disable HTTPS even if it is enabled in settings."))
    , externalHttpsOption({ QStringLiteral("external-https") },
                          QStringLiteral("Treat requests as externally served over HTTPS."))
    , allowInsecureHttpOption({ QStringLiteral("allow-insecure-http") },
                              QStringLiteral("Allow plain HTTP on non-loopback bind addresses."))
    , forwardedHeadersOption({ QStringLiteral("forwarded-headers") },
                             QStringLiteral("Trust Forwarded and X-Forwarded-* headers from trusted proxies."))
    , trustedProxyOption({ QStringLiteral("trusted-proxy") },
                         QStringLiteral("Trusted proxy address or CIDR. Can be repeated."),
                         QStringLiteral("address"))
    , publicHttpsHostOption({ QStringLiteral("public-https-host") },
                            QStringLiteral("Public HTTPS host used for redirects."),
                            QStringLiteral("host"))
    , tlsCertificateFileOption({ QStringLiteral("tls-certificate-file") },
                               QStringLiteral("Path to the PEM certificate chain file for HTTPS."),
                               QStringLiteral("file"))
    , tlsPrivateKeyFileOption({ QStringLiteral("tls-private-key-file") },
                              QStringLiteral("Path to the PEM private key file for HTTPS."),
                              QStringLiteral("file"))
    , tlsReloadIntervalOption({ QStringLiteral("tls-reload-interval") },
                              QStringLiteral("Seconds between TLS certificate reload attempts. 0 disables reload."),
                              QStringLiteral("seconds"))
    , threadsOption({ QStringLiteral("threads") },
                    QStringLiteral("Maximum number of request worker threads."),
                    QStringLiteral("count"))
    , storageFileOption({ QStringLiteral("storage-file") },
                        QStringLiteral("Path to the web server SQLite storage file."),
                        QStringLiteral("file"))
    , pageTitleOption({ QStringLiteral("web-page-title") },
                      QStringLiteral("Base title used for web interface pages."),
                      QStringLiteral("title"))
    , authMethodOption({ QStringLiteral("web-auth-method") },
                       QStringLiteral("Enabled web authorization method. Supported values: "
                                      "sign-without-registration, log-in-only, "
                                      "log-in-with-registration. Can be repeated or comma-separated."),
                       QStringLiteral("method"))
{
    parser.setApplicationDescription(QStringLiteral("Web interface for Booking Resources."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(configFileOption);
    parser.addOption(bindAddressOption);
    parser.addOption(httpPortOption);
    parser.addOption(httpsPortOption);
    parser.addOption(noHttpOption);
    parser.addOption(redirectHttpToHttpsOption);
    parser.addOption(sharedMemoryKeyOption);
    parser.addOption(sharedMemorySecretOption);
    parser.addOption(logFileOption);
    parser.addOption(httpsOption);
    parser.addOption(noHttpsOption);
    parser.addOption(externalHttpsOption);
    parser.addOption(allowInsecureHttpOption);
    parser.addOption(forwardedHeadersOption);
    parser.addOption(trustedProxyOption);
    parser.addOption(publicHttpsHostOption);
    parser.addOption(tlsCertificateFileOption);
    parser.addOption(tlsPrivateKeyFileOption);
    parser.addOption(tlsReloadIntervalOption);
    parser.addOption(threadsOption);
    parser.addOption(storageFileOption);
    parser.addOption(pageTitleOption);
    parser.addOption(authMethodOption);
}

WebCommandLineParser::WebCommandLineParser()
    : d(new Private())
{
    // do nothing
}

WebCommandLineParser::~WebCommandLineParser()
{
    delete d;
}

void WebCommandLineParser::process(const QCoreApplication &app)
{
    process(app.arguments());
}

void WebCommandLineParser::process(const QStringList &arguments)
{
    if (!d->parser.parse(arguments)) {
        QTextStream ts(stderr);
        ts << QCoreApplication::applicationName() << ": " << d->parser.errorText() << Qt::endl;
        ts << "Use '--help' to see available options." << Qt::endl;
        QCoreApplication::exit(1);
        return;
    }

    if (d->parser.isSet(QStringLiteral("help")))
        d->parser.showHelp();
    if (d->parser.isSet(QStringLiteral("version")))
        d->parser.showVersion();
}

QString WebCommandLineParser::configFilePath() const
{
    if (!d->parser.isSet(d->configFileOption))
        return QString();

    return d->parser.value(d->configFileOption).trimmed();
}

QString WebCommandLineParser::bindAddress(const WebConfig &config) const
{
    if (d->parser.isSet(d->bindAddressOption))
        return d->parser.value(d->bindAddressOption).trimmed();
    return config.bindAddress().trimmed();
}

quint16 WebCommandLineParser::httpPort(const WebConfig &config) const
{
    if (d->parser.isSet(d->httpPortOption))
        return static_cast<quint16>(d->parser.value(d->httpPortOption).toUInt());
    return config.httpPort();
}

quint16 WebCommandLineParser::httpsPort(const WebConfig &config) const
{
    if (d->parser.isSet(d->httpsPortOption))
        return static_cast<quint16>(d->parser.value(d->httpsPortOption).toUInt());
    return config.httpsPort();
}

bool WebCommandLineParser::httpEnabled(const WebConfig &config) const
{
    if (d->parser.isSet(d->noHttpOption))
        return false;
    return config.httpEnabled();
}

bool WebCommandLineParser::redirectHttpToHttps(const WebConfig &config) const
{
    if (d->parser.isSet(d->redirectHttpToHttpsOption))
        return true;
    return config.redirectHttpToHttps();
}

QString WebCommandLineParser::sharedMemoryKey(const WebConfig &config) const
{
    if (d->parser.isSet(d->sharedMemoryKeyOption))
        return d->parser.value(d->sharedMemoryKeyOption).trimmed();

    const WebConfig::LookupResult lookup = config.lookupValue(QStringLiteral("web/shared_memory_key"),
                                                              QStringLiteral("booking-resources-web"));
    const QString configured = lookup.value.toString().trimmed();
    if (!configured.isEmpty() && lookup.source != LayeredIniSettings::ValueSource::DefaultValue) {
        qCInfo(lcWebCmdLine).nospace()
                << "Using web shared memory key from " << lookupSourceDescription(lookup)
                << ", key: \"" << qPrintable(configured) << "\".";
    }
    return configured;
}

QString WebCommandLineParser::sharedMemorySecret(const WebConfig &config) const
{
    if (d->parser.isSet(d->sharedMemorySecretOption))
        return d->parser.value(d->sharedMemorySecretOption);

    const WebConfig::LookupResult lookup = config.lookupValue(QStringLiteral("web/shared_memory_secret"));
    const QString configured = lookup.value.toString();
    if (!configured.isEmpty()) {
        qCInfo(lcWebCmdLine).nospace()
                << "Using web shared memory secret from " << lookupSourceDescription(lookup) << ".";
    }
    return configured;
}

QString WebCommandLineParser::logFilePath(const WebConfig &config) const
{
    if (d->parser.isSet(d->logFileOption))
        return d->parser.value(d->logFileOption).trimmed();
    return config.logFile().trimmed();
}

bool WebCommandLineParser::httpsEnabled(const WebConfig &config) const
{
    if (d->parser.isSet(d->noHttpsOption))
        return false;
    if (d->parser.isSet(d->httpsOption))
        return true;
    return config.httpsEnabled();
}

bool WebCommandLineParser::externalHttpsEnabled(const WebConfig &config) const
{
    if (d->parser.isSet(d->externalHttpsOption))
        return true;
    return config.externalHttpsEnabled();
}

bool WebCommandLineParser::allowInsecureHttp(const WebConfig &config) const
{
    if (d->parser.isSet(d->allowInsecureHttpOption))
        return true;
    return config.allowInsecureHttp();
}

bool WebCommandLineParser::forwardedHeadersEnabled(const WebConfig &config) const
{
    if (d->parser.isSet(d->forwardedHeadersOption))
        return true;
    return config.forwardedHeadersEnabled();
}

QStringList WebCommandLineParser::trustedProxies(const WebConfig &config) const
{
    const QStringList values = d->parser.values(d->trustedProxyOption);
    if (!values.isEmpty())
        return values;
    return config.trustedProxies();
}

QString WebCommandLineParser::publicHttpsHost(const WebConfig &config) const
{
    if (d->parser.isSet(d->publicHttpsHostOption))
        return d->parser.value(d->publicHttpsHostOption).trimmed();
    return config.publicHttpsHost().trimmed();
}

QString WebCommandLineParser::tlsCertificateFile(const WebConfig &config) const
{
    if (d->parser.isSet(d->tlsCertificateFileOption))
        return d->parser.value(d->tlsCertificateFileOption).trimmed();
    return config.tlsCertificateFile().trimmed();
}

QString WebCommandLineParser::tlsPrivateKeyFile(const WebConfig &config) const
{
    if (d->parser.isSet(d->tlsPrivateKeyFileOption))
        return d->parser.value(d->tlsPrivateKeyFileOption).trimmed();
    return config.tlsPrivateKeyFile().trimmed();
}

int WebCommandLineParser::tlsReloadIntervalSeconds(const WebConfig &config) const
{
    if (d->parser.isSet(d->tlsReloadIntervalOption))
        return qMax(0, d->parser.value(d->tlsReloadIntervalOption).toInt());
    return config.tlsReloadIntervalSeconds();
}

int WebCommandLineParser::threadCount(const WebConfig &config) const
{
    if (d->parser.isSet(d->threadsOption))
        return qMax(1, d->parser.value(d->threadsOption).toInt());
    return config.threadCount();
}

QString WebCommandLineParser::storageFile(const WebConfig &config) const
{
    if (d->parser.isSet(d->storageFileOption)) {
        qCInfo(lcWebCmdLine) << "Using database file from command line option '--storage-file'.";
        return d->parser.value(d->storageFileOption).trimmed();
    }

    const WebConfig::LookupResult lookup = config.lookupValue(QStringLiteral("web/storage_file"));
    const QString configured = lookup.value.toString().trimmed();
    if (!configured.isEmpty()) {
        qCInfo(lcWebCmdLine).nospace()
                << "Using database file from " << lookupSourceDescription(lookup) << ".";
    }
    return configured;
}

QString WebCommandLineParser::pageTitle(const WebConfig &config) const
{
    QString title;
    if (d->parser.isSet(d->pageTitleOption))
        title = d->parser.value(d->pageTitleOption).trimmed();
    else
        title = config.pageTitle().trimmed();

    return title.isEmpty() ? QStringLiteral("Booking Resources") : title;
}

QStringList WebCommandLineParser::authMethods(const WebConfig &config) const
{
    const QStringList values = d->parser.values(d->authMethodOption);
    if (!values.isEmpty())
        return WebAuthMethods::normalizeList(values);
    return config.authMethods();
}
