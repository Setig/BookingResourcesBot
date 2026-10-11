#include "ServerDatabase_p.h"

#include "ServerStats.h"
#include "MessengerTextRender.h"
#include "ServerDateTimeFormat.h"
#include "ServerRuntimeSettings.h"

#include <QDir>
#include <QUuid>
#include <QFile>
#include <QVariant>
#include <QDateTime>
#include <QTimeZone>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QElapsedTimer>
#include <QStandardPaths>
#include <QLoggingCategory>

#include <utility>

Q_LOGGING_CATEGORY(lcServerDatabasePrivate, "booking.server.db")

namespace {

QString roleToStorageString(User::Role role)
{
    switch (role) {
    case User::Role::UserRole:
        return QStringLiteral("user");
    case User::Role::AdminRole:
    case User::Role::FixedAdminRole:
        return QStringLiteral("admin");
    }
    return QStringLiteral("user");
}

QString groupToStorageString(const Group &group)
{
    const QString path = QString(group);
    return path.isNull() ? QStringLiteral("") : path;
}

User::Role roleFromStorageString(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("fixed_admin") || normalized == QStringLiteral("fixed-admin"))
        return User::Role::AdminRole;
    if (normalized == QStringLiteral("admin"))
        return User::Role::AdminRole;
    return User::Role::UserRole;
}

} // namespace

ServerDatabasePrivate::ServerDatabasePrivate(ServerDatabase *qPtr, const QString &filePath)
    : q(qPtr)
    , connectionName(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , dbFilePath(filePath.isEmpty() ? ServerDatabasePrivate::defaultDatabasePath() : filePath)
    , ready(false)
    , targetSchemaVersion(3)
    , queueModel(&userModel, &resourceModel, qPtr)
    , settingsModel(qPtr)
    , stats(nullptr)
    , usersDirty(false)
    , resourcesDirty(false)
    , queueDirty(false)
    , resourceStatusColumnAvailable(false)
{
    // do nothing
}

ServerDatabasePrivate::UserSettingsRecord::UserSettingsRecord()
    : notifyMode(User::OccupationNotifyMode::OwnResourcesOnly)
{
    // do nothing
}

ResourceQueueEntry ServerDatabasePrivate::queueEntryFromQuery(const QSqlQuery &query)
{
    ResourceQueueEntry entry;
    entry.setResourceUuid(query.value(1).toString());

    MessengerUserKey user;
    user.setMessenger(messengerTypeFromString(query.value(2).toString()));
    user.setUserId(query.value(3).toString());
    entry.setUser(user);

    entry.setEnqueuedAtUtc(dateTimeFromEpoch(query.value(4).toLongLong()));
    entry.setStartAtUtc(dateTimeFromEpoch(query.value(5).toLongLong()));
    entry.setEndAtUtc(dateTimeFromEpoch(query.value(6).toLongLong()));
    entry.setStartMode(ResourceQueueEntry::startModeFromStorageToken(query.value(7).toString()));
    entry.setRequestedDurationMinutes(query.value(8).toInt());
    return entry;
}

qint64 ServerDatabasePrivate::toEpochSeconds(const QDateTime &dt)
{
    if (!dt.isValid())
        return 0;
    return dt.toSecsSinceEpoch();
}

QDateTime ServerDatabasePrivate::dateTimeFromEpoch(qint64 seconds)
{
    if (seconds <= 0)
        return QDateTime();
    return QDateTime::fromSecsSinceEpoch(seconds, QTimeZone::utc());
}

BlacklistEntry ServerDatabasePrivate::blacklistEntryFromQuery(const QSqlQuery &query)
{
    const MessengerType messenger = messengerTypeFromString(query.value(0).toString());
    const MessengerUserKey user(messenger, query.value(1).toString());
    const QString reason = query.value(2).toString();
    const QDateTime blockedAtUtc = dateTimeFromEpoch(query.value(3).toLongLong());
    const QVariant untilValue = query.value(4);
    QDateTime blockedUntilUtc;
    if (!untilValue.isNull()) {
        blockedUntilUtc = dateTimeFromEpoch(untilValue.toLongLong());
    }

    MessengerUserKey blockedBy;
    const QString blockedByUserId = query.value(6).toString();
    if (!blockedByUserId.isEmpty()) {
        const MessengerType blockedByMessenger = messengerTypeFromString(query.value(5).toString());
        blockedBy = MessengerUserKey(blockedByMessenger, blockedByUserId);
    }

    return BlacklistEntry(user, reason, blockedBy, blockedAtUtc, blockedUntilUtc);
}

void ServerDatabasePrivate::logBlacklistExpiry(const BlacklistEntry &entry,
                                               const UserModel *userModel)
{
    const QString reason = entry.reason();
    const QString expiredAt = entry.blockedUntilUtc().isValid()
                                      ? entry.blockedUntilUtc().toString(Qt::ISODate)
                                      : QStringLiteral("<unknown>");
    const User user = userModel ? userModel->userByKey(entry.user()) : User();
    const QString userText = user.isValid() ? MessengerTextRender::formatUserLine(user, false)
                                            : entry.user().toString();

    qCInfo(lcServerDatabasePrivate).nospace()
            << "Blacklist expired " << qPrintable(userText) << " reason=" << reason
            << " expired_at=" << expiredAt;
}

PendingAccessUserEntry ServerDatabasePrivate::pendingAccessUserFromQuery(const QSqlQuery &query)
{
    const MessengerUserKey user(messengerTypeFromString(query.value(0).toString()),
                                query.value(1).toString());
    const QString firstName = query.value(2).toString();
    const QString lastName = query.value(3).toString();
    const QDateTime lastMessageAtUtc = dateTimeFromEpoch(query.value(4).toLongLong());
    return PendingAccessUserEntry(user, firstName, lastName, lastMessageAtUtc);
}

QString ServerDatabasePrivate::userSettingsKey(const MessengerUserKey &user)
{
    QString messenger = messengerTypeToString(user.messenger());
    messenger.reserve(messenger.size() + 1 + user.userId().size());
    messenger.append(QLatin1Char('\n'));
    messenger.append(user.userId());
    return messenger;
}

bool ServerDatabasePrivate::init()
{
    if (!open())
        return false;

    if (!ensureSchema())
        return false;

    if (!loadUsers())
        return false;

    if (!loadResources())
        return false;

    if (!loadResourceQueueEntries())
        return false;

    if (!loadOccupationDurations())
        return false;

    if (!loadServerSettings())
        return false;

    if (!loadBlacklistEntries())
        return false;

    if (!loadPendingAccessUsers())
        return false;

    if (!blacklistModel.purgeExpiredEntries())
        return false;

    if (!pendingAccessUserModel.cleanupExpired())
        return false;

    return true;
}

QString ServerDatabasePrivate::settingsMetaKey(const QString &key) const
{
    const QString trimmed = key.trimmed();
    if (trimmed.isEmpty())
        return QString();
    return QStringLiteral("server_setting/") + trimmed;
}

QString ServerDatabasePrivate::readMetaValue(const QString &key) const
{
    const QString metaKey = settingsMetaKey(key);
    if (metaKey.isEmpty())
        return QString();

    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT value FROM meta WHERE key = :key"));
    query.bindValue(QStringLiteral(":key"), metaKey);
    if (!query.exec()) {
        logSqlError(query, QStringLiteral("select meta value"));
        return QString();
    }
    if (!query.next())
        return QString();
    return query.value(0).toString();
}

bool ServerDatabasePrivate::writeMetaValue(const QString &key, const QString &value) const
{
    const QString metaKey = settingsMetaKey(key);
    if (metaKey.isEmpty())
        return false;

    QSqlQuery update(db);
    update.prepare(QStringLiteral("UPDATE meta SET value = :value WHERE key = :key"));
    update.bindValue(QStringLiteral(":key"), metaKey);
    update.bindValue(QStringLiteral(":value"), value);
    if (!update.exec()) {
        logSqlError(update, QStringLiteral("update meta value"));
        return false;
    }

    if (update.numRowsAffected() > 0)
        return true;

    QSqlQuery insert(db);
    insert.prepare(QStringLiteral("INSERT INTO meta(key, value) VALUES (:key, :value)"));
    insert.bindValue(QStringLiteral(":key"), metaKey);
    insert.bindValue(QStringLiteral(":value"), value);
    if (!insert.exec()) {
        logSqlError(insert, QStringLiteral("insert meta value"));
        return false;
    }
    return true;
}

bool ServerDatabasePrivate::deleteMetaValue(const QString &key) const
{
    const QString metaKey = settingsMetaKey(key);
    if (metaKey.isEmpty())
        return false;

    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM meta WHERE key = :key"));
    query.bindValue(QStringLiteral(":key"), metaKey);
    if (!query.exec()) {
        logSqlError(query, QStringLiteral("delete meta value"));
        return false;
    }
    return true;
}

bool ServerDatabasePrivate::loadServerSettings()
{
    // Note: values are persisted in meta as strings; types are interpreted by key.
    const auto upsertSetting = [this](const QString &key, const QVariant &value) {
        ServerSettingEntry entry(key, value);
        settingsModel.upsertEntry(entry);
    };

    const auto readUInt = [this](const QString &key, uint fallback) -> uint {
        const QString raw = readMetaValue(key).trimmed();
        if (raw.isEmpty())
            return fallback;
        bool ok = false;
        const uint v = raw.toUInt(&ok);
        return ok ? v : fallback;
    };
    const auto readBool = [this](const QString &key, bool fallback) -> bool {
        const QString raw = readMetaValue(key).trimmed();
        if (raw.isEmpty())
            return fallback;

        bool ok = false;
        const uint v = raw.toUInt(&ok);
        if (ok) {
            if (v <= 1u)
                return v != 0u;
            return fallback;
        }

        const QString normalized = raw.toLower();
        if (normalized == QStringLiteral("true") || normalized == QStringLiteral("on")
            || normalized == QStringLiteral("enabled")) {
            return true;
        }
        if (normalized == QStringLiteral("false") || normalized == QStringLiteral("off")
            || normalized == QStringLiteral("disabled")) {
            return false;
        }
        return fallback;
    };
    const auto readString = [this](const QString &key, const QString &fallback) -> QString {
        const QString raw = readMetaValue(key).trimmed();
        return raw.isEmpty() ? fallback : raw;
    };

    // Defaults: keep in sync with component defaults.
    using Setting = ServerRuntimeSettings::Setting;

    const QList<Setting> settings = ServerRuntimeSettings::allSettings();
    for (Setting setting : settings) {
        const QString settingKey = QLatin1String(ServerRuntimeSettings::key(setting));
        switch (setting) {
        case Setting::MaxResourcesPerUser: {
            const uint value =
                    readUInt(settingKey,
                             ServerRuntimeSettings::defaultValue(Setting::MaxResourcesPerUser)
                                     .toUInt());
            queueModel.setMaxResourcesPerUser(value);
            upsertSetting(settingKey, QVariant::fromValue(value));
            break;
        }
        case Setting::MaxOccupationDurationMinutes: {
            const uint value = readUInt(settingKey,
                                        ServerRuntimeSettings::defaultValue(
                                                Setting::MaxOccupationDurationMinutes)
                                                .toUInt());
            queueModel.setMaxOccupationDurationMinutes(value);
            upsertSetting(settingKey, QVariant::fromValue(value));
            break;
        }
        case Setting::MenuUpdateMode:
        case Setting::MenuSessionTimeoutMinutes: {
            const uint value =
                    readUInt(settingKey, ServerRuntimeSettings::defaultValue(setting).toUInt());
            upsertSetting(settingKey, QVariant::fromValue(value));
            break;
        }
        case Setting::StatsEnabled: {
            const bool value =
                    readBool(settingKey,
                             ServerRuntimeSettings::defaultValue(Setting::StatsEnabled).toBool());
            if (stats) {
                stats->setEnabled(value);
                if (value) {
                    stats->captureInitialState(&userModel, &resourceModel, &queueModel);
                }
            }
            upsertSetting(settingKey, QVariant::fromValue(value));
            break;
        }
        case Setting::StatsAdminsOnly: {
            const bool value =
                    readBool(settingKey,
                             ServerRuntimeSettings::defaultValue(Setting::StatsAdminsOnly)
                                     .toBool());
            upsertSetting(settingKey, QVariant::fromValue(value));
            break;
        }
        case Setting::DateTimeFormat: {
            const QString defaultValue =
                    ServerRuntimeSettings::defaultValue(Setting::DateTimeFormat).toString();
            QString value = readString(settingKey, defaultValue);
            if (!value.isEmpty() && !ServerDateTimeFormat::isValidQtFormat(value)) {
                value = defaultValue;
            }
            upsertSetting(settingKey, QVariant::fromValue(value));
            break;
        }
        case Setting::PendingAccessUserReplyMessage: {
            const QString defaultValue =
                    ServerRuntimeSettings::defaultValue(Setting::PendingAccessUserReplyMessage)
                            .toString();
            QString value = readString(settingKey, defaultValue);
            const QString normalizedValue = value.toLower();
            if (normalizedValue == QStringLiteral("default")
                || normalizedValue == QStringLiteral("auto")) {
                value.clear();
            }
            upsertSetting(settingKey, QVariant::fromValue(value));
            break;
        }
        }
    }

    return true;
}

bool ServerDatabasePrivate::open()
{
    QFileInfo fileInfo(dbFilePath);
    QDir dir(fileInfo.absolutePath());
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }

    const QString backupPath = dbFilePath + ".bak";

    // If main DB is missing but backup exists, restore immediately
    if (!fileInfo.exists() && QFile::exists(backupPath)) {
        qCInfo(lcServerDatabasePrivate) << "Main database missing. Restoring from backup...";
        if (!restoreFromBackup()) {
            qCWarning(lcServerDatabasePrivate) << "Failed to restore from backup (pre-check).";
        }
    }

    db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(dbFilePath);

    auto tryOpenAndCheck = [this]() -> bool {
        if (!db.open()) {
            qCWarning(lcServerDatabasePrivate).nospace()
                    << "Failed to open SQLite database at " << dbFilePath << ": "
                    << db.lastError().text();
            return false;
        }

        QSqlQuery check(db);
        if (!check.exec(QStringLiteral("PRAGMA quick_check"))) {
            qCWarning(lcServerDatabasePrivate).nospace()
                    << "Integrity check failed to execute: " << check.lastError().text();
            db.close();
            return false;
        }
        if (!check.next()) {
            qCWarning(lcServerDatabasePrivate) << "Integrity check returned no result row.";
            db.close();
            return false;
        }

        QString result = check.value(0).toString();
        if (result != QStringLiteral("ok")) {
            qCWarning(lcServerDatabasePrivate) << "Database integrity check failed:" << result;
            db.close();
            return false;
        }
        return true;
    };

    if (!tryOpenAndCheck()) {
        qCWarning(lcServerDatabasePrivate)
                << "Database is inaccessible or corrupted. Attempting recovery...";

        if (db.isOpen())
            db.close();

        if (restoreFromBackup()) {
            qCInfo(lcServerDatabasePrivate) << "Database restored. Retrying open...";
            if (!tryOpenAndCheck()) {
                qCCritical(lcServerDatabasePrivate)
                        << "Restored database is also corrupted. Cannot proceed.";
                return false;
            }
        } else {
            qCCritical(lcServerDatabasePrivate) << "Recovery failed. No valid backup found.";
            return false;
        }
    }

    // Enable WAL mode for better concurrency and reliability.
    // This allows readers to not block writers and vice versa.
    QSqlQuery pragma(db);
    if (!pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL;"))) {
        qCWarning(lcServerDatabasePrivate)
                << "Failed to set WAL mode:" << pragma.lastError().text();
    }
    // NORMAL synchronous is safe with WAL and faster.
    if (!pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL;"))) {
        qCWarning(lcServerDatabasePrivate)
                << "Failed to set synchronous mode:" << pragma.lastError().text();
    }

    return true;
}

