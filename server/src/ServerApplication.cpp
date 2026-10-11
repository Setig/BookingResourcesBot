#include "ServerApplication.h"

#include "WebBot.h"
#include "ServerLog.h"
#include "MessengerBot.h"
#include "ServerConfig.h"
#include "ServerDatabase.h"
#include "MessengerBotPoll.h"
#include "LocalCliBot.h"
#include "LocalCliRegistry.h"
#include "ServerDatabaseError.h"
#include "MessengerMenuManager.h"
#include "ServerCommandLineParser.h"
#include "BookingResourcesTranslator.h"

#include <QHash>
#include <QList>
#include <QVector>
#include <QString>
#include <QElapsedTimer>

Q_LOGGING_CATEGORY(lcServerApplication, "booking.server.application")

namespace {

QString configLocationHint(const ServerConfig &config)
{
    const QString explicitFile = config.explicitFileName().trimmed();
    if (!explicitFile.isEmpty())
        return explicitFile;

    return QStringLiteral("%1 (user scope) or %2 (system scope)")
            .arg(config.userScopeFileName(), config.systemScopeFileName());
}

void logConfigSelection(const ServerConfig &config,
                        const QString &commandLineConfigFile,
                        const QString &environmentConfigFile)
{
    if (!commandLineConfigFile.isEmpty()) {
        qCInfo(lcServerApplication).nospace()
                << "Using configuration file from command line option '--config-file': "
                << config.fileName() << " with fallback to user-scope settings file "
                << config.userScopeFileName() << " and system-scope settings file "
                << config.systemScopeFileName() << ".";
        return;
    }

    if (!environmentConfigFile.isEmpty()) {
        qCInfo(lcServerApplication).nospace()
                << "Using configuration file from environment variable "
                << ServerConfig::configFileEnvironmentVariable() << ": " << config.fileName()
                << " with fallback to user-scope settings file " << config.userScopeFileName()
                << " and system-scope settings file " << config.systemScopeFileName() << ".";
        return;
    }

    qCInfo(lcServerApplication).nospace()
            << "Using layered Qt INI settings with user-scope file " << config.userScopeFileName()
            << " and system-scope file " << config.systemScopeFileName() << ".";
}

} // namespace

ServerApplication::ServerApplication(int &argc, char **argv)
    : BookingResourcesApplication(argc,
                                  argv,
                                  QStringLiteral(APP_NAME),
                                  QStringLiteral(APP_VERSION),
                                  QStringLiteral("server"),
                                  lcServerApplication())
    , m_botPoll(new MessengerBotPoll(this))
    , m_database()
    , m_translator(nullptr)
{
    // do nothing
}

ServerApplication::~ServerApplication()
{
    shutdown();

    delete m_botPoll;
    // Destroy QtSql handles before QCoreApplication teardown.
    delete m_database.data();
    delete m_translator;
}

