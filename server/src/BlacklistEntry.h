#ifndef BLACKLISTENTRY_H
#define BLACKLISTENTRY_H

#include <QString>
#include <QDateTime>

#include "MessengerUserKey.h"

/**
 * @brief Describes an entry in the messenger blacklist.
 *
 * The value is always stored in normalized form:
 * - reason is trimmed
 * - blockedAtUtc is guaranteed to be valid and in UTC (defaults to currentDateTimeUtc)
 * - blockedUntilUtc (when present) is stored in UTC
 */
class BlacklistEntry final
{
public:
    BlacklistEntry();
    explicit BlacklistEntry(const MessengerUserKey &user,
                            const QString &reason = QString(),
                            const MessengerUserKey &blockedBy = MessengerUserKey(),
                            const QDateTime &blockedAtUtc = QDateTime(),
                            const QDateTime &blockedUntilUtc = QDateTime());

    BlacklistEntry(const BlacklistEntry &other);
    BlacklistEntry(BlacklistEntry &&other) noexcept;
    BlacklistEntry &operator=(const BlacklistEntry &other);
    BlacklistEntry &operator=(BlacklistEntry &&other) noexcept;

    ~BlacklistEntry();

    [[nodiscard]] MessengerUserKey user() const;
    [[nodiscard]] QString reason() const;
    [[nodiscard]] MessengerUserKey blockedBy() const;
    [[nodiscard]] QDateTime blockedAtUtc() const;
    [[nodiscard]] QDateTime blockedUntilUtc() const;

    void setUser(const MessengerUserKey &user);
    void setReason(const QString &reason);
    void setBlockedBy(const MessengerUserKey &blockedBy);
    void setBlockedAtUtc(const QDateTime &blockedAtUtc);
    void setBlockedUntilUtc(const QDateTime &blockedUntilUtc);

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] bool isPermanent() const;
    [[nodiscard]] bool isExpired(const QDateTime &nowUtc = QDateTime::currentDateTimeUtc()) const;

private:
    [[nodiscard]] static QDateTime normalizeUtcOrNow(const QDateTime &dt);
    [[nodiscard]] static QDateTime normalizeUtcOrInvalid(const QDateTime &dt);

    MessengerUserKey m_user;
    QString m_reason;
    MessengerUserKey m_blockedBy;
    QDateTime m_blockedAtUtc;
    QDateTime m_blockedUntilUtc;
};

#endif // BLACKLISTENTRY_H
