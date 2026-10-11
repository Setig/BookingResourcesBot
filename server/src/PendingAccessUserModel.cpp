#include "PendingAccessUserModel.h"

using Base = AbstractServerDatabaseModel<PendingAccessUserEntry, MessengerUserKey>;
using BaseStorageAdapter = Base::StorageAdapter;

PendingAccessUserModel::PendingAccessUserModel(QObject *parent)
    : Base(InsertPolicy::Sorted, parent)
    , m_maxRecent(100)
    , m_retentionDays(3)
    , m_hasStorageUpsert(false)
    , m_hasStorageRemove(false)
{
    // do nothing
}

PendingAccessUserModel::~PendingAccessUserModel()
{
    // do nothing
}

void PendingAccessUserModel::setStorageAdapter(StorageAdapter adapter)
{
    m_hasStorageUpsert = static_cast<bool>(adapter.upsert);
    m_hasStorageRemove = static_cast<bool>(adapter.remove);

    BaseStorageAdapter baseAdapter;
    baseAdapter.upsert = std::move(adapter.upsert);
    baseAdapter.remove = std::move(adapter.remove);

    Base::setStorageAdapter(std::move(baseAdapter));
}

void PendingAccessUserModel::setRetentionPolicy(int maxRecent, int retentionDays)
{
    if (maxRecent > 0)
        m_maxRecent = maxRecent;
    if (retentionDays > 0)
        m_retentionDays = retentionDays;
}

QVariant PendingAccessUserModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= entryCount())
        return QVariant();

    const PendingAccessUserEntry &entry = entryAt(index.row());
    switch (role) {
    case MessengerRole:
        return messengerTypeToString(entry.user().messenger());
    case UserIdRole:
        return entry.user().userId();
    case UserKeyRole:
        return QVariant::fromValue(entry.user());
    case UserKeyStringRole:
        return entry.user().toString();
    case FirstNameRole:
        return entry.firstName();
    case LastNameRole:
        return entry.lastName();
    case LastMessageAtRole:
        return entry.lastMessageAtUtc();
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> PendingAccessUserModel::roleNames() const
{
    return {
        { MessengerRole, QByteArrayLiteral("messenger") },
        { UserIdRole, QByteArrayLiteral("userId") },
        { UserKeyRole, QByteArrayLiteral("userKey") },
        { UserKeyStringRole, QByteArrayLiteral("userKeyString") },
        { FirstNameRole, QByteArrayLiteral("firstName") },
        { LastNameRole, QByteArrayLiteral("lastName") },
        { LastMessageAtRole, QByteArrayLiteral("lastMessageAt") },
    };
}

void PendingAccessUserModel::replaceAll(const QList<PendingAccessUserEntry> &entries)
{
    replaceAllEntries(entries);
}

bool PendingAccessUserModel::upsert(const PendingAccessUserEntry &entry)
{
    return upsertEntry(entry);
}

bool PendingAccessUserModel::removeByKey(const MessengerUserKey &key)
{
    return Base::removeEntryByKey(key);
}

bool PendingAccessUserModel::exists(const MessengerUserKey &key) const
{
    return containsKey(key);
}

const QList<PendingAccessUserEntry> &PendingAccessUserModel::items() const
{
    return entriesRef();
}

PendingAccessUserEntry PendingAccessUserModel::entryByKey(const MessengerUserKey &key) const
{
    return Base::entryByKey(key);
}

QList<PendingAccessUserEntry> PendingAccessUserModel::entriesOlderThan(
        const QDateTime &cutoffUtc) const
{
    QList<PendingAccessUserEntry> entries;
    for (const PendingAccessUserEntry &entry : entriesRef()) {
        const QDateTime lastMessageAt = entry.lastMessageAtUtc();
        if (lastMessageAt.isValid() && lastMessageAt < cutoffUtc)
            entries.append(entry);
    }
    return entries;
}

QList<PendingAccessUserEntry> PendingAccessUserModel::entriesBeyondLimit(int limit) const
{
    QList<PendingAccessUserEntry> entries;
    if (limit < 0)
        return entries;

    const QList<PendingAccessUserEntry> &all = entriesRef();
    for (int i = limit; i < all.size(); ++i)
        entries.append(all.at(i));
    return entries;
}

bool PendingAccessUserModel::recordUser(const PendingAccessUserEntry &entry)
{
    if (!entry.isValid() || !m_hasStorageUpsert)
        return false;

    if (!persistUpsert(entry)) {
        emit errorOccurred(QStringLiteral("Failed to persist user awaiting access activation %1.")
                                   .arg(entry.user().toString()));
        return false;
    }

    upsert(entry);
    cleanupExpired();
    return true;
}

QList<PendingAccessUserEntry> PendingAccessUserModel::recentUsers(int limit) const
{
    PendingAccessUserModel *self = const_cast<PendingAccessUserModel *>(this);
    self->cleanupExpired();

    int effectiveLimit = limit;
    if (effectiveLimit <= 0 || effectiveLimit > m_maxRecent)
        effectiveLimit = m_maxRecent;
    effectiveLimit = qMin(effectiveLimit, entryCount());

    QList<PendingAccessUserEntry> recent;
    for (int i = 0; i < effectiveLimit; ++i)
        recent.append(entryAt(i));
    return recent;
}

bool PendingAccessUserModel::clearUser(const MessengerUserKey &key)
{
    if (!key.isValid() || !m_hasStorageRemove)
        return false;

    if (!persistRemove(key)) {
        emit errorOccurred(QStringLiteral("Failed to remove user awaiting access activation %1.")
                                   .arg(key.toString()));
        return false;
    }

    return Base::removeEntryByKey(key);
}

bool PendingAccessUserModel::cleanupExpired()
{
    if (!m_hasStorageRemove) {
        emit errorOccurred(
                QStringLiteral(
                        "Cannot cleanup users awaiting access activation: storage unavailable."));
        return false;
    }

    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addDays(-qMax(0, m_retentionDays));
    const QList<PendingAccessUserEntry> oldEntries = entriesOlderThan(cutoff);
    const QList<PendingAccessUserEntry> overflowEntries = entriesBeyondLimit(qMax(0, m_maxRecent));

    auto removeEntries = [&](const QList<PendingAccessUserEntry> &entries) {
        bool ok = true;
        for (const PendingAccessUserEntry &entry : entries) {
            if (!persistRemove(entry.user())) {
                emit errorOccurred(QStringLiteral("Failed to purge user awaiting access activation "
                                                 "%1.")
                                           .arg(entry.user().toString()));
                ok = false;
                continue;
            }
            Base::removeEntryByKey(entry.user());
        }
        return ok;
    };

    bool ok = removeEntries(oldEntries);
    ok = removeEntries(overflowEntries) && ok;
    return ok;
}

MessengerUserKey PendingAccessUserModel::keyFromEntry(const PendingAccessUserEntry &entry) const
{
    return entry.user();
}

bool PendingAccessUserModel::lessThan(const PendingAccessUserEntry &lhs,
                                      const PendingAccessUserEntry &rhs) const
{
    if (isMoreRecent(lhs.lastMessageAtUtc(), rhs.lastMessageAtUtc()))
        return true;
    if (isMoreRecent(rhs.lastMessageAtUtc(), lhs.lastMessageAtUtc()))
        return false;
    return lhs.user().toString() < rhs.user().toString();
}
