#include "ServerConfig.h"

#include <QByteArray>

namespace {

constexpr char kServerConfigFileEnvVar[] = "BOOKING_RESOURCES_BOT_SERVER_CONFIG_FILE";

QString botSettingsKey(MessengerType messenger, const QString &name)
{
    return QStringLiteral("bots/%1/%2").arg(messengerTypeToString(messenger), name);
}

QStringList botTokenKeys(MessengerType messenger)
{
    QStringList keys;
    keys.append(botSettingsKey(messenger, QStringLiteral("token")));
    if (messenger == MessengerType::Telegram)
        keys.append(QStringLiteral("telegram/token"));
    return keys;
}

QStringList adminIdKeys(MessengerType messenger)
{
    QStringList keys;
    keys.append(botSettingsKey(messenger, QStringLiteral("admin_ids")));
    if (messenger == MessengerType::Telegram)
        keys.append(QStringLiteral("telegram/admin_ids"));
    return keys;
}

} // namespace

ServerConfig::ServerConfig()
    : ServerConfig(QString())
{
    // do nothing
}

ServerConfig::ServerConfig(const QString &explicitFilePath)
    : m_settings(QStringLiteral(APP_NAME), QStringLiteral("config"), explicitFilePath)
{
    // do nothing
}

const char *ServerConfig::configFileEnvironmentVariable()
{
    return kServerConfigFileEnvVar;
}

QString ServerConfig::environmentConfigFilePath()
{
    return QString::fromLocal8Bit(qgetenv(configFileEnvironmentVariable())).trimmed();
}

QString ServerConfig::resolveExplicitConfigFilePath(const QString &commandLineConfigFile)
{
    const QString trimmedCommandLinePath = commandLineConfigFile.trimmed();
    if (!trimmedCommandLinePath.isEmpty())
        return trimmedCommandLinePath;

    return environmentConfigFilePath();
}

QString ServerConfig::fileName() const
{
    return m_settings.fileName();
}

QString ServerConfig::userScopeFileName() const
{
    return m_settings.userScopeFileName();
}

QString ServerConfig::systemScopeFileName() const
{
    return m_settings.systemScopeFileName();
}

QString ServerConfig::explicitFileName() const
{
    return m_settings.explicitFileName();
}

void ServerConfig::sync()
{
    m_settings.sync();
}

ServerConfig::LookupResult ServerConfig::lookupValue(const QString &key,
                                                     const QVariant &defaultValue) const
{
    return m_settings.lookup(key, defaultValue);
}

ServerConfig::LookupResult ServerConfig::lookupAnyValue(const QStringList &keys,
                                                        const QVariant &defaultValue) const
{
    return m_settings.lookupAny(keys, defaultValue);
}

QString ServerConfig::botToken(MessengerType messenger) const
{
    return lookupAnyValue(botTokenKeys(messenger)).value.toString();
}

void ServerConfig::setBotToken(MessengerType messenger, const QString &token)
{
    const QString key = botSettingsKey(messenger, QStringLiteral("token"));
    m_settings.setValue(key, token);

    if (messenger == MessengerType::Telegram) {
        // Keep legacy key in sync for backwards compatibility.
        m_settings.setValue(QStringLiteral("telegram/token"), token);
    }
}

QString ServerConfig::logFile() const
{
    return m_settings.value(QStringLiteral("log/file")).toString();
}

void ServerConfig::setLogFile(const QString &filePath)
{
    m_settings.setValue(QStringLiteral("log/file"), filePath);
}

QString ServerConfig::databaseFile() const
{
    return m_settings.value(QStringLiteral("database/file")).toString();
}

void ServerConfig::setDatabaseFile(const QString &filePath)
{
    m_settings.setValue(QStringLiteral("database/file"), filePath);
}

QString ServerConfig::webSharedMemoryKey() const
{
    return m_settings.value(QStringLiteral("web/shared_memory_key")).toString();
}

void ServerConfig::setWebSharedMemoryKey(const QString &key)
{
    m_settings.setValue(QStringLiteral("web/shared_memory_key"), key);
}

QString ServerConfig::webSharedMemorySecret() const
{
    return m_settings.value(QStringLiteral("web/shared_memory_secret")).toString();
}

void ServerConfig::setWebSharedMemorySecret(const QString &secret)
{
    m_settings.setValue(QStringLiteral("web/shared_memory_secret"), secret);
}

QStringList ServerConfig::adminUserIds(MessengerType messenger) const
{
    const QVariant raw = lookupAnyValue(adminIdKeys(messenger)).value;

    const QVariantList list = raw.toList();
    QStringList result;
    result.reserve(list.size());
    for (const QVariant &item : list) {
        const QString id = item.toString().trimmed();
        if (!id.isEmpty())
            result.append(id);
    }
    return result;
}

void ServerConfig::setAdminUserIds(MessengerType messenger, const QStringList &userIds)
{
    QVariantList list;
    list.reserve(userIds.size());
    for (const QString &id : userIds) {
        list.append(id);
    }

    const QString key = botSettingsKey(messenger, QStringLiteral("admin_ids"));
    m_settings.setValue(key, list);

    if (messenger == MessengerType::Telegram) {
        m_settings.setValue(QStringLiteral("telegram/admin_ids"), list);
    }
}
