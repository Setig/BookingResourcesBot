#ifndef SERVERDATABASE_H
#define SERVERDATABASE_H

#include <QObject>

#include <QDateTime>

#include "UserModel.h"
#include "ResourceModel.h"
#include "BlacklistModel.h"
#include "MessengerUserKey.h"
#include "ResourceQueueModel.h"
#include "ServerDatabaseError.h"
#include "ServerRuntimeSettings.h"
#include "PendingAccessUserModel.h"
#include "OccupationDurationModel.h"

class ServerStats;
class ServerSettingsModel;

class ServerDatabasePrivate;

/**
 * @brief Facade over the server's SQLite storage and in-memory models.
 *
 * ServerDatabase owns the in-memory domain models (users, resources, queues, blacklist, etc.)
 * and wires them to a SQLite backend. The public API provides access-controlled and (where
 * necessary) transactional operations that keep persistent storage and in-memory state in sync.
 *
 * @par Initialization and readiness
 * The constructor attempts to open/create the SQLite database, ensure the schema (including
 * migrations), and load all in-memory models. If initialization fails, isReady() returns false,
 * most operations return a ServerDatabaseError with Code::DatabaseNotReady, and errorOccurred()
 * is emitted.
 *
 * @par Threading / thread affinity
 * This class is not thread-safe. It must be created and used from a single thread because the
 * underlying QSqlDatabase connection is thread-affine.
 *
 * @par Error reporting
 * Most operations return ServerDatabaseError with a detailed failure reason. Additionally,
 * errorOccurred() is emitted for non-fatal internal failures and is forwarded from underlying
 * models that have their own persistence hooks.
 *
 * @note Local CLI actors (MessengerType::LocalCli) bypass admin/access checks.
 * @sa UserModel, ResourceModel, ResourceQueueModel, OccupationDurationModel, BlacklistModel,
 *     PendingAccessUserModel
 */
class ServerDatabase : public QObject
{
    Q_OBJECT

public:
    /** @name Construction and state
     * @{
     */
    /**
     * @brief Creates and initializes the database facade.
     *
     * If @p filePath is empty, a default path is used (based on
     * QStandardPaths::AppDataLocation; falls back to the current working directory).
     *
     * Initialization includes opening the SQLite database, ensuring schema/migrations, and
     * loading the in-memory models.
     *
     * @note This object is not thread-safe. Use it from the thread it was created in.
     */
    explicit ServerDatabase(const QString &filePath = QString(), QObject *parent = nullptr);

    /**
     * @brief Destroys the database facade instance.
     *
     * Destruction releases owned resources. Best-effort persistence should be triggered
     * explicitly via shutdown() by the application shutdown coordinator.
     */
    ~ServerDatabase() override;

    /**
     * @brief Performs a best-effort graceful shutdown.
     *
     * This method persists pending in-memory changes to SQLite and then closes and removes the
     * underlying QSqlDatabase connection. It is safe to call multiple times.
     */
    void shutdown();

    /**
     * @brief Returns true if the SQLite storage and in-memory models were initialized.
     *
     * When false, most operations will return ServerDatabaseError::Code::DatabaseNotReady.
     */
    bool isReady() const;

    /**
     * @brief Returns the effective database file path used by this instance.
     */
    QString databasePath() const;

    /**
     * @brief Returns the latest schema version supported by this build.
     *
     * On successful initialization, the database is migrated to this schema version.
     */
    int schemaVersion() const;
    /** @} */

    /** @name Model accessors
     * @{
     */
    /**
     * @brief Returns the in-memory user model.
     * @note The returned pointer is owned by ServerDatabase and remains valid until destruction.
     */
    const UserModel *userModel() const;

    /**
     * @brief Returns the in-memory resource model.
     * @note The returned pointer is owned by ServerDatabase and remains valid until destruction.
     */
    const ResourceModel *resourceModel() const;

    /**
     * @brief Returns the in-memory resource queue model.
     * @note The returned pointer is owned by ServerDatabase and remains valid until destruction.
     */
    const ResourceQueueModel *resourceQueueModel() const;

