#ifndef PENDINGACCESSUSERMODEL_H
#define PENDINGACCESSUSERMODEL_H

#include "AbstractServerDatabaseModel.h"

#include <QString>
#include <QDateTime>

#include "MessengerUserKey.h"
#include "PendingAccessUserEntry.h"

/**
 * @brief List model that tracks recently seen users awaiting access activation.
 *
 * The model keeps a rolling set of recent users for admin visibility and can enforce a retention
 * policy (maximum count and age). Persistence is optional and provided via StorageAdapter.
 */
class PendingAccessUserModel
    : public AbstractServerDatabaseModel<PendingAccessUserEntry, MessengerUserKey>
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
        LastMessageAtRole
    };

    explicit PendingAccessUserModel(QObject *parent = nullptr);
    ~PendingAccessUserModel() override;

    /**
     * @brief Storage callbacks used to persist pending-access user records.
     */
    struct StorageAdapter
    {
        std::function<bool(const PendingAccessUserEntry &entry)> upsert;
        std::function<bool(const MessengerUserKey &key)> remove;
    };

    void setStorageAdapter(StorageAdapter adapter);
    void setRetentionPolicy(int maxRecent, int retentionDays);

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void replaceAll(const QList<PendingAccessUserEntry> &entries);
    bool upsert(const PendingAccessUserEntry &entry);
    bool removeByKey(const MessengerUserKey &key);
    bool exists(const MessengerUserKey &key) const;

    const QList<PendingAccessUserEntry> &items() const;
    PendingAccessUserEntry entryByKey(const MessengerUserKey &key) const;
    QList<PendingAccessUserEntry> entriesOlderThan(const QDateTime &cutoffUtc) const;
    QList<PendingAccessUserEntry> entriesBeyondLimit(int limit) const;

    bool recordUser(const PendingAccessUserEntry &entry);
    QList<PendingAccessUserEntry> recentUsers(int limit) const;
    bool clearUser(const MessengerUserKey &key);
    bool cleanupExpired();

protected:
    MessengerUserKey keyFromEntry(const PendingAccessUserEntry &entry) const override;
    bool lessThan(const PendingAccessUserEntry &lhs,
                  const PendingAccessUserEntry &rhs) const override;

private:
    int m_maxRecent;
    int m_retentionDays;
    bool m_hasStorageUpsert;
    bool m_hasStorageRemove;
};

#endif // PENDINGACCESSUSERMODEL_H
