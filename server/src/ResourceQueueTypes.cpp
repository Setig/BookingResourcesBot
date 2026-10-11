#include "ResourceQueueTypes.h"

#include <QUuid>
#include <QTimeZone>

ResourceQueueKey::ResourceQueueKey()
{
    // do nothing
}

ResourceQueueKey::ResourceQueueKey(const QString &resourceUuid,
                                   const MessengerUserKey &user,
                                   const QDateTime &startAtUtc)
    : m_resourceUuid(normalizeResourceUuid(resourceUuid))
    , m_user(user)
    , m_startAtUtc(normalizeUtcOrInvalidSeconds(startAtUtc))
{
    // do nothing
}

ResourceQueueKey::ResourceQueueKey(const ResourceQueueKey &other)
    : m_resourceUuid(other.m_resourceUuid)
    , m_user(other.m_user)
    , m_startAtUtc(other.m_startAtUtc)
{
    // do nothing
}

ResourceQueueKey::ResourceQueueKey(ResourceQueueKey &&other) noexcept
    : m_resourceUuid(std::move(other.m_resourceUuid))
    , m_user(std::move(other.m_user))
    , m_startAtUtc(std::move(other.m_startAtUtc))
{
    // do nothing
}

ResourceQueueKey &ResourceQueueKey::operator=(const ResourceQueueKey &other)
{
    if (this != &other) {
        m_resourceUuid = other.m_resourceUuid;
        m_user = other.m_user;
        m_startAtUtc = other.m_startAtUtc;
    }
    return *this;
}

ResourceQueueKey &ResourceQueueKey::operator=(ResourceQueueKey &&other) noexcept
{
    if (this != &other) {
        m_resourceUuid = std::move(other.m_resourceUuid);
        m_user = std::move(other.m_user);
        m_startAtUtc = std::move(other.m_startAtUtc);
    }
    return *this;
}

ResourceQueueKey::~ResourceQueueKey()
{
    // do nothing
}

QString ResourceQueueKey::resourceUuid() const
{
    return m_resourceUuid;
}

MessengerUserKey ResourceQueueKey::user() const
{
    return m_user;
}

QDateTime ResourceQueueKey::startAtUtc() const
{
    return m_startAtUtc;
}

void ResourceQueueKey::setResourceUuid(const QString &resourceUuid)
{
    m_resourceUuid = normalizeResourceUuid(resourceUuid);
}

void ResourceQueueKey::setUser(const MessengerUserKey &user)
{
    m_user = user;
}

void ResourceQueueKey::setStartAtUtc(const QDateTime &startAtUtc)
{
    m_startAtUtc = normalizeUtcOrInvalidSeconds(startAtUtc);
}

bool ResourceQueueKey::isValid() const
{
    return !m_resourceUuid.isEmpty() && m_user.isValid() && m_startAtUtc.isValid();
}

bool ResourceQueueKey::operator==(const ResourceQueueKey &other) const
{
    return m_resourceUuid == other.m_resourceUuid && m_user == other.m_user
           && m_startAtUtc == other.m_startAtUtc;
}

bool ResourceQueueKey::operator!=(const ResourceQueueKey &other) const
{
    return !(*this == other);
}

QString ResourceQueueKey::normalizeResourceUuid(const QString &uuid)
{
    const QString trimmed = uuid.trimmed();
    if (trimmed.isEmpty())
        return QString();
    const QString withBraces = (trimmed.startsWith('{') && trimmed.endsWith('}'))
                                       ? trimmed
                                       : QStringLiteral("{%1}").arg(trimmed);
    const QUuid parsed(withBraces);
    if (parsed.isNull())
        return QString();
    return parsed.toString(QUuid::WithoutBraces).toLower();
}

QDateTime ResourceQueueKey::normalizeUtcOrInvalidSeconds(const QDateTime &dt)
{
    if (!dt.isValid())
        return QDateTime();

    return QDateTime::fromSecsSinceEpoch(dt.toSecsSinceEpoch(), QTimeZone::utc());
}

uint qHash(const ResourceQueueKey &key, uint seed) noexcept
{
    seed ^= qHash(key.resourceUuid(), seed ^ 0x9e3779b9U);
    seed ^= qHash(key.user(), seed ^ 0x9e3779b9U);
    const qint64 startEpoch = key.startAtUtc().isValid() ? key.startAtUtc().toSecsSinceEpoch() : 0;
    seed ^= qHash(startEpoch, seed ^ 0x9e3779b9U);
    return seed;
}

ResourceQueueEntry::ResourceQueueEntry()
    : m_enqueuedAtUtc(normalizeUtcOrNowSeconds(QDateTime()))
{
    // do nothing
}

