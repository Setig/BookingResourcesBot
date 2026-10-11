#include "WebApplication.h"

#include "WebLog.h"
#include "WebConfig.h"
#include "WebServer.h"
#include "WebClient.h"
#include "WebAuthManager.h"
#include "WebLocalCliCommands.h"
#include "WebSessionStore.h"
#include "LocalCliEndpoint.h"
#include "WebSecurityPolicy.h"
#include "WebCommandLineParser.h"
#include "WebStaticTranslations.h"
#include "BookingResourcesTranslator.h"

#include <QJsonObject>
#include <QStringList>
#include <QHostAddress>
#include <QElapsedTimer>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcWebApplication, "booking.web.application")

namespace {

QString configLocationHint(const WebConfig &config)
{
    const QString explicitFile = config.explicitFileName().trimmed();
    if (!explicitFile.isEmpty())
        return explicitFile;

    return QStringLiteral("%1 (user scope) or %2 (system scope)")
            .arg(config.userScopeFileName(), config.systemScopeFileName());
}

void logConfigSelection(const WebConfig &config,
                        const QString &commandLineConfigFile,
                        const QString &environmentConfigFile)
{
    if (!commandLineConfigFile.isEmpty()) {
        qCInfo(lcWebApplication).nospace()
                << "Using configuration file from command line option '--config-file': "
                << config.fileName() << " with fallback to user-scope settings file "
                << config.userScopeFileName() << " and system-scope settings file "
                << config.systemScopeFileName() << ".";
        return;
    }

    if (!environmentConfigFile.isEmpty()) {
        qCInfo(lcWebApplication).nospace()
                << "Using configuration file from environment variable "
                << WebConfig::configFileEnvironmentVariable() << ": " << config.fileName()
                << " with fallback to user-scope settings file " << config.userScopeFileName()
                << " and system-scope settings file " << config.systemScopeFileName() << ".";
        return;
    }

    qCInfo(lcWebApplication).nospace()
            << "Using layered Qt INI settings with user-scope file " << config.userScopeFileName()
            << " and system-scope file " << config.systemScopeFileName() << ".";
}

} // namespace

WebApplication::WebApplication(int &argc, char **argv)
    : BookingResourcesApplication(argc,
                                  argv,
                                  QStringLiteral(APP_NAME),
                                  QStringLiteral(APP_VERSION),
                                  QStringLiteral("web"),
                                  lcWebApplication())
    , m_store(nullptr)
    , m_webClient(nullptr)
    , m_authManager(nullptr)
    , m_server(nullptr)
    , m_localCliCommands(nullptr)
    , m_localCli(nullptr)
    , m_translator(nullptr)
{
    // do nothing
}

WebApplication::~WebApplication()
{
    shutdown();

    delete m_localCli;
    delete m_localCliCommands;
    delete m_server;
    delete m_authManager;
    delete m_webClient;
    delete m_store;
    delete m_translator;
}

