#ifndef SERVERDATABASE_P_H
#define SERVERDATABASE_P_H

#include "ServerDatabase.h"
#include "ServerSettingsModel.h"

#include <QMap>
#include <QSet>
#include <QList>
#include <QHash>
#include <QSqlDatabase>

class QDebug;
class QSqlQuery;

class ServerDatabasePrivate
{
public:
    struct UserSettingsRecord
    {
        QString locale;
        UserTimeZone timezone;
        User::OccupationNotifyMode notifyMode;

        UserSettingsRecord();
    };

    ServerDatabasePrivate(ServerDatabase *q, const QString &filePath);
    static ResourceQueueEntry queueEntryFromQuery(const QSqlQuery &query);
    static qint64 toEpochSeconds(const QDateTime &dt);
    static QDateTime dateTimeFromEpoch(qint64 seconds);
    static BlacklistEntry blacklistEntryFromQuery(const QSqlQuery &query);
    static void logBlacklistExpiry(const BlacklistEntry &entry, const UserModel *userModel);
    static PendingAccessUserEntry pendingAccessUserFromQuery(const QSqlQuery &query);
    static QString userSettingsKey(const MessengerUserKey &user);

    bool init();
    bool open();
    bool ensureSchema();
    bool refreshResourceStatusColumnAvailability();
    bool ensureMetaTable();
    bool ensureSchemaVersion();
    bool applyMigrations(int currentVersion);
    bool loadUsers();
    bool loadUserGroups(QHash<QString, QMap<Group, User::Role>> *groupsByUser);
    bool loadUserSettings(QHash<QString, UserSettingsRecord> *settingsByKey);
    bool loadResources();
    bool loadResourceGroups(QHash<QString, QSet<Group>> *groupsByResource);
    bool loadResourceQueueEntries();
    bool loadBlacklistEntries();
    bool loadPendingAccessUsers();
    bool loadOccupationDurations();
    bool loadServerSettings();
    bool saveAll();
    bool saveUsers();
    bool replaceUserSettings(const QList<User> &users);
    bool replaceUserGroups(const QList<User> &users);
    bool saveResources();
    bool replaceResourceGroups(const QList<Resource> &resources);
    bool saveResourceQueueEntries();
    bool upsertUserRecord(const User &user);
    bool upsertUserGroups(const User &user);
    bool upsertUserSettings(const User &user);
    bool deleteUserRecord(const MessengerUserKey &user);
    bool deleteUserGroups(const MessengerUserKey &user);
    bool deleteUserSettings(const MessengerUserKey &user);
    bool persistUserSnapshot(const User &user, const QString &errorMessage);
    bool persistUserDeletion(const MessengerUserKey &user, const QString &errorMessage);
    bool persistUserSettingsRecord(const User &user);
    bool upsertResourceRecord(const Resource &resource);
    bool upsertResourceGroups(const Resource &resource);
    bool deleteResourceRecord(const QString &uuid);
    bool deleteResourceGroups(const QString &uuid);
    bool persistResourceSnapshot(const Resource &resource, const QString &errorMessage);
    bool persistResourceDeletion(const QString &uuid, const QString &errorMessage);
    bool upsertQueueEntry(const ResourceQueueEntry &entry);
    bool deleteQueueEntry(const ResourceQueueKey &key);
    bool deleteQueueEntries(const QList<ResourceQueueKey> &keys);
    bool deleteQueueEntriesForUser(const MessengerUserKey &user);
    bool upsertBlacklistRecord(const BlacklistEntry &entry);
    bool deleteBlacklistRecord(const MessengerUserKey &user);
    bool upsertPendingAccessUserRecord(const PendingAccessUserEntry &entry);
    bool deletePendingAccessUserRecord(const MessengerUserKey &user);
    bool replaceOccupationDurations(const QList<int> &minutes);
    QList<int> defaultOccupationDurations() const;
    QList<int> normalizeDurations(const QList<int> &minutes) const;
    bool writeBackupSnapshot();
    void logSqlError(const QSqlQuery &query, const QString &context) const;
    bool restoreFromBackup();
    void cleanupOldCorruptedBackups();
    static QString defaultDatabasePath();
    QString settingsMetaKey(const QString &key) const;
    QString readMetaValue(const QString &key) const;
    bool writeMetaValue(const QString &key, const QString &value) const;
    bool deleteMetaValue(const QString &key) const;

    void appendActorToLog(QDebug &dbg,
                          const MessengerUserKey &actor,
                          const MessengerUserKey &subjectUser = MessengerUserKey()) const;
    bool isActorAdmin(const MessengerUserKey &actor) const;
    bool isActorFixedAdmin(const MessengerUserKey &actor) const;
    bool actorHasActiveAccess(const MessengerUserKey &actor) const;
    bool actorCanManageServerSettings(const MessengerUserKey &actor) const;
    QSet<Group> actorAdministeredGroupRoots(const MessengerUserKey &actor) const;
    bool actorCanAdministerGroups(const MessengerUserKey &actor, const QSet<Group> &groups) const;
    bool actorCanModifyResource(const MessengerUserKey &actor, const Resource &resource) const;
    bool actorCanAdministerUser(const MessengerUserKey &actor, const User &target) const;
    bool actorCanManageBlacklistTarget(const MessengerUserKey &actor,
                                       const MessengerUserKey &targetUser) const;
    bool userHasAccessToResource(const MessengerUserKey &user, const Resource &resource) const;
    bool actorCanAdministerQueueEntriesForUser(const MessengerUserKey &actor,
                                               const MessengerUserKey &targetUser) const;
    bool actorCanModifyUser(const MessengerUserKey &actor, const MessengerUserKey &targetUser);

