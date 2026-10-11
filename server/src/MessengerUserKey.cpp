#include "MessengerUserKey.h"

MessengerUserKey::MessengerUserKey()
    : m_messenger(MessengerType::Telegram)
{
    // do nothing
}

MessengerUserKey::MessengerUserKey(MessengerType messenger, const QString &userId)
    : m_messenger(messenger)
    , m_userId(userId.trimmed())
{
    // do nothing
}

MessengerUserKey::~MessengerUserKey()
{
    // do nothing
}

MessengerUserKey::MessengerUserKey(const MessengerUserKey &other)
    : m_messenger(other.m_messenger)
    , m_userId(other.m_userId)
{
}

MessengerUserKey::MessengerUserKey(MessengerUserKey &&other) noexcept
    : m_messenger(other.m_messenger)
    , m_userId(std::move(other.m_userId))
{
    // do nothing
}

MessengerUserKey &MessengerUserKey::operator=(const MessengerUserKey &other)
{
    if (this == &other)
        return *this;

    m_messenger = other.m_messenger;
    m_userId = other.m_userId;
    return *this;
}

MessengerUserKey &MessengerUserKey::operator=(MessengerUserKey &&other) noexcept
{
    if (this == &other)
        return *this;

    m_messenger = other.m_messenger;
    m_userId = std::move(other.m_userId);
    return *this;
}

MessengerType MessengerUserKey::messenger() const
{
    return m_messenger;
}

void MessengerUserKey::setMessenger(MessengerType messenger)
{
    m_messenger = messenger;
}

const QString &MessengerUserKey::userId() const
{
    return m_userId;
}

void MessengerUserKey::setUserId(const QString &userId)
{
    m_userId = userId.trimmed();
}

bool MessengerUserKey::isValid() const
{
    return !m_userId.isEmpty();
}

QString MessengerUserKey::toString() const
{
    if (!isValid())
        return QString();

    return messengerTypeToString(m_messenger) + QLatin1Char(':') + m_userId;
}

MessengerUserKey MessengerUserKey::fromString(const QString &text, bool *isOk)
{
    if (isOk)
        *isOk = false;

    MessengerUserKey key;

    QString token = text.trimmed();
    if (token.isEmpty())
        return key;

    for (int i = 0; i < token.size(); ++i) {
        const QChar ch = token.at(i);
        if (ch.isSpace() || ch == QLatin1Char('(') || ch == QLatin1Char(')'))
            return key;
    }

    const int colonIndex = token.indexOf(QLatin1Char(':'));
    if (colonIndex < 0)
        return key;

    if (colonIndex <= 0 || colonIndex >= token.size() - 1)
        return key;

    const QString messengerToken = token.left(colonIndex).trimmed().toLower();
    const QString userIdToken = token.mid(colonIndex + 1).trimmed();
    if (messengerToken.isEmpty() || userIdToken.isEmpty())
        return key;

    const MessengerType messenger = messengerTypeFromString(messengerToken);
    if (messengerTypeToString(messenger) != messengerToken)
        return key;

    key.setMessenger(messenger);
    key.setUserId(userIdToken);

    if (isOk)
        *isOk = key.isValid();

    return key;
}

bool MessengerUserKey::operator==(const MessengerUserKey &other) const
{
    return m_messenger == other.m_messenger && m_userId == other.m_userId;
}

bool MessengerUserKey::operator!=(const MessengerUserKey &other) const
{
    return !(*this == other);
}

uint qHash(const MessengerUserKey &key, uint seed) noexcept
{
    seed ^= static_cast<uint>(key.messenger()) + 0x9e3779b9U + (seed << 6) + (seed >> 2);
    return qHash(key.userId(), seed);
}
