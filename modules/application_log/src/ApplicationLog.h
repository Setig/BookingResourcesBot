#ifndef APPLICATIONLOG_H
#define APPLICATIONLOG_H

#include <QString>

/**
 * @brief Utility class that configures application logging.
 *
 * This class installs a custom Qt message handler that:
 * - prefixes each log message with a timestamp and severity;
 * - optionally redirects log output to a rotating log file.
 *
 * When a log file path is configured, console output is redirected to the log
 * file (no messages are printed to the console).
 *
 * When no log file path is configured, log messages are written to the standard
 * streams based on their severity:
 * - Debug / Info / Warning  -> stdout
 * - Critical / Fatal        -> stderr
 */
class ApplicationLog final
{
public:
    /**
     * @brief Initializes application logging.
     *
     * Installs a Qt message handler that adds timestamps to all log output.
     * If @p filePath is not empty, log messages are redirected to a rotating
     * log file that is:
     * - split by days;
     * - compressed when reaching a predefined maximum size.
     *
     * @param filePath Base path to the log file. The actual files will have
     *        the current date appended to the base name.
     */
    static void initializeLogging(const QString &filePath);

private:
    ApplicationLog();
};

#endif // APPLICATIONLOG_H
