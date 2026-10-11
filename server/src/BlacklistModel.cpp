#include "BlacklistModel.h"

using Base = AbstractServerDatabaseModel<BlacklistEntry, MessengerUserKey>;
using BaseStorageAdapter = Base::StorageAdapter;

BlacklistModel::BlacklistModel(QObject *parent)
    : Base(InsertPolicy::Sorted, parent)
{
    // do nothing
}

BlacklistModel::~BlacklistModel()
{
    // do nothing
}

void BlacklistModel::setStorageAdapter(StorageAdapter adapter)
{
    BaseStorageAdapter baseAdapter;
    baseAdapter.upsert = std::move(adapter.upsert);
    baseAdapter.remove = std::move(adapter.remove);

    Base::setStorageAdapter(std::move(baseAdapter));
    m_logExpiry = std::move(adapter.logExpiry);
}

QVariant BlacklistModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= entryCount())
        return QVariant();

    const BlacklistEntry &entry = entryAt(index.row());
    switch (role) {
    case MessengerRole:
        return messengerTypeToString(entry.user().messenger());
    case UserIdRole:
        return entry.user().userId();
    case UserKeyRole:
        return QVariant::fromValue(entry.user());
    case UserKeyStringRole:
        return entry.user().toString();
    case ReasonRole:
        return entry.reason();
    case BlockedAtRole:
        return entry.blockedAtUtc();
    case BlockedUntilRole:
        return entry.blockedUntilUtc();
    case BlockedByMessengerRole:
        return entry.blockedBy().isValid() ? messengerTypeToString(entry.blockedBy().messenger())
                                           : QVariant();
    case BlockedByUserIdRole:
        return entry.blockedBy().userId();
    case BlockedByKeyRole:
        return QVariant::fromValue(entry.blockedBy());
    case BlockedByKeyStringRole:
        return entry.blockedBy().toString();
    case IsPermanentRole:
        return entry.isPermanent();
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> BlacklistModel::roleNames() const
{
    return {
        { MessengerRole, QByteArrayLiteral("messenger") },
        { UserIdRole, QByteArrayLiteral("userId") },
        { UserKeyRole, QByteArrayLiteral("userKey") },
        { UserKeyStringRole, QByteArrayLiteral("userKeyString") },
        { ReasonRole, QByteArrayLiteral("reason") },
        { BlockedAtRole, QByteArrayLiteral("blockedAt") },
        { BlockedUntilRole, QByteArrayLiteral("blockedUntil") },
        { BlockedByMessengerRole, QByteArrayLiteral("blockedByMessenger") },
        { BlockedByUserIdRole, QByteArrayLiteral("blockedByUserId") },
        { BlockedByKeyRole, QByteArrayLiteral("blockedByKey") },
        { BlockedByKeyStringRole, QByteArrayLiteral("blockedByKeyString") },
        { IsPermanentRole, QByteArrayLiteral("isPermanent") },
    };
}

void BlacklistModel::replaceAll(const QList<BlacklistEntry> &entries)
{
    replaceAllEntries(entries);
}

bool BlacklistModel::upsert(const BlacklistEntry &entry)
{
    return upsertEntry(entry);
}

bool BlacklistModel::removeByKey(const MessengerUserKey &key)
{
    return Base::removeEntryByKey(key);
}

bool BlacklistModel::exists(const MessengerUserKey &key) const
{
    return containsKey(key);
}

const QList<BlacklistEntry> &BlacklistModel::items() const
{
    return entriesRef();
}

BlacklistEntry BlacklistModel::entryByKey(const MessengerUserKey &key) const
{
    return Base::entryByKey(key);
}

QList<BlacklistEntry> BlacklistModel::expiredEntries(const QDateTime &nowUtc) const
{
    QList<BlacklistEntry> expired;
    for (const BlacklistEntry &entry : entriesRef()) {
        if (entry.isExpired(nowUtc))
            expired.append(entry);
    }
    return expired;
}

bool BlacklistModel::saveEntry(const BlacklistEntry &entry)
{
    if (!entry.isValid())
        return false;

    if (!purgeExpiredEntries()) {
        emit errorOccurred(QStringLiteral("Failed to purge expired blacklist entries."));
        return false;
    }

    if (!persistUpsert(entry)) {
        emit errorOccurred(QStringLiteral("Failed to persist blacklist entry for %1.")
                                   .arg(entry.user().toString()));
        return false;
    }

    upsert(entry);
    return true;
}

bool BlacklistModel::removeEntry(const MessengerUserKey &key)
{
    if (!key.isValid())
        return false;

    if (!purgeExpiredEntries()) {
        emit errorOccurred(QStringLiteral("Failed to purge expired blacklist entries."));
        return false;
    }

    if (!persistRemove(key)) {
        emit errorOccurred(
                QStringLiteral("Failed to remove blacklist entry for %1.").arg(key.userId()));
        return false;
    }

    return Base::removeEntryByKey(key);
}

bool BlacklistModel::isBlacklisted(const MessengerUserKey &key, BlacklistEntry *entryOut)
{
    if (!key.isValid())
        return false;

    if (!purgeExpiredEntries()) {
        emit errorOccurred(QStringLiteral("Failed to purge expired blacklist entries."));
        return false;
    }

    const BlacklistEntry entry = entryByKey(key);
    if (!entry.isValid())
        return false;

    if (entryOut)
        *entryOut = entry;

    return true;
}

QList<BlacklistEntry> BlacklistModel::entries() const
{
    return entriesCopy();
}

bool BlacklistModel::purgeExpiredEntries(const QDateTime &nowUtc)
{
    const QList<BlacklistEntry> expired = expiredEntries(nowUtc);
    if (expired.isEmpty())
        return true;

    bool ok = true;
    for (const BlacklistEntry &entry : expired) {
        if (!persistRemove(entry.user())) {
            emit errorOccurred(QStringLiteral("Failed to remove expired blacklist entry for %1.")
                                       .arg(entry.user().toString()));
            ok = false;
            continue;
        }
        Base::removeEntryByKey(entry.user());
        if (m_logExpiry)
            m_logExpiry(entry);
    }

    return ok;
}

MessengerUserKey BlacklistModel::keyFromEntry(const BlacklistEntry &entry) const
{
    return entry.user();
}

bool BlacklistModel::lessThan(const BlacklistEntry &lhs, const BlacklistEntry &rhs) const
{
    if (isMoreRecent(lhs.blockedAtUtc(), rhs.blockedAtUtc()))
        return true;
    if (isMoreRecent(rhs.blockedAtUtc(), lhs.blockedAtUtc()))
        return false;
    return lhs.user().toString() < rhs.user().toString();
}
