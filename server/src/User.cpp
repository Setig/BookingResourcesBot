#include "User.h"

namespace {

QMap<Group, User::Role> groupRolesForRole(User::Role role)
{
    QMap<Group, User::Role> roles;
    roles.insert(Group::root(), role);
    return roles;
}

QMap<Group, User::Role> normalizedGroupRoles(const QMap<Group, User::Role> &roles)
{
    QMap<Group, User::Role> normalized;
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
        normalized.insert(Group(it.key()), it.value());
    }
    if (normalized.isEmpty())
        normalized.insert(Group::root(), User::Role::UserRole);
    return normalized;
}

User::Role aggregateRole(const QMap<Group, User::Role> &roles)
{
    User::Role resolved = User::Role::UserRole;
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
        if (it.value() == User::Role::FixedAdminRole)
            return User::Role::FixedAdminRole;
        if (it.value() == User::Role::AdminRole)
            resolved = User::Role::AdminRole;
    }
    return resolved;
}

} // namespace

class User::UserData : public QSharedData
{
public:
    UserData(const MessengerUserKey &key,
             const QString &firstName,
             const QString &lastName,
             const QMap<Group, Role> &groupRoles,
             const QString &locale,
             const UserTimeZone &timezone,
             OccupationNotifyMode notifyMode);

    MessengerUserKey key;
    QString firstName;
    QString lastName;
    QString locale;
    UserTimeZone timezone;
    QMap<Group, Role> groupRoles;
    OccupationNotifyMode occupationNotifyMode;
};

User::UserData::UserData(const MessengerUserKey &key,
                         const QString &firstName,
                         const QString &lastName,
                         const QMap<Group, Role> &groupRoles,
                         const QString &locale,
                         const UserTimeZone &timezone,
                         OccupationNotifyMode notifyMode)
    : key(key)
    , firstName(firstName.trimmed())
    , lastName(lastName.trimmed())
    , locale(locale.trimmed())
    , timezone(timezone)
    , groupRoles(normalizedGroupRoles(groupRoles))
    , occupationNotifyMode(notifyMode)
{
    // do nothing
}

User::User()
    : d(new UserData(MessengerUserKey(),
                     QString(),
                     QString(),
                     groupRolesForRole(Role::UserRole),
                     QString(),
                     UserTimeZone(),
                     OccupationNotifyMode::OwnResourcesOnly))
{
    // do nothing
}

User::User(MessengerType messenger,
           const QString &userId,
           const QString &firstName,
           const QString &lastName,
           bool isAdmin,
           const QString &locale)
    : d(new UserData(MessengerUserKey(messenger, userId),
                     firstName,
                     lastName,
                     groupRolesForRole(isAdmin ? Role::AdminRole : Role::UserRole),
                     locale,
                     UserTimeZone(),
                     OccupationNotifyMode::OwnResourcesOnly))
{
    // do nothing
}

User::User(MessengerType messenger,
           const QString &userId,
           const QString &firstName,
           const QString &lastName,
           Role role,
           const QString &locale)
    : d(new UserData(MessengerUserKey(messenger, userId),
                     firstName,
                     lastName,
                     groupRolesForRole(role),
                     locale,
                     UserTimeZone(),
                     OccupationNotifyMode::OwnResourcesOnly))
{
    // do nothing
}

User::User(const MessengerUserKey &key,
           const QString &firstName,
           const QString &lastName,
           bool isAdmin,
           const QString &locale)
    : d(new UserData(key,
                     firstName,
                     lastName,
                     groupRolesForRole(isAdmin ? Role::AdminRole : Role::UserRole),
                     locale,
                     UserTimeZone(),
                     OccupationNotifyMode::OwnResourcesOnly))
{
    // do nothing
}

User::User(const MessengerUserKey &key,
           const QString &firstName,
           const QString &lastName,
           Role role,
           const QString &locale)
    : d(new UserData(key,
                     firstName,
                     lastName,
                     groupRolesForRole(role),
                     locale,
                     UserTimeZone(),
                     OccupationNotifyMode::OwnResourcesOnly))
{
    // do nothing
}