bool ServerDatabasePrivate::ensureSchema()
{
    QSqlQuery query(db);

    if (!query.exec(QString::fromLatin1(ServerDatabasePrivate::kMetaTableSql))) {
        logSqlError(query, QStringLiteral("create meta table"));
        return false;
    }
    if (!query.exec(QString::fromLatin1(ServerDatabasePrivate::kUsersTableSql))) {
        logSqlError(query, QStringLiteral("create users table"));
        return false;
    }
    if (!query.exec(QString::fromLatin1(ServerDatabasePrivate::kUserSettingsTableSql))) {
        logSqlError(query, QStringLiteral("create user_settings table"));
        return false;
    }
    if (!query.exec(QString::fromLatin1(ServerDatabasePrivate::kUserGroupsTableSql))) {
        logSqlError(query, QStringLiteral("create user_groups table"));
        return false;
    }
    if (!query.exec(QString::fromLatin1(ServerDatabasePrivate::kResourcesTableSql))) {
        logSqlError(query, QStringLiteral("create resources table"));
        return false;
    }
    if (!query.exec(QString::fromLatin1(ServerDatabasePrivate::kResourceGroupsTableSql))) {
        logSqlError(query, QStringLiteral("create resource_groups table"));
        return false;
    }
    if (!query.exec(QString::fromLatin1(ServerDatabasePrivate::kResourceQueueTableSql))) {
        logSqlError(query, QStringLiteral("create resource_queue table"));
        return false;
    }
    if (!query.exec(QString::fromLatin1(ServerDatabasePrivate::kResourceQueueUuidIndexSql))) {
        logSqlError(query, QStringLiteral("create resource_queue index"));
        return false;
    }
    if (!query.exec(QString::fromLatin1(ServerDatabasePrivate::kOccupationDurationsTableSql))) {
        logSqlError(query, QStringLiteral("create occupation_durations table"));
        return false;
    }
    if (!query.exec(QString::fromLatin1(ServerDatabasePrivate::kBlacklistTableSql))) {
        logSqlError(query, QStringLiteral("create blacklist table"));
        return false;
    }
    if (!query.exec(QString::fromLatin1(ServerDatabasePrivate::kPendingAccessUsersTableSql))) {
        logSqlError(query, QStringLiteral("create pending_access_users table"));
        return false;
    }

    if (!ensureSchemaVersion())
        return false;

    if (!refreshResourceStatusColumnAvailability())
        return false;

    return true;
}

bool ServerDatabasePrivate::refreshResourceStatusColumnAvailability()
{
    resourceStatusColumnAvailable = false;

    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA table_info(resources)"))) {
        logSqlError(query, QStringLiteral("check resources.status column"));
        return false;
    }

    while (query.next()) {
        const QString columnName = query.value(1).toString().trimmed().toLower();
        if (columnName == QStringLiteral("status")) {
            resourceStatusColumnAvailable = true;
            break;
        }
    }

    return true;
}