    /**
     * @brief Returns the in-memory list of allowed occupation durations.
     *
     * Durations are stored as minutes; 0 represents "indefinite".
     *
     * @note The returned pointer is owned by ServerDatabase and remains valid until destruction.
     */
    const OccupationDurationModel *occupationDurationModel() const;

    /**
     * @brief Returns the in-memory blacklist model.
     * @note The returned pointer is owned by ServerDatabase and remains valid until destruction.
     */
    const BlacklistModel *blacklistModel() const;

    /**
     * @brief Returns the in-memory model of recent users awaiting access activation.
     * @note The returned pointer is owned by ServerDatabase and remains valid until destruction.
     */
    const PendingAccessUserModel *pendingAccessUserModel() const;

    /**
     * @brief Returns runtime server settings model.
     * @note The returned pointer is owned by ServerDatabase and remains valid until destruction.
     */
    const ServerSettingsModel *serverSettingsModel() const;

    /**
     * @brief Returns statistics storage helper.
     * @note The returned pointer is owned by ServerDatabase and remains valid until destruction.
     */
    ServerStats *stats() const;
    /** @} */

    /** @name Group authorization
     * @{
     */
    /**
     * @brief Returns the minimal roots of the group subtrees administered by an actor.
     *
     * Fixed administrators and Local CLI actors receive the root group. Invalid, unknown, and
     * non-admin actors receive an empty set.
     */
    [[nodiscard]] QSet<Group> administeredGroupRoots(const MessengerUserKey &actor) const;

    /**
     * @brief Returns whether an actor administers every group in @p groups.
     */
    [[nodiscard]] bool canAdministerGroups(const MessengerUserKey &actor,
                                           const QSet<Group> &groups) const;

    /**
     * @brief Returns whether an actor can administer all groups of @p resource.
     */
    [[nodiscard]] bool canAdministerResource(const MessengerUserKey &actor,
                                             const Resource &resource) const;

    /**
     * @brief Returns whether an actor can administer all groups of @p user.
     *
     * This predicate intentionally has no self-service shortcut.
     */
    [[nodiscard]] bool canAdministerUser(const MessengerUserKey &actor, const User &user) const;

    /**
     * @brief Returns whether an actor can add, update, or remove a blacklist entry for a target.
     */
    [[nodiscard]] bool canManageBlacklistTarget(const MessengerUserKey &actor,
                                                const MessengerUserKey &targetUser) const;
    /** @} */

    /** @name Resource management
     * @{
     */
    /**
     * @brief Adds a new resource.
     *
     * Persists the resource in SQLite and updates the in-memory ResourceModel.
     *
     * @param resource Resource to add.
     * @param actor Actor performing the operation (must administer every resource group).
     * @return Empty error on success; otherwise a ServerDatabaseError describing the failure.
     */
    ServerDatabaseError addResource(const Resource &resource, const MessengerUserKey &actor);

    /**
     * @brief Updates an existing resource (by name).
     *
     * Persists changes in SQLite and updates the in-memory ResourceModel.
     * This method does not rename the resource; use renameResource() for that.
     *
     * @param resource Updated resource data (identified by name).
     * @param actor Actor performing the operation (must administer old and new groups).
     * @return Empty error on success; otherwise a ServerDatabaseError describing the failure.
     */
    ServerDatabaseError updateResource(const Resource &resource, const MessengerUserKey &actor);

    /**
     * @brief Atomically replaces all mutable fields of an existing resource.
     *
     * The existing resource is identified by @p currentName. Name, metadata, status, and groups
     * are authorized and persisted in one transaction.
     */
    ServerDatabaseError replaceResource(const QString &currentName,
                                        const Resource &resource,
                                        const MessengerUserKey &actor);

    /**
     * @brief Updates resource status and optionally resets all queue entries for the resource.
     *
     * When the new status is Resource::Status::TemporarilyUnavailable, all queue entries for the
     * resource are removed in the same transaction. In-memory models are synchronized after commit.
     *
     * @param name Resource name.
     * @param status New status to persist.
     * @param actor Actor performing the operation (must administer every resource group).
     * @return Empty error on success; otherwise a ServerDatabaseError describing the failure.
     */
    ServerDatabaseError setResourceStatus(const QString &name,
                                          Resource::Status status,
                                          const MessengerUserKey &actor);