ResourceQueueEntry::ResourceQueueEntry(const QString &resourceUuid,
                                       const MessengerUserKey &user,
                                       const QDateTime &enqueuedAtUtc,
                                       const QDateTime &startAtUtc,
                                       const QDateTime &endAtUtc,
                                       StartMode startMode,
                                       int requestedDurationMinutes)
    : m_resourceUuid(normalizeResourceUuid(resourceUuid))
    , m_user(user)
    , m_enqueuedAtUtc(normalizeUtcOrNowSeconds(enqueuedAtUtc))
    , m_startAtUtc(normalizeUtcOrInvalidSeconds(startAtUtc))
    , m_endAtUtc(normalizeUtcOrInvalidSeconds(endAtUtc))
    , m_startMode(startMode)
    , m_requestedDurationMinutes(requestedDurationMinutes < 0 ? -1 : requestedDurationMinutes)
{
    // do nothing
}

ResourceQueueEntry::ResourceQueueEntry(const ResourceQueueEntry &other)
    : m_resourceUuid(other.m_resourceUuid)
    , m_user(other.m_user)
    , m_enqueuedAtUtc(other.m_enqueuedAtUtc)
    , m_startAtUtc(other.m_startAtUtc)
    , m_endAtUtc(other.m_endAtUtc)
    , m_startMode(other.m_startMode)
    , m_requestedDurationMinutes(other.m_requestedDurationMinutes)
{
    // do nothing
}

ResourceQueueEntry::ResourceQueueEntry(ResourceQueueEntry &&other) noexcept
    : m_resourceUuid(std::move(other.m_resourceUuid))
    , m_user(std::move(other.m_user))
    , m_enqueuedAtUtc(std::move(other.m_enqueuedAtUtc))
    , m_startAtUtc(std::move(other.m_startAtUtc))
    , m_endAtUtc(std::move(other.m_endAtUtc))
    , m_startMode(other.m_startMode)
    , m_requestedDurationMinutes(other.m_requestedDurationMinutes)
{
    // do nothing
}

ResourceQueueEntry &ResourceQueueEntry::operator=(const ResourceQueueEntry &other)
{
    if (this != &other) {
        m_resourceUuid = other.m_resourceUuid;
        m_user = other.m_user;
        m_enqueuedAtUtc = other.m_enqueuedAtUtc;
        m_startAtUtc = other.m_startAtUtc;
        m_endAtUtc = other.m_endAtUtc;
        m_startMode = other.m_startMode;
        m_requestedDurationMinutes = other.m_requestedDurationMinutes;
    }
    return *this;
}

ResourceQueueEntry &ResourceQueueEntry::operator=(ResourceQueueEntry &&other) noexcept
{
    if (this != &other) {
        m_resourceUuid = std::move(other.m_resourceUuid);
        m_user = std::move(other.m_user);
        m_enqueuedAtUtc = std::move(other.m_enqueuedAtUtc);
        m_startAtUtc = std::move(other.m_startAtUtc);
        m_endAtUtc = std::move(other.m_endAtUtc);
        m_startMode = other.m_startMode;
        m_requestedDurationMinutes = other.m_requestedDurationMinutes;
    }
    return *this;
}

ResourceQueueEntry::~ResourceQueueEntry()
{
    // do nothing
}

QString ResourceQueueEntry::resourceUuid() const
{
    return m_resourceUuid;
}

MessengerUserKey ResourceQueueEntry::user() const
{
    return m_user;
}

QDateTime ResourceQueueEntry::enqueuedAtUtc() const
{
    return m_enqueuedAtUtc;
}

QDateTime ResourceQueueEntry::startAtUtc() const
{
    return m_startAtUtc;
}

QDateTime ResourceQueueEntry::endAtUtc() const
{
    return m_endAtUtc;
}

ResourceQueueEntry::StartMode ResourceQueueEntry::startMode() const
{
    return m_startMode;
}

int ResourceQueueEntry::requestedDurationMinutes() const
{
    return m_requestedDurationMinutes;
}

void ResourceQueueEntry::setResourceUuid(const QString &resourceUuid)
{
    m_resourceUuid = normalizeResourceUuid(resourceUuid);
}

void ResourceQueueEntry::setUser(const MessengerUserKey &user)
{
    m_user = user;
}

void ResourceQueueEntry::setEnqueuedAtUtc(const QDateTime &enqueuedAtUtc)
{
    m_enqueuedAtUtc = normalizeUtcOrNowSeconds(enqueuedAtUtc);
}

void ResourceQueueEntry::setStartAtUtc(const QDateTime &startAtUtc)
{
    m_startAtUtc = normalizeUtcOrInvalidSeconds(startAtUtc);
}

