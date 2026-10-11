#ifndef PENDINGACCESSUSERENTRY_H
#define PENDINGACCESSUSERENTRY_H

#include <QString>
#include <QDateTime>

#include "MessengerUserKey.h"

/**
 * @brief Represents a recently seen messenger user awaiting access activation.
 *
 * The value is always stored in normalized form:
 * - first/last names are trimmed
 * - lastMessageAtUtc is guaranteed to be valid and in UTC (defaults to currentDateTimeUtc)
 */
class PendingAccessUserEntry final
{
public:
    PendingAccessUserEntry();
    explicit PendingAccessUserEntry(const MessengerUserKey &user,
                                    const QString &firstName = QString(),
                                    const QString &lastName = QString(),
                                    const QDateTime &lastMessageAtUtc = QDateTime());

    PendingAccessUserEntry(const PendingAccessUserEntry &other);
    PendingAccessUserEntry(PendingAccessUserEntry &&other) noexcept;
    PendingAccessUserEntry &operator=(const PendingAccessUserEntry &other);
    PendingAccessUserEntry &operator=(PendingAccessUserEntry &&other) noexcept;

    ~PendingAccessUserEntry();

    [[nodiscard]] MessengerUserKey user() const;
    [[nodiscard]] QString firstName() const;
    [[nodiscard]] QString lastName() const;
    [[nodiscard]] QDateTime lastMessageAtUtc() const;

    void setUser(const MessengerUserKey &user);
    void setFirstName(const QString &firstName);
    void setLastName(const QString &lastName);
    void setLastMessageAtUtc(const QDateTime &lastMessageAtUtc);

    [[nodiscard]] bool isValid() const;

private:
    [[nodiscard]] static QDateTime normalizeUtcOrNow(const QDateTime &dt);

    MessengerUserKey m_user;
    QString m_firstName;
    QString m_lastName;
    QDateTime m_lastMessageAtUtc;
};

#endif // PENDINGACCESSUSERENTRY_H
