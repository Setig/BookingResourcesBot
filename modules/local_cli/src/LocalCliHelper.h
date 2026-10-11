#ifndef LOCALCLIHELPER_H
#define LOCALCLIHELPER_H

#include <QtGlobal>

#include <QString>

/**
 * @brief Shared helpers for the local CLI client/server implementation.
 *
 * This class intentionally lives in the LocalCli module so both the server and the CLI binary
 * can reuse common parsing and presentation logic.
 */
class LocalCliHelper
{
public:
    /**
     * @brief Parses a PID token to qint64.
     *
     * @param value Token value to parse.
     * @param ok Optional out flag set to true when conversion succeeded.
     */
    [[nodiscard]] static qint64 parsePid(const QString &value, bool *ok = nullptr);

    /**
     * @brief Returns the local host label used to identify CLI sessions.
     *
     * Uses the system host name when available, otherwise returns "local-cli".
     */
    [[nodiscard]] static QString localHostLabel();
};

#endif // LOCALCLIHELPER_H