bool ServerDatabasePrivate::ensureMetaTable()
{
    QSqlQuery query(db);
    if (!query.exec(QString::fromLatin1(ServerDatabasePrivate::kMetaTableSql))) {
        logSqlError(query, QStringLiteral("ensure meta table"));
        return false;
    }
    return true;
}

bool ServerDatabasePrivate::ensureSchemaVersion()
{
    if (!ensureMetaTable())
        return false;

    QSqlQuery selectQuery(db);
    selectQuery.prepare(QStringLiteral("SELECT value FROM meta WHERE key = :key"));
    selectQuery.bindValue(QStringLiteral(":key"), QStringLiteral("schema_version"));

    if (!selectQuery.exec()) {
        logSqlError(selectQuery, QStringLiteral("select schema_version"));
        return false;
    }

    int currentVersion = 0;
    if (selectQuery.next()) {
        currentVersion = selectQuery.value(0).toInt();
    }

    if (currentVersion == 0) {
        QSqlQuery insertQuery(db);
        insertQuery.prepare(QStringLiteral("INSERT INTO meta(key, value) VALUES (:key, :value)"));
        insertQuery.bindValue(QStringLiteral(":key"), QStringLiteral("schema_version"));
        insertQuery.bindValue(QStringLiteral(":value"), targetSchemaVersion);
        if (!insertQuery.exec()) {
            logSqlError(insertQuery, QStringLiteral("insert schema_version"));
            return false;
        }
        return true;
    }

    if (currentVersion < targetSchemaVersion) {
        const int targetVersion = targetSchemaVersion;
        QElapsedTimer migrationTimer;
        migrationTimer.start();

        const auto logMigrationResult = [&](bool success) {
            const qint64 elapsedMs = migrationTimer.elapsed();
            if (success) {
                qCInfo(lcServerDatabasePrivate).nospace()
                        << "Database migration completed successfully in " << elapsedMs
                        << " ms (from v" << currentVersion << " to v" << targetVersion << ')';
            } else {
                qCCritical(lcServerDatabasePrivate).nospace()
                        << "Database migration failed after " << elapsedMs << " ms (from v"
                        << currentVersion << " to v" << targetVersion << ')';
            }
        };

        qCInfo(lcServerDatabasePrivate).nospace()
                << "Database migration started (from v" << currentVersion << " to v"
                << targetVersion << ')';

        if (!applyMigrations(currentVersion)) {
            logMigrationResult(false);
            return false;
        }

        QSqlQuery updateQuery(db);
        updateQuery.prepare(QStringLiteral("UPDATE meta SET value = :value WHERE key = :key"));
        updateQuery.bindValue(QStringLiteral(":value"), targetVersion);
        updateQuery.bindValue(QStringLiteral(":key"), QStringLiteral("schema_version"));
        if (!updateQuery.exec()) {
            logSqlError(updateQuery, QStringLiteral("update schema_version"));
            logMigrationResult(false);
            return false;
        }

        logMigrationResult(true);
    } else if (currentVersion > targetSchemaVersion) {
        qCWarning(lcServerDatabasePrivate).nospace()
                << "Database schema version " << currentVersion << " is newer than supported "
                << targetSchemaVersion;
    }

    return true;
}

bool ServerDatabasePrivate::applyMigrations(int currentVersion)
{
    if (currentVersion < 2) {
        QSqlQuery userGroupsQuery(db);
        if (!userGroupsQuery.exec(QStringLiteral(
                    "INSERT OR IGNORE INTO user_groups(messenger_type, user_id, group_path, role) "
                    "SELECT messenger_type, user_id, '', 'admin' FROM users WHERE is_admin <> "
                    "0"))) {
            logSqlError(userGroupsQuery, QStringLiteral("migrate user groups"));
            return false;
        }

        QSqlQuery resourceGroupsQuery(db);
        if (!resourceGroupsQuery.exec(QStringLiteral(
                    "INSERT OR IGNORE INTO resource_groups(resource_uuid, group_path) "
                    "SELECT uuid, '' FROM resources"))) {
            logSqlError(resourceGroupsQuery, QStringLiteral("migrate resource groups"));
            return false;
        }
    }

    if (currentVersion < 3) {
        QSqlQuery startModeQuery(db);
        if (!startModeQuery.exec(
                    QStringLiteral("ALTER TABLE resource_queue "
                                   "ADD COLUMN start_mode TEXT NOT NULL DEFAULT 'at_time'"))) {
            logSqlError(startModeQuery, QStringLiteral("migrate resource queue start mode"));
            return false;
        }

        QSqlQuery durationQuery(db);
        if (!durationQuery.exec(
                    QStringLiteral("ALTER TABLE resource_queue "
                                   "ADD COLUMN duration_minutes INTEGER NOT NULL DEFAULT -1"))) {
            logSqlError(durationQuery, QStringLiteral("migrate resource queue duration minutes"));
            return false;
        }
    }

    return true;
}

bool ServerDatabasePrivate::loadUsers()
{
    QHash<QString, UserSettingsRecord> settingsByKey;
    if (!loadUserSettings(&settingsByKey))
        return false;

    QHash<QString, QMap<Group, User::Role>> groupsByUser;
    if (!loadUserGroups(&groupsByUser))
        return false;

    QSqlQuery query(db);
    if (!query.exec(
                QStringLiteral("SELECT messenger_type, user_id, first_name, last_name, is_admin "
                               "FROM users ORDER BY messenger_type ASC, user_id ASC"))) {
        logSqlError(query, QStringLiteral("load users"));
        return false;
    }

    QList<User> users;
    while (query.next()) {
        const QString messengerStr = query.value(0).toString();
        const MessengerType messenger = messengerTypeFromString(messengerStr);
        const QString id = query.value(1).toString();
        const QString first = query.value(2).toString();
        const QString last = query.value(3).toString();
        const bool isAdmin = query.value(4).toInt() != 0;

        User user(messenger, id, first, last, isAdmin);
        const MessengerUserKey userKey(messenger, id);
        const QString key = userSettingsKey(userKey);
        if (groupsByUser.contains(key)) {
            user.setGroupRoles(groupsByUser.value(key));
        }
        const UserSettingsRecord settings = settingsByKey.value(key);
        if (!settings.locale.isEmpty())
            user.setLocale(settings.locale);
        if (settings.timezone.hasCustomOffset())
            user.setTimezone(settings.timezone);
        user.setOccupationNotifyMode(settings.notifyMode);
        users.append(user);
    }

    userModel.replaceAll(users);
    usersDirty = false;
    return true;
}

bool ServerDatabasePrivate::loadUserGroups(QHash<QString, QMap<Group, User::Role>> *groupsByUser)
{
    if (!groupsByUser)
        return true;

    groupsByUser->clear();

    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT messenger_type, user_id, group_path, role "
                                   "FROM user_groups"))) {
        logSqlError(query, QStringLiteral("load user groups"));
        return false;
    }

    while (query.next()) {
        const MessengerType messenger = messengerTypeFromString(query.value(0).toString());
        const QString userId = query.value(1).toString();
        const Group group(query.value(2).toString());
        const User::Role role = roleFromStorageString(query.value(3).toString());
        const QString key = userSettingsKey(MessengerUserKey(messenger, userId));
        (*groupsByUser)[key].insert(group, role);
    }

    return true;
}

bool ServerDatabasePrivate::loadUserSettings(QHash<QString, UserSettingsRecord> *settingsByKey)
{
    if (!settingsByKey)
        return true;

    settingsByKey->clear();

    const auto normalizeNotifyMode = [](const QVariant &value) -> User::OccupationNotifyMode {
        bool ok = false;
        const int raw = value.toInt(&ok);
        if (!ok)
            return User::OccupationNotifyMode::OwnResourcesOnly;
        switch (raw) {
        case 0:
            return User::OccupationNotifyMode::AllResources;
        case 1:
            return User::OccupationNotifyMode::OwnResourcesOnly;
        case 2:
            return User::OccupationNotifyMode::Disabled;
        default:
            return User::OccupationNotifyMode::OwnResourcesOnly;
        }
    };

    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT messenger_type, user_id, locale, timezone, "
                                   "occupation_notify_mode "
                                   "FROM user_settings"))) {
        logSqlError(query, QStringLiteral("load user settings"));
        return false;
    }

    while (query.next()) {
        const MessengerType messenger = messengerTypeFromString(query.value(0).toString());
        const QString userId = query.value(1).toString();
        const QString locale = query.value(2).toString().trimmed();
        const UserTimeZone timezone = UserTimeZone::fromStorageValue(query.value(3).toString());
        const User::OccupationNotifyMode notifyMode = normalizeNotifyMode(query.value(4));

        UserSettingsRecord record;
        record.locale = locale;
        record.timezone = timezone;
        record.notifyMode = notifyMode;
        settingsByKey->insert(userSettingsKey(MessengerUserKey(messenger, userId)), record);
    }

    return true;
}

