#ifndef SERVERCONFIG_H
#define SERVERCONFIG_H

#include <QStringList>
#include <QVariant>

#include "MessengerType.h"
#include "LayeredIniSettings.h"

/**
 * @brief Layered server config wrapper based on QSettings.
 *
 * The wrapper reads INI settings from an optional explicit file and then
 * falls back to the standard Qt user-scope and system-scope locations.
 * Writes always target the explicit file when one is configured, otherwise
 * they target the user-scope settings file.
 */
class ServerConfig final
{
public:
    using LookupResult = LayeredIniSettings::LookupResult;

    /**
     * @brief Constructs a ServerConfig instance.
     *
     * The wrapper uses Qt INI user scope as the writable target and reads
     * system scope as a fallback source.
     */
    ServerConfig();
    explicit ServerConfig(const QString &explicitFilePath);

    static const char *configFileEnvironmentVariable();
    static QString environmentConfigFilePath();
    static QString resolveExplicitConfigFilePath(const QString &commandLineConfigFile);

    QString fileName() const;
    QString userScopeFileName() const;
    QString systemScopeFileName() const;
    QString explicitFileName() const;
    void sync();

    LookupResult lookupValue(const QString &key, const QVariant &defaultValue = QVariant()) const;
    LookupResult lookupAnyValue(const QStringList &keys,
                                const QVariant &defaultValue = QVariant()) const;

    /**
     * @brief Returns stored bot token for the given messenger.
     */
    QString botToken(MessengerType messenger) const;

    /**
     * @brief Stores bot token for the given messenger.
     */
    void setBotToken(MessengerType messenger, const QString &token);

    /**
     * @brief Returns stored log file path, or empty string if not set.
     */
    QString logFile() const;

    /**
     * @brief Stores log file path value.
     * @param filePath New log file path.
     */
    void setLogFile(const QString &filePath);

    /**
     * @brief Returns stored SQLite database file path, or empty string if not set.
     */
    QString databaseFile() const;

    /**
     * @brief Stores SQLite database file path.
     * @param filePath New database file path.
     */
    void setDatabaseFile(const QString &filePath);

    /**
     * @brief Returns shared memory key used by the web IPC transport.
     */
    QString webSharedMemoryKey() const;

    /**
     * @brief Stores shared memory key used by the web IPC transport.
     */
    void setWebSharedMemoryKey(const QString &key);

    /**
     * @brief Returns shared secret used to authenticate web IPC frames.
     */
    QString webSharedMemorySecret() const;

    /**
     * @brief Stores shared secret used to authenticate web IPC frames.
     */
    void setWebSharedMemorySecret(const QString &secret);

    /**
     * @brief Returns stored admin user IDs for the given messenger.
     */
    QStringList adminUserIds(MessengerType messenger) const;

    /**
     * @brief Stores admin user IDs for the given messenger.
     */
    void setAdminUserIds(MessengerType messenger, const QStringList &userIds);

private:
    LayeredIniSettings m_settings;
};

#endif // SERVERCONFIG_H