User::User(const User &other)
    : d(other.d)
{
    // do nothing
}

User::User(User &&other) noexcept
    : d(std::move(other.d))
{
    // do nothing
}

User &User::operator=(const User &other)
{
    if (this != &other) {
        d = other.d;
    }
    return *this;
}

User &User::operator=(User &&other) noexcept
{
    if (this != &other) {
        d.swap(other.d);
    }
    return *this;
}

User::~User()
{
    // do nothing
}

MessengerUserKey User::key() const
{
    return d ? d->key : MessengerUserKey();
}

QString User::userId() const
{
    return d ? d->key.userId() : QString();
}

QString User::firstName() const
{
    return d ? d->firstName : QString();
}

QString User::lastName() const
{
    return d ? d->lastName : QString();
}

bool User::isAdmin() const
{
    if (!d)
        return false;

    const Role resolved = aggregateRole(d->groupRoles);
    return resolved == Role::AdminRole || resolved == Role::FixedAdminRole;
}

User::Role User::role() const
{
    if (!d)
        return Role::UserRole;

    return aggregateRole(d->groupRoles);
}

QMap<Group, User::Role> User::groupRoles() const
{
    return d ? d->groupRoles : QMap<Group, Role>();
}

QSet<Group> User::groups() const
{
    QSet<Group> result;
    if (!d)
        return result;

    if (d->groupRoles.isEmpty()) {
        result.insert(Group::root());
        return result;
    }

    for (auto it = d->groupRoles.constBegin(); it != d->groupRoles.constEnd(); ++it) {
        result.insert(it.key());
    }
    return result;
}

QSet<Group> User::administeredGroupRoots() const
{
    QSet<Group> roots;
    if (!d)
        return roots;

    if (isFixedAdmin()) {
        roots.insert(Group::root());
        return roots;
    }

    for (auto candidate = d->groupRoles.constBegin(); candidate != d->groupRoles.constEnd();
         ++candidate) {
        if (candidate.value() != Role::AdminRole)
            continue;

        bool coveredByAncestor = false;
        for (auto other = d->groupRoles.constBegin(); other != d->groupRoles.constEnd(); ++other) {
            if (other == candidate || other.value() != Role::AdminRole)
                continue;
            if (other.key().contains(candidate.key())) {
                coveredByAncestor = true;
                break;
            }
        }
        if (!coveredByAncestor)
            roots.insert(candidate.key());
    }

    return roots;
}

User::Role User::roleForGroup(const Group &group) const
{
    if (!d)
        return Role::UserRole;

    if (isFixedAdmin())
        return Role::FixedAdminRole;

    Role resolved = Role::UserRole;
    for (auto it = d->groupRoles.constBegin(); it != d->groupRoles.constEnd(); ++it) {
        if (!it.key().contains(group))
            continue;
        if (it.value() == Role::FixedAdminRole)
            return Role::FixedAdminRole;
        if (it.value() == Role::AdminRole)
            resolved = Role::AdminRole;
    }
    return resolved;
}

bool User::isFixedAdmin() const
{
    if (!d)
        return false;

    return aggregateRole(d->groupRoles) == Role::FixedAdminRole;
}

bool User::hasAccessToGroup(const Group &group) const
{
    if (!d)
        return false;

    if (isFixedAdmin())
        return true;

    if (d->groupRoles.isEmpty())
        return Group::root().contains(group);

    for (auto it = d->groupRoles.constBegin(); it != d->groupRoles.constEnd(); ++it) {
        if (it.key().contains(group))
            return true;
    }
    return false;
}

bool User::canAdministerGroup(const Group &group) const
{
    if (!d)
        return false;

    if (isFixedAdmin())
        return true;

    for (auto it = d->groupRoles.constBegin(); it != d->groupRoles.constEnd(); ++it) {
        if (it.value() == Role::AdminRole && it.key().contains(group))
            return true;
    }
    return false;
}

