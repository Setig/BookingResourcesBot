#include "ResourceQueueModel.h"

#include "User.h"
#include "Resource.h"
#include "UserModel.h"
#include "ResourceModel.h"
#include "MessengerType.h"
#include "UserTimeZone.h"
#include "MessengerTextRender.h"
#include "ServerDateTimeFormat.h"
#include "ServerRuntimeSettings.h"
#include "ResourceQueueUserMessages.h"

#include <QSet>
#include <QTimer>
#include <QVector>
#include <QDateTime>
#include <QTimeZone>

using Base = AbstractServerDatabaseModel<ResourceQueueEntry, ResourceQueueKey>;
using BaseStorageAdapter = Base::StorageAdapter;

namespace {

void setError(QString *errorOut, const QString &text)
{
    if (errorOut)
        *errorOut = text;
}

} // namespace

class ResourceQueueModelPrivate
{
public:
    ResourceQueueModelPrivate(ResourceQueueModel *qPtr,
                              UserModel *userModelPtr,
                              ResourceModel *resourceModelPtr);

    [[nodiscard]] static QDateTime utcNowSeconds();
    [[nodiscard]] static QDateTime normalizeUtcSeconds(const QDateTime &dt);
    int durationMinutesForEntry(const ResourceQueueEntry &entry) const;

    QString resolveResourceUuid(const QString &resourceName,
                                QString *resolvedNameOut = nullptr) const;
    QString resourceNameForUuid(const QString &resourceUuid) const;
    bool isValidUser(const MessengerUserKey &user) const;
    QString userDisplayName(const MessengerUserKey &user) const;
    UserTimeZone timeZoneForUser(const MessengerUserKey &user) const;
    bool isSelfAction(const MessengerUserKey &actor, const MessengerUserKey &user) const;

    ResourceQueueKey entryKey(const QString &resourceUuid,
                              const MessengerUserKey &user,
                              const QDateTime &startAtUtc) const;

    void rebuildIndexes();
    int registerRowForResource(const QString &resourceUuid, int row);
    int registerRowForUser(const MessengerUserKey &user, int row);
    void unregisterRowForResource(const QString &resourceUuid, int row);
    void unregisterRowForUser(const MessengerUserKey &user, int row);
    void adjustRowsAfterRemoval(int removedRow);
    int registerEntry(const ResourceQueueEntry &entry, int row);
    void unregisterEntry(const ResourceQueueEntry &entry, int row);
    ResourceQueueEntry takeEntryAtRow(int row);

    bool resourceRowLess(int lhs, int rhs) const;
    void repositionResourceRow(const QString &resourceUuid, int row);

    int rowForEntryKey(const ResourceQueueKey &key) const;

    void purgeExpiredEntries(const QDateTime &nowUtc, QList<ResourceQueueEntry> *expiredOut);

    bool validateAllowedDuration(int durationMinutes,
                                 const QList<int> &allowedDurations,
                                 QString *errorOut) const;
    bool validateMaxDuration(const QDateTime &startUtc,
                             const QDateTime &endUtc,
                             QString *errorOut) const;
    bool validateMaxResourcesForUser(const MessengerUserKey &user,
                                     const QString &resourceUuid,
                                     QString *errorOut) const;
    bool validateNoOverlapForResource(const QString &resourceUuid,
                                      const QDateTime &startUtc,
                                      const QDateTime &endUtc,
                                      int ignoreRow,
                                      const MessengerUserKey &viewer,
                                      QString *errorOut) const;
    bool hasAfterReleaseEntriesForResource(const QString &resourceUuid, int ignoreRow = -1) const;
    bool hasFutureAtTimeEntriesForResource(const QString &resourceUuid,
                                           const QDateTime &nowUtc,
                                           int ignoreRow = -1) const;
    bool hasAfterReleaseEntryForUser(const QString &resourceUuid,
                                     const MessengerUserKey &user,
                                     int ignoreRow = -1) const;
    int firstAfterReleaseRowForResource(const QString &resourceUuid) const;
    QSet<ResourceQueueKey> promoteAfterReleaseEntries(const QSet<QString> &resources,
                                                      const QDateTime &nowUtc);

    bool occupyInternal(const QString &resourceName,
                        const MessengerUserKey &user,
                        int durationMinutes,
                        const QList<int> &allowedDurations,
                        const MessengerUserKey &actor,
                        QString *errorOut,
                        ResourceQueueEntry *storedOut);

    bool scheduleInternal(const QString &resourceName,
                          const MessengerUserKey &user,
                          const QDateTime &startUtc,
                          const QDateTime &endUtc,
                          bool requireStartNotPast,
                          const MessengerUserKey &actor,
                          QString *errorOut,
                          ResourceQueueEntry *storedOut);

    bool removeUserSchedulesInternal(const QString &resourceName,
                                     const MessengerUserKey &user,
                                     QString *errorOut,
                                     QList<ResourceQueueEntry> *removedOut);

    bool releaseUserOccupationInternal(const QString &resourceName,
                                       const MessengerUserKey &user,
                                       const MessengerUserKey &actor,
                                       QString *errorOut,
                                       ResourceQueueEntry *removedOut);

    bool removeResourceEntriesInternal(const QString &resourceName,
                                       QString *errorOut,
                                       QList<ResourceQueueEntry> *removedOut);

    int removeUserEntriesInternal(const MessengerUserKey &user,
                                  QList<ResourceQueueEntry> *removedOut);

    ResourceQueueEntry activeEntryForResourceUuid(const QString &resourceUuid,
                                                  const QDateTime &nowUtc) const;

    QDateTime computeHandoverUtcForActiveEntry(const ResourceQueueEntry &entry) const;

    void refreshOccupations(bool notifyTransitions, const QSet<QString> &resourcesHint = {});
    void scheduleNextTimer();
    void onScheduleTimer();

    void incrementUserResourceCount(const MessengerUserKey &user, const QString &resourceUuid);
    void decrementUserResourceCount(const MessengerUserKey &user, const QString &resourceUuid);

    ResourceQueueModel *q = nullptr;
    UserModel *userModel = nullptr;
    ResourceModel *resourceModel = nullptr;

    QHash<QString, QList<int>> rowsByResource;
    QHash<MessengerUserKey, QList<int>> rowsByUser;
    std::function<bool(const QList<ResourceQueueKey> &keys)> removeEntries;
    std::function<bool(const MessengerUserKey &user)> removeUserEntries;
    QTimer scheduleTimer;
    QHash<QString, ResourceQueueKey> activeKeyByResource;
    QHash<QString, MessengerUserKey> lastReleaseByResource;
    QHash<MessengerUserKey, QHash<QString, int>> resourceCountsByUser;
    QHash<QString, int> maxRowByResource;
    QHash<MessengerUserKey, int> maxRowByUser;
    QSet<QString> nextScheduleResources;

    uint maxResourcesPerUser =
            ServerRuntimeSettings::defaultValue(ServerRuntimeSettings::Setting::MaxResourcesPerUser)
                    .toUInt(); // 0 = unlimited
    uint maxOccupationDurationMinutes =
            ServerRuntimeSettings::defaultValue(
                    ServerRuntimeSettings::Setting::MaxOccupationDurationMinutes)
                    .toUInt(); // 0 = unlimited
};

QDateTime ResourceQueueModelPrivate::utcNowSeconds()
{
    return QDateTime::fromSecsSinceEpoch(QDateTime::currentSecsSinceEpoch(), QTimeZone::utc());
}

QDateTime ResourceQueueModelPrivate::normalizeUtcSeconds(const QDateTime &dt)
{
    if (!dt.isValid())
        return QDateTime();
    return QDateTime::fromSecsSinceEpoch(dt.toSecsSinceEpoch(), QTimeZone::utc());
}

int ResourceQueueModelPrivate::durationMinutesForEntry(const ResourceQueueEntry &entry) const
{
    if (entry.waitsForRelease()) {
        const int requested = entry.requestedDurationMinutes();
        return requested < 0 ? 0 : requested;
    }
    if (!entry.startAtUtc().isValid())
        return 0;
    if (!entry.endAtUtc().isValid())
        return 0;
    const qint64 secs = entry.startAtUtc().secsTo(entry.endAtUtc());
    if (secs <= 0)
        return 0;
    return static_cast<int>(secs / 60);
}