    /**
     * @brief Deletes a resource and all associated queue entries.
     *
     * The operation removes the resource record and its queue/schedule entries from SQLite and
     * updates in-memory models accordingly. The DB-side change is executed within a transaction.
     *
     * @param name Resource name.
     * @param actor Actor performing the operation (must administer every resource group).
     * @return Empty error on success; otherwise a ServerDatabaseError describing the failure.
     */
    ServerDatabaseError deleteResource(const QString &name, const MessengerUserKey &actor);

    /**
     * @brief Renames a resource and migrates queue entries to the new name.
     *
     * This operation updates both the resources table and the resource_queue table to preserve
     * existing queues/schedules. The DB-side change is executed within a transaction, and the
     * in-memory models are updated only after the transaction is committed.
     *
     * @param oldName Existing resource name.
     * @param newName New resource name.
     * @param description New description to store for the renamed resource.
     * @param startMessage New start message to store for the renamed resource.
     * @param actor Actor performing the operation (must administer every assigned group).
     * @return Empty error on success; otherwise a ServerDatabaseError describing the failure.
     */
    ServerDatabaseError renameResource(const QString &oldName,
                                       const QString &newName,
                                       const QString &description,
                                       const QString &startMessage,
                                       const MessengerUserKey &actor);
    /** @} */

    /** @name User management
     * @{
     */
    /**
     * @brief Registers a user.
     *
     * Persists the user in SQLite and updates the in-memory UserModel. If the user has a blacklist
     * entry or is present in the pending-access list, those records are removed as part of
     * the same DB transaction.
     *
     * @param removedFromBlacklistOut Optional output flag that is set to true if a blacklist entry
     *                                existed and was removed.
     * @param user User to register.
     * @param actor Actor performing the operation (must administer every assigned group; activating
     *              an inactive blacklisted key additionally requires server-admin access).
     * @return Empty error on success; otherwise a ServerDatabaseError describing the failure.
     */
    ServerDatabaseError addUser(const User &user,
                                const MessengerUserKey &actor,
                                bool *removedFromBlacklistOut = nullptr);

    /**
     * @brief Updates a user with active access.
     *
     * Persists changes (including locale/UTC-offset preferences) in SQLite and updates the
     * in-memory
     * UserModel. This
     * method does not change the user identity (messenger/userId).
     *
     * @param user Updated user data (identified by key).
     * @param actor Actor performing the operation.
     *             - Admins can update users only when they administer every old and new group.
     *             - Non-admins can only update their own record and are restricted to non-sensitive
     *               fields (e.g. locale/UTC-offset preferences).
     * @return Empty error on success; otherwise a ServerDatabaseError describing the failure.
     */
    ServerDatabaseError updateUser(const User &user, const MessengerUserKey &actor);

    /**
     * @brief Deletes a user and all related records.
     *
     * Removes the user record and locale, clears any blacklist/pending-access records, and removes
     * all queue/schedule entries for the user. The DB-side change is executed within a
     * transaction. In-memory models are updated after commit.
     *
     * @param user User to delete.
     * @param actor Actor performing the operation (must administer every target group and affected
     *              queue resource).
     * @return Empty error on success; otherwise a ServerDatabaseError describing the failure.
     */
    ServerDatabaseError deleteUser(const MessengerUserKey &user, const MessengerUserKey &actor);
    /** @} */

    /** @name Blacklist management
     * @{
     */
    /**
     * @brief Checks whether a user is currently blacklisted.
     *
     * This method uses the in-memory BlacklistModel (which may purge expired entries as part of
     * the lookup) and optionally returns the matched entry.
     *
     * @param user User key to check.
     * @param entryOut Optional output matched blacklist entry.
     * @return true if a (non-expired) blacklist entry exists; otherwise false.
     */
    bool isBlacklisted(const MessengerUserKey &user, BlacklistEntry *entryOut = nullptr);

    /**
     * @brief Upserts a blacklist entry.
     *
     * Persists the entry in SQLite and updates the in-memory BlacklistModel. This method does not
     * remove the user from queues; use banUser() to also clear queue entries.
     *
     * @param entry Blacklist entry to store (must already be normalized).
     * @param actor Actor performing the operation (must administer the active target; unknown
     *              targets require server-admin access).
     * @return Empty error on success; otherwise a ServerDatabaseError describing the failure.
     */
    ServerDatabaseError saveBlacklistEntry(const BlacklistEntry &entry,
                                           const MessengerUserKey &actor);