void ResourceQueueEntry::setEndAtUtc(const QDateTime &endAtUtc)
{
    m_endAtUtc = normalizeUtcOrInvalidSeconds(endAtUtc);
}

void ResourceQueueEntry::setStartMode(StartMode startMode)
{
    m_startMode = startMode;
}

void ResourceQueueEntry::setRequestedDurationMinutes(int minutes)
{
    m_requestedDurationMinutes = minutes < 0 ? -1 : minutes;
}

bool ResourceQueueEntry::isValid() const
{
    return !m_resourceUuid.isEmpty() && m_user.isValid() && m_startAtUtc.isValid();
}

bool ResourceQueueEntry::waitsForRelease() const
{
    return m_startMode == StartMode::AfterRelease;
}

bool ResourceQueueEntry::isIndefinite() const
{
    if (waitsForRelease())
        return m_requestedDurationMinutes == 0;
    return m_startAtUtc.isValid() && !m_endAtUtc.isValid();
}

bool ResourceQueueEntry::isFinite() const
{
    if (waitsForRelease())
        return m_requestedDurationMinutes > 0;
    return m_startAtUtc.isValid() && m_endAtUtc.isValid() && m_startAtUtc < m_endAtUtc;
}

bool ResourceQueueEntry::isActiveAtUtc(const QDateTime &nowUtc) const
{
    if (waitsForRelease())
        return false;

    if (!nowUtc.isValid() || !m_startAtUtc.isValid())
        return false;

    if (!m_endAtUtc.isValid())
        return m_startAtUtc <= nowUtc;

    if (m_startAtUtc >= m_endAtUtc)
        return false;

    return m_startAtUtc <= nowUtc && nowUtc < m_endAtUtc;
}

ResourceQueueKey ResourceQueueEntry::key() const
{
    return ResourceQueueKey(m_resourceUuid, m_user, m_startAtUtc);
}

bool ResourceQueueEntry::windowsOverlapUtc(const QDateTime &aStartUtc,
                                           const QDateTime &aEndUtc,
                                           const QDateTime &bStartUtc,
                                           const QDateTime &bEndUtc)
{
    if (!aStartUtc.isValid() || !bStartUtc.isValid())
        return false;

    const bool aIndefinite = !aEndUtc.isValid();
    const bool bIndefinite = !bEndUtc.isValid();

    if (!aIndefinite && aStartUtc >= aEndUtc)
        return false;
    if (!bIndefinite && bStartUtc >= bEndUtc)
        return false;

    const bool aStartLessBEnd = bIndefinite ? true : (aStartUtc < bEndUtc);
    const bool bStartLessAEnd = aIndefinite ? true : (bStartUtc < aEndUtc);
    return aStartLessBEnd && bStartLessAEnd;
}

QString ResourceQueueEntry::startModeToStorageToken(StartMode mode)
{
    switch (mode) {
    case StartMode::AtTime:
        return QStringLiteral("at_time");
    case StartMode::AfterRelease:
        return QStringLiteral("after_release");
    }
    return QStringLiteral("at_time");
}

ResourceQueueEntry::StartMode ResourceQueueEntry::startModeFromStorageToken(const QString &token)
{
    const QString normalized = token.trimmed().toLower();
    if (normalized == QStringLiteral("after_release")
        || normalized == QStringLiteral("after-release")) {
        return StartMode::AfterRelease;
    }
    return StartMode::AtTime;
}

QString ResourceQueueEntry::normalizeResourceUuid(const QString &uuid)
{
    const QString trimmed = uuid.trimmed();
    if (trimmed.isEmpty())
        return QString();
    const QString withBraces = (trimmed.startsWith('{') && trimmed.endsWith('}'))
                                       ? trimmed
                                       : QStringLiteral("{%1}").arg(trimmed);
    const QUuid parsed(withBraces);
    if (parsed.isNull())
        return QString();
    return parsed.toString(QUuid::WithoutBraces).toLower();
}

QDateTime ResourceQueueEntry::normalizeUtcOrNowSeconds(const QDateTime &dt)
{
    if (!dt.isValid()) {
        return QDateTime::fromSecsSinceEpoch(QDateTime::currentSecsSinceEpoch(), QTimeZone::utc());
    }
    return QDateTime::fromSecsSinceEpoch(dt.toSecsSinceEpoch(), QTimeZone::utc());
}

QDateTime ResourceQueueEntry::normalizeUtcOrInvalidSeconds(const QDateTime &dt)
{
    if (!dt.isValid())
        return QDateTime();
    return QDateTime::fromSecsSinceEpoch(dt.toSecsSinceEpoch(), QTimeZone::utc());
}