ResourceQueueModelPrivate::ResourceQueueModelPrivate(ResourceQueueModel *qPtr,
                                                     UserModel *userModelPtr,
                                                     ResourceModel *resourceModelPtr)
    : q(qPtr)
    , userModel(userModelPtr)
    , resourceModel(resourceModelPtr)
{
    scheduleTimer.setSingleShot(true);
    QObject::connect(&scheduleTimer, &QTimer::timeout, q, [this]() { onScheduleTimer(); });
}

QString ResourceQueueModelPrivate::resolveResourceUuid(const QString &resourceName,
                                                       QString *resolvedNameOut) const
{
    const QString trimmed = resourceName.trimmed();
    if (resolvedNameOut)
        *resolvedNameOut = trimmed;
    if (trimmed.isEmpty() || !resourceModel)
        return QString();

    const Resource resource = resourceModel->resourceByName(trimmed);
    if (!resource.isValid())
        return QString();

    if (resolvedNameOut)
        *resolvedNameOut = resource.name();
    return resource.uuidString();
}

QString ResourceQueueModelPrivate::resourceNameForUuid(const QString &resourceUuid) const
{
    if (!resourceModel)
        return ResourceQueueModel::tr("(unknown)");
    const Resource resource = resourceModel->resourceByUuid(resourceUuid);
    if (!resource.isValid())
        return ResourceQueueModel::tr("(unknown)");
    return resource.name();
}

bool ResourceQueueModelPrivate::isValidUser(const MessengerUserKey &user) const
{
    if (!userModel)
        return false;
    return userModel->exists(user);
}

QString ResourceQueueModelPrivate::userDisplayName(const MessengerUserKey &user) const
{
    if (userModel) {
        const User existing = userModel->userByKey(user);
        if (existing.isValid()) {
            const QString display =
                    MessengerTextRender::formatUserDisplayName(existing, false).trimmed();
            const QString keyText = user.toString().trimmed();
            if (!display.isEmpty() && display != keyText)
                return display;
        }
    }

    const QString userId = user.userId().trimmed();
    return userId.isEmpty() ? user.toString().trimmed() : userId;
}

UserTimeZone ResourceQueueModelPrivate::timeZoneForUser(const MessengerUserKey &user) const
{
    if (!userModel)
        return UserTimeZone();
    const User existing = userModel->userByKey(user);
    if (!existing.isValid())
        return UserTimeZone();
    return existing.timezone();
}

bool ResourceQueueModelPrivate::isSelfAction(const MessengerUserKey &actor,
                                             const MessengerUserKey &user) const
{
    return !actor.isValid() || actor == user;
}

ResourceQueueKey ResourceQueueModelPrivate::entryKey(const QString &resourceUuid,
                                                     const MessengerUserKey &user,
                                                     const QDateTime &startAtUtc) const
{
    return ResourceQueueKey(resourceUuid, user, startAtUtc);
}

int ResourceQueueModelPrivate::registerRowForResource(const QString &resourceUuid, int row)
{
    QList<int> &rows = rowsByResource[resourceUuid];
    int position = 1;
    QList<int>::iterator it = rows.begin();
    for (; it != rows.end(); ++it, ++position) {
        if (!resourceRowLess(*it, row))
            break;
    }
    rows.insert(it, row);
    const int currentMax = maxRowByResource.value(resourceUuid, -1);
    if (row > currentMax)
        maxRowByResource.insert(resourceUuid, row);
    return position;
}

int ResourceQueueModelPrivate::registerRowForUser(const MessengerUserKey &user, int row)
{
    QList<int> &rows = rowsByUser[user];
    rows.append(row);
    const int currentMax = maxRowByUser.value(user, -1);
    if (row > currentMax)
        maxRowByUser.insert(user, row);
    return rows.size();
}

void ResourceQueueModelPrivate::unregisterRowForResource(const QString &resourceUuid, int row)
{
    QHash<QString, QList<int>>::iterator it = rowsByResource.find(resourceUuid);
    if (it == rowsByResource.end())
        return;

    QList<int> &rows = it.value();
    rows.removeAll(row);
    if (rows.isEmpty()) {
        rowsByResource.erase(it);
        maxRowByResource.remove(resourceUuid);
        return;
    }

    const int currentMax = maxRowByResource.value(resourceUuid, -1);
    if (currentMax == row) {
        int newMax = -1;
        for (int storedRow : rows)
            newMax = std::max(newMax, storedRow);
        if (newMax >= 0)
            maxRowByResource.insert(resourceUuid, newMax);
        else
            maxRowByResource.remove(resourceUuid);
    }
}

void ResourceQueueModelPrivate::unregisterRowForUser(const MessengerUserKey &user, int row)
{
    QHash<MessengerUserKey, QList<int>>::iterator it = rowsByUser.find(user);
    if (it == rowsByUser.end())
        return;

    QList<int> &rows = it.value();
    rows.removeAll(row);
    if (rows.isEmpty()) {
        rowsByUser.erase(it);
        maxRowByUser.remove(user);
        return;
    }

    const int currentMax = maxRowByUser.value(user, -1);
    if (currentMax == row) {
        int newMax = -1;
        for (int storedRow : rows)
            newMax = std::max(newMax, storedRow);
        if (newMax >= 0)
            maxRowByUser.insert(user, newMax);
        else
            maxRowByUser.remove(user);
    }
}

void ResourceQueueModelPrivate::adjustRowsAfterRemoval(int removedRow)
{
    if (!q)
        return;
    if (removedRow >= q->entryCount())
        return;

    for (QHash<QString, QList<int>>::iterator it = rowsByResource.begin();
         it != rowsByResource.end();
         ++it) {
        const QString resourceUuid = it.key();
        const int maxRow = maxRowByResource.value(resourceUuid, -1);
        if (maxRow <= removedRow)
            continue;
        QList<int> &rows = it.value();
        for (int &storedRow : rows) {
            if (storedRow > removedRow)
                --storedRow;
        }
        maxRowByResource.insert(resourceUuid, maxRow - 1);
    }

    for (QHash<MessengerUserKey, QList<int>>::iterator it = rowsByUser.begin();
         it != rowsByUser.end();
         ++it) {
        const MessengerUserKey userKey = it.key();
        const int maxRow = maxRowByUser.value(userKey, -1);
        if (maxRow <= removedRow)
            continue;
        QList<int> &rows = it.value();
        for (int &storedRow : rows) {
            if (storedRow > removedRow)
                --storedRow;
        }
        maxRowByUser.insert(userKey, maxRow - 1);
    }
}

int ResourceQueueModelPrivate::registerEntry(const ResourceQueueEntry &entry, int row)
{
    registerRowForResource(entry.resourceUuid(), row);
    registerRowForUser(entry.user(), row);
    incrementUserResourceCount(entry.user(), entry.resourceUuid());
    return row;
}

void ResourceQueueModelPrivate::unregisterEntry(const ResourceQueueEntry &entry, int row)
{
    unregisterRowForResource(entry.resourceUuid(), row);
    unregisterRowForUser(entry.user(), row);
    decrementUserResourceCount(entry.user(), entry.resourceUuid());
    adjustRowsAfterRemoval(row);
}

ResourceQueueEntry ResourceQueueModelPrivate::takeEntryAtRow(int row)
{
    if (!q || row < 0 || row >= q->entryCount())
        return ResourceQueueEntry();

    ResourceQueueEntry removed;
    if (!q->removeEntryAt(row, &removed))
        return ResourceQueueEntry();

    unregisterEntry(removed, row);
    return removed;
}

bool ResourceQueueModelPrivate::resourceRowLess(int lhs, int rhs) const
{
    if (!q)
        return lhs < rhs;

    const int count = q->entryCount();
    if (lhs < 0 || lhs >= count || rhs < 0 || rhs >= count)
        return lhs < rhs;

    const ResourceQueueEntry &le = q->entryAt(lhs);
    const ResourceQueueEntry &re = q->entryAt(rhs);

    if (le.waitsForRelease() && re.waitsForRelease()) {
        if (le.enqueuedAtUtc() == re.enqueuedAtUtc())
            return lhs < rhs;
        return le.enqueuedAtUtc() < re.enqueuedAtUtc();
    }
    if (le.waitsForRelease())
        return false;
    if (re.waitsForRelease())
        return true;

    if (le.startAtUtc().isValid() && re.startAtUtc().isValid()) {
        if (le.startAtUtc() == re.startAtUtc())
            return lhs < rhs;
        return le.startAtUtc() < re.startAtUtc();
    }
    if (le.startAtUtc().isValid())
        return true;
    if (re.startAtUtc().isValid())
        return false;
    return lhs < rhs;
}

