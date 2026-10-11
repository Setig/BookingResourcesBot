#ifndef BLACKLISTMODEL_H
#define BLACKLISTMODEL_H

#include "AbstractServerDatabaseModel.h"

#include <QString>
#include <QDateTime>

#include "BlacklistEntry.h"
#include "MessengerUserKey.h"

/**
 * @brief List model that stores blacklisted users and provides lookup/purge helpers.
 *
 * The model can optionally persist changes via callbacks provided through StorageAdapter.
 */
class BlacklistModel : public AbstractServerDatabaseModel<BlacklistEntry, MessengerUserKey>
{
    Q_OBJECT

public:
    enum Roles {
        MessengerRole = Qt::UserRole + 1,
        UserIdRole,
        UserKeyRole,
        UserKeyStringRole,
        ReasonRole,
        BlockedAtRole,
        BlockedUntilRole,
        BlockedByMessengerRole,
        BlockedByUserIdRole,
        BlockedByKeyRole,
        BlockedByKeyStringRole,
        IsPermanentRole
    };

    explicit BlacklistModel(QObject *parent = nullptr);
    ~BlacklistModel() override;

    /**
     * @brief Storage callbacks used to persist blacklist changes.
     */
    struct StorageAdapter
    {
        std::function<bool(const BlacklistEntry &entry)> upsert;
        std::function<bool(const MessengerUserKey &key)> remove;
        std::function<void(const BlacklistEntry &entry)> logExpiry;
    };

    void setStorageAdapter(StorageAdapter adapter);

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void replaceAll(const QList<BlacklistEntry> &entries);
    bool upsert(const BlacklistEntry &entry);
    bool removeByKey(const MessengerUserKey &key);
    bool exists(const MessengerUserKey &key) const;

    const QList<BlacklistEntry> &items() const;
    BlacklistEntry entryByKey(const MessengerUserKey &key) const;
    QList<BlacklistEntry> expiredEntries(const QDateTime &nowUtc) const;

    bool saveEntry(const BlacklistEntry &entry);
    bool removeEntry(const MessengerUserKey &key);
    bool isBlacklisted(const MessengerUserKey &key, BlacklistEntry *entryOut = nullptr);
    QList<BlacklistEntry> entries() const;
    bool purgeExpiredEntries(const QDateTime &nowUtc = QDateTime::currentDateTimeUtc());

protected:
    MessengerUserKey keyFromEntry(const BlacklistEntry &entry) const override;
    bool lessThan(const BlacklistEntry &lhs, const BlacklistEntry &rhs) const override;

private:
    std::function<void(const BlacklistEntry &entry)> m_logExpiry;
};

#endif // BLACKLISTMODEL_H
