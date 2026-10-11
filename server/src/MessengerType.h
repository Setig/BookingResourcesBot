#ifndef MESSENGERTYPE_H
#define MESSENGERTYPE_H

#include <type_traits>

#include <QList>
#include <QString>
#include <QMetaType>
#include <QHashFunctions>

/**
 * @brief Unified messenger type used across the server.
 */
enum class MessengerType {
    LocalCli = 0,
    Telegram,
    KonturTalk,
    Web,
};

Q_DECLARE_METATYPE(MessengerType)

inline QString messengerTypeToString(MessengerType messenger)
{
    switch (messenger) {
    case MessengerType::LocalCli:
        return QStringLiteral("local-cli");
    case MessengerType::Telegram:
        return QStringLiteral("telegram");
    case MessengerType::KonturTalk:
        return QStringLiteral("kontur-talk");
    case MessengerType::Web:
        return QStringLiteral("web");
    }

    return QStringLiteral("telegram");
}

inline QString messengerTypeDisplayName(MessengerType messenger)
{
    switch (messenger) {
    case MessengerType::LocalCli:
        return QStringLiteral("Local CLI");
    case MessengerType::Telegram:
        return QStringLiteral("Telegram");
    case MessengerType::KonturTalk:
        return QStringLiteral("Kontur.Talk");
    case MessengerType::Web:
        return QStringLiteral("Web");
    }

    return QStringLiteral("Telegram");
}

inline MessengerType messengerTypeFromString(const QString &value)
{
    const QString v = value.trimmed().toLower();

    if (v == QStringLiteral("local-cli"))
        return MessengerType::LocalCli;
    if (v == QStringLiteral("telegram"))
        return MessengerType::Telegram;
    if (v == QStringLiteral("kontur-talk"))
        return MessengerType::KonturTalk;
    if (v == QStringLiteral("web"))
        return MessengerType::Web;

    return MessengerType::Telegram;
}

inline QList<MessengerType> allMessengerTypes()
{
    return {
        MessengerType::LocalCli,
        MessengerType::Telegram,
        MessengerType::KonturTalk,
        MessengerType::Web,
    };
}

inline uint qHash(MessengerType messenger, uint seed = 0) noexcept
{
    using UnderlyingType = typename std::underlying_type<MessengerType>::type;
    return ::qHash(static_cast<UnderlyingType>(messenger), seed);
}

#endif // MESSENGERTYPE_H