ServerApplication::ExitCode ServerApplication::tryStart()
{
    ServerCommandLineParser cmdParser;
    cmdParser.process(*this);
    const QString commandLineConfigFile = cmdParser.configFilePath();
    const QString environmentConfigFile = ServerConfig::environmentConfigFilePath();
    const QString explicitConfigFile =
            ServerConfig::resolveExplicitConfigFilePath(commandLineConfigFile);
    ServerConfig config(explicitConfigFile);

    const QString logFilePath = cmdParser.logFilePath(config);
    ServerLog::initializeLogging(logFilePath);
    logConfigSelection(config, commandLineConfigFile, environmentConfigFile);
    logStartup();

    BookingResourcesTranslator *translator =
            new BookingResourcesTranslator(QStringLiteral("booking-resources-server"));
    translator->initialize();
    setTranslator(translator);

    if (!logFilePath.isEmpty()) {
        qCInfo(lcServerApplication).nospace() << "Logging to file base path: " << logFilePath;
    } else {
        qCInfo(lcServerApplication) << "Logging to console only (no log file path is configured).";
    }

    const QString databaseFile = cmdParser.databaseFilePath(config);
    ServerDatabase *database = new ServerDatabase(databaseFile, this);
    setDatabase(database);
    qCInfo(lcServerApplication).nospace()
            << "Using SQLite database file: " << database->databasePath();

    if (!database->isReady()) {
        qCCritical(lcServerApplication).nospace()
                << "Failed to initialize database at " << databaseFile;
        shutdown();
        return ExitCode::DatabaseInitFailed;
    }

    const QHash<MessengerType, QStringList> adminMap = cmdParser.adminUserIds(config);
    if (adminMap.isEmpty()) {
        qCInfo(lcServerApplication) << "No fixed messenger admins configured.";
    } else {
        for (QHash<MessengerType, QStringList>::const_iterator it = adminMap.cbegin();
             it != adminMap.cend();
             ++it) {
            const QStringList admins = it.value();
            if (admins.isEmpty())
                continue;
            qCInfo(lcServerApplication).nospace()
                    << "Fixed " << messengerTypeDisplayName(it.key())
                    << " admins(user_id): " << admins.join(QStringLiteral(", "));
            for (const QString &adminId : admins) {
                const ServerDatabaseError error =
                        database->ensureFixedAdmin(MessengerUserKey(it.key(), adminId));
                if (error) {
                    qCWarning(lcServerApplication).nospace()
                            << "Failed to ensure fixed admin privileges for "
                            << messengerTypeDisplayName(it.key()) << " user id " << adminId;
                }
            }
        }
    }

    const QString webSharedMemoryKey = cmdParser.webSharedMemoryKey(config);
    const QString webSharedMemorySecret = cmdParser.webSharedMemorySecret(config);
    const QHash<MessengerType, QString> tokens = cmdParser.botTokens(config);
    if (tokens.isEmpty() && webSharedMemoryKey.isEmpty()) {
        qCCritical(lcServerApplication).nospace()
                << "At least one bot token or web shared memory key is required. Specify "
                   "--<messenger>-token or --web-shared-memory-key on the command line "
                << "or configure tokens/shared memory key in " << configLocationHint(config) << ".";
        shutdown();
        return ExitCode::MissingBotTokens;
    }
    if (!webSharedMemoryKey.isEmpty() && webSharedMemorySecret.isEmpty()) {
        qCCritical(lcServerApplication)
                << "Web IPC shared memory secret is required when web IPC is enabled. Configure "
                   "web/shared_memory_secret or pass --web-shared-memory-secret.";
        shutdown();
        return ExitCode::WebIpcSecretMissing;
    }

    bool atLeastOneBotStarted = false;

    {
        const QString cliEndpoint =
                LocalCliRegistry::defaultServerName(QCoreApplication::applicationPid());
        MessengerBot *cliBot = createBot(MessengerType::LocalCli, cliEndpoint);
        if (cliBot) {
            QObject::connect(cliBot,
                             &MessengerBot::errorOccurred,
                             this,
                             [](const QString &description) {
                                 qCWarning(lcServerApplication).nospace()
                                         << "Local CLI error: " << description;
                             });
            if (LocalCliBot *localCliBot = qobject_cast<LocalCliBot *>(cliBot->backendBot())) {
                QObject::connect(localCliBot, &LocalCliBot::clientSessionStarted, this, []() {
                    BookingResourcesApplication::retainConsoleInterruptSuppression();
                });
                QObject::connect(
                        localCliBot,
                        &LocalCliBot::clientSessionFinished,
                        this,
                        []() { BookingResourcesApplication::releaseConsoleInterruptSuppression(); },
                        Qt::QueuedConnection);
            }
            cliBot->start();
        } else {
            qCWarning(lcServerApplication) << "Failed to initialize Local CLI endpoint.";
        }
    }

    if (!webSharedMemoryKey.isEmpty()) {
        MessengerBot *webBot = createBot(MessengerType::Web, webSharedMemoryKey);
        if (webBot) {
            if (WebBot *typedWebBot = qobject_cast<WebBot *>(webBot->backendBot())) {
                typedWebBot->setSharedMemorySecret(webSharedMemorySecret);
            }
            QObject::connect(webBot,
                             &MessengerBot::errorOccurred,
                             this,
                             [](const QString &description) {
                                 qCWarning(lcServerApplication).nospace()
                                         << "Web IPC error: " << description;
                             });
            webBot->start();
            atLeastOneBotStarted = true;
        } else {
            qCWarning(lcServerApplication) << "Failed to initialize Web IPC endpoint.";
        }
    }

    for (QHash<MessengerType, QString>::const_iterator it = tokens.cbegin(); it != tokens.cend();
         ++it) {
        MessengerBot *botInstance = createBot(it.key(), it.value());
        if (!botInstance) {
            qCWarning(lcServerApplication).nospace()
                    << "Failed to create " << messengerTypeDisplayName(it.key())
                    << " bot instance.";
            continue;
        }

        QObject::connect(botInstance,
                         &MessengerBot::errorOccurred,
                         this,
                         [type = it.key()](const QString &description) {
                             qCWarning(lcServerApplication).nospace()
                                     << messengerTypeDisplayName(type)
                                     << " bot error: " << description;
                         });
        botInstance->start();
        atLeastOneBotStarted = true;
    }

    if (!atLeastOneBotStarted) {
        qCCritical(lcServerApplication) << "No messenger bots were started, aborting.";
        shutdown();
        return ExitCode::NoBotsStarted;
    }

    return ExitCode::Success;
}

