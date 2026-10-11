#ifndef SERVERAPPLICATION_H
#define SERVERAPPLICATION_H

#include <QString>
#include <QPointer>
#include <QLoggingCategory>

#include "MessengerType.h"
#include "BookingResourcesApplication.h"

class MessengerBot;
class MessengerBotPoll;
class ServerDatabase;
class BookingResourcesTranslator;

Q_DECLARE_LOGGING_CATEGORY(lcServerApplication)

/**
 * @brief Application wrapper for the server process.
 *
 * This class owns server-specific startup and shutdown resources.
 */
class ServerApplication final : public BookingResourcesApplication
{
public:
    ServerApplication(int &argc, char **argv);
    ~ServerApplication() override;

    /**
     * @brief Application exit codes.
     *
     * These codes document the reasons for early startup termination.
     */
    enum class ExitCode {
        Success = 0,
        CommandLineError = 1,
        DatabaseInitFailed = 2,
        MissingBotTokens = 3,
        NoBotsStarted = 4,
        WebIpcSecretMissing = 5,
    };

    /**
     * @brief Performs server initialization without starting the event loop.
     *
     * This method owns the high-level startup flow previously implemented in main.cpp:
     * - command line parsing
     * - logging initialization
     * - database initialization and bootstrapping (fixed admins)
     * - bot creation and startup
     *
     * @return ExitCode value for early failures, or Success on success.
     */
    ExitCode tryStart();

    using BookingResourcesApplication::logStartup;
    using BookingResourcesApplication::shutdown;

    void setDatabase(ServerDatabase *database);
    void setTranslator(BookingResourcesTranslator *translator);

    MessengerBot *createBot(MessengerType type, const QString &token, QObject *parent = nullptr);
    MessengerBot *bot(MessengerType type, const QString &token) const;

protected:
    void performShutdown() override;

private:
    QString normalizeToken(const QString &token) const;

    MessengerBotPoll *m_botPoll;
    QPointer<ServerDatabase> m_database;
    BookingResourcesTranslator *m_translator;
};

#endif // SERVERAPPLICATION_H