void ResourceQueueModelPrivate::repositionResourceRow(const QString &resourceUuid, int row)
{
    QList<int> &rows = rowsByResource[resourceUuid];
    rows.removeAll(row);

    QList<int>::iterator insertIt = rows.begin();
    for (; insertIt != rows.end(); ++insertIt) {
        if (!resourceRowLess(*insertIt, row))
            break;
    }
    rows.insert(insertIt, row);
}

int ResourceQueueModelPrivate::rowForEntryKey(const ResourceQueueKey &key) const
{
    return q ? q->indexOfKey(key) : -1;
}

void ResourceQueueModelPrivate::rebuildIndexes()
{
    rowsByResource.clear();
    rowsByUser.clear();
    maxRowByResource.clear();
    maxRowByUser.clear();
    resourceCountsByUser.clear();

    if (!q)
        return;

    const int count = q->entryCount();
    for (int i = 0; i < count; ++i)
        registerEntry(q->entryAt(i), i);
}

void ResourceQueueModelPrivate::incrementUserResourceCount(const MessengerUserKey &user,
                                                           const QString &resourceUuid)
{
    if (!user.isValid())
        return;
    const QString normalized = Resource::normalizeUuidString(resourceUuid);
    if (normalized.isEmpty())
        return;

    QHash<QString, int> &counts = resourceCountsByUser[user];
    counts[normalized] = counts.value(normalized) + 1;
}

void ResourceQueueModelPrivate::decrementUserResourceCount(const MessengerUserKey &user,
                                                           const QString &resourceUuid)
{
    if (!user.isValid())
        return;
    const QString normalized = Resource::normalizeUuidString(resourceUuid);
    if (normalized.isEmpty())
        return;

    QHash<MessengerUserKey, QHash<QString, int>>::iterator it = resourceCountsByUser.find(user);
    if (it == resourceCountsByUser.end())
        return;

    QHash<QString, int> &counts = it.value();
    const int next = counts.value(normalized) - 1;
    if (next > 0) {
        counts.insert(normalized, next);
    } else {
        counts.remove(normalized);
        if (counts.isEmpty())
            resourceCountsByUser.erase(it);
    }
}

void ResourceQueueModelPrivate::purgeExpiredEntries(const QDateTime &nowUtc,
                                                    QList<ResourceQueueEntry> *expiredOut)
{
    if (expiredOut)
        expiredOut->clear();

    if (!q)
        return;

    QList<ResourceQueueEntry> retainedEntries;
    QList<ResourceQueueEntry> expiredEntries;
    retainedEntries.reserve(q->entryCount());
    expiredEntries.reserve(q->entryCount());
    for (const ResourceQueueEntry &entry : std::as_const(q->entriesRef())) {
        if (entry.endAtUtc().isValid() && entry.endAtUtc() <= nowUtc) {
            expiredEntries.append(entry);
        } else {
            retainedEntries.append(entry);
        }
    }

    if (expiredEntries.isEmpty())
        return;

    q->replaceAllEntries(retainedEntries);
    rebuildIndexes();
    if (expiredOut)
        *expiredOut = expiredEntries;
}

bool ResourceQueueModelPrivate::validateAllowedDuration(int durationMinutes,
                                                        const QList<int> &allowedDurations,
                                                        QString *errorOut) const
{
    const int clamped = durationMinutes < 0 ? 0 : durationMinutes;
    if (!allowedDurations.isEmpty() && !allowedDurations.contains(clamped)) {
        setError(errorOut, ResourceQueueUserMessages::durationNotAllowed(clamped));
        return false;
    }
    return true;
}

bool ResourceQueueModelPrivate::validateMaxDuration(const QDateTime &startUtc,
                                                    const QDateTime &endUtc,
                                                    QString *errorOut) const
{
    if (maxOccupationDurationMinutes == 0)
        return true;

    if (!endUtc.isValid()) {
        setError(errorOut, ResourceQueueUserMessages::indefiniteOccupationNotAllowed());
        return false;
    }

    const qint64 maxSecs = static_cast<qint64>(maxOccupationDurationMinutes) * 60;
    const qint64 secs = startUtc.secsTo(endUtc);
    if (secs <= 0) {
        setError(errorOut, ResourceQueueUserMessages::endMustBeAfterStart());
        return false;
    }
    if (secs > maxSecs) {
        setError(errorOut,
                 ResourceQueueUserMessages::occupationDurationExceedsLimit(
                         maxOccupationDurationMinutes));
        return false;
    }
    return true;
}

bool ResourceQueueModelPrivate::validateMaxResourcesForUser(const MessengerUserKey &user,
                                                            const QString &resourceUuid,
                                                            QString *errorOut) const
{
    if (maxResourcesPerUser == 0)
        return true;

    const QHash<MessengerUserKey, QHash<QString, int>>::const_iterator it =
            resourceCountsByUser.constFind(user);
    const bool hasUser = it != resourceCountsByUser.cend();
    const bool alreadyHas = hasUser && it.value().contains(resourceUuid);
    const uint uniqueCount = hasUser ? static_cast<uint>(it.value().size()) : 0;

    if (!alreadyHas && uniqueCount >= maxResourcesPerUser) {
        setError(errorOut, ResourceQueueUserMessages::resourceLimitReached(maxResourcesPerUser));
        return false;
    }

    return true;
}

bool ResourceQueueModelPrivate::validateNoOverlapForResource(const QString &resourceUuid,
                                                             const QDateTime &startUtc,
                                                             const QDateTime &endUtc,
                                                             int ignoreRow,
                                                             const MessengerUserKey &viewer,
                                                             QString *errorOut) const
{
    const QList<int> rows = rowsByResource.value(resourceUuid);
    if (!startUtc.isValid() || rows.isEmpty())
        return true;

    const auto reportOverlap = [this, viewer, errorOut](const ResourceQueueEntry &existing) {
        const QString format = ServerDateTimeFormat::defaultLocalizedFormat();
        const UserTimeZone timeZone = timeZoneForUser(viewer);
        const QString startLocal = MessengerTextRender::formatLocalDateTime(existing.startAtUtc(),
                                                                            QString(),
                                                                            format,
                                                                            timeZone);
        const QString endLocal =
                existing.endAtUtc().isValid()
                        ? MessengerTextRender::formatLocalDateTime(existing.endAtUtc(),
                                                                   QString(),
                                                                   format,
                                                                   timeZone)
                        : QString();
        setError(errorOut, ResourceQueueUserMessages::scheduleOverlaps(startLocal, endLocal));
    };

    const auto rowStartLess = [this](int row, const QDateTime &value) {
        if (!q || row < 0 || row >= q->entryCount())
            return true;
        const ResourceQueueEntry &entry = q->entryAt(row);
        if (entry.waitsForRelease())
            return false;
        if (!entry.startAtUtc().isValid())
            return false;
        if (!value.isValid())
            return false;
        return entry.startAtUtc() < value;
    };

    const QList<int>::const_iterator it =
            std::lower_bound(rows.cbegin(), rows.cend(), startUtc, rowStartLess);

    if (it != rows.cbegin()) {
        const int prevRow = *(it - 1);
        if (prevRow != ignoreRow && q && prevRow >= 0 && prevRow < q->entryCount()) {
            const ResourceQueueEntry &existing = q->entryAt(prevRow);
            if (existing.waitsForRelease())
                return true;
            if (existing.startAtUtc().isValid()
                && ResourceQueueEntry::windowsOverlapUtc(startUtc,
                                                         endUtc,
                                                         existing.startAtUtc(),
                                                         existing.endAtUtc())) {
                reportOverlap(existing);
                return false;
            }
        }
    }

    for (QList<int>::const_iterator scan = it; scan != rows.cend(); ++scan) {
        const int row = *scan;
        if (row == ignoreRow)
            continue;
        if (!q || row < 0 || row >= q->entryCount())
            continue;
        const ResourceQueueEntry &existing = q->entryAt(row);
        if (existing.waitsForRelease())
            break;
        if (!existing.startAtUtc().isValid())
            break;
        if (endUtc.isValid() && existing.startAtUtc() >= endUtc)
            break;
        if (ResourceQueueEntry::windowsOverlapUtc(startUtc,
                                                  endUtc,
                                                  existing.startAtUtc(),
                                                  existing.endAtUtc())) {
            reportOverlap(existing);
            return false;
        }
    }
    return true;
}