    /**
     * @brief Removes a blacklist entry for the given user.
     *
     * The in-memory BlacklistModel is updated on success.
     *
     * @param user User key to un-blacklist.
     * @param actor Actor performing the operation (same scope rules as saveBlacklistEntry()).
     * @return Empty error on success; otherwise a ServerDatabaseError describing the failure.
     */
    ServerDatabaseError removeBlacklistEntry(const MessengerUserKey &user,
                                             const MessengerUserKey &actor);

    /**
     * @brief Adds/updates a blacklist entry and clears all queue entries for the user.
     *
     * The DB-side change is executed within a transaction to keep blacklist and queue state
     * consistent. In-memory models are updated after commit.
     *
     * @param entry Blacklist entry to store (must already be normalized).
     * @param actor Actor performing the operation (must administer the target and affected queue
     *              resources).
     * @param removedQueueEntriesOut Optional output count of removed queue entries (best-effort,
     *                               based on in-memory queue state).
     * @return Empty error on success; otherwise a ServerDatabaseError describing the failure.
     */
    ServerDatabaseError banUser(const BlacklistEntry &entry,
                                const MessengerUserKey &actor,
                                int *removedQueueEntriesOut = nullptr);

    /**
     * @brief Removes a user from the blacklist.
     *
     * @param previousEntryOut Optional output that receives the previous blacklist entry.
     * @param user User to unban.
     * @param actor Actor performing the operation (same scope rules as saveBlacklistEntry()).
     * @return Empty error on success; otherwise a ServerDatabaseError describing the failure.
     */
    ServerDatabaseError unbanUser(const MessengerUserKey &user,
                                  const MessengerUserKey &actor,
                                  BlacklistEntry *previousEntryOut = nullptr);
    /** @} */

    /** @name Queue / booking operations
     * @{
     */
    /**
     * @brief Occupies a resource starting now.
     *
     * Creates a schedule entry with start = now (UTC) and end = now + durationMinutes (UTC).
     * If durationMinutes is 0, the occupation becomes indefinite (end is not specified).
     *
     * Delegates validation and in-memory updates to ResourceQueueModel and persists changes via
     * its storage adapter.
     */
    ServerDatabaseError occupyResourceUser(const QString &resourceName,
                                           const MessengerUserKey &user,
                                           int durationMinutes,
                                           const QList<int> &allowedDurations,
                                           const MessengerUserKey &actor);

    /**
     * @brief Creates or updates a scheduled reservation for a resource.
     *
     * Delegates validation and in-memory updates to ResourceQueueModel and persists changes via
     * its storage adapter.
     *
     * @param resourceName Target resource name.
     * @param user User to schedule.
     * @param startUtc Start timestamp in UTC.
     * @param endUtc End timestamp in UTC. Can be invalid to denote an indefinite reservation.
     * @param actor Actor performing the operation (must have active access; must match @p user unless
     *             the actor is an admin).
     * @return Empty error on success; otherwise a ServerDatabaseError describing the failure.
     */
    ServerDatabaseError scheduleResourceUser(const QString &resourceName,
                                             const MessengerUserKey &user,
                                             const QDateTime &startUtc,
                                             const QDateTime &endUtc,
                                             const MessengerUserKey &actor);

    /**
     * @brief Removes the user's scheduled reservations for a resource.
     *
     * Delegates validation and in-memory updates to ResourceQueueModel and persists changes via
     * its storage adapter.
     *
     * @param resourceName Target resource name.
     * @param user User whose schedules should be removed.
     * @param actor Actor performing the operation (must have active access; must match @p user unless
     *             the actor is an admin).
     * @return Empty error on success; otherwise a ServerDatabaseError describing the failure.
     */
    ServerDatabaseError removeUserSchedulesForResource(const QString &resourceName,
                                                       const MessengerUserKey &user,
                                                       const MessengerUserKey &actor);