bool ServerDatabasePrivate::loadResources()
{
    QHash<QString, QSet<Group>> groupsByResource;
    if (!loadResourceGroups(&groupsByResource))
        return false;

    QSqlQuery query(db);
    const QString selectSql = resourceStatusColumnAvailable
                                      ? QStringLiteral("SELECT uuid, name, description, "
                                                       "start_message, status "
                                                       "FROM resources ORDER BY name ASC")
                                      : QStringLiteral("SELECT uuid, name, description, "
                                                       "start_message "
                                                       "FROM resources ORDER BY name ASC");
    if (!query.exec(selectSql)) {
        logSqlError(query, QStringLiteral("load resources"));
        return false;
    }

    QList<Resource> resources;
    while (query.next()) {
        const QString uuidText = query.value(0).toString();
        const QUuid uuid = Resource::uuidFromString(uuidText);
        const QString name = query.value(1).toString();
        const QString description = query.value(2).toString();
        const QString start = query.value(3).toString();
        const Resource::Status status =
                resourceStatusColumnAvailable
                        ? Resource::statusFromStorageString(query.value(4).toString())
                        : Resource::Status::On;
        if (uuid.isNull() || name.trimmed().isEmpty())
            continue;
        Resource resource(uuid, name, description, start, status);
        resource.setGroups(groupsByResource.value(Resource::uuidToString(uuid)));
        resources.append(resource);
    }

    resourceModel.replaceAll(resources);
    resourcesDirty = false;
    return true;
}

bool ServerDatabasePrivate::loadResourceGroups(QHash<QString, QSet<Group>> *groupsByResource)
{
    if (!groupsByResource)
        return true;

    groupsByResource->clear();

    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT resource_uuid, group_path FROM resource_groups"))) {
        logSqlError(query, QStringLiteral("load resource groups"));
        return false;
    }

    while (query.next()) {
        const QString uuid = Resource::normalizeUuidString(query.value(0).toString());
        if (uuid.isEmpty())
            continue;
        (*groupsByResource)[uuid].insert(Group(query.value(1).toString()));
    }

    return true;
}

bool ServerDatabasePrivate::loadResourceQueueEntries()
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT id, resource_uuid, messenger_type, user_id, "
                                   "enqueued_at, start_at, end_at, start_mode, duration_minutes "
                                   "FROM resource_queue "
                                   "ORDER BY id ASC"))) {
        logSqlError(query, QStringLiteral("load resource queues"));
        return false;
    }

    QList<ResourceQueueEntry> entries;
    QList<qint64> invalidIds;
    while (query.next()) {
        const qint64 rowId = query.value(0).toLongLong();
        const ResourceQueueEntry entry = ServerDatabasePrivate::queueEntryFromQuery(query);
        const Resource resource = resourceModel.resourceByUuid(entry.resourceUuid());
        if (!entry.isValid() || !resource.isValid()
            || !userHasAccessToResource(entry.user(), resource)) {
            if (rowId > 0)
                invalidIds.append(rowId);
            continue;
        }
        entries.append(entry);
    }

    QSqlQuery deleteInvalid(db);
    deleteInvalid.prepare(QStringLiteral("DELETE FROM resource_queue WHERE id = :id"));
    for (qint64 rowId : std::as_const(invalidIds)) {
        deleteInvalid.bindValue(QStringLiteral(":id"), rowId);
        if (!deleteInvalid.exec()) {
            qCWarning(lcServerDatabasePrivate)
                    << "Failed to remove an invalid persisted queue entry.";
            return false;
        }
    }

    queueModel.replaceAll(entries);
    queueDirty = false;
    return true;
}

bool ServerDatabasePrivate::loadBlacklistEntries()
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT messenger_type, user_id, reason, blocked_at, "
                                   "blocked_until, blocked_by_messenger, blocked_by_user_id "
                                   "FROM blacklist "
                                   "ORDER BY blocked_at DESC"))) {
        logSqlError(query, QStringLiteral("load blacklist entries"));
        return false;
    }

    QList<BlacklistEntry> entries;
    while (query.next()) {
        entries.append(ServerDatabasePrivate::blacklistEntryFromQuery(query));
    }

    blacklistModel.replaceAll(entries);
    return true;
}

bool ServerDatabasePrivate::loadPendingAccessUsers()
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT messenger_type, user_id, first_name, last_name, "
                                   "last_message_at "
                                   "FROM pending_access_users "
                                   "ORDER BY last_message_at DESC"))) {
        logSqlError(query, QStringLiteral("load users awaiting access activation"));
        return false;
    }

    QList<PendingAccessUserEntry> entries;
    while (query.next()) {
        entries.append(ServerDatabasePrivate::pendingAccessUserFromQuery(query));
    }

    pendingAccessUserModel.replaceAll(entries);
    return true;
}

bool ServerDatabasePrivate::saveAll()
{
    if (!ready)
        return false;

    if (!usersDirty && !resourcesDirty && !queueDirty)
        return true;

    if (!db.isOpen() && !db.open()) {
        qCWarning(lcServerDatabasePrivate)
                << "Unable to reopen SQLite database for saving." << db.lastError().text();
        return false;
    }

    if (!db.transaction()) {
        qCWarning(lcServerDatabasePrivate)
                << "Failed to start transaction for saving models:" << db.lastError().text();
        return false;
    }

    auto rollbackOnError = [this]() {
        if (!db.rollback()) {
            qCWarning(lcServerDatabasePrivate)
                    << "Failed to rollback transaction:" << db.lastError().text();
        }
    };

    if (usersDirty && !saveUsers()) {
        rollbackOnError();
        return false;
    }

    if (resourcesDirty && !saveResources()) {
        rollbackOnError();
        return false;
    }

    if (queueDirty && !saveResourceQueueEntries()) {
        rollbackOnError();
        return false;
    }

    if (!db.commit()) {
        qCWarning(lcServerDatabasePrivate)
                << "Failed to commit transaction when saving models:" << db.lastError().text();
        rollbackOnError();
        return false;
    }

    if (usersDirty)
        usersDirty = false;
    if (resourcesDirty)
        resourcesDirty = false;
    if (queueDirty)
        queueDirty = false;

    writeBackupSnapshot();
    return true;
}

bool ServerDatabasePrivate::saveUsers()
{
    QSqlQuery deleteQuery(db);
    if (!deleteQuery.exec(QStringLiteral("DELETE FROM users"))) {
        logSqlError(deleteQuery, QStringLiteral("saveUsers delete"));
        return false;
    }

    QSqlQuery insertQuery(db);
    insertQuery.prepare(QStringLiteral(
            "INSERT INTO users(messenger_type, user_id, first_name, last_name, is_admin) "
            "VALUES (:messenger, :id, :first, :last, :is_admin)"));

    const QList<User> &users = userModel.items();
    for (const User &user : users) {
        insertQuery.bindValue(QStringLiteral(":messenger"),
                              messengerTypeToString(user.messenger()));
        insertQuery.bindValue(QStringLiteral(":id"), user.userId());
        insertQuery.bindValue(QStringLiteral(":first"), user.firstName());
        insertQuery.bindValue(QStringLiteral(":last"), user.lastName());
        insertQuery.bindValue(QStringLiteral(":is_admin"), user.isAdmin() ? 1 : 0);

        if (!insertQuery.exec()) {
            logSqlError(insertQuery, QStringLiteral("saveUsers insert"));
            return false;
        }
    }

    if (!replaceUserSettings(users))
        return false;

    if (!replaceUserGroups(users))
        return false;

    return true;
}

bool ServerDatabasePrivate::replaceUserSettings(const QList<User> &users)
{
    QSqlQuery deleteQuery(db);
    if (!deleteQuery.exec(QStringLiteral("DELETE FROM user_settings"))) {
        logSqlError(deleteQuery, QStringLiteral("clear user_settings"));
        return false;
    }

    QSqlQuery insertQuery(db);
    insertQuery.prepare(QStringLiteral("INSERT INTO user_settings(messenger_type, user_id, "
                                       "locale, timezone, occupation_notify_mode) "
                                       "VALUES (:messenger, :user, :locale, :timezone, "
                                       ":notify_mode)"));

    for (const User &user : users) {
        const QString locale = user.locale().trimmed();
        const UserTimeZone timezone = user.timezone();
        const User::OccupationNotifyMode notifyMode = user.occupationNotifyMode();
        const bool isDefaultLocale = locale.isEmpty();
        const bool isDefaultTimezone = !timezone.hasCustomOffset();
        const bool isDefaultNotify = notifyMode == User::OccupationNotifyMode::OwnResourcesOnly;
        if (isDefaultLocale && isDefaultTimezone && isDefaultNotify)
            continue;

        insertQuery.bindValue(QStringLiteral(":messenger"),
                              messengerTypeToString(user.messenger()));
        insertQuery.bindValue(QStringLiteral(":user"), user.userId());
        insertQuery.bindValue(QStringLiteral(":locale"), locale);
        if (timezone.hasCustomOffset()) {
            insertQuery.bindValue(QStringLiteral(":timezone"),
                                  static_cast<int>(timezone.utcOffsetMinutes()));
        } else {
            insertQuery.bindValue(QStringLiteral(":timezone"), QVariant());
        }
        insertQuery.bindValue(QStringLiteral(":notify_mode"), static_cast<int>(notifyMode));

        if (!insertQuery.exec()) {
            logSqlError(insertQuery, QStringLiteral("insert user_settings"));
            return false;
        }
    }

    return true;
}

bool ServerDatabasePrivate::replaceUserGroups(const QList<User> &users)
{
    QSqlQuery deleteQuery(db);
    if (!deleteQuery.exec(QStringLiteral("DELETE FROM user_groups"))) {
        logSqlError(deleteQuery, QStringLiteral("clear user_groups"));
        return false;
    }

    for (const User &user : users) {
        if (!upsertUserGroups(user))
            return false;
    }

    return true;
}

