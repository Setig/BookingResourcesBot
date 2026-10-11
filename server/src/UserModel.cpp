#include "UserModel.h"

using Base = AbstractServerDatabaseModel<User, MessengerUserKey>;
using BaseStorageAdapter = Base::StorageAdapter;

UserModel::UserModel(QObject *parent)
    : Base(InsertPolicy::PreserveInsertionOrder, parent)
{
    // do nothing
}

UserModel::~UserModel()
{
    // do nothing
}

void UserModel::setStorageAdapter(StorageAdapter adapter)
{
    BaseStorageAdapter baseAdapter;
    baseAdapter.upsert = std::move(adapter.upsert);
    baseAdapter.remove = std::move(adapter.remove);
    Base::setStorageAdapter(std::move(baseAdapter));
}

QVariant UserModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= entryCount())
        return QVariant();

    const User &user = entryAt(index.row());
    switch (role) {
    case MessengerRole:
        return messengerTypeToString(user.messenger());
    case UserIdRole:
        return user.userId();
    case UserKeyRole:
        return QVariant::fromValue(user.key());
    case UserKeyStringRole:
        return user.key().toString();
    case FirstNameRole:
        return user.firstName();
    case LastNameRole:
        return user.lastName();
    case LocaleRole:
        return user.locale();
    case OccupationNotifyModeRole:
        return static_cast<int>(user.occupationNotifyMode());
    case IsAdminRole:
        return user.isAdmin();
    case RoleNameRole:
        switch (user.role()) {
        case User::Role::UserRole:
            return QStringLiteral("user");
        case User::Role::AdminRole:
            return QStringLiteral("admin");
        case User::Role::FixedAdminRole:
            return QStringLiteral("fixed_admin");
        }
    case GroupsRole:
        return Group::toStringList(user.groups());
    case GroupRolesRole: {
        QStringList entries;
        const QMap<Group, User::Role> roles = user.groupRoles();
        for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
            QString roleName = QStringLiteral("user");
            if (it.value() == User::Role::AdminRole) {
                roleName = QStringLiteral("admin");
            } else if (it.value() == User::Role::FixedAdminRole) {
                roleName = QStringLiteral("fixed_admin");
            }
            entries.append(QStringLiteral("%1=%2").arg(QString(it.key()), roleName));
        }
        return entries;
    }
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> UserModel::roleNames() const
{
    return {
        { MessengerRole, QByteArrayLiteral("messenger") },
        { UserIdRole, QByteArrayLiteral("userId") },
        { UserKeyRole, QByteArrayLiteral("userKey") },
        { UserKeyStringRole, QByteArrayLiteral("userKeyString") },
        { FirstNameRole, QByteArrayLiteral("firstName") },
        { LastNameRole, QByteArrayLiteral("lastName") },
        { LocaleRole, QByteArrayLiteral("locale") },
        { OccupationNotifyModeRole, QByteArrayLiteral("occupationNotifyMode") },
        { IsAdminRole, QByteArrayLiteral("isAdmin") },
        { RoleNameRole, QByteArrayLiteral("roleName") },
        { GroupsRole, QByteArrayLiteral("groups") },
        { GroupRolesRole, QByteArrayLiteral("groupRoles") },
    };
}

void UserModel::replaceAll(const QList<User> &users)
{
    replaceAllEntries(users);
}

bool UserModel::upsert(const User &user)
{
    return upsertEntry(user);
}

bool UserModel::removeByKey(const MessengerUserKey &key)
{
    return Base::removeEntryByKey(key);
}

bool UserModel::exists(const MessengerUserKey &key) const
{
    return containsKey(key);
}

const QList<User> &UserModel::items() const
{
    return entriesRef();
}

User UserModel::userByKey(const MessengerUserKey &key) const
{
    return Base::entryByKey(key);
}

bool UserModel::addUser(const User &user)
{
    if (!user.isValid()) {
        emit errorOccurred(QStringLiteral("User is invalid."));
        return false;
    }

    if (exists(user.key())) {
        emit errorOccurred(QStringLiteral("User %1 already exists.").arg(user.userId()));
        return false;
    }

    if (!persistUpsert(user)) {
        emit errorOccurred(QStringLiteral("Failed to persist user %1.").arg(user.userId()));
        return false;
    }

    upsert(user);
    return true;
}

bool UserModel::updateUser(const User &user)
{
    if (!user.isValid()) {
        emit errorOccurred(QStringLiteral("User is invalid."));
        return false;
    }

    if (!exists(user.key())) {
        emit errorOccurred(QStringLiteral("User %1 does not exist.").arg(user.userId()));
        return false;
    }

    if (!persistUpsert(user)) {
        emit errorOccurred(QStringLiteral("Failed to persist user %1.").arg(user.userId()));
        return false;
    }

    upsert(user);
    return true;
}

bool UserModel::deleteUser(const MessengerUserKey &key)
{
    if (!key.isValid()) {
        emit errorOccurred(QStringLiteral("User is invalid."));
        return false;
    }

    if (!exists(key)) {
        emit errorOccurred(QStringLiteral("User %1 does not exist.").arg(key.toString()));
        return false;
    }

    if (!persistRemove(key)) {
        emit errorOccurred(
                QStringLiteral("Failed to persist user deletion for %1.").arg(key.toString()));
        return false;
    }

    removeByKey(key);
    return true;
}

bool UserModel::ensureFixedAdmin(const MessengerUserKey &key)
{
    if (!key.isValid()) {
        emit errorOccurred(QStringLiteral("User is invalid."));
        return false;
    }

    User candidate = userByKey(key);
    if (candidate.isValid()) {
        if (candidate.isFixedAdmin())
            return true;

        candidate.setRole(User::Role::FixedAdminRole);
        User persisted = candidate;
        persisted.setRole(User::Role::AdminRole);
        if (!persistUpsert(persisted)) {
            emit errorOccurred(QStringLiteral("Failed to persist user %1.").arg(key.toString()));
            return false;
        }

        upsert(candidate);
        return true;
    }

    User adminSeed(key, QString(), QString(), User::Role::FixedAdminRole);
    User persisted = adminSeed;
    persisted.setRole(User::Role::AdminRole);
    if (!persistUpsert(persisted)) {
        emit errorOccurred(QStringLiteral("Failed to persist user %1.").arg(key.toString()));
        return false;
    }

    upsert(adminSeed);
    return true;
}

MessengerUserKey UserModel::keyFromEntry(const User &user) const
{
    return user.key();
}