    /**
     * @brief Releases the current occupant for a resource.
     *
     * Delegates validation and in-memory updates to ResourceQueueModel and persists changes via
     * its storage adapter.
     *
     * @param resourceName Target resource name.
     * @param user Current occupant user to release.
     * @param actor Actor performing the operation (must have active access; must match @p user unless
     *             the actor is an admin).
     * @return Empty error on success; otherwise a ServerDatabaseError describing the failure.
     */
    ServerDatabaseError releaseUserOccupationForResource(const QString &resourceName,
                                                         const MessengerUserKey &user,
                                                         const MessengerUserKey &actor);

    /**
     * @brief Removes all queue and schedule entries for a resource.
     *
     * Delegates the operation to ResourceQueueModel and persists changes via its storage adapter.
     *
     * @param resourceName Target resource name.
     * @param actor Actor performing the operation (must be able to administer the resource).
     * @param removedCountOut Optional output number of removed entries.
     * @return Empty error on success; otherwise a ServerDatabaseError describing the failure.
     */
    ServerDatabaseError removeResourceQueueEntries(const QString &resourceName,
                                                   const MessengerUserKey &actor,
                                                   int *removedCountOut = nullptr);

    /**
     * @brief Removes all queue and schedule entries for a user.
     *
     * Delegates the operation to ResourceQueueModel and persists changes via its storage adapter.
     *
     * @param user User to remove from all queues/schedules.
     * @param actor Actor performing the operation (must have active access; must match @p user unless
     *             the actor is an admin).
     * @param removedCountOut Optional output number of removed entries.
     * @return Empty error on success; otherwise a ServerDatabaseError describing the failure.
     */
    ServerDatabaseError removeUserFromQueues(const MessengerUserKey &user,
                                             const MessengerUserKey &actor,
                                             int *removedCountOut = nullptr);
    /** @} */

    /** @name Users awaiting access activation (recent activity)
     * @{
     */
    /**
     * @brief Records a pending-access user entry (recent activity).
     *
     * Persists the entry in SQLite (if storage is enabled) and updates the in-memory
     * PendingAccessUserModel. Retention policy is enforced by the model.
     *
     * @param entry Activity entry to record.
     * @param actor Actor performing the operation (used for auditing/logging).
     * @return Empty error on success; otherwise a ServerDatabaseError describing the failure.
     */
    ServerDatabaseError recordPendingAccessUser(const PendingAccessUserEntry &entry,
                                                const MessengerUserKey &actor);

    /**
     * @brief Removes a pending-access user entry.
     *
     * Persists the removal in SQLite (if storage is enabled) and updates the in-memory
     * PendingAccessUserModel.
     *
     * @param user User to clear from the pending-access set.
     * @param actor Actor performing the operation (must be a server administrator because pending
     *              entries do not have a group scope).
     * @return Empty error on success; otherwise a ServerDatabaseError describing the failure.
     */
    ServerDatabaseError clearPendingAccessUser(const MessengerUserKey &user,
                                               const MessengerUserKey &actor);
    /** @} */

    /** @name Administration / bootstrapping
     * @{
     */
    /**
     * @brief Ensures that a user exists and has fixed admin privileges.
     *
     * If the user does not exist, a minimal admin user record is created with empty name fields.
     * The fixed-admin marker is kept in memory for the current process and is not persisted.
     * The DB-side change is executed within a transaction.
     *
     * Intended for internal bootstrapping. This operation does not perform actor-based access checks.
     *
     * @param user Target user that should become a fixed admin.
     * @return Empty error on success; otherwise a ServerDatabaseError describing the failure.
     */
    /** @} */

    /** @name Runtime server settings
     * @{
     */
    /**
     * @brief Returns whether an actor can manage runtime server settings.
     *
     * Server-wide settings can be changed only by fixed admins or administrators in the root
     * group. Group-scoped administrators cannot change these settings.
     */
    [[nodiscard]] bool canManageServerSettings(const MessengerUserKey &actor) const;

    /**
     * @brief Returns current runtime setting value by typed setting id.
     *
     * If the setting is not present in the in-memory settings model, the component default value
     * is returned.
     */
    QVariant serverSettingValue(ServerRuntimeSettings::Setting setting) const;