bool ServerDatabasePrivate::saveResources()
{
    QSqlQuery deleteQuery(db);
    if (!deleteQuery.exec(QStringLiteral("DELETE FROM resources"))) {
        logSqlError(deleteQuery, QStringLiteral("saveResources delete"));
        return false;
    }

    QSqlQuery insertQuery(db);
    if (resourceStatusColumnAvailable) {
        insertQuery.prepare(
                QStringLiteral("INSERT INTO resources(uuid, name, description, start_message, "
                               "status) "
                               "VALUES (:uuid, :name, :description, :start, :status)"));
    } else {
        insertQuery.prepare(
                QStringLiteral("INSERT INTO resources(uuid, name, description, start_message) "
                               "VALUES (:uuid, :name, :description, :start)"));
    }

    const QList<Resource> &resources = resourceModel.items();
    for (const Resource &resource : resources) {
        insertQuery.bindValue(QStringLiteral(":uuid"), resource.uuidString());
        insertQuery.bindValue(QStringLiteral(":name"), resource.name());
        insertQuery.bindValue(QStringLiteral(":description"), resource.description());
        insertQuery.bindValue(QStringLiteral(":start"), resource.startMessage());
        if (resourceStatusColumnAvailable) {
            insertQuery.bindValue(QStringLiteral(":status"),
                                  Resource::statusToStorageString(resource.status()));
        }

        if (!insertQuery.exec()) {
            logSqlError(insertQuery, QStringLiteral("saveResources insert"));
            return false;
        }
    }

    if (!replaceResourceGroups(resources))
        return false;

    return true;
}

bool ServerDatabasePrivate::replaceResourceGroups(const QList<Resource> &resources)
{
    QSqlQuery deleteQuery(db);
    if (!deleteQuery.exec(QStringLiteral("DELETE FROM resource_groups"))) {
        logSqlError(deleteQuery, QStringLiteral("clear resource_groups"));
        return false;
    }

    for (const Resource &resource : resources) {
        if (!upsertResourceGroups(resource))
            return false;
    }

    return true;
}

bool ServerDatabasePrivate::saveResourceQueueEntries()
{
    QSqlQuery deleteQuery(db);
    if (!deleteQuery.exec(QStringLiteral("DELETE FROM resource_queue"))) {
        logSqlError(deleteQuery, QStringLiteral("save resource queue delete"));
        return false;
    }

    QSqlQuery insertQuery(db);
    insertQuery.prepare(
            QStringLiteral("INSERT INTO resource_queue(resource_uuid, messenger_type, "
                           "user_id, enqueued_at, start_at, end_at, start_mode, "
                           "duration_minutes) "
                           "VALUES (:uuid, :messenger, :user, :enqueued_at, :start_at, :end_at, "
                           ":start_mode, :duration_minutes)"));

    const QList<ResourceQueueEntry> &entries = queueModel.items();
    for (const ResourceQueueEntry &entry : entries) {
        insertQuery.bindValue(QStringLiteral(":uuid"), entry.resourceUuid());
        const MessengerUserKey user = entry.user();
        insertQuery.bindValue(QStringLiteral(":messenger"),
                              messengerTypeToString(user.messenger()));
        insertQuery.bindValue(QStringLiteral(":user"), user.userId());
        const qint64 enqueuedAt = ServerDatabasePrivate::toEpochSeconds(entry.enqueuedAtUtc());
        insertQuery.bindValue(QStringLiteral(":enqueued_at"), enqueuedAt);
        insertQuery.bindValue(QStringLiteral(":start_at"), toEpochSeconds(entry.startAtUtc()));
        insertQuery.bindValue(QStringLiteral(":end_at"), toEpochSeconds(entry.endAtUtc()));
        insertQuery.bindValue(QStringLiteral(":start_mode"),
                              ResourceQueueEntry::startModeToStorageToken(entry.startMode()));
        insertQuery.bindValue(QStringLiteral(":duration_minutes"),
                              entry.requestedDurationMinutes());

        if (!insertQuery.exec()) {
            logSqlError(insertQuery, QStringLiteral("save resource queue insert"));
            return false;
        }
    }

    return true;
}

bool ServerDatabasePrivate::upsertUserRecord(const User &user)
{
    if (!db.isOpen() && !db.open()) {
        qCWarning(lcServerDatabasePrivate)
                << "Unable to open SQLite database to update user:" << db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
            "INSERT INTO users(messenger_type, user_id, first_name, last_name, is_admin) "
            "VALUES (:messenger, :user, :first, :last, :is_admin) "
            "ON CONFLICT(messenger_type, user_id) DO UPDATE SET "
            "first_name = excluded.first_name, "
            "last_name = excluded.last_name, "
            "is_admin = excluded.is_admin"));
    query.bindValue(QStringLiteral(":messenger"), messengerTypeToString(user.messenger()));
    query.bindValue(QStringLiteral(":user"), user.userId());
    query.bindValue(QStringLiteral(":first"), user.firstName());
    query.bindValue(QStringLiteral(":last"), user.lastName());
    query.bindValue(QStringLiteral(":is_admin"), user.isAdmin() ? 1 : 0);

    if (!query.exec()) {
        logSqlError(query, QStringLiteral("upsert user record"));
        return false;
    }

    return upsertUserGroups(user);
}

bool ServerDatabasePrivate::upsertUserGroups(const User &user)
{
    if (!deleteUserGroups(user.key()))
        return false;

    const QMap<Group, User::Role> roles = user.groupRoles();
    if (roles.isEmpty())
        return true;

    QSqlQuery query(db);
    query.prepare(QStringLiteral("INSERT INTO user_groups(messenger_type, user_id, group_path, "
                                 "role) "
                                 "VALUES (:messenger, :user, :group, :role)"));
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
        query.bindValue(QStringLiteral(":messenger"), messengerTypeToString(user.messenger()));
        query.bindValue(QStringLiteral(":user"), user.userId());
        query.bindValue(QStringLiteral(":group"), groupToStorageString(it.key()));
        query.bindValue(QStringLiteral(":role"), roleToStorageString(it.value()));
        if (!query.exec()) {
            logSqlError(query, QStringLiteral("upsert user groups"));
            return false;
        }
    }

    return true;
}

bool ServerDatabasePrivate::upsertUserSettings(const User &user)
{
    const QString locale = user.locale().trimmed();
    const UserTimeZone timezone = user.timezone();
    const User::OccupationNotifyMode notifyMode = user.occupationNotifyMode();
    const bool isDefaultLocale = locale.isEmpty();
    const bool isDefaultTimezone = !timezone.hasCustomOffset();
    const bool isDefaultNotify = notifyMode == User::OccupationNotifyMode::OwnResourcesOnly;
    if (isDefaultLocale && isDefaultTimezone && isDefaultNotify)
        return deleteUserSettings(user.key());

    if (!db.isOpen() && !db.open()) {
        qCWarning(lcServerDatabasePrivate)
                << "Unable to open SQLite database to update user settings:"
                << db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral("INSERT INTO user_settings(messenger_type, user_id, locale, "
                                 "timezone, occupation_notify_mode) "
                                 "VALUES (:messenger, :user, :locale, :timezone, :notify_mode) "
                                 "ON CONFLICT(messenger_type, user_id) DO UPDATE SET "
                                 "locale = excluded.locale, "
                                 "timezone = excluded.timezone, "
                                 "occupation_notify_mode = excluded.occupation_notify_mode"));
    query.bindValue(QStringLiteral(":messenger"), messengerTypeToString(user.messenger()));
    query.bindValue(QStringLiteral(":user"), user.userId());
    query.bindValue(QStringLiteral(":locale"), locale);
    if (timezone.hasCustomOffset()) {
        query.bindValue(QStringLiteral(":timezone"), static_cast<int>(timezone.utcOffsetMinutes()));
    } else {
        query.bindValue(QStringLiteral(":timezone"), QVariant());
    }
    query.bindValue(QStringLiteral(":notify_mode"), static_cast<int>(notifyMode));

    if (!query.exec()) {
        logSqlError(query, QStringLiteral("upsert user settings"));
        return false;
    }

    return true;
}

bool ServerDatabasePrivate::deleteUserRecord(const MessengerUserKey &user)
{
    if (!db.isOpen() && !db.open()) {
        qCWarning(lcServerDatabasePrivate)
                << "Unable to open SQLite database to delete user:" << db.lastError().text();
        return false;
    }

    if (!deleteUserGroups(user))
        return false;

    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM users "
                                 "WHERE messenger_type = :messenger AND user_id = :user"));
    query.bindValue(QStringLiteral(":messenger"), messengerTypeToString(user.messenger()));
    query.bindValue(QStringLiteral(":user"), user.userId());

    if (!query.exec()) {
        logSqlError(query, QStringLiteral("delete user record"));
        return false;
    }

    return true;
}

bool ServerDatabasePrivate::deleteUserGroups(const MessengerUserKey &user)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM user_groups "
                                 "WHERE messenger_type = :messenger AND user_id = :user"));
    query.bindValue(QStringLiteral(":messenger"), messengerTypeToString(user.messenger()));
    query.bindValue(QStringLiteral(":user"), user.userId());

    if (!query.exec()) {
        logSqlError(query, QStringLiteral("delete user groups"));
        return false;
    }

    return true;
}

