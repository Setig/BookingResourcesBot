#include "BlacklistEntry.h"

BlacklistEntry::BlacklistEntry()
    : m_blockedAtUtc(QDateTime::currentDateTimeUtc())
    , m_blockedUntilUtc(QDateTime())
{
    // do nothing
}

BlacklistEntry::BlacklistEntry(const MessengerUserKey &user,
                               const QString &reason,
                               const MessengerUserKey &blockedBy,
                               const QDateTime &blockedAtUtc,
                               const QDateTime &blockedUntilUtc)
    : m_user(user)
    , m_reason(reason.trimmed())
    , m_blockedBy(blockedBy)
    , m_blockedAtUtc(normalizeUtcOrNow(blockedAtUtc))
    , m_blockedUntilUtc(normalizeUtcOrInvalid(blockedUntilUtc))
{
    // do nothing
}

BlacklistEntry::BlacklistEntry(const BlacklistEntry &other)
    : m_user(other.m_user)
    , m_reason(other.m_reason)
    , m_blockedBy(other.m_blockedBy)
    , m_blockedAtUtc(other.m_blockedAtUtc)
    , m_blockedUntilUtc(other.m_blockedUntilUtc)
{
    // do nothing
}

BlacklistEntry::BlacklistEntry(BlacklistEntry &&other) noexcept
    : m_user(std::move(other.m_user))
    , m_reason(std::move(other.m_reason))
    , m_blockedBy(std::move(other.m_blockedBy))
    , m_blockedAtUtc(std::move(other.m_blockedAtUtc))
    , m_blockedUntilUtc(std::move(other.m_blockedUntilUtc))
{
    // do nothing
}

BlacklistEntry &BlacklistEntry::operator=(const BlacklistEntry &other)
{
    if (this != &other) {
        m_user = other.m_user;
        m_reason = other.m_reason;
        m_blockedBy = other.m_blockedBy;
        m_blockedAtUtc = other.m_blockedAtUtc;
        m_blockedUntilUtc = other.m_blockedUntilUtc;
    }
    return *this;
}

BlacklistEntry &BlacklistEntry::operator=(BlacklistEntry &&other) noexcept
{
    if (this != &other) {
        m_user = std::move(other.m_user);
        m_reason = std::move(other.m_reason);
        m_blockedBy = std::move(other.m_blockedBy);
        m_blockedAtUtc = std::move(other.m_blockedAtUtc);
        m_blockedUntilUtc = std::move(other.m_blockedUntilUtc);
    }
    return *this;
}

BlacklistEntry::~BlacklistEntry()
{
    // do nothing
}

MessengerUserKey BlacklistEntry::user() const
{
    return m_user;
}

QString BlacklistEntry::reason() const
{
    return m_reason;
}

MessengerUserKey BlacklistEntry::blockedBy() const
{
    return m_blockedBy;
}

QDateTime BlacklistEntry::blockedAtUtc() const
{
    return m_blockedAtUtc;
}

QDateTime BlacklistEntry::blockedUntilUtc() const
{
    return m_blockedUntilUtc;
}

void BlacklistEntry::setUser(const MessengerUserKey &user)
{
    m_user = user;
}

void BlacklistEntry::setReason(const QString &reason)
{
    m_reason = reason.trimmed();
}

void BlacklistEntry::setBlockedBy(const MessengerUserKey &blockedBy)
{
    m_blockedBy = blockedBy;
}

void BlacklistEntry::setBlockedAtUtc(const QDateTime &blockedAtUtc)
{
    m_blockedAtUtc = normalizeUtcOrNow(blockedAtUtc);
}

void BlacklistEntry::setBlockedUntilUtc(const QDateTime &blockedUntilUtc)
{
    m_blockedUntilUtc = normalizeUtcOrInvalid(blockedUntilUtc);
}

bool BlacklistEntry::isValid() const
{
    return m_user.isValid();
}

bool BlacklistEntry::isPermanent() const
{
    return !m_blockedUntilUtc.isValid();
}

bool BlacklistEntry::isExpired(const QDateTime &nowUtc) const
{
    if (isPermanent() || !m_blockedUntilUtc.isValid())
        return false;

    return m_blockedUntilUtc <= nowUtc;
}

QDateTime BlacklistEntry::normalizeUtcOrNow(const QDateTime &dt)
{
    if (!dt.isValid())
        return QDateTime::currentDateTimeUtc();
    return dt.toUTC();
}

QDateTime BlacklistEntry::normalizeUtcOrInvalid(const QDateTime &dt)
{
    if (!dt.isValid())
        return QDateTime();
    return dt.toUTC();
}