WebApplication::ExitCode WebApplication::tryStart()
{
    WebCommandLineParser parser;
    parser.process(*this);
    const QString commandLineConfigFile = parser.configFilePath();
    const QString environmentConfigFile = WebConfig::environmentConfigFilePath();
    const QString explicitConfigFile =
            WebConfig::resolveExplicitConfigFilePath(commandLineConfigFile);
    WebConfig config(explicitConfigFile);

    const QString logFilePath = parser.logFilePath(config);
    WebLog::initializeLogging(logFilePath);
    logConfigSelection(config, commandLineConfigFile, environmentConfigFile);
    logStartup();

    if (!logFilePath.isEmpty()) {
        qCInfo(lcWebApplication).nospace() << "Logging to file base path: " << logFilePath;
    } else {
        qCInfo(lcWebApplication) << "Logging to console only (no log file path is configured).";
    }

    WebStaticTranslations::touchTranslations();

    m_translator = new BookingResourcesTranslator(QStringLiteral("booking-resources-web"));
    m_translator->initialize(QStringLiteral("en"));

    m_store = new WebSessionStore(this);
    const QString storageFile = parser.storageFile(config);
    if (!m_store->open(storageFile)) {
        qCCritical(lcWebApplication).noquote()
                << "Failed to initialize web storage:" << m_store->lastError();
        return ExitCode::StorageInitFailed;
    }
    qCInfo(lcWebApplication).nospace() << "Using SQLite database file: " << m_store->storagePath();

    const QString sharedMemorySecret = parser.sharedMemorySecret(config);
    if (sharedMemorySecret.isEmpty()) {
        qCCritical(lcWebApplication).nospace()
                << "Web IPC shared memory secret is required. Configure "
                   "web/shared_memory_secret in "
                << configLocationHint(config) << " or pass --web-shared-memory-secret.";
        return ExitCode::WebIpcInitFailed;
    }

    m_webClient = new WebClient(parser.sharedMemoryKey(config), this);
    m_webClient->setUserProfileProvider([this](const QString &userId) {
        QJsonObject response;
        const WebSessionStore::User user =
                m_store ? m_store->user(userId) : WebSessionStore::User();
        if (!user.isValid()) {
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"),
                            QCoreApplication::translate("WebApplication",
                                                        "Web user was not found."));
            return response;
        }

        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("user_id"), user.userId);
        response.insert(QStringLiteral("username"), QStringLiteral("web:%1").arg(user.userId));
        response.insert(QStringLiteral("first_name"), user.firstName);
        response.insert(QStringLiteral("last_name"), user.lastName);
        response.insert(QStringLiteral("language_code"), user.locale);
        response.insert(QStringLiteral("is_bot"), false);
        return response;
    });
    m_webClient->setSharedMemorySecret(sharedMemorySecret);
    QObject::connect(m_webClient, &WebClient::connected, this, []() {
        qCInfo(lcWebApplication) << "Web IPC handshake completed.";
    });
    QObject::connect(m_webClient, &WebClient::disconnected, this, [this]() {
        qCWarning(lcWebApplication).noquote()
                << "Web IPC connection lost, retrying:" << m_webClient->sharedMemoryKey();
    });
    m_webClient->startAutoReconnect();

    QObject::connect(
            m_webClient, &WebClient::botEventReceived, this, [this](const QJsonObject &event) {
                if (event.value(QStringLiteral("type")).toString()
                    == QLatin1String("command_list")) {
                    return;
                }

                if (!m_store)
                    return;

                QJsonObject storedEvent = event;
                if (storedEvent.value(QStringLiteral("type")).toString()
                            == QLatin1String("command_cache_invalidated")
                    && storedEvent.value(QStringLiteral("global")).toBool()) {
                    m_store->clearAllCommands();
                    const QStringList users = m_store->userIds();
                    for (const QString &targetUserId : users) {
                        QJsonObject userEvent = storedEvent;
                        userEvent.insert(QStringLiteral("chat_id"), targetUserId);
                        m_store->appendEvent(targetUserId, userEvent);
                    }
                    return;
                }

                const QString userId =
                        storedEvent.value(QStringLiteral("chat_id")).toString().trimmed();
                if (userId.isEmpty())
                    return;

                if (storedEvent.value(QStringLiteral("type")).toString()
                    == QLatin1String("command_cache_invalidated")) {
                    m_store->clearCommands(userId);
                }

                if (storedEvent.value(QStringLiteral("type")).toString()
                    == QLatin1String("bot_file")) {
                    WebSessionStore::StoredFile file;
                    const QString filePath =
                            storedEvent.value(QStringLiteral("file_path")).toString();
                    const QString fileName =
                            storedEvent.value(QStringLiteral("file_name")).toString();
                    const QByteArray fileData = QByteArray::fromBase64(
                            storedEvent.value(QStringLiteral("file_content_base64"))
                                    .toString()
                                    .toLatin1());
                    storedEvent.remove(QStringLiteral("file_path"));
                    storedEvent.remove(QStringLiteral("file_content_base64"));
                    storedEvent.insert(QStringLiteral("file_available"), false);
                    const bool registered =
                            (!fileData.isEmpty()
                             && m_store->registerFileData(userId, fileName, fileData, &file))
                            || m_store->registerFile(userId, filePath, &file);
                    if (registered) {
                        storedEvent.insert(QStringLiteral("file_available"), true);
                        storedEvent.insert(QStringLiteral("file_id"), file.fileId);
                        storedEvent.insert(QStringLiteral("file_name"), file.fileName);
                        storedEvent.insert(QStringLiteral("file_size"),
                                           static_cast<double>(file.fileSize));
                    }
                }

                m_store->appendEvent(userId, storedEvent);
            });

    m_authManager = new WebAuthManager(m_store, this);
    m_localCliCommands = new WebLocalCliCommands(m_authManager, m_store);
    m_localCli = new LocalCliEndpoint(this);
    m_localCli->setKind(LocalCliRegistry::Kind::Web);
    m_localCli->setSharedMemoryKey(parser.sharedMemoryKey(config));
    const QString localCliLocale = BookingResourcesTranslator::resolvePreferredLocale();
    {
        BookingResourcesTranslator::ScopedLocaleOverride welcomeGuard =
                m_translator ? m_translator->useLocale(localCliLocale)
                             : BookingResourcesTranslator::ScopedLocaleOverride();
        Q_UNUSED(welcomeGuard);
        m_localCli->setWelcomeInfo(
                QCoreApplication::translate("WebApplication",
                                            "Local CLI accepts web user-management commands. "
                                            "Type '\\help' inside the CLI for usage details."));
    }
    m_localCli->setCommandHandler([this, localCliLocale](const QString &command) {
        BookingResourcesTranslator::ScopedLocaleOverride localeGuard =
                m_translator ? m_translator->useLocale(localCliLocale)
                             : BookingResourcesTranslator::ScopedLocaleOverride();
        Q_UNUSED(localeGuard);
        const WebLocalCliCommands::Result result = m_localCliCommands->execute(command);
        LocalCliEndpoint::CommandResult endpointResult;
        endpointResult.ok = result.ok;
        endpointResult.text = result.text;
        return endpointResult;
    });
    m_localCli->setCompletionHandler([this, localCliLocale](const QString &text, int cursor) {
        BookingResourcesTranslator::ScopedLocaleOverride localeGuard =
                m_translator ? m_translator->useLocale(localCliLocale)
                             : BookingResourcesTranslator::ScopedLocaleOverride();
        Q_UNUSED(localeGuard);
        const WebLocalCliCommands::Completion completion =
                m_localCliCommands->complete(text, cursor);
        LocalCliProtocol::CompletionResult endpointResult;
        endpointResult.replaceFrom = completion.replaceFrom;
        endpointResult.replaceTo = completion.replaceTo;
        endpointResult.values = completion.values;
        endpointResult.appendSpace = completion.appendSpace;
        return endpointResult;
    });
    QObject::connect(m_localCli, &LocalCliEndpoint::clientSessionStarted, this, []() {
        BookingResourcesApplication::retainConsoleInterruptSuppression();
    });
    // Queue the release so a QEvent::Quit already posted by the same Ctrl+C is
    // still suppressed while booking-resources-cli shares this console.
    QObject::connect(
            m_localCli,
            &LocalCliEndpoint::clientSessionFinished,
            this,
            []() { BookingResourcesApplication::releaseConsoleInterruptSuppression(); },
            Qt::QueuedConnection);
    if (!m_localCli->start()) {
        qCCritical(lcWebApplication).noquote()
                << "Failed to start web Local CLI endpoint:" << m_localCli->endpointName();
        return ExitCode::LocalCliInitFailed;
    }

    m_server = new WebServer(m_authManager, m_webClient, m_store, m_translator, this);
    m_server->setThreadCount(parser.threadCount(config));
    m_server->setExternalHttpsEnabled(parser.externalHttpsEnabled(config));
    m_server->setPageTitle(parser.pageTitle(config));
    m_server->setAuthMethods(parser.authMethods(config));
    m_localCliCommands->setAuthMethods(parser.authMethods(config));
    m_server->setRedirectHttpToHttps(parser.redirectHttpToHttps(config));
    m_server->setForwardedHeadersEnabled(parser.forwardedHeadersEnabled(config));
    m_server->setTrustedProxies(parser.trustedProxies(config));
    m_server->setPublicHttpsHost(parser.publicHttpsHost(config));

    WebServer::TlsSettings tlsSettings;
    tlsSettings.enabled = parser.httpsEnabled(config);
    tlsSettings.certificateFile = parser.tlsCertificateFile(config);
    tlsSettings.privateKeyFile = parser.tlsPrivateKeyFile(config);
    if (!m_server->setTlsSettings(tlsSettings)) {
        qCCritical(lcWebApplication).noquote()
                << "Failed to configure web server TLS:" << m_server->lastError();
        return ExitCode::WebServerInitFailed;
    }
    m_server->setTlsReloadIntervalSeconds(parser.tlsReloadIntervalSeconds(config));

    const QHostAddress address(parser.bindAddress(config));
    const bool httpsEnabled = m_server->httpsEnabled();
    const bool allowInsecureHttp = parser.allowInsecureHttp(config);
    bool listened = false;

    const bool httpEnabled = parser.httpEnabled(config);
    const bool redirectHttpToHttps = httpsEnabled && parser.redirectHttpToHttps(config);
    const quint16 httpPort = parser.httpPort(config);
    const quint16 httpsPort = parser.httpsPort(config);
    if (httpEnabled && httpsEnabled && httpPort == httpsPort) {
        qCCritical(lcWebApplication) << "HTTP and HTTPS listeners cannot use the same port.";
        return ExitCode::WebServerInitFailed;
    }

    if (httpEnabled) {
        const WebSecurityPolicy::PlainHttpPurpose purpose =
                redirectHttpToHttps ? WebSecurityPolicy::PlainHttpPurpose::Redirect
                                    : WebSecurityPolicy::PlainHttpPurpose::Application;
        if (!WebSecurityPolicy::isPlainHttpBindAllowed(address, purpose, allowInsecureHttp)) {
            qCCritical(lcWebApplication).noquote()
                    << "Refusing to listen on plain HTTP for non-loopback address "
                    << address.toString()
                    << ". Enable HTTPS redirect or pass --allow-insecure-http explicitly.";
            return ExitCode::WebServerInitFailed;
        }
        const bool httpListening = redirectHttpToHttps
                                           ? m_server->listenHttpRedirect(address, httpPort)
                                           : m_server->listenHttp(address, httpPort);
        if (!httpListening) {
            qCCritical(lcWebApplication).noquote()
                    << "Failed to start HTTP web listener:" << m_server->lastError();
            return ExitCode::WebServerInitFailed;
        }
        qCInfo(lcWebApplication).noquote().nospace()
                << "booking-resources-web listening on http://" << address.toString() << ":"
                << httpPort << (redirectHttpToHttps ? " (redirects to HTTPS)." : ".");
        listened = true;
    }

    if (httpsEnabled) {
        if (!m_server->listenHttps(address, httpsPort)) {
            qCCritical(lcWebApplication).noquote()
                    << "Failed to start HTTPS web listener:" << m_server->lastError();
            return ExitCode::WebServerInitFailed;
        }
        qCInfo(lcWebApplication).noquote().nospace()
                << "booking-resources-web listening on https://" << address.toString() << ":"
                << httpsPort << ".";
        listened = true;
    }

    if (!listened) {
        qCCritical(lcWebApplication) << "No web listener is enabled. Enable HTTP or HTTPS.";
        return ExitCode::WebServerInitFailed;
    }

    m_localCliCommands->setPublicOrigin(m_server->publicOrigin());

    qCInfo(lcWebApplication).noquote().nospace()
            << "booking-resources-web using shared memory key: \"" << parser.sharedMemoryKey(config)
            << "\".";
    return ExitCode::Success;
}

void WebApplication::performShutdown()
{
    QElapsedTimer shutdownTimer;
    shutdownTimer.start();

    logShutdownStarted();

    if (m_localCli)
        m_localCli->stop();
    if (m_server)
        m_server->close();
    if (m_webClient)
        m_webClient->stop();

    logShutdownFinished(shutdownTimer.elapsed());
}