bool ServerDatabasePrivate::deleteUserSettings(const MessengerUserKey &user)
{
    if (!db.isOpen() && !db.open()) {
        qCWarning(lcServerDatabasePrivate)
                << "Unable to open SQLite database to delete user settings:"
                << db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM user_settings "
                                 "WHERE messenger_type = :messenger AND user_id = :user"));
    query.bindValue(QStringLiteral(":messenger"), messengerTypeToString(user.messenger()));
    query.bindValue(QStringLiteral(":user"), user.userId());

    if (!query.exec()) {
        logSqlError(query, QStringLiteral("delete user settings"));
        return false;
    }

    return true;
}

bool ServerDatabasePrivate::persistUserSnapshot(const User &user, const QString &errorMessage)
{
    if (upsertUserRecord(user) && persistUserSettingsRecord(user))
        return true;

    emit q->errorOccurred(errorMessage);
    return false;
}

bool ServerDatabasePrivate::persistUserDeletion(const MessengerUserKey &user,
                                                const QString &errorMessage)
{
    if (deleteUserRecord(user) && deleteUserSettings(user))
        return true;

    emit q->errorOccurred(errorMessage);
    return false;
}

bool ServerDatabasePrivate::persistUserSettingsRecord(const User &user)
{
    return upsertUserSettings(user);
}

bool ServerDatabasePrivate::upsertResourceRecord(const Resource &resource)
{
    if (!db.isOpen() && !db.open()) {
        qCWarning(lcServerDatabasePrivate)
                << "Unable to open SQLite database to update resource:" << db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    if (resourceStatusColumnAvailable) {
        query.prepare(
                QStringLiteral("INSERT INTO resources(uuid, name, description, start_message, "
                               "status) "
                               "VALUES (:uuid, :name, :description, :start, :status) "
                               "ON CONFLICT(uuid) DO UPDATE SET "
                               "name = excluded.name, "
                               "description = excluded.description, "
                               "start_message = excluded.start_message, "
                               "status = excluded.status"));
    } else {
        query.prepare(
                QStringLiteral("INSERT INTO resources(uuid, name, description, start_message) "
                               "VALUES (:uuid, :name, :description, :start) "
                               "ON CONFLICT(uuid) DO UPDATE SET "
                               "name = excluded.name, "
                               "description = excluded.description, "
                               "start_message = excluded.start_message"));
    }
    query.bindValue(QStringLiteral(":uuid"), resource.uuidString());
    query.bindValue(QStringLiteral(":name"), resource.name());
    query.bindValue(QStringLiteral(":description"), resource.description());
    query.bindValue(QStringLiteral(":start"), resource.startMessage());
    if (resourceStatusColumnAvailable) {
        query.bindValue(QStringLiteral(":status"),
                        Resource::statusToStorageString(resource.status()));
    }

    if (!query.exec()) {
        logSqlError(query, QStringLiteral("upsert resource record"));
        return false;
    }

    return upsertResourceGroups(resource);
}

bool ServerDatabasePrivate::upsertResourceGroups(const Resource &resource)
{
    const QString uuid = resource.uuidString();
    if (!deleteResourceGroups(uuid))
        return false;

    QSqlQuery query(db);
    query.prepare(QStringLiteral("INSERT INTO resource_groups(resource_uuid, group_path) "
                                 "VALUES (:uuid, :group)"));
    const QSet<Group> groups = resource.groups();
    for (const Group &group : groups) {
        query.bindValue(QStringLiteral(":uuid"), uuid);
        query.bindValue(QStringLiteral(":group"), groupToStorageString(group));
        if (!query.exec()) {
            logSqlError(query, QStringLiteral("upsert resource groups"));
            return false;
        }
    }

    return true;
}

bool ServerDatabasePrivate::deleteResourceRecord(const QString &uuid)
{
    if (!db.isOpen() && !db.open()) {
        qCWarning(lcServerDatabasePrivate)
                << "Unable to open SQLite database to delete resource:" << db.lastError().text();
        return false;
    }

    if (!deleteResourceGroups(uuid))
        return false;

    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM resources WHERE uuid = :uuid"));
    query.bindValue(QStringLiteral(":uuid"), uuid);

    if (!query.exec()) {
        logSqlError(query, QStringLiteral("delete resource record"));
        return false;
    }

    return true;
}

bool ServerDatabasePrivate::deleteResourceGroups(const QString &uuid)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM resource_groups WHERE resource_uuid = :uuid"));
    query.bindValue(QStringLiteral(":uuid"), Resource::normalizeUuidString(uuid));

    if (!query.exec()) {
        logSqlError(query, QStringLiteral("delete resource groups"));
        return false;
    }

    return true;
}

bool ServerDatabasePrivate::persistResourceSnapshot(const Resource &resource,
                                                    const QString &errorMessage)
{
    if (upsertResourceRecord(resource))
        return true;

    emit q->errorOccurred(errorMessage);
    return false;
}

bool ServerDatabasePrivate::persistResourceDeletion(const QString &uuid,
                                                    const QString &errorMessage)
{
    if (deleteResourceRecord(uuid))
        return true;

    emit q->errorOccurred(errorMessage);
    return false;
}

bool ServerDatabasePrivate::upsertQueueEntry(const ResourceQueueEntry &entry)
{
    if (!db.isOpen() && !db.open()) {
        qCWarning(lcServerDatabasePrivate)
                << "Unable to open SQLite database to upsert queue entry:" << db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
            "INSERT INTO resource_queue(resource_uuid, messenger_type, user_id, enqueued_at, "
            "start_at, end_at, start_mode, duration_minutes) "
            "VALUES (:uuid, :messenger, :user, :enqueued_at, :start_at, :end_at, :start_mode, "
            ":duration_minutes) "
            "ON CONFLICT(resource_uuid, messenger_type, user_id, start_at) DO UPDATE SET "
            "enqueued_at = excluded.enqueued_at, "
            "end_at = excluded.end_at, "
            "start_mode = excluded.start_mode, "
            "duration_minutes = excluded.duration_minutes"));
    query.bindValue(QStringLiteral(":uuid"), entry.resourceUuid());
    const MessengerUserKey user = entry.user();
    query.bindValue(QStringLiteral(":messenger"), messengerTypeToString(user.messenger()));
    query.bindValue(QStringLiteral(":user"), user.userId());
    const qint64 enqueuedAt = ServerDatabasePrivate::toEpochSeconds(entry.enqueuedAtUtc());
    query.bindValue(QStringLiteral(":enqueued_at"), enqueuedAt);
    query.bindValue(QStringLiteral(":start_at"),
                    ServerDatabasePrivate::toEpochSeconds(entry.startAtUtc()));
    query.bindValue(QStringLiteral(":end_at"),
                    ServerDatabasePrivate::toEpochSeconds(entry.endAtUtc()));
    query.bindValue(QStringLiteral(":start_mode"),
                    ResourceQueueEntry::startModeToStorageToken(entry.startMode()));
    query.bindValue(QStringLiteral(":duration_minutes"), entry.requestedDurationMinutes());

    if (!query.exec()) {
        logSqlError(query, QStringLiteral("upsert queue entry"));
        return false;
    }

    return true;
}

bool ServerDatabasePrivate::deleteQueueEntry(const ResourceQueueKey &key)
{
    if (!db.isOpen() && !db.open()) {
        qCWarning(lcServerDatabasePrivate)
                << "Unable to open SQLite database to delete queue entry:" << db.lastError().text();
        return false;
    }

    QSqlQuery deleteQuery(db);
    if (!deleteQuery.prepare(QStringLiteral("DELETE FROM resource_queue "
                                            "WHERE resource_uuid = :uuid "
                                            "AND messenger_type = :messenger "
                                            "AND user_id = :user "
                                            "AND start_at = :start_at"))) {
        logSqlError(deleteQuery, QStringLiteral("prepare queue-entry deletion"));
        return false;
    }
    deleteQuery.bindValue(QStringLiteral(":uuid"), key.resourceUuid());
    deleteQuery.bindValue(QStringLiteral(":messenger"),
                          messengerTypeToString(key.user().messenger()));
    deleteQuery.bindValue(QStringLiteral(":user"), key.user().userId());
    deleteQuery.bindValue(QStringLiteral(":start_at"),
                          ServerDatabasePrivate::toEpochSeconds(key.startAtUtc()));

    if (!deleteQuery.exec()) {
        logSqlError(deleteQuery, QStringLiteral("delete queue entry"));
        return false;
    }

    return true;
}

bool ServerDatabasePrivate::deleteQueueEntries(const QList<ResourceQueueKey> &keys)
{
    if (keys.isEmpty())
        return true;

    if (!db.isOpen() && !db.open()) {
        qCWarning(lcServerDatabasePrivate)
                << "Unable to open SQLite database to delete queue entries:"
                << db.lastError().text();
        return false;
    }

    if (!db.transaction()) {
        qCWarning(lcServerDatabasePrivate)
                << "Failed to start queue-entry deletion transaction:" << db.lastError().text();
        return false;
    }

    const auto rollback = [this]() {
        if (!db.rollback()) {
            qCWarning(lcServerDatabasePrivate)
                    << "Failed to rollback queue-entry deletion transaction:"
                    << db.lastError().text();
        }
    };

    QSqlQuery deleteQuery(db);
    if (!deleteQuery.prepare(QStringLiteral("DELETE FROM resource_queue "
                                            "WHERE resource_uuid = :uuid "
                                            "AND messenger_type = :messenger "
                                            "AND user_id = :user "
                                            "AND start_at = :start_at"))) {
        logSqlError(deleteQuery, QStringLiteral("prepare queue-entry deletion"));
        rollback();
        return false;
    }
    for (const ResourceQueueKey &key : keys) {
        deleteQuery.bindValue(QStringLiteral(":uuid"), key.resourceUuid());
        deleteQuery.bindValue(QStringLiteral(":messenger"),
                              messengerTypeToString(key.user().messenger()));
        deleteQuery.bindValue(QStringLiteral(":user"), key.user().userId());
        deleteQuery.bindValue(QStringLiteral(":start_at"),
                              ServerDatabasePrivate::toEpochSeconds(key.startAtUtc()));
        if (!deleteQuery.exec()) {
            logSqlError(deleteQuery, QStringLiteral("delete queue entries"));
            rollback();
            return false;
        }
    }

    if (!db.commit()) {
        qCWarning(lcServerDatabasePrivate)
                << "Failed to commit queue-entry deletion transaction:" << db.lastError().text();
        rollback();
        return false;
    }

    return true;
}