bool ResourceQueueModelPrivate::hasAfterReleaseEntriesForResource(const QString &resourceUuid,
                                                                  int ignoreRow) const
{
    const QList<int> rows = rowsByResource.value(resourceUuid);
    for (int row : rows) {
        if (row == ignoreRow)
            continue;
        if (!q || row < 0 || row >= q->entryCount())
            continue;
        if (q->entryAt(row).waitsForRelease())
            return true;
    }
    return false;
}

bool ResourceQueueModelPrivate::hasFutureAtTimeEntriesForResource(const QString &resourceUuid,
                                                                  const QDateTime &nowUtc,
                                                                  int ignoreRow) const
{
    const QList<int> rows = rowsByResource.value(resourceUuid);
    for (int row : rows) {
        if (row == ignoreRow)
            continue;
        if (!q || row < 0 || row >= q->entryCount())
            continue;
        const ResourceQueueEntry &entry = q->entryAt(row);
        if (entry.waitsForRelease() || !entry.startAtUtc().isValid())
            continue;
        if (entry.startAtUtc() > nowUtc)
            return true;
    }
    return false;
}

bool ResourceQueueModelPrivate::hasAfterReleaseEntryForUser(const QString &resourceUuid,
                                                            const MessengerUserKey &user,
                                                            int ignoreRow) const
{
    const QList<int> rows = rowsByResource.value(resourceUuid);
    for (int row : rows) {
        if (row == ignoreRow)
            continue;
        if (!q || row < 0 || row >= q->entryCount())
            continue;
        const ResourceQueueEntry &entry = q->entryAt(row);
        if (entry.waitsForRelease() && entry.user() == user)
            return true;
    }
    return false;
}

int ResourceQueueModelPrivate::firstAfterReleaseRowForResource(const QString &resourceUuid) const
{
    const QList<int> rows = rowsByResource.value(resourceUuid);
    int bestRow = -1;
    QDateTime bestEnqueuedAt;
    for (int row : rows) {
        if (!q || row < 0 || row >= q->entryCount())
            continue;
        const ResourceQueueEntry &entry = q->entryAt(row);
        if (!entry.waitsForRelease())
            continue;
        if (bestRow < 0 || entry.enqueuedAtUtc() < bestEnqueuedAt) {
            bestRow = row;
            bestEnqueuedAt = entry.enqueuedAtUtc();
        }
    }
    return bestRow;
}

QSet<ResourceQueueKey> ResourceQueueModelPrivate::promoteAfterReleaseEntries(
        const QSet<QString> &resources, const QDateTime &nowUtc)
{
    QSet<ResourceQueueKey> promotedKeys;
    if (!q || !nowUtc.isValid())
        return promotedKeys;

    for (const QString &resourceUuid : resources) {
        if (activeEntryForResourceUuid(resourceUuid, nowUtc).isValid())
            continue;

        const int row = firstAfterReleaseRowForResource(resourceUuid);
        if (row < 0 || row >= q->entryCount())
            continue;

        ResourceQueueEntry waiting = q->entryAt(row);
        const ResourceQueueKey oldKey = waiting.key();
        waiting = takeEntryAtRow(row);
        if (!waiting.isValid())
            continue;

        const int durationMinutes = durationMinutesForEntry(waiting);
        ResourceQueueEntry promoted = waiting;
        promoted.setStartMode(ResourceQueueEntry::StartMode::AtTime);
        promoted.setEnqueuedAtUtc(nowUtc);
        promoted.setStartAtUtc(nowUtc);
        promoted.setEndAtUtc(durationMinutes > 0
                                     ? nowUtc.addSecs(static_cast<qint64>(durationMinutes) * 60)
                                     : QDateTime());
        promoted.setRequestedDurationMinutes(durationMinutes);

        const int insertRow = q->entryCount();
        q->insertEntry(insertRow, promoted);
        registerEntry(promoted, insertRow);

        q->persistRemove(oldKey);
        q->persistUpsert(promoted);
        promotedKeys.insert(promoted.key());
    }

    return promotedKeys;
}

bool ResourceQueueModelPrivate::occupyInternal(const QString &resourceName,
                                               const MessengerUserKey &user,
                                               int durationMinutes,
                                               const QList<int> &allowedDurations,
                                               const MessengerUserKey &actor,
                                               QString *errorOut,
                                               ResourceQueueEntry *storedOut)
{
    QString resolvedName;
    const QString resourceUuid = resolveResourceUuid(resourceName, &resolvedName);
    const QString displayName = resolvedName.isEmpty() ? resourceName.trimmed() : resolvedName;
    const bool forSelf = isSelfAction(actor, user);
    const QString displayUser = userDisplayName(user);
    if (resourceUuid.isEmpty()) {
        setError(errorOut, ResourceQueueUserMessages::resourceNotFound(displayName));
        return false;
    }

    if (resourceModel) {
        const Resource resource = resourceModel->resourceByUuid(resourceUuid);
        if (resource.isValid()) {
            if (resource.status() == Resource::Status::Off) {
                setError(errorOut, ResourceQueueUserMessages::resourceNotFound(displayName));
                return false;
            }
            if (resource.status() == Resource::Status::TemporarilyUnavailable) {
                setError(errorOut,
                         ResourceQueueUserMessages::resourceTemporarilyUnavailable(displayName));
                return false;
            }
        }
    }

    if (!user.isValid() || !isValidUser(user)) {
        setError(errorOut, ResourceQueueUserMessages::accessNotActivated(forSelf, displayUser));
        return false;
    }

    if (!validateAllowedDuration(durationMinutes, allowedDurations, errorOut))
        return false;

    refreshOccupations(false); // purge expired entries before validation

    const QDateTime nowUtc = utcNowSeconds();
    const QDateTime start = nowUtc;
    const int clamped = durationMinutes < 0 ? 0 : durationMinutes;
    const QDateTime end =
            (clamped > 0) ? start.addSecs(static_cast<qint64>(clamped) * 60) : QDateTime();
    const ResourceQueueEntry occupant = activeEntryForResourceUuid(resourceUuid, nowUtc);
    const bool waitForRelease = occupant.isValid();

    if (!validateMaxDuration(start, end, errorOut))
        return false;

    if (!validateMaxResourcesForUser(user, resourceUuid, errorOut))
        return false;

    if (waitForRelease) {
        if (occupant.user() == user) {
            setError(errorOut,
                     ResourceQueueUserMessages::alreadyOccupying(forSelf,
                                                                 displayUser,
                                                                 displayName));
            return false;
        }
        if (hasFutureAtTimeEntriesForResource(resourceUuid, nowUtc)) {
            setError(errorOut, ResourceQueueUserMessages::hasScheduledReservations(displayName));
            return false;
        }
        if (hasAfterReleaseEntryForUser(resourceUuid, user)) {
            setError(errorOut,
                     ResourceQueueUserMessages::alreadyWaitingForRelease(forSelf,
                                                                         displayUser,
                                                                         displayName));
            return false;
        }
    }

    const ResourceQueueKey key = entryKey(resourceUuid, user, start);
    const int existingRow = rowForEntryKey(key);

    if (!waitForRelease
        && !validateNoOverlapForResource(resourceUuid, start, end, existingRow, actor, errorOut)) {
        return false;
    }

    ResourceQueueEntry entry;
    entry.setResourceUuid(resourceUuid);
    entry.setUser(user);
    entry.setEnqueuedAtUtc(nowUtc);
    entry.setStartAtUtc(start);
    entry.setStartMode(waitForRelease ? ResourceQueueEntry::StartMode::AfterRelease
                                      : ResourceQueueEntry::StartMode::AtTime);
    entry.setRequestedDurationMinutes(clamped);
    entry.setEndAtUtc(waitForRelease ? QDateTime() : end);

    if (existingRow >= 0 && q && existingRow < q->entryCount()) {
        q->mutableEntryAt(existingRow) = entry;
        const QModelIndex modelIndex = q->index(existingRow, 0);
        emit q->dataChanged(modelIndex, modelIndex);
    } else {
        const int row = q ? q->entryCount() : 0;
        if (q)
            q->insertEntry(row, entry);
        registerEntry(entry, row);
    }

    if (storedOut)
        *storedOut = entry;
    return true;
}

