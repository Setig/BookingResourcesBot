#ifndef RESOURCEQUEUETYPES_H
#define RESOURCEQUEUETYPES_H

#include <QString>
#include <QDateTime>
#include <QHashFunctions>

#include "MessengerUserKey.h"

/**
 * @brief Composite key identifying a unique schedule entry.
 *
 * The value is always stored in normalized form:
 * - resourceUuid is normalized (canonical string)
 * - startAtUtc is stored in UTC with seconds precision (invalid means "not set")
 */
class ResourceQueueKey final
{
public:
    ResourceQueueKey();
    explicit ResourceQueueKey(const QString &resourceUuid,
                              const MessengerUserKey &user = MessengerUserKey(),
                              const QDateTime &startAtUtc = QDateTime());

    ResourceQueueKey(const ResourceQueueKey &other);
    ResourceQueueKey(ResourceQueueKey &&other) noexcept;
    ResourceQueueKey &operator=(const ResourceQueueKey &other);
    ResourceQueueKey &operator=(ResourceQueueKey &&other) noexcept;
    ~ResourceQueueKey();

    [[nodiscard]] QString resourceUuid() const;
    [[nodiscard]] MessengerUserKey user() const;
    [[nodiscard]] QDateTime startAtUtc() const;

    void setResourceUuid(const QString &resourceUuid);
    void setUser(const MessengerUserKey &user);
    void setStartAtUtc(const QDateTime &startAtUtc);

    [[nodiscard]] bool isValid() const;

    bool operator==(const ResourceQueueKey &other) const;
    bool operator!=(const ResourceQueueKey &other) const;

private:
    [[nodiscard]] static QString normalizeResourceUuid(const QString &uuid);
    [[nodiscard]] static QDateTime normalizeUtcOrInvalidSeconds(const QDateTime &dt);

    QString m_resourceUuid;
    MessengerUserKey m_user;
    QDateTime m_startAtUtc;
};

uint qHash(const ResourceQueueKey &key, uint seed = 0) noexcept;

/**
 * @brief Entry in a resource schedule.
 *
 * The schedule entry occupies the resource in the interval [startAtUtc, endAtUtc),
 * where endAtUtc can be invalid to denote an "indefinite" (open-ended) occupation
 * starting at startAtUtc.
 *
 * The value is always stored in normalized form:
 * - resourceUuid is normalized (canonical string)
 * - enqueuedAtUtc is guaranteed to be valid and stored in UTC with seconds precision
 * - startAtUtc is stored in UTC with seconds precision (invalid means "not set")
 * - endAtUtc is stored in UTC with seconds precision (invalid means "indefinite")
 */
class ResourceQueueEntry final
{
public:
    enum class StartMode {
        AtTime = 0,
        AfterRelease
    };

    ResourceQueueEntry();
    explicit ResourceQueueEntry(const QString &resourceUuid,
                                const MessengerUserKey &user = MessengerUserKey(),
                                const QDateTime &enqueuedAtUtc = QDateTime(),
                                const QDateTime &startAtUtc = QDateTime(),
                                const QDateTime &endAtUtc = QDateTime(),
                                StartMode startMode = StartMode::AtTime,
                                int requestedDurationMinutes = -1);

    ResourceQueueEntry(const ResourceQueueEntry &other);
    ResourceQueueEntry(ResourceQueueEntry &&other) noexcept;
    ResourceQueueEntry &operator=(const ResourceQueueEntry &other);
    ResourceQueueEntry &operator=(ResourceQueueEntry &&other) noexcept;
    ~ResourceQueueEntry();

    [[nodiscard]] QString resourceUuid() const;
    [[nodiscard]] MessengerUserKey user() const;
    [[nodiscard]] QDateTime enqueuedAtUtc() const;
    [[nodiscard]] QDateTime startAtUtc() const;
    [[nodiscard]] QDateTime endAtUtc() const;
    [[nodiscard]] StartMode startMode() const;
    [[nodiscard]] int requestedDurationMinutes() const;

    void setResourceUuid(const QString &resourceUuid);
    void setUser(const MessengerUserKey &user);
    void setEnqueuedAtUtc(const QDateTime &enqueuedAtUtc);
    void setStartAtUtc(const QDateTime &startAtUtc);
    void setEndAtUtc(const QDateTime &endAtUtc);
    void setStartMode(StartMode startMode);
    void setRequestedDurationMinutes(int minutes);

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] bool waitsForRelease() const;
    [[nodiscard]] bool isIndefinite() const;
    [[nodiscard]] bool isFinite() const;
    [[nodiscard]] bool isActiveAtUtc(const QDateTime &nowUtc) const;
    [[nodiscard]] ResourceQueueKey key() const;

    [[nodiscard]] static bool windowsOverlapUtc(const QDateTime &aStartUtc,
                                                const QDateTime &aEndUtc,
                                                const QDateTime &bStartUtc,
                                                const QDateTime &bEndUtc);
    [[nodiscard]] static QString startModeToStorageToken(StartMode mode);
    [[nodiscard]] static StartMode startModeFromStorageToken(const QString &token);

private:
    [[nodiscard]] static QString normalizeResourceUuid(const QString &uuid);
    [[nodiscard]] static QDateTime normalizeUtcOrNowSeconds(const QDateTime &dt);
    [[nodiscard]] static QDateTime normalizeUtcOrInvalidSeconds(const QDateTime &dt);

    QString m_resourceUuid;
    MessengerUserKey m_user;
    QDateTime m_enqueuedAtUtc;
    QDateTime m_startAtUtc;
    QDateTime m_endAtUtc;
    StartMode m_startMode = StartMode::AtTime;
    int m_requestedDurationMinutes = -1;
};

#endif // RESOURCEQUEUETYPES_H
