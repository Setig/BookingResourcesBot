#ifndef USERMODEL_H
#define USERMODEL_H

#include "AbstractServerDatabaseModel.h"

#include "User.h"
#include "MessengerUserKey.h"

class UserModelPrivate;

/**
 * @brief List model that stores users with active access.
 *
 * Stores User values and provides lookup/mutation helpers. Persistence is optional and can be
 * supplied via StorageAdapter.
 */
class UserModel : public AbstractServerDatabaseModel<User, MessengerUserKey>
{
    Q_OBJECT

public:
    enum Roles {
        MessengerRole = Qt::UserRole + 1,
        UserIdRole,
        UserKeyRole,
        UserKeyStringRole,
        FirstNameRole,
        LastNameRole,
        LocaleRole,
        OccupationNotifyModeRole,
        IsAdminRole,
        RoleNameRole,
        GroupsRole,
        GroupRolesRole
    };

    explicit UserModel(QObject *parent = nullptr);
    ~UserModel() override;

    /**
     * @brief Storage callbacks used to persist user changes.
     */
    struct StorageAdapter
    {
        std::function<bool(const User &user)> upsert;
        std::function<bool(const MessengerUserKey &key)> remove;
    };

    void setStorageAdapter(StorageAdapter adapter);

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void replaceAll(const QList<User> &users);
    bool upsert(const User &user);
    bool removeByKey(const MessengerUserKey &key);
    bool exists(const MessengerUserKey &key) const;

    const QList<User> &items() const;
    User userByKey(const MessengerUserKey &key) const;

    bool addUser(const User &user);
    bool updateUser(const User &user);
    bool deleteUser(const MessengerUserKey &key);
    bool ensureFixedAdmin(const MessengerUserKey &key);

protected:
    MessengerUserKey keyFromEntry(const User &user) const override;
};

#endif // USERMODEL_H