    /**
     * @brief Updates a runtime-configurable setting by typed setting id.
     *
     * Applies the new value immediately to the running server and persists it in SQLite.
     * Only fixed admins and root-group administrators can change settings.
     */
    ServerDatabaseError setServerSetting(ServerRuntimeSettings::Setting setting,
                                         const QVariant &value,
                                         const MessengerUserKey &actor);

    /**
     * @brief Updates a runtime-configurable server setting.
     *
     * Applies the new value immediately to the running server and persists it in SQLite.
     * Only fixed admins and root-group administrators can change settings.
     *
     * @param key Setting key (see ServerRuntimeSettings).
     * @param value New value (type depends on key).
     * @param actor Actor performing the operation (must be fixed admin or root-group admin).
     */
    ServerDatabaseError setServerSetting(const QString &key,
                                         const QVariant &value,
                                         const MessengerUserKey &actor);
    /** @} */

    /** @name Occupation duration management
     * @{
     */
    /**
     * @brief Adds an allowed occupation duration (in minutes).
     *
     * Persists the updated list in SQLite and updates the in-memory OccupationDurationModel.
     * Only fixed administrators, root-group administrators, and Local CLI can change occupation
     * durations.
     *
     * @param minutes Duration in minutes (0 = indefinite).
     * @param actor Actor performing the operation (must be a server administrator).
     * @return Empty error on success; otherwise a ServerDatabaseError describing the failure.
     */
    ServerDatabaseError addOccupationDuration(int minutes, const MessengerUserKey &actor);

    /**
     * @brief Removes an allowed occupation duration (in minutes).
     *
     * The indefinite duration (0) is required and cannot be removed.
     * Persists the updated list in SQLite and updates the in-memory OccupationDurationModel.
     * Only fixed administrators, root-group administrators, and Local CLI can change occupation
     * durations.
     *
     * @param minutes Duration in minutes (0 = indefinite).
     * @param actor Actor performing the operation (must be a server administrator).
     * @return Empty error on success; otherwise a ServerDatabaseError describing the failure.
     */
    ServerDatabaseError removeOccupationDuration(int minutes, const MessengerUserKey &actor);
    /** @} */

    /**
     * @brief Notification types for resource occupation events.
     */
    enum class OccupationNotificationType {
        Occupied = 0,
        Handover,
        Released
    };
    Q_ENUM(OccupationNotificationType)

signals:
    /**
     * @brief Emitted when a non-fatal internal error occurs.
     *
     * This signal is emitted by ServerDatabase itself (e.g. failed initialization) and is also
     * forwarded from underlying models that report persistence/runtime issues.
     */
    void errorOccurred(const QString &description);

    /**
     * @brief Emitted when a resource occupation notification should be sent.
     *
     * @param type Notification type (occupied/handover/released).
     * @param resourceName Resource name (normalized, if possible).
     * @param releasedBy User who released the resource (empty if not applicable).
     * @param occupiedBy User who occupied the resource (empty if not applicable).
     * @param initiator User who initiated the manual action (empty if not applicable).
     * @param durationMinutes Occupation duration in minutes (0 = indefinite).
     */
    void occupationNotification(ServerDatabase::OccupationNotificationType type,
                                const QString &resourceName,
                                const MessengerUserKey &releasedBy,
                                const MessengerUserKey &occupiedBy,
                                const MessengerUserKey &initiator,
                                int durationMinutes);

    /**
     * @brief Emitted when a runtime server setting value has changed.
     *
     * This signal is emitted only after a successful setServerSetting() operation and
     * carries the normalized value stored in the settings model.
     *
     * @param setting Changed setting identifier.
     * @param value New normalized value.
     */
    void serverSettingChanged(ServerRuntimeSettings::Setting setting, const QVariant &value);

    /**
     * @brief Emitted after a user record changes.
     *
     * This signal is emitted only after successful add/update/delete operations. Consumers that
     * cache user-dependent visibility, such as web command lists, should invalidate their cache for
     * the provided user key.
     *
     * @param user Changed user key.
     */
    void userChanged(const MessengerUserKey &user);

private:
    ServerDatabasePrivate *d;

    // Startup-only internal bootstrapping. Accessible by ServerApplication (friend).
    ServerDatabaseError ensureFixedAdmin(const MessengerUserKey &user);

    friend class ServerApplication;
};

#endif // SERVERDATABASE_H