bool ResourceQueueModelPrivate::scheduleInternal(const QString &resourceName,
                                                 const MessengerUserKey &user,
                                                 const QDateTime &startUtc,
                                                 const QDateTime &endUtc,
                                                 bool requireStartNotPast,
                                                 const MessengerUserKey &actor,
                                                 QString *errorOut,
                                                 ResourceQueueEntry *storedOut)
{
    QString resolvedName;
    const QString resourceUuid = resolveResourceUuid(resourceName, &resolvedName);
    const QString displayName = resolvedName.isEmpty() ? resourceName.trimmed() : resolvedName;
    const bool forSelf = isSelfAction(actor, user);
    const QString displayUser = userDisplayName(user);
    if (resourceUuid.isEmpty()) {
        setError(errorOut, ResourceQueueUserMessages::resourceNotFound(displayName));
        return false;
    }

    if (resourceModel) {
        const Resource resource = resourceModel->resourceByUuid(resourceUuid);
        if (resource.isValid()) {
            if (resource.status() == Resource::Status::Off) {
                setError(errorOut, ResourceQueueUserMessages::resourceNotFound(displayName));
                return false;
            }
            if (resource.status() == Resource::Status::TemporarilyUnavailable) {
                setError(errorOut,
                         ResourceQueueUserMessages::resourceTemporarilyUnavailable(displayName));
                return false;
            }
        }
    }

    if (!user.isValid() || !isValidUser(user)) {
        setError(errorOut, ResourceQueueUserMessages::accessNotActivated(forSelf, displayUser));
        return false;
    }

    if (!startUtc.isValid()) {
        setError(errorOut, ResourceQueueUserMessages::invalidScheduleStart());
        return false;
    }

    refreshOccupations(false); // purge expired entries before validation

    const QDateTime nowUtc = utcNowSeconds();
    const QDateTime start = normalizeUtcSeconds(startUtc);
    const QDateTime end = endUtc.isValid() ? normalizeUtcSeconds(endUtc) : QDateTime();

    if (requireStartNotPast && start < nowUtc) {
        setError(errorOut, ResourceQueueUserMessages::scheduleStartInPast());
        return false;
    }

    if (end.isValid() && start >= end) {
        setError(errorOut, ResourceQueueUserMessages::endMustBeAfterStart());
        return false;
    }

    if (!validateMaxDuration(start, end, errorOut))
        return false;

    if (!validateMaxResourcesForUser(user, resourceUuid, errorOut))
        return false;

    const ResourceQueueKey key = entryKey(resourceUuid, user, start);
    const int existingRow = rowForEntryKey(key);

    if (hasAfterReleaseEntriesForResource(resourceUuid, existingRow)) {
        setError(errorOut, ResourceQueueUserMessages::cannotScheduleWhileWaiting(displayName));
        return false;
    }

    if (!validateNoOverlapForResource(resourceUuid, start, end, existingRow, actor, errorOut))
        return false;

    ResourceQueueEntry entry;
    entry.setResourceUuid(resourceUuid);
    entry.setUser(user);
    entry.setEnqueuedAtUtc(nowUtc);
    entry.setStartAtUtc(start);
    entry.setEndAtUtc(end);
    entry.setStartMode(ResourceQueueEntry::StartMode::AtTime);
    if (end.isValid())
        entry.setRequestedDurationMinutes(static_cast<int>(start.secsTo(end) / 60));
    else
        entry.setRequestedDurationMinutes(0);

    if (existingRow >= 0 && q && existingRow < q->entryCount()) {
        q->mutableEntryAt(existingRow) = entry;
        const QModelIndex modelIndex = q->index(existingRow, 0);
        emit q->dataChanged(modelIndex, modelIndex);
        repositionResourceRow(resourceUuid, existingRow);
    } else {
        const int row = q ? q->entryCount() : 0;
        if (q)
            q->insertEntry(row, entry);
        registerEntry(entry, row);
    }

    if (storedOut)
        *storedOut = entry;
    return true;
}

bool ResourceQueueModelPrivate::removeUserSchedulesInternal(const QString &resourceName,
                                                            const MessengerUserKey &user,
                                                            QString *errorOut,
                                                            QList<ResourceQueueEntry> *removedOut)
{
    QString resolvedName;
    const QString resourceUuid = resolveResourceUuid(resourceName, &resolvedName);
    const QString displayName = resolvedName.isEmpty() ? resourceName.trimmed() : resolvedName;
    if (resourceUuid.isEmpty()) {
        setError(errorOut, ResourceQueueUserMessages::resourceNotFound(displayName));
        return false;
    }

    if (!user.isValid()) {
        setError(errorOut, ResourceQueueUserMessages::userNotSpecified(userDisplayName(user)));
        return false;
    }

    if (removedOut)
        removedOut->clear();

    refreshOccupations(false); // purge expired entries before removal

    const QList<int> rows = rowsByUser.value(user);
    if (rows.isEmpty()) {
        setError(errorOut, ResourceQueueUserMessages::noSchedulesFound(displayName));
        return false;
    }

    QVector<int> rowsToRemove;
    rowsToRemove.reserve(rows.size());
    for (int row : rows) {
        if (!q || row < 0 || row >= q->entryCount())
            continue;
        const ResourceQueueEntry &entry = q->entryAt(row);
        if (entry.resourceUuid() == resourceUuid) {
            rowsToRemove.append(row);
        }
    }

    if (rowsToRemove.isEmpty()) {
        setError(errorOut, ResourceQueueUserMessages::noSchedulesFound(displayName));
        return false;
    }

    std::sort(rowsToRemove.begin(), rowsToRemove.end());
    for (QVector<int>::const_reverse_iterator it = rowsToRemove.crbegin();
         it != rowsToRemove.crend();
         ++it) {
        const ResourceQueueEntry removed = takeEntryAtRow(*it);
        if (removed.isValid() && removedOut)
            removedOut->append(removed);
    }

    return true;
}

ResourceQueueEntry ResourceQueueModelPrivate::activeEntryForResourceUuid(
        const QString &resourceUuid, const QDateTime &nowUtc) const
{
    const QList<int> rows = rowsByResource.value(resourceUuid);
    for (int row : rows) {
        if (!q || row < 0 || row >= q->entryCount())
            continue;
        const ResourceQueueEntry &entry = q->entryAt(row);
        if (!entry.startAtUtc().isValid())
            continue;

        // rows are sorted by startAtUtc, so we can stop once we're in the future.
        if (entry.startAtUtc() > nowUtc)
            break;

        if (entry.isActiveAtUtc(nowUtc))
            return entry;
    }
    return ResourceQueueEntry();
}

QDateTime ResourceQueueModelPrivate::computeHandoverUtcForActiveEntry(
        const ResourceQueueEntry &entry) const
{
    if (!entry.isValid())
        return QDateTime();
    return entry.endAtUtc();
}

bool ResourceQueueModelPrivate::releaseUserOccupationInternal(const QString &resourceName,
                                                              const MessengerUserKey &user,
                                                              const MessengerUserKey &actor,
                                                              QString *errorOut,
                                                              ResourceQueueEntry *removedOut)
{
    QString resolvedName;
    const QString resourceUuid = resolveResourceUuid(resourceName, &resolvedName);
    const QString displayName = resolvedName.isEmpty() ? resourceName.trimmed() : resolvedName;
    if (resourceUuid.isEmpty()) {
        setError(errorOut, ResourceQueueUserMessages::resourceNotFound(displayName));
        return false;
    }

    if (!user.isValid()) {
        setError(errorOut, ResourceQueueUserMessages::userNotSpecified(userDisplayName(user)));
        return false;
    }

    refreshOccupations(false); // purge expired entries before release

    const QDateTime nowUtc = utcNowSeconds();
    const ResourceQueueEntry occupant = activeEntryForResourceUuid(resourceUuid, nowUtc);
    if (!occupant.isValid()) {
        setError(errorOut, ResourceQueueUserMessages::noCurrentOccupant(displayName));
        return false;
    }
    if (occupant.user() != user) {
        setError(errorOut,
                 ResourceQueueUserMessages::notOccupying(isSelfAction(actor, user),
                                                         userDisplayName(user),
                                                         displayName));
        return false;
    }

    const int row = rowForEntryKey(occupant.key());
    if (!q || row < 0 || row >= q->entryCount()) {
        setError(errorOut, ResourceQueueUserMessages::failedToLocateActiveEntry());
        return false;
    }

    const ResourceQueueEntry removed = takeEntryAtRow(row);
    if (removedOut)
        *removedOut = removed;

    return true;
}

