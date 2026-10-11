#ifndef USER_H
#define USER_H

#include <QSharedDataPointer>

#include <QMap>
#include <QSet>
#include <QString>

#include "Group.h"
#include "UserTimeZone.h"
#include "MessengerUserKey.h"

/**
 * @brief Lightweight user value type using Qt implicit sharing.
 *
 * Follows the rule of five and keeps messenger user key (messenger + userId),
 * first/last names, group roles, and user-specific preferences.
 */
class User final
{
public:
    enum class Role {
        UserRole = 0,
        AdminRole,
        FixedAdminRole,
    };

    enum class OccupationNotifyMode {
        AllResources = 0,
        OwnResourcesOnly,
        Disabled,
    };

    User();
    explicit User(MessengerType messenger,
                  const QString &userId,
                  const QString &firstName,
                  const QString &lastName,
                  bool isAdmin = false,
                  const QString &locale = QString());
    explicit User(MessengerType messenger,
                  const QString &userId,
                  const QString &firstName,
                  const QString &lastName,
                  Role role,
                  const QString &locale = QString());
    explicit User(const MessengerUserKey &key,
                  const QString &firstName,
                  const QString &lastName,
                  bool isAdmin = false,
                  const QString &locale = QString());
    explicit User(const MessengerUserKey &key,
                  const QString &firstName,
                  const QString &lastName,
                  Role role,
                  const QString &locale = QString());

    User(const User &other);
    User(User &&other) noexcept;
    User &operator=(const User &other);
    User &operator=(User &&other) noexcept;

    ~User();

    MessengerUserKey key() const;
    MessengerType messenger() const;
    QString userId() const;
    QString firstName() const;
    QString lastName() const;
    QString locale() const;
    UserTimeZone timezone() const;
    Role role() const;
    QMap<Group, Role> groupRoles() const;
    QSet<Group> groups() const;
    QSet<Group> administeredGroupRoots() const;
    Role roleForGroup(const Group &group) const;
    bool isAdmin() const;
    bool isFixedAdmin() const;
    bool hasAccessToGroup(const Group &group) const;
    bool canAdministerGroup(const Group &group) const;
    bool canAdministerGroups(const QSet<Group> &groups) const;
    OccupationNotifyMode occupationNotifyMode() const;

    void setKey(const MessengerUserKey &key);
    void setMessenger(MessengerType messenger);
    void setUserId(const QString &id);
    void setFirstName(const QString &firstName);
    void setLastName(const QString &lastName);
    void setLocale(const QString &locale);
    void setTimezone(const UserTimeZone &timezone);
    void setIsAdmin(bool admin);
    void setRole(Role role);
    void setGroupRoles(const QMap<Group, Role> &roles);
    void setGroups(const QSet<Group> &groups, Role role = Role::UserRole);
    void setOccupationNotifyMode(OccupationNotifyMode mode);

    bool isValid() const;

private:
    class UserData;
    QSharedDataPointer<UserData> d;
};

#endif // USER_H
