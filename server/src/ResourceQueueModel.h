#ifndef RESOURCEQUEUEMODEL_H
#define RESOURCEQUEUEMODEL_H

#include "AbstractServerDatabaseModel.h"

#include <QList>
#include <QHash>
#include <QString>
#include <QDateTime>

#include "ResourceQueueTypes.h"

class UserModel;
class ResourceModel;

class ResourceQueueModelPrivate;

/**
 * @brief Model that keeps resource queue entries in memory and performs validation.
 */
class ResourceQueueModel : public AbstractServerDatabaseModel<ResourceQueueEntry, ResourceQueueKey>
{
    Q_OBJECT

public:
    enum Roles {
        ResourceNameRole = Qt::UserRole + 1,
        MessengerRole,
        UserIdRole,
        DurationMinutesRole,
        EnqueuedAtRole,
        StartAtRole,
        EndAtRole,
    };

    explicit ResourceQueueModel(UserModel *userModel = nullptr,
                                ResourceModel *resourceModel = nullptr,
                                QObject *parent = nullptr);
    ~ResourceQueueModel() override;

    /**
     * @brief Storage callbacks used to persist queue changes.
     */
    struct StorageAdapter
    {
        std::function<bool(const ResourceQueueEntry &entry)> upsert;
        std::function<bool(const ResourceQueueKey &key)> remove;
        std::function<bool(const QList<ResourceQueueKey> &keys)> removeEntries;
        std::function<bool(const MessengerUserKey &user)> removeUserEntries;
    };

    void setStorageAdapter(StorageAdapter adapter);

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void replaceAll(const QList<ResourceQueueEntry> &entries);
    const QList<ResourceQueueEntry> &items() const;

    QList<ResourceQueueEntry> queueForResource(const QString &resourceName) const;
    QList<ResourceQueueEntry> schedulesForResource(const QString &resourceName) const;
    ResourceQueueEntry currentOccupantForResource(const QString &resourceName) const;
    QDateTime nextScheduleStartUtc(const QString &resourceName,
                                   const QDateTime &afterUtc = QDateTime()) const;
    QList<ResourceQueueEntry> soonestReleasingResources(int limit) const;
    QList<QString> freeResourceNames() const;

    [[nodiscard]] uint maxResourcesPerUser() const;
    [[nodiscard]] uint maxOccupationDurationMinutes() const;

    void setMaxResourcesPerUser(uint limit);
    void setMaxOccupationDurationMinutes(uint limit);

    bool occupyResourceUser(const QString &resourceName,
                            const MessengerUserKey &user,
                            int durationMinutes,
                            const QList<int> &allowedDurations,
                            const MessengerUserKey &actor = MessengerUserKey(),
                            QString *errorOut = nullptr);

    bool scheduleResourceUser(const QString &resourceName,
                              const MessengerUserKey &user,
                              const QDateTime &startUtc,
                              const QDateTime &endUtc,
                              const MessengerUserKey &actor = MessengerUserKey(),
                              QString *errorOut = nullptr);

    bool removeUserSchedulesForResource(const QString &resourceName,
                                        const MessengerUserKey &user,
                                        QString *errorOut = nullptr,
                                        int *removedCountOut = nullptr);

    bool releaseUserOccupationForResource(const QString &resourceName,
                                          const MessengerUserKey &user,
                                          const MessengerUserKey &actor = MessengerUserKey(),
                                          QString *errorOut = nullptr);

    bool removeResourceEntriesForResource(const QString &resourceName,
                                          int *removedCountOut = nullptr,
                                          QString *errorOut = nullptr);

    bool removeUserFromQueues(const MessengerUserKey &user,
                              int *removedCountOut = nullptr,
                              QString *errorOut = nullptr);

signals:
    void occupationStarted(const QString &resourceName,
                           const MessengerUserKey &user,
                           bool scheduled,
                           const QDateTime &handoverAtUtc);
    void occupationEnded(const QString &resourceName,
                         const MessengerUserKey &user,
                         bool scheduled,
                         const QString &reason,
                         const QDateTime &endedAtUtc);
    /**
     * @brief Emitted when an occupancy transition occurs due to manual occupy/release.
     *
     * @param resourceName Target resource name (resolved from the model if possible).
     * @param previousOccupant Previous occupant (invalid if none).
     * @param nextOccupant Next occupant (invalid if none).
     * @param initiator User who initiated the manual action (empty if not applicable).
     * @param nextDurationMinutes Duration of next occupation in minutes (0 = indefinite/unknown).
     * @param queueEmpty True if the queue is empty after the transition.
     */
    void occupationTransition(const QString &resourceName,
                              const MessengerUserKey &previousOccupant,
                              const MessengerUserKey &nextOccupant,
                              const MessengerUserKey &initiator,
                              int nextDurationMinutes,
                              bool queueEmpty);

protected:
    ResourceQueueKey keyFromEntry(const ResourceQueueEntry &entry) const override;

private:
    ResourceQueueModelPrivate *d;

    friend class ResourceQueueModelPrivate;
};

#endif // RESOURCEQUEUEMODEL_H