    inline static constexpr char kMetaTableSql[] = "CREATE TABLE IF NOT EXISTS meta ("
                                                   "key TEXT PRIMARY KEY,"
                                                   "value TEXT NOT NULL)";

    inline static constexpr char kUsersTableSql[] =
            "CREATE TABLE IF NOT EXISTS users ("
            "messenger_type TEXT NOT NULL DEFAULT 'telegram',"
            "user_id TEXT NOT NULL,"
            "first_name TEXT,"
            "last_name TEXT,"
            "is_admin INTEGER NOT NULL DEFAULT 0,"
            "PRIMARY KEY (messenger_type, user_id))";

    inline static constexpr char kUserSettingsTableSql[] =
            "CREATE TABLE IF NOT EXISTS user_settings ("
            "messenger_type TEXT NOT NULL,"
            "user_id TEXT NOT NULL,"
            "locale TEXT,"
            "timezone INTEGER,"
            "occupation_notify_mode INTEGER NOT NULL DEFAULT 1,"
            "PRIMARY KEY (messenger_type, user_id))";

    inline static constexpr char kUserGroupsTableSql[] =
            "CREATE TABLE IF NOT EXISTS user_groups ("
            "messenger_type TEXT NOT NULL,"
            "user_id TEXT NOT NULL,"
            "group_path TEXT NOT NULL,"
            "role TEXT NOT NULL DEFAULT 'user',"
            "PRIMARY KEY (messenger_type, user_id, group_path),"
            "FOREIGN KEY(messenger_type, user_id) REFERENCES users(messenger_type, user_id) "
            "ON DELETE CASCADE)";

    inline static constexpr char kResourcesTableSql[] = "CREATE TABLE IF NOT EXISTS resources ("
                                                        "uuid TEXT PRIMARY KEY,"
                                                        "name TEXT NOT NULL UNIQUE,"
                                                        "description TEXT,"
                                                        "start_message TEXT,"
                                                        "status TEXT NOT NULL DEFAULT 'on')";

    inline static constexpr char kResourceGroupsTableSql[] =
            "CREATE TABLE IF NOT EXISTS resource_groups ("
            "resource_uuid TEXT NOT NULL,"
            "group_path TEXT NOT NULL,"
            "PRIMARY KEY (resource_uuid, group_path),"
            "FOREIGN KEY(resource_uuid) REFERENCES resources(uuid) ON DELETE CASCADE)";

    inline static constexpr char kResourceQueueTableSql[] =
            "CREATE TABLE IF NOT EXISTS resource_queue ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "resource_uuid TEXT NOT NULL,"
            "messenger_type TEXT NOT NULL,"
            "user_id TEXT NOT NULL,"
            "enqueued_at INTEGER NOT NULL,"
            "start_at INTEGER NOT NULL,"
            "end_at INTEGER NOT NULL DEFAULT 0,"
            "start_mode TEXT NOT NULL DEFAULT 'at_time',"
            "duration_minutes INTEGER NOT NULL DEFAULT -1,"
            "UNIQUE(resource_uuid, messenger_type, user_id, start_at),"
            "FOREIGN KEY(resource_uuid) REFERENCES resources(uuid) ON DELETE CASCADE)";

    inline static constexpr char kResourceQueueUuidIndexSql[] =
            "CREATE INDEX IF NOT EXISTS idx_resource_queue_uuid "
            "ON resource_queue(resource_uuid, id)";

    inline static constexpr char kOccupationDurationsTableSql[] =
            "CREATE TABLE IF NOT EXISTS occupation_durations ("
            "minutes INTEGER PRIMARY KEY)";

    inline static constexpr char kBlacklistTableSql[] = "CREATE TABLE IF NOT EXISTS blacklist ("
                                                        "messenger_type TEXT NOT NULL,"
                                                        "user_id TEXT NOT NULL,"
                                                        "reason TEXT,"
                                                        "blocked_at INTEGER NOT NULL,"
                                                        "blocked_until INTEGER,"
                                                        "blocked_by_messenger TEXT,"
                                                        "blocked_by_user_id TEXT,"
                                                        "PRIMARY KEY (messenger_type, user_id))";
    inline static constexpr char kPendingAccessUsersTableSql[] =
            "CREATE TABLE IF NOT EXISTS pending_access_users ("
            "messenger_type TEXT NOT NULL,"
            "user_id TEXT NOT NULL,"
            "first_name TEXT,"
            "last_name TEXT,"
            "last_message_at INTEGER NOT NULL,"
            "PRIMARY KEY (messenger_type, user_id))";
    inline static constexpr int kRecentPendingAccessLimit = 100;
    inline static constexpr int kPendingAccessRetentionDays = 3;

    ServerDatabase *q;
    QString connectionName;
    QString dbFilePath;
    QSqlDatabase db;
    bool ready;
    const int targetSchemaVersion;
    UserModel userModel;
    ResourceModel resourceModel;
    OccupationDurationModel durationModel;
    ResourceQueueModel queueModel;
    ServerSettingsModel settingsModel;
    ServerStats *stats;
    BlacklistModel blacklistModel;
    PendingAccessUserModel pendingAccessUserModel;
    bool usersDirty;
    bool resourcesDirty;
    bool queueDirty;
    bool resourceStatusColumnAvailable;
};

#endif // SERVERDATABASE_P_H