bool User::canAdministerGroups(const QSet<Group> &groups) const
{
    const QSet<Group> normalized = Group::normalizedSet(groups);
    for (const Group &group : normalized) {
        if (!canAdministerGroup(group))
            return false;
    }
    return true;
}

User::OccupationNotifyMode User::occupationNotifyMode() const
{
    return d ? d->occupationNotifyMode : OccupationNotifyMode::OwnResourcesOnly;
}

QString User::locale() const
{
    return d ? d->locale : QString();
}

UserTimeZone User::timezone() const
{
    return d ? d->timezone : UserTimeZone();
}

MessengerType User::messenger() const
{
    return d ? d->key.messenger() : MessengerType::Telegram;
}

void User::setKey(const MessengerUserKey &key)
{
    if (!d)
        return;

    const QSharedDataPointer<UserData> &cd = d;
    if (cd->key == key)
        return;

    d->key = key;
}

void User::setUserId(const QString &id)
{
    if (!d)
        return;

    const QString trimmed = id.trimmed();
    const QSharedDataPointer<UserData> &cd = d;
    if (cd->key.userId() == trimmed)
        return;

    d->key.setUserId(trimmed);
}

void User::setFirstName(const QString &firstName)
{
    if (!d)
        return;

    const QString trimmed = firstName.trimmed();
    const QSharedDataPointer<UserData> &cd = d;
    if (cd->firstName == trimmed)
        return;

    d->firstName = trimmed;
}

void User::setLastName(const QString &lastName)
{
    if (!d)
        return;

    const QString trimmed = lastName.trimmed();
    const QSharedDataPointer<UserData> &cd = d;
    if (cd->lastName == trimmed)
        return;

    d->lastName = trimmed;
}

void User::setLocale(const QString &locale)
{
    if (!d)
        return;

    const QString trimmed = locale.trimmed();
    const QSharedDataPointer<UserData> &cd = d;
    if (cd->locale == trimmed)
        return;

    d->locale = trimmed;
}

void User::setTimezone(const UserTimeZone &timezone)
{
    if (!d)
        return;

    const QSharedDataPointer<UserData> &cd = d;
    if (cd->timezone == timezone)
        return;

    d->timezone = timezone;
}

void User::setIsAdmin(bool admin)
{
    if (!d)
        return;

    if (admin && isFixedAdmin())
        return;

    const QMap<Group, Role> roles = groupRolesForRole(admin ? Role::AdminRole : Role::UserRole);
    const QSharedDataPointer<UserData> &cd = d;
    if (cd->groupRoles == roles)
        return;

    d->groupRoles = roles;
}

void User::setRole(Role role)
{
    if (!d)
        return;

    const QMap<Group, Role> roles = groupRolesForRole(role);
    const QSharedDataPointer<UserData> &cd = d;
    if (cd->groupRoles == roles)
        return;

    d->groupRoles = roles;
}

void User::setGroupRoles(const QMap<Group, Role> &roles)
{
    if (!d)
        return;

    const QMap<Group, Role> normalized = normalizedGroupRoles(roles);
    const QSharedDataPointer<UserData> &cd = d;
    if (cd->groupRoles == normalized)
        return;

    d->groupRoles = normalized;
}

void User::setGroups(const QSet<Group> &groups, Role role)
{
    QMap<Group, Role> roles;
    const QSet<Group> normalizedGroups = Group::normalizedSet(groups);
    for (const Group &group : normalizedGroups) {
        roles.insert(Group(group), role);
    }
    setGroupRoles(roles);
}

void User::setOccupationNotifyMode(OccupationNotifyMode mode)
{
    if (!d)
        return;

    const QSharedDataPointer<UserData> &cd = d;
    if (cd->occupationNotifyMode == mode)
        return;

    d->occupationNotifyMode = mode;
}

void User::setMessenger(MessengerType messenger)
{
    if (!d)
        return;

    const QSharedDataPointer<UserData> &cd = d;
    if (cd->key.messenger() == messenger)
        return;

    d->key.setMessenger(messenger);
}

bool User::isValid() const
{
    return d && d->key.isValid();
}
