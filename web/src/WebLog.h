#ifndef WEBLOG_H
#define WEBLOG_H

#include <QString>

/**
 * @brief Utility class that configures web application logging.
 *
 * Log file configuration is provided by WebConfig or command line options and
 * passed to WebLog via initializeLogging(). The actual Qt message handler
 * implementation is shared with other applications.
 */
class WebLog final
{
public:
    /**
     * @brief Initializes web application logging.
     * @param filePath Base path to the rotating log file, or empty for console logging.
     */
    static void initializeLogging(const QString &filePath);

private:
    WebLog();
};

#endif // WEBLOG_H