bool ServerDatabasePrivate::deleteQueueEntriesForUser(const MessengerUserKey &user)
{
    if (!db.isOpen() && !db.open()) {
        qCWarning(lcServerDatabasePrivate)
                << "Unable to open SQLite database to delete user queue entries:"
                << db.lastError().text();
        return false;
    }

    QSqlQuery deleteQuery(db);
    deleteQuery.prepare(QStringLiteral("DELETE FROM resource_queue "
                                       "WHERE messenger_type = :messenger "
                                       "AND user_id = :user"));
    deleteQuery.bindValue(QStringLiteral(":messenger"), messengerTypeToString(user.messenger()));
    deleteQuery.bindValue(QStringLiteral(":user"), user.userId());

    if (!deleteQuery.exec()) {
        logSqlError(deleteQuery, QStringLiteral("delete queue entries by user"));
        return false;
    }

    return true;
}

bool ServerDatabasePrivate::upsertBlacklistRecord(const BlacklistEntry &entry)
{
    if (!db.isOpen() && !db.open()) {
        qCWarning(lcServerDatabasePrivate)
                << "Unable to open SQLite database to update blacklist:" << db.lastError().text();
        return false;
    }

    const qint64 blockedAt = ServerDatabasePrivate::toEpochSeconds(entry.blockedAtUtc());

    QVariant blockedUntilVariant;
    if (entry.blockedUntilUtc().isValid()) {
        blockedUntilVariant = ServerDatabasePrivate::toEpochSeconds(entry.blockedUntilUtc());
    }

    QVariant blockedByMessengerVariant;
    QVariant blockedByUserIdVariant;
    const MessengerUserKey blockedBy = entry.blockedBy();
    if (blockedBy.isValid()) {
        blockedByMessengerVariant = messengerTypeToString(blockedBy.messenger());
        blockedByUserIdVariant = blockedBy.userId();
    }

    QSqlQuery query(db);
    query.prepare(
            QStringLiteral("INSERT INTO blacklist(messenger_type, user_id, reason, blocked_at, "
                           "blocked_until, blocked_by_messenger, blocked_by_user_id) "
                           "VALUES (:messenger, :user, :reason, :blocked_at, :blocked_until, "
                           ":blocked_by_messenger, :blocked_by_user_id) "
                           "ON CONFLICT(messenger_type, user_id) DO UPDATE SET "
                           "reason = excluded.reason, "
                           "blocked_at = excluded.blocked_at, "
                           "blocked_until = excluded.blocked_until, "
                           "blocked_by_messenger = excluded.blocked_by_messenger, "
                           "blocked_by_user_id = excluded.blocked_by_user_id"));
    query.bindValue(QStringLiteral(":messenger"), messengerTypeToString(entry.user().messenger()));
    query.bindValue(QStringLiteral(":user"), entry.user().userId());
    query.bindValue(QStringLiteral(":reason"), entry.reason());
    query.bindValue(QStringLiteral(":blocked_at"), blockedAt);
    query.bindValue(QStringLiteral(":blocked_until"), blockedUntilVariant);
    query.bindValue(QStringLiteral(":blocked_by_messenger"), blockedByMessengerVariant);
    query.bindValue(QStringLiteral(":blocked_by_user_id"), blockedByUserIdVariant);

    if (!query.exec()) {
        logSqlError(query, QStringLiteral("upsert blacklist entry"));
        return false;
    }

    return true;
}

bool ServerDatabasePrivate::deleteBlacklistRecord(const MessengerUserKey &user)
{
    if (!db.isOpen() && !db.open()) {
        qCWarning(lcServerDatabasePrivate)
                << "Unable to open SQLite database to remove blacklist entry:"
                << db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM blacklist "
                                 "WHERE messenger_type = :messenger AND user_id = :user"));
    query.bindValue(QStringLiteral(":messenger"), messengerTypeToString(user.messenger()));
    query.bindValue(QStringLiteral(":user"), user.userId());

    if (!query.exec()) {
        logSqlError(query, QStringLiteral("delete blacklist entry"));
        return false;
    }

    return query.numRowsAffected() > 0;
}

bool ServerDatabasePrivate::upsertPendingAccessUserRecord(const PendingAccessUserEntry &entry)
{
    if (!db.isOpen() && !db.open()) {
        qCWarning(lcServerDatabasePrivate)
                << "Unable to open SQLite database to record user awaiting access activation:"
                << db.lastError().text();
        return false;
    }

    const qint64 lastSeenSeconds = ServerDatabasePrivate::toEpochSeconds(entry.lastMessageAtUtc());

    QSqlQuery query(db);
    query.prepare(QStringLiteral("INSERT INTO pending_access_users("
                                 "messenger_type, user_id, first_name, last_name, last_message_at) "
                                 "VALUES (:messenger, :user, :first, :last, :seen) "
                                 "ON CONFLICT(messenger_type, user_id) DO UPDATE SET "
                                 "first_name = excluded.first_name, "
                                 "last_name = excluded.last_name, "
                                 "last_message_at = excluded.last_message_at"));
    query.bindValue(QStringLiteral(":messenger"), messengerTypeToString(entry.user().messenger()));
    query.bindValue(QStringLiteral(":user"), entry.user().userId());
    query.bindValue(QStringLiteral(":first"), entry.firstName());
    query.bindValue(QStringLiteral(":last"), entry.lastName());
    query.bindValue(QStringLiteral(":seen"), lastSeenSeconds);

    if (!query.exec()) {
        logSqlError(query, QStringLiteral("upsert user awaiting access activation"));
        return false;
    }

    return true;
}

bool ServerDatabasePrivate::deletePendingAccessUserRecord(const MessengerUserKey &user)
{
    if (!db.isOpen() && !db.open()) {
        qCWarning(lcServerDatabasePrivate)
                << "Unable to open SQLite database to remove user awaiting access activation:"
                << db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM pending_access_users "
                                 "WHERE messenger_type = :messenger AND user_id = :user"));
    query.bindValue(QStringLiteral(":messenger"), messengerTypeToString(user.messenger()));
    query.bindValue(QStringLiteral(":user"), user.userId());

    if (!query.exec()) {
        logSqlError(query, QStringLiteral("delete user awaiting access activation"));
        return false;
    }

    return true;
}

bool ServerDatabasePrivate::writeBackupSnapshot()
{
    if (!db.isOpen())
        return false;

    const QString backupPath = dbFilePath + ".bak";

    if (QFile::exists(backupPath) && !QFile::remove(backupPath)) {
        qCWarning(lcServerDatabasePrivate).nospace()
                << "Failed to remove old backup file: " << backupPath;
        return false;
    }

    QString escapedPath = backupPath;
    escapedPath.replace(QStringLiteral("'"), QStringLiteral("''"));

    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("VACUUM INTO '%1'").arg(escapedPath))) {
        qCWarning(lcServerDatabasePrivate).nospace()
                << "Failed to perform database backup (VACUUM INTO): " << query.lastError().text();
        return false;
    }

    return true;
}

void ServerDatabasePrivate::logSqlError(const QSqlQuery &query, const QString &context) const
{
    qCWarning(lcServerDatabasePrivate).nospace()
            << "SQLite error during " << context << ": " << query.lastError().text();
}

QString ServerDatabasePrivate::defaultDatabasePath()
{
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (baseDir.isEmpty()) {
        baseDir = QDir::currentPath();
    }

    QDir dir(baseDir);
    dir.mkpath(QStringLiteral("."));
    return dir.filePath(QStringLiteral("resources_bot.sqlite"));
}

void ServerDatabasePrivate::appendActorToLog(QDebug &dbg,
                                             const MessengerUserKey &actor,
                                             const MessengerUserKey &subjectUser) const
{
    if (!actor.isValid())
        return;

    if (subjectUser.isValid() && actor == subjectUser)
        return;

    const User actorUser = userModel.userByKey(actor);
    const QString actorText = actorUser.isValid()
                                      ? MessengerTextRender::formatUserLine(actorUser, false)
                                      : actor.toString();
    dbg << " by=" << qPrintable(actorText);
}

bool ServerDatabasePrivate::isActorAdmin(const MessengerUserKey &actor) const
{
    if (!actor.isValid())
        return false;

    if (actor.messenger() == MessengerType::LocalCli)
        return true;

    const User actorUser = userModel.userByKey(actor);
    return actorUser.isValid() && actorUser.isAdmin();
}

