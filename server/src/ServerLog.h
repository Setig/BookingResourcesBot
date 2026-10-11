#ifndef SERVERLOG_H
#define SERVERLOG_H

#include <QString>

/**
 * @brief Utility class that configures server logging.
 *
 * Log file configuration (such as file path) is provided by ServerConfig or
 * command line options and passed to ServerLog via initializeLogging(). The
 * actual Qt message handler implementation is shared with other applications.
 */
class ServerLog final
{
public:
    /**
     * @brief Initializes server logging.
     *
     * Installs a Qt message handler that adds timestamps to all log output.
     * If @p filePath is not empty, log messages are redirected to a
     * rotating log file that is:
     * - split by days;
     * - compressed when reaching a predefined maximum size.
     *
     * @param filePath Base path to the log file. The actual files will have
     *        the current date appended to the base name.
     */
    static void initializeLogging(const QString &filePath);

private:
    ServerLog();
};

#endif // SERVERLOG_H
