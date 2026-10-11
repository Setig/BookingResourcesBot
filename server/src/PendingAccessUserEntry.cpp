#include "PendingAccessUserEntry.h"

PendingAccessUserEntry::PendingAccessUserEntry()
    : m_lastMessageAtUtc(QDateTime::currentDateTimeUtc())
{
    // do nothing
}

PendingAccessUserEntry::PendingAccessUserEntry(const MessengerUserKey &user,
                                               const QString &firstName,
                                               const QString &lastName,
                                               const QDateTime &lastMessageAtUtc)
    : m_user(user)
    , m_firstName(firstName.trimmed())
    , m_lastName(lastName.trimmed())
    , m_lastMessageAtUtc(normalizeUtcOrNow(lastMessageAtUtc))
{
    // do nothing
}

PendingAccessUserEntry::PendingAccessUserEntry(const PendingAccessUserEntry &other)
    : m_user(other.m_user)
    , m_firstName(other.m_firstName)
    , m_lastName(other.m_lastName)
    , m_lastMessageAtUtc(other.m_lastMessageAtUtc)
{
    // do nothing
}

PendingAccessUserEntry::PendingAccessUserEntry(PendingAccessUserEntry &&other) noexcept
    : m_user(std::move(other.m_user))
    , m_firstName(std::move(other.m_firstName))
    , m_lastName(std::move(other.m_lastName))
    , m_lastMessageAtUtc(std::move(other.m_lastMessageAtUtc))
{
    // do nothing
}

PendingAccessUserEntry &PendingAccessUserEntry::operator=(const PendingAccessUserEntry &other)
{
    if (this != &other) {
        m_user = other.m_user;
        m_firstName = other.m_firstName;
        m_lastName = other.m_lastName;
        m_lastMessageAtUtc = other.m_lastMessageAtUtc;
    }
    return *this;
}

PendingAccessUserEntry &PendingAccessUserEntry::operator=(PendingAccessUserEntry &&other) noexcept
{
    if (this != &other) {
        m_user = std::move(other.m_user);
        m_firstName = std::move(other.m_firstName);
        m_lastName = std::move(other.m_lastName);
        m_lastMessageAtUtc = std::move(other.m_lastMessageAtUtc);
    }
    return *this;
}

PendingAccessUserEntry::~PendingAccessUserEntry()
{
    // do nothing
}

MessengerUserKey PendingAccessUserEntry::user() const
{
    return m_user;
}

QString PendingAccessUserEntry::firstName() const
{
    return m_firstName;
}

QString PendingAccessUserEntry::lastName() const
{
    return m_lastName;
}

QDateTime PendingAccessUserEntry::lastMessageAtUtc() const
{
    return m_lastMessageAtUtc;
}

void PendingAccessUserEntry::setUser(const MessengerUserKey &user)
{
    m_user = user;
}

void PendingAccessUserEntry::setFirstName(const QString &firstName)
{
    m_firstName = firstName.trimmed();
}

void PendingAccessUserEntry::setLastName(const QString &lastName)
{
    m_lastName = lastName.trimmed();
}

void PendingAccessUserEntry::setLastMessageAtUtc(const QDateTime &lastMessageAtUtc)
{
    m_lastMessageAtUtc = normalizeUtcOrNow(lastMessageAtUtc);
}

bool PendingAccessUserEntry::isValid() const
{
    return m_user.isValid();
}

QDateTime PendingAccessUserEntry::normalizeUtcOrNow(const QDateTime &dt)
{
    if (!dt.isValid())
        return QDateTime::currentDateTimeUtc();
    return dt.toUTC();
}