bool ResourceQueueModelPrivate::removeResourceEntriesInternal(const QString &resourceName,
                                                              QString *errorOut,
                                                              QList<ResourceQueueEntry> *removedOut)
{
    if (removedOut)
        removedOut->clear();

    QString resolvedName;
    const QString resourceUuid = resolveResourceUuid(resourceName, &resolvedName);
    const QString displayName = resolvedName.isEmpty() ? resourceName.trimmed() : resolvedName;
    if (resourceUuid.isEmpty()) {
        setError(errorOut, ResourceQueueUserMessages::resourceNotFound(displayName));
        return false;
    }

    refreshOccupations(false); // purge expired entries before removal

    const QList<int> rows = rowsByResource.value(resourceUuid);
    QVector<int> rowsToRemove;
    rowsToRemove.reserve(rows.size());
    for (int row : rows) {
        rowsToRemove.append(row);
    }
    if (rowsToRemove.isEmpty())
        return true;

    std::sort(rowsToRemove.begin(), rowsToRemove.end());
    for (QVector<int>::const_reverse_iterator it = rowsToRemove.crbegin();
         it != rowsToRemove.crend();
         ++it) {
        const ResourceQueueEntry removed = takeEntryAtRow(*it);
        if (removed.isValid() && removedOut)
            removedOut->append(removed);
    }

    return true;
}

int ResourceQueueModelPrivate::removeUserEntriesInternal(const MessengerUserKey &user,
                                                         QList<ResourceQueueEntry> *removedOut)
{
    if (removedOut)
        removedOut->clear();
    if (!user.isValid())
        return 0;

    const QList<int> rows = rowsByUser.value(user);
    if (rows.isEmpty())
        return 0;

    QList<int> rowsToRemove = rows;
    std::sort(rowsToRemove.begin(), rowsToRemove.end());

    int removedCount = 0;
    for (auto it = rowsToRemove.crbegin(); it != rowsToRemove.crend(); ++it) {
        const int row = *it;
        const ResourceQueueEntry entry = takeEntryAtRow(row);
        if (!entry.isValid())
            continue;
        ++removedCount;
        if (removedOut)
            removedOut->append(entry);
    }

    return removedCount;
}

void ResourceQueueModelPrivate::refreshOccupations(bool notifyTransitions,
                                                   const QSet<QString> &resourcesHint)
{
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    const QHash<QString, ResourceQueueKey> previousActive = activeKeyByResource;

    // Remove expired finite entries.
    QList<ResourceQueueEntry> expired;
    purgeExpiredEntries(nowUtc, &expired);
    if (q && !expired.isEmpty()) {
        QList<ResourceQueueKey> expiredKeys;
        expiredKeys.reserve(expired.size());
        for (const ResourceQueueEntry &entry : std::as_const(expired))
            expiredKeys.append(entry.key());

        bool persisted = true;
        if (removeEntries) {
            persisted = removeEntries(expiredKeys);
        } else {
            for (const ResourceQueueKey &key : std::as_const(expiredKeys)) {
                if (!q->persistRemove(key))
                    persisted = false;
            }
        }
        if (!persisted)
            emit q->errorOccurred(
                    QStringLiteral("Failed to persist expired resource queue entries."));
    }

    QSet<QString> resourcesToUpdate = resourcesHint;
    for (const ResourceQueueEntry &entry : std::as_const(expired))
        resourcesToUpdate.insert(entry.resourceUuid());

    if (resourcesToUpdate.isEmpty()) {
        for (QHash<QString, QList<int>>::const_iterator it = rowsByResource.cbegin();
             it != rowsByResource.cend();
             ++it) {
            resourcesToUpdate.insert(it.key());
        }
        for (QHash<QString, ResourceQueueKey>::const_iterator it = previousActive.cbegin();
             it != previousActive.cend();
             ++it) {
            resourcesToUpdate.insert(it.key());
        }
    }

    const QSet<ResourceQueueKey> afterReleasePromoted =
            promoteAfterReleaseEntries(resourcesToUpdate, nowUtc);

    for (const QString &resourceUuid : std::as_const(resourcesToUpdate)) {
        const ResourceQueueEntry occupant = activeEntryForResourceUuid(resourceUuid, nowUtc);
        if (occupant.isValid()) {
            activeKeyByResource.insert(resourceUuid, occupant.key());
        } else {
            activeKeyByResource.remove(resourceUuid);
        }
    }

    scheduleNextTimer();

    if (!notifyTransitions)
        return;

    QSet<QString> allResources = resourcesToUpdate;
    for (QHash<QString, ResourceQueueKey>::const_iterator it = previousActive.cbegin();
         it != previousActive.cend();
         ++it) {
        allResources.insert(it.key());
    }
    for (QHash<QString, ResourceQueueKey>::const_iterator it = activeKeyByResource.cbegin();
         it != activeKeyByResource.cend();
         ++it) {
        allResources.insert(it.key());
    }

    for (const QString &resourceUuid : std::as_const(allResources)) {
        const bool hasPrev = previousActive.contains(resourceUuid);
        const bool hasNext = activeKeyByResource.contains(resourceUuid);
        const ResourceQueueKey prevKey = previousActive.value(resourceUuid);
        const ResourceQueueKey nextKey = activeKeyByResource.value(resourceUuid);
        const QString displayName = resourceNameForUuid(resourceUuid);

        if (hasPrev && (!hasNext || prevKey != nextKey)) {
            const int prevRow = rowForEntryKey(prevKey);
            if (q && prevRow >= 0 && prevRow < q->entryCount()) {
                const ResourceQueueEntry prevEntry = q->entryAt(prevRow);
                emit q->occupationEnded(displayName,
                                        prevEntry.user(),
                                        true,
                                        QStringLiteral("ended"),
                                        nowUtc);
            } else {
                emit q->occupationEnded(displayName,
                                        prevKey.user(),
                                        true,
                                        QStringLiteral("ended"),
                                        nowUtc);
            }
        }

        if (hasNext && (!hasPrev || prevKey != nextKey)) {
            const int nextRow = rowForEntryKey(nextKey);
            if (!q || nextRow < 0 || nextRow >= q->entryCount())
                continue;
            const ResourceQueueEntry nextEntry = q->entryAt(nextRow);
            const QDateTime handoverUtc = computeHandoverUtcForActiveEntry(nextEntry);
            const bool scheduled = !afterReleasePromoted.contains(nextEntry.key());
            emit q->occupationStarted(displayName, nextEntry.user(), scheduled, handoverUtc);
        }
    }
}

void ResourceQueueModelPrivate::scheduleNextTimer()
{
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    QDateTime next;
    nextScheduleResources.clear();

    if (!q) {
        scheduleTimer.stop();
        return;
    }

    for (const ResourceQueueEntry &entry : std::as_const(q->entriesRef())) {
        if (!entry.startAtUtc().isValid())
            continue;

        if (nowUtc < entry.startAtUtc()) {
            if (!next.isValid() || entry.startAtUtc() < next) {
                next = entry.startAtUtc();
                nextScheduleResources.clear();
                nextScheduleResources.insert(entry.resourceUuid());
            } else if (entry.startAtUtc() == next) {
                nextScheduleResources.insert(entry.resourceUuid());
            }
        }

        if (entry.endAtUtc().isValid() && nowUtc < entry.endAtUtc()) {
            if (!next.isValid() || entry.endAtUtc() < next) {
                next = entry.endAtUtc();
                nextScheduleResources.clear();
                nextScheduleResources.insert(entry.resourceUuid());
            } else if (entry.endAtUtc() == next) {
                nextScheduleResources.insert(entry.resourceUuid());
            }
        }
    }

    if (!next.isValid()) {
        scheduleTimer.stop();
        nextScheduleResources.clear();
        return;
    }

    const qint64 ms = std::max<qint64>(0, nowUtc.msecsTo(next));
    const int clamped = (ms > std::numeric_limits<int>::max()) ? std::numeric_limits<int>::max()
                                                               : static_cast<int>(ms);
    scheduleTimer.start(clamped);
}