void ServerApplication::setDatabase(ServerDatabase *database)
{
    m_database = database;
    if (m_botPoll)
        m_botPoll->setDatabase(database);
}

void ServerApplication::setTranslator(BookingResourcesTranslator *translator)
{
    if (m_translator == translator)
        return;

    delete m_translator;
    m_translator = translator;
    if (m_botPoll)
        m_botPoll->setTranslator(m_translator);
}

MessengerBot *ServerApplication::createBot(MessengerType type,
                                           const QString &token,
                                           QObject *parent)
{
    if (!m_botPoll)
        return nullptr;

    return m_botPoll->createBot(type, normalizeToken(token), parent);
}

MessengerBot *ServerApplication::bot(MessengerType type, const QString &token) const
{
    if (!m_botPoll)
        return nullptr;

    return m_botPoll->bot(type, normalizeToken(token));
}

QString ServerApplication::normalizeToken(const QString &token) const
{
    return token.trimmed();
}

void ServerApplication::performShutdown()
{
    QElapsedTimer shutdownTimer;
    shutdownTimer.start();

    logShutdownStarted();

    QVector<QPointer<MessengerBot>> bots;

    // Collect bots
    const QList<MessengerBot *> storedBots =
            m_botPoll ? m_botPoll->bots() : QList<MessengerBot *>();
    for (MessengerBot *bot : storedBots) {
        if (!bot)
            continue;

        bots.append(bot);
    }

    // Close interactive menus first while the messenger backends are still active.
    for (const QPointer<MessengerBot> &botPtr : bots) {
        MessengerBot *bot = botPtr.data();

        if (!bot)
            continue;

        if (MessengerMenuManager *menuManager = bot->menuManager()) {
            menuManager->shutdown();
        }
    }

    // Start messenger backend shutdown for all bots (non-blocking).
    for (const QPointer<MessengerBot> &botPtr : bots) {
        MessengerBot *bot = botPtr.data();
        if (!bot)
            continue;
        AbstractBot *backend = bot->backendBot();
        if (backend) {
            backend->startShutdown();
        } else {
            bot->stop();
        }
    }

    // Persist and close the database while bots are draining their pending work.
    if (m_database) {
        m_database->shutdown();
    }

    // Wait for messenger backend shutdown completion (may drain pending outgoing requests).
    for (const QPointer<MessengerBot> &botPtr : bots) {
        MessengerBot *bot = botPtr.data();
        if (!bot)
            continue;
        AbstractBot *backend = bot->backendBot();
        if (!backend)
            continue;
        backend->waitShutdown();
    }

    logShutdownFinished(shutdownTimer.elapsed());
}