bool ServerDatabasePrivate::isActorFixedAdmin(const MessengerUserKey &actor) const
{
    if (!actor.isValid())
        return false;

    if (actor.messenger() == MessengerType::LocalCli)
        return true;

    const User actorUser = userModel.userByKey(actor);
    return actorUser.isValid() && actorUser.isFixedAdmin();
}

bool ServerDatabasePrivate::actorHasActiveAccess(const MessengerUserKey &actor) const
{
    if (!actor.isValid())
        return false;

    if (actor.messenger() == MessengerType::LocalCli)
        return true;

    return userModel.exists(actor);
}

bool ServerDatabasePrivate::actorCanManageServerSettings(const MessengerUserKey &actor) const
{
    if (!actor.isValid())
        return false;

    if (actor.messenger() == MessengerType::LocalCli)
        return true;

    const User actorUser = userModel.userByKey(actor);
    if (!actorUser.isValid())
        return false;

    if (actorUser.isFixedAdmin())
        return true;

    return actorUser.roleForGroup(Group::root()) == User::Role::AdminRole;
}

QSet<Group> ServerDatabasePrivate::actorAdministeredGroupRoots(const MessengerUserKey &actor) const
{
    if (!actor.isValid())
        return {};

    if (actor.messenger() == MessengerType::LocalCli)
        return { Group::root() };

    const User actorUser = userModel.userByKey(actor);
    if (!actorUser.isValid())
        return {};

    return actorUser.administeredGroupRoots();
}

bool ServerDatabasePrivate::actorCanAdministerGroups(const MessengerUserKey &actor,
                                                     const QSet<Group> &groups) const
{
    if (!actor.isValid())
        return false;

    if (actor.messenger() == MessengerType::LocalCli)
        return true;

    const User actorUser = userModel.userByKey(actor);
    if (!actorUser.isValid())
        return false;

    if (actorUser.isFixedAdmin())
        return true;

    return actorUser.canAdministerGroups(groups);
}

bool ServerDatabasePrivate::actorCanModifyResource(const MessengerUserKey &actor,
                                                   const Resource &resource) const
{
    return actorCanAdministerGroups(actor, resource.groups());
}

bool ServerDatabasePrivate::actorCanAdministerUser(const MessengerUserKey &actor,
                                                   const User &target) const
{
    if (!target.isValid())
        return false;
    return actorCanAdministerGroups(actor, target.groups());
}

bool ServerDatabasePrivate::actorCanManageBlacklistTarget(const MessengerUserKey &actor,
                                                          const MessengerUserKey &targetUser) const
{
    if (!actor.isValid() || !targetUser.isValid())
        return false;

    if (actor.messenger() == MessengerType::LocalCli)
        return true;

    const User target = userModel.userByKey(targetUser);
    if (!target.isValid())
        return actorCanManageServerSettings(actor);
    if (target.isFixedAdmin())
        return false;
    return actorCanAdministerUser(actor, target);
}

bool ServerDatabasePrivate::userHasAccessToResource(const MessengerUserKey &user,
                                                    const Resource &resource) const
{
    if (!user.isValid() || !resource.isValid())
        return false;

    const User target = userModel.userByKey(user);
    if (!target.isValid())
        return false;
    if (user.messenger() == MessengerType::LocalCli)
        return true;

    for (const Group &group : resource.groups()) {
        if (target.hasAccessToGroup(group))
            return true;
    }
    return false;
}

bool ServerDatabasePrivate::actorCanAdministerQueueEntriesForUser(
        const MessengerUserKey &actor, const MessengerUserKey &targetUser) const
{
    if (!actor.isValid() || !targetUser.isValid())
        return false;

    if (actor.messenger() == MessengerType::LocalCli)
        return true;

    const QList<ResourceQueueEntry> entries = queueModel.items();
    for (const ResourceQueueEntry &entry : entries) {
        if (!entry.isValid() || entry.user() != targetUser)
            continue;
        const Resource resource = resourceModel.resourceByUuid(entry.resourceUuid());
        if (!resource.isValid() || !actorCanModifyResource(actor, resource))
            return false;
    }
    return true;
}

bool ServerDatabasePrivate::actorCanModifyUser(const MessengerUserKey &actor,
                                               const MessengerUserKey &targetUser)
{
    if (!actor.isValid())
        return false;

    if (actor.messenger() == MessengerType::LocalCli)
        return true;

    if (targetUser.isValid() && actor == targetUser)
        return true;

    if (isActorFixedAdmin(actor))
        return true;

    const User target = userModel.userByKey(targetUser);
    if (!target.isValid())
        return actorCanManageServerSettings(actor);

    return actorCanAdministerUser(actor, target);
}

void ServerDatabasePrivate::cleanupOldCorruptedBackups()
{
    QFileInfo dbInfo(dbFilePath);
    QDir dir = dbInfo.absoluteDir();

    // Pattern matches: resources_bot.sqlite.corrupted.*
    QStringList nameFilters;
    nameFilters << dbInfo.fileName() + QStringLiteral(".corrupted.*");

    QFileInfoList corruptedFiles = dir.entryInfoList(nameFilters,
                                                     QDir::Files,
                                                     QDir::Time); // Sort by time (newest first)

    const int kMaxCorruptedBackups = 20;

    if (corruptedFiles.size() > kMaxCorruptedBackups) {
        for (int i = kMaxCorruptedBackups; i < corruptedFiles.size(); ++i) {
            QFile::remove(corruptedFiles.at(i).absoluteFilePath());
        }
    }
}

bool ServerDatabasePrivate::restoreFromBackup()
{
    const QString backupPath = dbFilePath + ".bak";
    if (!QFile::exists(backupPath)) {
        return false;
    }

    if (QFile::exists(dbFilePath)) {
        const QString corruptedPath = dbFilePath + ".corrupted."
                                      + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
        if (!QFile::rename(dbFilePath, corruptedPath)) {
            qCWarning(lcServerDatabasePrivate)
                    << "Failed to rename corrupted DB to" << corruptedPath;
            // Attempt to delete if rename fails?
            // SQLite might keep locks even if closed?
            if (!QFile::remove(dbFilePath)) {
                qCWarning(lcServerDatabasePrivate) << "Failed to remove corrupted DB file.";
                return false;
            }
        } else {
            qCInfo(lcServerDatabasePrivate) << "Corrupted database moved to" << corruptedPath;
            cleanupOldCorruptedBackups();
        }
    }

    if (QFile::copy(backupPath, dbFilePath)) {
        qCInfo(lcServerDatabasePrivate) << "Successfully restored database from" << backupPath;
        return true;
    } else {
        qCWarning(lcServerDatabasePrivate) << "Failed to copy backup file to" << dbFilePath;
        return false;
    }
}

bool ServerDatabasePrivate::loadOccupationDurations()
{
    QSqlQuery query(db);
    if (!query.exec(
                QStringLiteral("SELECT minutes FROM occupation_durations ORDER BY minutes ASC"))) {
        logSqlError(query, QStringLiteral("load occupation durations"));
        return false;
    }

    QList<int> minutes;
    while (query.next()) {
        minutes.append(query.value(0).toInt());
    }

    if (minutes.isEmpty()) {
        return replaceOccupationDurations(defaultOccupationDurations());
    }

    durationModel.replaceAll(normalizeDurations(minutes));
    return true;
}

bool ServerDatabasePrivate::replaceOccupationDurations(const QList<int> &minutes)
{
    if (!db.isOpen() && !db.open()) {
        qCWarning(lcServerDatabasePrivate)
                << "Unable to open SQLite database to update occupation durations:"
                << db.lastError().text();
        return false;
    }

    if (!db.transaction()) {
        qCWarning(lcServerDatabasePrivate)
                << "Failed to start transaction for occupation durations:" << db.lastError().text();
        return false;
    }

    auto rollback = [this]() {
        if (!db.rollback()) {
            qCWarning(lcServerDatabasePrivate)
                    << "Failed to rollback occupation duration transaction:"
                    << db.lastError().text();
        }
    };

    QSqlQuery deleteQuery(db);
    if (!deleteQuery.exec(QStringLiteral("DELETE FROM occupation_durations"))) {
        logSqlError(deleteQuery, QStringLiteral("clear occupation durations"));
        rollback();
        return false;
    }

    QSqlQuery insertQuery(db);
    insertQuery.prepare(
            QStringLiteral("INSERT INTO occupation_durations(minutes) VALUES (:minutes)"));

    const QList<int> normalized = normalizeDurations(minutes);
    for (int value : normalized) {
        insertQuery.bindValue(QStringLiteral(":minutes"), value);
        if (!insertQuery.exec()) {
            logSqlError(insertQuery, QStringLiteral("insert occupation duration"));
            rollback();
            return false;
        }
    }

    if (!db.commit()) {
        qCWarning(lcServerDatabasePrivate)
                << "Failed to commit occupation durations:" << db.lastError().text();
        rollback();
        return false;
    }

    durationModel.replaceAll(normalized);
    return true;
}

QList<int> ServerDatabasePrivate::defaultOccupationDurations() const
{
    return { 0, 5, 30, 60, 120, 240 };
}

QList<int> ServerDatabasePrivate::normalizeDurations(const QList<int> &minutes) const
{
    QList<int> normalized = minutes;
    normalized.append(0); // Ensure indefinite option is preserved.

    for (int &value : normalized) {
        if (value < 0)
            value = 0;
    }

    std::sort(normalized.begin(), normalized.end());
    normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
    return normalized;
}