void ResourceQueueModelPrivate::onScheduleTimer()
{
    refreshOccupations(true, nextScheduleResources);
}

ResourceQueueModel::ResourceQueueModel(UserModel *userModel,
                                       ResourceModel *resourceModel,
                                       QObject *parent)
    : Base(InsertPolicy::PreserveInsertionOrder, parent)
    , d(new ResourceQueueModelPrivate(this, userModel, resourceModel))
{
    // do nothing
}

ResourceQueueModel::~ResourceQueueModel()
{
    delete d;
}

void ResourceQueueModel::setStorageAdapter(StorageAdapter adapter)
{
    BaseStorageAdapter baseAdapter;
    baseAdapter.upsert = std::move(adapter.upsert);
    baseAdapter.remove = std::move(adapter.remove);
    Base::setStorageAdapter(std::move(baseAdapter));
    d->removeEntries = std::move(adapter.removeEntries);
    d->removeUserEntries = std::move(adapter.removeUserEntries);
}

QVariant ResourceQueueModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= entryCount())
        return QVariant();

    const ResourceQueueEntry &entry = entryAt(index.row());
    switch (role) {
    case ResourceNameRole:
        return d ? d->resourceNameForUuid(entry.resourceUuid()) : QString();
    case MessengerRole:
        return messengerTypeToString(entry.user().messenger());
    case UserIdRole:
        return entry.user().userId();
    case DurationMinutesRole: {
        if (entry.waitsForRelease())
            return d ? d->durationMinutesForEntry(entry) : 0;
        const QDateTime startUtc = entry.startAtUtc();
        const QDateTime endUtc = entry.endAtUtc();
        if (!startUtc.isValid())
            return QVariant();
        if (!endUtc.isValid())
            return -1;
        const qint64 secs = startUtc.secsTo(endUtc);
        if (secs <= 0)
            return 0;
        const qint64 minutes = secs / 60;
        return minutes;
    }
    case EnqueuedAtRole:
        return entry.enqueuedAtUtc();
    case StartAtRole:
        return entry.startAtUtc();
    case EndAtRole:
        return entry.endAtUtc();
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> ResourceQueueModel::roleNames() const
{
    return {
        { ResourceNameRole, QByteArrayLiteral("resourceName") },
        { MessengerRole, QByteArrayLiteral("messenger") },
        { UserIdRole, QByteArrayLiteral("userId") },
        { DurationMinutesRole, QByteArrayLiteral("durationMinutes") },
        { EnqueuedAtRole, QByteArrayLiteral("enqueuedAtUtc") },
        { StartAtRole, QByteArrayLiteral("startAtUtc") },
        { EndAtRole, QByteArrayLiteral("endAtUtc") },
    };
}

void ResourceQueueModel::replaceAll(const QList<ResourceQueueEntry> &entries)
{
    replaceAllEntries(entries);
    d->rebuildIndexes();
    d->lastReleaseByResource.clear();
    d->refreshOccupations(false);
}

const QList<ResourceQueueEntry> &ResourceQueueModel::items() const
{
    return entriesRef();
}

QList<ResourceQueueEntry> ResourceQueueModel::queueForResource(const QString &resourceName) const
{
    return schedulesForResource(resourceName);
}

QList<ResourceQueueEntry> ResourceQueueModel::schedulesForResource(
        const QString &resourceName) const
{
    QList<ResourceQueueEntry> entries;
    const QString resourceUuid = d->resolveResourceUuid(resourceName);
    if (resourceUuid.isEmpty())
        return entries;

    const QList<int> rows = d->rowsByResource.value(resourceUuid);
    entries.reserve(rows.size());
    for (int row : rows) {
        if (row < 0 || row >= entryCount())
            continue;
        entries.append(entryAt(row));
    }

    return entries;
}

ResourceQueueEntry ResourceQueueModel::currentOccupantForResource(const QString &resourceName) const
{
    const QString resourceUuid = d->resolveResourceUuid(resourceName);
    if (resourceUuid.isEmpty())
        return ResourceQueueEntry();

    const QHash<QString, ResourceQueueKey>::const_iterator it =
            d->activeKeyByResource.constFind(resourceUuid);
    if (it != d->activeKeyByResource.cend()) {
        const int row = d->rowForEntryKey(it.value());
        if (row >= 0 && row < entryCount())
            return entryAt(row);
    }

    // Fallback if active cache is stale.
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    return d->activeEntryForResourceUuid(resourceUuid, nowUtc);
}

QDateTime ResourceQueueModel::nextScheduleStartUtc(const QString &resourceName,
                                                   const QDateTime &afterUtc) const
{
    const QString resourceUuid = d->resolveResourceUuid(resourceName);
    if (resourceUuid.isEmpty())
        return QDateTime();

    const QDateTime after = afterUtc.isValid() ? afterUtc : QDateTime::currentDateTimeUtc();
    QDateTime best;

    const QList<int> rows = d->rowsByResource.value(resourceUuid);
    for (int row : rows) {
        if (row < 0 || row >= entryCount())
            continue;
        const ResourceQueueEntry &entry = entryAt(row);
        if (!entry.startAtUtc().isValid())
            continue;
        if (entry.startAtUtc() <= after)
            continue;
        if (!best.isValid() || entry.startAtUtc() < best)
            best = entry.startAtUtc();
    }

    return best;
}

QList<ResourceQueueEntry> ResourceQueueModel::soonestReleasingResources(int limit) const
{
    QList<ResourceQueueEntry> ranked;
    if (limit <= 0)
        return ranked;

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();

    struct Candidate
    {
        ResourceQueueEntry entry;
        qint64 releaseEpoch = std::numeric_limits<qint64>::max();
    };

    QList<Candidate> candidates;
    candidates.reserve(d->rowsByResource.size());

    for (QHash<QString, QList<int>>::const_iterator it = d->rowsByResource.cbegin();
         it != d->rowsByResource.cend();
         ++it) {
        const QString resourceUuid = it.key();
        const ResourceQueueEntry occupant = d->activeEntryForResourceUuid(resourceUuid, nowUtc);
        if (!occupant.isValid())
            continue;
        if (!occupant.endAtUtc().isValid())
            continue; // Indefinite occupation: no predictable release.

        Candidate c;
        c.entry = occupant;
        c.releaseEpoch = occupant.endAtUtc().toSecsSinceEpoch();
        candidates.append(c);
    }

    const auto comparator = [](const Candidate &lhs, const Candidate &rhs) {
        return lhs.releaseEpoch < rhs.releaseEpoch;
    };

    if (candidates.size() <= limit) {
        std::sort(candidates.begin(), candidates.end(), comparator);
        ranked.reserve(candidates.size());
        for (const Candidate &c : candidates)
            ranked.append(c.entry);
        return ranked;
    }

    const QList<Candidate>::iterator middle = candidates.begin() + limit;
    std::nth_element(candidates.begin(), middle, candidates.end(), comparator);
    std::sort(candidates.begin(), middle, comparator);
    ranked.reserve(limit);
    for (QList<Candidate>::const_iterator it = candidates.cbegin(); it != middle; ++it) {
        ranked.append(it->entry);
    }
    return ranked;
}

QList<QString> ResourceQueueModel::freeResourceNames() const
{
    QList<QString> freeResources;
    if (!d->resourceModel)
        return freeResources;

    const QList<Resource> &resources = d->resourceModel->items();
    freeResources.reserve(resources.size());
    for (const Resource &resource : resources) {
        if (resource.status() != Resource::Status::On)
            continue;
        if (!currentOccupantForResource(resource.name()).isValid()) {
            freeResources.append(resource.name());
        }
    }

    return freeResources;
}

uint ResourceQueueModel::maxResourcesPerUser() const
{
    return d->maxResourcesPerUser;
}

uint ResourceQueueModel::maxOccupationDurationMinutes() const
{
    return d->maxOccupationDurationMinutes;
}

void ResourceQueueModel::setMaxResourcesPerUser(uint limit)
{
    d->maxResourcesPerUser = limit;
}

void ResourceQueueModel::setMaxOccupationDurationMinutes(uint limit)
{
    d->maxOccupationDurationMinutes = limit;
}

bool ResourceQueueModel::occupyResourceUser(const QString &resourceName,
                                            const MessengerUserKey &user,
                                            int durationMinutes,
                                            const QList<int> &allowedDurations,
                                            const MessengerUserKey &actor,
                                            QString *errorOut)
{
    const QString resourceUuid = d->resolveResourceUuid(resourceName);
    const bool queueWasEmpty =
            resourceUuid.isEmpty() ? true : d->rowsByResource.value(resourceUuid).isEmpty();

    ResourceQueueEntry entry;
    if (!d->occupyInternal(
                resourceName, user, durationMinutes, allowedDurations, actor, errorOut, &entry))
        return false;

    if (!persistUpsert(entry)) {
        setError(errorOut,
                 ResourceQueueUserMessages::failedToPersistScheduleEntry(
                         d->resourceNameForUuid(entry.resourceUuid())));
        return false;
    }

    QSet<QString> resources;
    resources.insert(entry.resourceUuid());
    d->refreshOccupations(false, resources);

    const ResourceQueueEntry occupant =
            d->activeEntryForResourceUuid(entry.resourceUuid(), QDateTime::currentDateTimeUtc());
    if (!occupant.isValid())
        return true;

    if (queueWasEmpty) {
        d->lastReleaseByResource.remove(resourceUuid);
        emit occupationTransition(d->resourceNameForUuid(occupant.resourceUuid()),
                                  MessengerUserKey(),
                                  occupant.user(),
                                  actor,
                                  d->durationMinutesForEntry(occupant),
                                  false);
        return true;
    }

    const MessengerUserKey releasedBy = d->lastReleaseByResource.value(resourceUuid);
    if (releasedBy.isValid()) {
        d->lastReleaseByResource.remove(resourceUuid);
        emit occupationTransition(d->resourceNameForUuid(occupant.resourceUuid()),
                                  releasedBy,
                                  occupant.user(),
                                  actor,
                                  d->durationMinutesForEntry(occupant),
                                  false);
    }
    return true;
}

bool ResourceQueueModel::scheduleResourceUser(const QString &resourceName,
                                              const MessengerUserKey &user,
                                              const QDateTime &startUtc,
                                              const QDateTime &endUtc,
                                              const MessengerUserKey &actor,
                                              QString *errorOut)
{
    ResourceQueueEntry entry;
    if (!d->scheduleInternal(resourceName, user, startUtc, endUtc, true, actor, errorOut, &entry)) {
        return false;
    }

    if (!persistUpsert(entry)) {
        setError(errorOut,
                 ResourceQueueUserMessages::failedToPersistScheduleEntry(
                         d->resourceNameForUuid(entry.resourceUuid())));
        return false;
    }

    QSet<QString> resources;
    resources.insert(entry.resourceUuid());
    d->refreshOccupations(false, resources);
    return true;
}

bool ResourceQueueModel::removeUserSchedulesForResource(const QString &resourceName,
                                                        const MessengerUserKey &user,
                                                        QString *errorOut,
                                                        int *removedCountOut)
{
    QList<ResourceQueueEntry> removed;
    if (!d->removeUserSchedulesInternal(resourceName, user, errorOut, &removed))
        return false;

    if (removedCountOut)
        *removedCountOut = removed.size();

    bool persisted = true;
    for (const ResourceQueueEntry &entry : std::as_const(removed)) {
        if (!persistRemove(entry.key())) {
            persisted = false;
            break;
        }
    }
    if (!persisted) {
        setError(errorOut,
                 ResourceQueueUserMessages::failedToPersistScheduleRemoval(resourceName.trimmed()));
    }

    QSet<QString> resources;
    for (const ResourceQueueEntry &entry : std::as_const(removed)) {
        resources.insert(entry.resourceUuid());
    }
    d->refreshOccupations(false, resources);
    return persisted;
}

bool ResourceQueueModel::releaseUserOccupationForResource(const QString &resourceName,
                                                          const MessengerUserKey &user,
                                                          const MessengerUserKey &actor,
                                                          QString *errorOut)
{
    const QString resourceUuid = d->resolveResourceUuid(resourceName);

    ResourceQueueEntry removed;
    if (!d->releaseUserOccupationInternal(resourceName, user, actor, errorOut, &removed))
        return false;

    if (removed.isValid() && !persistRemove(removed.key())) {
        setError(errorOut,
                 ResourceQueueUserMessages::failedToPersistScheduleRemoval(
                         d->resourceNameForUuid(removed.resourceUuid())));
        return false;
    }

    QSet<QString> resources;
    resources.insert(removed.resourceUuid());
    d->refreshOccupations(false, resources);

    if (!removed.isValid())
        return true;

    const ResourceQueueEntry nextOccupant =
            d->activeEntryForResourceUuid(removed.resourceUuid(), QDateTime::currentDateTimeUtc());
    if (nextOccupant.isValid()) {
        d->lastReleaseByResource.remove(resourceUuid);
        emit occupationTransition(d->resourceNameForUuid(nextOccupant.resourceUuid()),
                                  removed.user(),
                                  nextOccupant.user(),
                                  actor,
                                  d->durationMinutesForEntry(nextOccupant),
                                  false);
        return true;
    }

    const bool queueEmpty =
            resourceUuid.isEmpty() ? true : d->rowsByResource.value(resourceUuid).isEmpty();
    if (queueEmpty) {
        d->lastReleaseByResource.remove(resourceUuid);
        emit occupationTransition(d->resourceNameForUuid(removed.resourceUuid()),
                                  removed.user(),
                                  MessengerUserKey(),
                                  actor,
                                  0,
                                  true);
    } else {
        d->lastReleaseByResource.insert(resourceUuid, removed.user());
    }
    return true;
}

bool ResourceQueueModel::removeResourceEntriesForResource(const QString &resourceName,
                                                          int *removedCountOut,
                                                          QString *errorOut)
{
    QList<ResourceQueueEntry> removedEntries;
    if (!d->removeResourceEntriesInternal(resourceName, errorOut, &removedEntries)) {
        if (removedCountOut)
            *removedCountOut = removedEntries.size();
        return false;
    }

    if (removedCountOut)
        *removedCountOut = removedEntries.size();
    if (removedEntries.isEmpty())
        return true;

    bool persisted = true;
    for (const ResourceQueueEntry &entry : std::as_const(removedEntries)) {
        if (!persistRemove(entry.key())) {
            persisted = false;
            break;
        }
    }
    if (!persisted && errorOut) {
        setError(errorOut,
                 ResourceQueueUserMessages::failedToPersistQueueRemoval(resourceName.trimmed()));
    }

    QSet<QString> resources;
    for (const ResourceQueueEntry &entry : std::as_const(removedEntries)) {
        resources.insert(entry.resourceUuid());
    }
    for (const QString &resourceUuid : std::as_const(resources)) {
        d->lastReleaseByResource.remove(resourceUuid);
    }
    d->refreshOccupations(false, resources);
    return persisted;
}

bool ResourceQueueModel::removeUserFromQueues(const MessengerUserKey &user,
                                              int *removedCountOut,
                                              QString *errorOut)
{
    QList<ResourceQueueEntry> removedEntries;
    const int removed = d->removeUserEntriesInternal(user, &removedEntries);
    if (removedCountOut)
        *removedCountOut = removed;
    if (removed == 0)
        return true;

    bool persisted = true;
    if (d->removeUserEntries) {
        persisted = d->removeUserEntries(user);
    } else {
        for (const ResourceQueueEntry &entry : std::as_const(removedEntries)) {
            if (!persistRemove(entry.key())) {
                persisted = false;
                break;
            }
        }
    }

    if (!persisted && errorOut) {
        setError(errorOut,
                 ResourceQueueUserMessages::failedToPersistQueueRemovalForUser(
                         d->userDisplayName(user)));
    }

    QSet<QString> resources;
    for (const ResourceQueueEntry &entry : std::as_const(removedEntries)) {
        resources.insert(entry.resourceUuid());
    }
    d->refreshOccupations(false, resources);
    return persisted;
}

ResourceQueueKey ResourceQueueModel::keyFromEntry(const ResourceQueueEntry &entry) const
{
    return entry.key();
}
