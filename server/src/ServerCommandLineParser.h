#ifndef SERVERCOMMANDLINEPARSER_H
#define SERVERCOMMANDLINEPARSER_H

#include <QHash>
#include <QString>
#include <QStringList>

#include "MessengerType.h"

class QCoreApplication;

class ServerConfig;

class ServerCommandLineParserPrivate;

/**
 * @brief Command-line parser helper for the server process.
 *
 * This helper encapsulates configuration and handling of QCommandLineParser
 * for the Booking Resources server. It defines supported options and provides
 * convenient accessors to resolved values such as bot tokens and log file path.
 */
class ServerCommandLineParser final
{
public:
    /**
     * @brief Constructs the command-line parser helper and configures options.
     */
    ServerCommandLineParser();

    /**
     * @brief Destroys the parser helper.
     */
    ~ServerCommandLineParser();

    /**
     * @brief Processes command-line arguments for the provided Qt core instance.
     *
     * Call this once during startup after QCoreApplication has been constructed.
     *
     * @param coreApp Reference to the Qt event loop host.
     */
    void process(const QCoreApplication &coreApp);
    void process(const QStringList &arguments);

    /**
     * @brief Returns the explicit config file path from `--config-file`.
     */
    QString configFilePath() const;

    /**
     * @brief Returns the resolved log file path.
     *
     * If the corresponding command-line option is set, its value is used.
     * Otherwise, the value is taken from @p config.
     *
     * @param config Server config used as a fallback source.
     * @return Log file path or empty string if not configured.
     */
    QString logFilePath(const ServerConfig &config) const;

    /**
     * @brief Returns the resolved SQLite database file path.
     *
     * If the corresponding command-line option is set, its value is used.
     * Otherwise, the value is taken from @p config.
     */
    QString databaseFilePath(const ServerConfig &config) const;

    /**
     * @brief Returns the resolved shared memory key for the web IPC transport.
     *
     * If the corresponding command-line option is set, its value is used.
     * Otherwise, the value is taken from @p config.
     */
    QString webSharedMemoryKey(const ServerConfig &config) const;

    /**
     * @brief Returns the resolved shared secret for the web IPC transport.
     */
    QString webSharedMemorySecret(const ServerConfig &config) const;

    /**
     * @brief Returns resolved bot tokens for every messenger.
     */
    QHash<MessengerType, QString> botTokens(const ServerConfig &config) const;

    /**
     * @brief Returns resolved admin user ids per messenger.
     */
    QHash<MessengerType, QStringList> adminUserIds(const ServerConfig &config) const;

private:
    ServerCommandLineParserPrivate *d;
};

#endif // SERVERCOMMANDLINEPARSER_H
