#include "ServerDatabase.h"
#include "ServerDatabase_p.h"

#include "UserModel.h"
#include "ServerStats.h"
#include "ResourceModel.h"
#include "MessengerTextRender.h"
#include "MessengerUserFacingErrors.h"
#include "ServerDateTimeFormat.h"
#include "ServerRuntimeSettings.h"
#include "OccupationDurationModel.h"

#include <QDir>
#include <QHash>
#include <QUuid>
#include <QFile>
#include <QDebug>
#include <QVariant>
#include <QDateTime>
#include <QTimeZone>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QElapsedTimer>
#include <QStandardPaths>
#include <QMessageLogger>
#include <QLoggingCategory>

#include <algorithm>
#include <utility>

Q_LOGGING_CATEGORY(lcServerDatabase, "booking.server.db")

namespace {

QDebug infoLog()
{
    return QMessageLogger(QT_MESSAGELOG_FILE, QT_MESSAGELOG_LINE, QT_MESSAGELOG_FUNC)
            .info(lcServerDatabase())
            .nospace();
}

bool isScheduledQueueEntry(const ResourceQueueEntry &entry)
{
    if (!entry.isValid())
        return false;
    if (!entry.enqueuedAtUtc().isValid() || !entry.startAtUtc().isValid())
        return false;
    return entry.enqueuedAtUtc() < entry.startAtUtc();
}

void ensureStatsSnapshot(ServerDatabasePrivate *d)
{
    if (!d || !d->stats)
        return;
    d->stats->captureInitialState(&d->userModel, &d->resourceModel, &d->queueModel);
}

QString logUserText(ServerDatabasePrivate *d, const User &user)
{
    if (user.isValid())
        return MessengerTextRender::formatUserLine(user, false);

    if (!d || !user.key().isValid())
        return user.key().toString();

    const User existing = d->userModel.userByKey(user.key());
    return existing.isValid() ? MessengerTextRender::formatUserLine(existing, false)
                              : user.key().toString();
}

QString logUserText(ServerDatabasePrivate *d, const MessengerUserKey &user)
{
    if (!d)
        return user.toString();

    const User existing = d->userModel.userByKey(user);
    return existing.isValid() ? MessengerTextRender::formatUserLine(existing, false)
                              : user.toString();
}

QString logResourceText(const Resource &resource)
{
    return resource.isValid() ? MessengerTextRender::formatResourceLine(resource, false)
                              : QString();
}

QString logResourceText(ServerDatabasePrivate *d, const QString &resourceName)
{
    const QString trimmed = resourceName.trimmed();
    if (!d)
        return trimmed;

    const Resource resource = d->resourceModel.resourceByName(trimmed);
    return resource.isValid() ? logResourceText(resource) : trimmed;
}

void releaseDatabaseConnection(ServerDatabasePrivate *d)
{
    if (!d)
        return;

    const QString name = d->connectionName;
    if (name.isEmpty())
        return;

    if (d->db.isOpen()) {
        d->db.close();
    }

    d->connectionName.clear();
    d->db = QSqlDatabase(); // release handle before removal
    QSqlDatabase::removeDatabase(name);
}

ServerDatabaseError validateResourceAvailabilityForQueueOperation(
        ServerDatabasePrivate *d,
        const QString &resourceName,
        const MessengerUserKey &actor,
        ServerDatabaseError::Operation operation)
{
    using Code = ServerDatabaseError::Code;

    if (!d)
        return ServerDatabaseError(Code::OperationFailed, operation);

    const QString trimmedName = resourceName.trimmed();
    if (trimmedName.isEmpty())
        return ServerDatabaseError(Code::ResourceNameEmpty, operation);

    const Resource resource = d->resourceModel.resourceByName(trimmedName);
    if (!resource.isValid())
        return ServerDatabaseError(Code::ResourceNotFound, operation, trimmedName);

    switch (resource.status()) {
    case Resource::Status::On:
        break;
    case Resource::Status::TemporarilyUnavailable:
        if (operation == ServerDatabaseError::Operation::Enqueue
            || operation == ServerDatabaseError::Operation::Schedule) {
            return ServerDatabaseError(Code::ResourceTemporarilyUnavailable,
                                       operation,
                                       trimmedName);
        }
        break;
    case Resource::Status::Off:
        if (operation == ServerDatabaseError::Operation::Enqueue
            || operation == ServerDatabaseError::Operation::Schedule) {
            if (!d->actorCanModifyResource(actor, resource))
                return ServerDatabaseError(Code::ResourceNotFound, operation, trimmedName);
            return ServerDatabaseError(Code::ResourceOff, operation, trimmedName);
        }
        break;
    }

    return ServerDatabaseError();
}

} // namespace

class TransactionGuard
{
public:
    explicit TransactionGuard(QSqlDatabase &dbRef)
        : db(dbRef)
    {
        // do nothing
    }

    bool begin() { return active = db.transaction(); }

    bool commit()
    {
        if (!active)
            return false;
        if (!db.commit())
            return false;
        active = false;
        return true;
    }

    bool rollback()
    {
        if (!active)
            return true;
        const bool ok = db.rollback();
        active = false;
        return ok;
    }

    ~TransactionGuard()
    {
        if (active)
            db.rollback();
    }

private:
    QSqlDatabase &db;
    bool active = false;
};

ServerDatabase::ServerDatabase(const QString &filePath, QObject *parent)
    : QObject(parent)
    , d(new ServerDatabasePrivate(this, filePath))
{
    d->userModel.setStorageAdapter(UserModel::StorageAdapter{
            [this](const User &user) {
                const QString error =
                        QStringLiteral("Failed to persist user %1").arg(user.key().toString());
                return d->persistUserSnapshot(user, error);
            },
            [this](const MessengerUserKey &key) {
                const QString error = QStringLiteral("Failed to persist user deletion for %1")
                                              .arg(key.toString());
                return d->persistUserDeletion(key, error);
            } });

    d->resourceModel.setStorageAdapter(ResourceModel::StorageAdapter{
            [this](const Resource &resource) {
                const QString error =
                        QStringLiteral("Failed to persist resource \"%1\"").arg(resource.name());
                return d->persistResourceSnapshot(resource, error);
            },
            [this](const QString &uuid) {
                const QString error = QStringLiteral("Failed to persist resource deletion.");
                return d->persistResourceDeletion(uuid, error);
            } });

    d->blacklistModel.setStorageAdapter(BlacklistModel::StorageAdapter{
            [this](const BlacklistEntry &entry) { return d->upsertBlacklistRecord(entry); },
            [this](const MessengerUserKey &key) { return d->deleteBlacklistRecord(key); },
            [this](const BlacklistEntry &entry) {
                ServerDatabasePrivate::logBlacklistExpiry(entry, &d->userModel);
            } });

    d->pendingAccessUserModel.setStorageAdapter(PendingAccessUserModel::StorageAdapter{
            [this](const PendingAccessUserEntry &entry) {
                return d->upsertPendingAccessUserRecord(entry);
            },
            [this](const MessengerUserKey &key) {
                return d->deletePendingAccessUserRecord(key);
            } });
    d->pendingAccessUserModel
            .setRetentionPolicy(ServerDatabasePrivate::kRecentPendingAccessLimit,
                                ServerDatabasePrivate::kPendingAccessRetentionDays);

    d->queueModel.setStorageAdapter(
            ResourceQueueModel::StorageAdapter{ [this](const ResourceQueueEntry &entry) {
                                                   if (d->upsertQueueEntry(entry))
                                                       return true;
                                                   d->queueDirty = true;
                                                   return d->saveAll();
                                               },
                                                [this](const ResourceQueueKey &key) {
                                                    if (d->deleteQueueEntry(key))
                                                        return true;
                                                    d->queueDirty = true;
                                                    return d->saveAll();
                                                },
                                                [this](const QList<ResourceQueueKey> &keys) {
                                                    if (d->deleteQueueEntries(keys))
                                                        return true;
                                                    d->queueDirty = true;
                                                    return d->saveAll();
                                                },
                                                [this](const MessengerUserKey &user) {
                                                    if (d->deleteQueueEntriesForUser(user))
                                                        return true;
                                                    d->queueDirty = true;
                                                    return d->saveAll();
                                                } });

    d->stats = new ServerStats(d->dbFilePath, this);

    QObject::connect(&d->queueModel,
                     &ResourceQueueModel::occupationTransition,
                     this,
                     [this](const QString &resourceName,
                            const MessengerUserKey &previousOccupant,
                            const MessengerUserKey &nextOccupant,
                            const MessengerUserKey &initiator,
                            int nextDurationMinutes,
                            bool queueEmpty) {
                         if (d->stats) {
                             ensureStatsSnapshot(d);
                             const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
                             const Resource resource =
                                     d->resourceModel.resourceByName(resourceName);
                             const QString resourceUuid =
                                     resource.isValid() ? resource.uuidString() : QString();
                             if (previousOccupant.isValid()) {
                                 d->stats->recordOccupationEvent(resourceName,
                                                                 resourceUuid,
                                                                 previousOccupant,
                                                                 initiator,
                                                                 QStringLiteral("occupation_end"),
                                                                 false,
                                                                 0,
                                                                 nowUtc);
                             }
                             if (nextOccupant.isValid()) {
                                 d->stats->recordOccupationEvent(resourceName,
                                                                 resourceUuid,
                                                                 nextOccupant,
                                                                 initiator,
                                                                 QStringLiteral("occupation_start"),
                                                                 false,
                                                                 nextDurationMinutes,
                                                                 nowUtc);
                             }
                         }
                         if (nextOccupant.isValid()) {
                             const OccupationNotificationType type =
                                     previousOccupant.isValid()
                                             ? OccupationNotificationType::Handover
                                             : OccupationNotificationType::Occupied;
                             emit occupationNotification(type,
                                                         resourceName,
                                                         previousOccupant,
                                                         nextOccupant,
                                                         initiator,
                                                         nextDurationMinutes);
                             return;
                         }

                         if (queueEmpty && previousOccupant.isValid()) {
                             emit occupationNotification(OccupationNotificationType::Released,
                                                         resourceName,
                                                         previousOccupant,
                                                         MessengerUserKey(),
                                                         initiator,
                                                         0);
                         }
                     });

    QObject::connect(&d->queueModel,
                     &ResourceQueueModel::occupationStarted,
                     this,
                     [this](const QString &resourceName,
                            const MessengerUserKey &user,
                            bool scheduled,
                            const QDateTime &handoverAtUtc) {
                         Q_UNUSED(handoverAtUtc);
                         if (!d->stats)
                             return;
                         ensureStatsSnapshot(d);
                         const Resource resource = d->resourceModel.resourceByName(resourceName);
                         const QString resourceUuid =
                                 resource.isValid() ? resource.uuidString() : QString();
                         d->stats->recordOccupationEvent(resourceName,
                                                         resourceUuid,
                                                         user,
                                                         MessengerUserKey(),
                                                         QStringLiteral("occupation_start"),
                                                         scheduled,
                                                         0,
                                                         QDateTime::currentDateTimeUtc());
                     });

    QObject::connect(&d->queueModel,
                     &ResourceQueueModel::occupationEnded,
                     this,
                     [this](const QString &resourceName,
                            const MessengerUserKey &user,
                            bool scheduled,
                            const QString &reason,
                            const QDateTime &endedAtUtc) {
                         if (!d->stats)
                             return;
                         ensureStatsSnapshot(d);
                         const Resource resource = d->resourceModel.resourceByName(resourceName);
                         const QString resourceUuid =
                                 resource.isValid() ? resource.uuidString() : QString();
                         d->stats->recordOccupationEvent(resourceName,
                                                         resourceUuid,
                                                         user,
                                                         MessengerUserKey(),
                                                         QStringLiteral("occupation_end"),
                                                         scheduled,
                                                         0,
                                                         endedAtUtc.isValid()
                                                                 ? endedAtUtc
                                                                 : QDateTime::currentDateTimeUtc(),
                                                         reason);
                     });

    d->ready = d->init();
    if (!d->ready) {
        emit errorOccurred(QStringLiteral("Failed to initialize SQLite storage"));
    }

    QObject::connect(&d->userModel,
                     &UserModel::errorOccurred,
                     this,
                     &ServerDatabase::errorOccurred);
    QObject::connect(&d->resourceModel,
                     &ResourceModel::errorOccurred,
                     this,
                     &ServerDatabase::errorOccurred);
    QObject::connect(&d->blacklistModel,
                     &BlacklistModel::errorOccurred,
                     this,
                     &ServerDatabase::errorOccurred);
    QObject::connect(&d->pendingAccessUserModel,
                     &PendingAccessUserModel::errorOccurred,
                     this,
                     &ServerDatabase::errorOccurred);
}

ServerDatabase::~ServerDatabase()
{
    releaseDatabaseConnection(d);

    delete d;
}

void ServerDatabase::shutdown()
{
    if (!d)
        return;
    if (d->connectionName.isEmpty())
        return;

    if (d->ready && !d->saveAll()) {
        qCWarning(lcServerDatabase) << "Failed to persist in-memory data during shutdown.";
    }

    // Prevent further operations from treating the database as initialized during shutdown.
    d->ready = false;

    releaseDatabaseConnection(d);
}

bool ServerDatabase::isReady() const
{
    return d->ready;
}

QString ServerDatabase::databasePath() const
{
    return d->dbFilePath;
}

int ServerDatabase::schemaVersion() const
{
    return d->targetSchemaVersion;
}

const UserModel *ServerDatabase::userModel() const
{
    return &d->userModel;
}

const ResourceModel *ServerDatabase::resourceModel() const
{
    return &d->resourceModel;
}

const ResourceQueueModel *ServerDatabase::resourceQueueModel() const
{
    return &d->queueModel;
}

const OccupationDurationModel *ServerDatabase::occupationDurationModel() const
{
    return &d->durationModel;
}

const BlacklistModel *ServerDatabase::blacklistModel() const
{
    return &d->blacklistModel;
}

const PendingAccessUserModel *ServerDatabase::pendingAccessUserModel() const
{
    return &d->pendingAccessUserModel;
}

const ServerSettingsModel *ServerDatabase::serverSettingsModel() const
{
    return &d->settingsModel;
}

bool ServerDatabase::canManageServerSettings(const MessengerUserKey &actor) const
{
    if (!d->ready)
        return false;

    return d->actorCanManageServerSettings(actor);
}

QVariant ServerDatabase::serverSettingValue(ServerRuntimeSettings::Setting setting) const
{
    const QVariant fallbackValue = ServerRuntimeSettings::defaultValue(setting);
    if (!d->ready)
        return fallbackValue;

    const QString key = QLatin1String(ServerRuntimeSettings::key(setting));
    const ServerSettingEntry entry = d->settingsModel.entryByKey(key);
    return entry.isValid() ? entry.value() : fallbackValue;
}

ServerStats *ServerDatabase::stats() const
{
    return d->stats;
}

QSet<Group> ServerDatabase::administeredGroupRoots(const MessengerUserKey &actor) const
{
    if (!d->ready)
        return {};
    return d->actorAdministeredGroupRoots(actor);
}

bool ServerDatabase::canAdministerGroups(const MessengerUserKey &actor,
                                         const QSet<Group> &groups) const
{
    return d->ready && d->actorCanAdministerGroups(actor, groups);
}

bool ServerDatabase::canAdministerResource(const MessengerUserKey &actor,
                                           const Resource &resource) const
{
    return d->ready && d->actorCanModifyResource(actor, resource);
}

bool ServerDatabase::canAdministerUser(const MessengerUserKey &actor, const User &user) const
{
    return d->ready && user.isValid() && d->actorCanAdministerUser(actor, user);
}

bool ServerDatabase::canManageBlacklistTarget(const MessengerUserKey &actor,
                                              const MessengerUserKey &targetUser) const
{
    return d->ready && d->actorCanManageBlacklistTarget(actor, targetUser);
}

ServerDatabaseError ServerDatabase::setServerSetting(ServerRuntimeSettings::Setting setting,
                                                     const QVariant &value,
                                                     const MessengerUserKey &actor)
{
    return setServerSetting(QLatin1String(ServerRuntimeSettings::key(setting)), value, actor);
}

ServerDatabaseError ServerDatabase::setServerSetting(const QString &key,
                                                     const QVariant &value,
                                                     const MessengerUserKey &actor)
{
    using Code = ServerDatabaseError::Code;
    using Op = ServerDatabaseError::Operation;
    static constexpr Op kOp = Op::UpdateServerSetting;

    if (!d->ready)
        return ServerDatabaseError(Code::DatabaseNotReady, kOp);

    const QString trimmedKey = key.trimmed();
    if (trimmedKey.isEmpty()) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   QString(),
                                   ServerDatabaseError::tr("Setting key cannot be empty."));
    }

    if (!d->actorCanManageServerSettings(actor)) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   trimmedKey,
                                   ServerDatabaseError::tr("Access denied. Admins only."));
    }

    // Validate and apply.
    using Setting = ServerRuntimeSettings::Setting;

    QVariant normalizedValue;
    bool hasDefault = false;
    bool shouldCaptureStatsSnapshot = false;
    QString persistedValue;
    QString defaultPersistedValue;
    Setting changedSetting = Setting::MaxResourcesPerUser;
    bool hasChangedSetting = false;
    QVariant previousValue;

    const auto previousSettingValue = [this](Setting setting) -> QVariant {
        const QString settingKey = QLatin1String(ServerRuntimeSettings::key(setting));
        const ServerSettingEntry current = d->settingsModel.entryByKey(settingKey);
        if (current.isValid())
            return current.value();
        return ServerRuntimeSettings::defaultValue(setting);
    };

    const auto parseUInt = [&](uint *out) -> bool {
        if (!out)
            return false;
        if (!value.isValid() || value.isNull())
            return false;
        bool ok = false;
        const uint v = value.toUInt(&ok);
        if (!ok)
            return false;
        *out = v;
        return true;
    };
    const auto parseBool = [&](bool *out) -> bool {
        if (!out)
            return false;
        if (!value.isValid() || value.isNull())
            return false;

        bool ok = false;
        const uint asUInt = value.toUInt(&ok);
        if (ok) {
            if (asUInt > 1u)
                return false;
            *out = asUInt != 0u;
            return true;
        }

        const QString normalized = value.toString().trimmed().toLower();
        if (normalized == QStringLiteral("true") || normalized == QStringLiteral("on")
            || normalized == QStringLiteral("enabled")) {
            *out = true;
            return true;
        }
        if (normalized == QStringLiteral("false") || normalized == QStringLiteral("off")
            || normalized == QStringLiteral("disabled")) {
            *out = false;
            return true;
        }

        return false;
    };

    if (trimmedKey == QLatin1String(ServerRuntimeSettings::key(Setting::MaxResourcesPerUser))) {
        changedSetting = Setting::MaxResourcesPerUser;
        hasChangedSetting = true;
        previousValue = previousSettingValue(changedSetting);

        uint normalized = 0;
        if (!parseUInt(&normalized)) {
            return ServerDatabaseError(Code::OperationFailed,
                                       kOp,
                                       trimmedKey,
                                       ServerDatabaseError::tr(
                                               "Invalid value. Expected a non-negative integer."));
        }
        defaultPersistedValue = QString::number(
                ServerRuntimeSettings::defaultValue(Setting::MaxResourcesPerUser).toUInt());
        hasDefault = true;
        persistedValue = QString::number(normalized);
        normalizedValue = QVariant::fromValue(normalized);
    } else if (trimmedKey
               == QLatin1String(
                       ServerRuntimeSettings::key(Setting::MaxOccupationDurationMinutes))) {
        changedSetting = Setting::MaxOccupationDurationMinutes;
        hasChangedSetting = true;
        previousValue = previousSettingValue(changedSetting);

        uint normalized = 0;
        if (!parseUInt(&normalized)) {
            return ServerDatabaseError(Code::OperationFailed,
                                       kOp,
                                       trimmedKey,
                                       ServerDatabaseError::tr(
                                               "Invalid value. Expected a non-negative integer."));
        }
        defaultPersistedValue = QString::number(
                ServerRuntimeSettings::defaultValue(Setting::MaxOccupationDurationMinutes)
                        .toUInt());
        hasDefault = true;
        persistedValue = QString::number(normalized);
        normalizedValue = QVariant::fromValue(normalized);
    } else if (trimmedKey == QLatin1String(ServerRuntimeSettings::key(Setting::MenuUpdateMode))) {
        changedSetting = Setting::MenuUpdateMode;
        hasChangedSetting = true;
        previousValue = previousSettingValue(changedSetting);

        uint normalized = 0;
        if (!parseUInt(&normalized) || normalized > 1) {
            return ServerDatabaseError(Code::OperationFailed,
                                       kOp,
                                       trimmedKey,
                                       ServerDatabaseError::tr("Invalid value. Expected 0 or 1."));
        }
        defaultPersistedValue = QString::number(
                ServerRuntimeSettings::defaultValue(Setting::MenuUpdateMode).toUInt());
        hasDefault = true;
        persistedValue = QString::number(normalized);
        normalizedValue = QVariant::fromValue(normalized);
    } else if (trimmedKey
               == QLatin1String(ServerRuntimeSettings::key(Setting::MenuSessionTimeoutMinutes))) {
        changedSetting = Setting::MenuSessionTimeoutMinutes;
        hasChangedSetting = true;
        previousValue = previousSettingValue(changedSetting);

        uint normalized = 0;
        if (!parseUInt(&normalized)) {
            return ServerDatabaseError(Code::OperationFailed,
                                       kOp,
                                       trimmedKey,
                                       ServerDatabaseError::tr(
                                               "Invalid value. Expected a non-negative integer."));
        }
        defaultPersistedValue = QString::number(
                ServerRuntimeSettings::defaultValue(Setting::MenuSessionTimeoutMinutes).toUInt());
        hasDefault = true;
        persistedValue = QString::number(normalized);
        normalizedValue = QVariant::fromValue(normalized);
    } else if (trimmedKey == QLatin1String(ServerRuntimeSettings::key(Setting::StatsEnabled))) {
        changedSetting = Setting::StatsEnabled;
        hasChangedSetting = true;
        previousValue = previousSettingValue(changedSetting);

        bool normalized = false;
        if (!parseBool(&normalized)) {
            return ServerDatabaseError(Code::OperationFailed,
                                       kOp,
                                       trimmedKey,
                                       ServerDatabaseError::tr(
                                               "Invalid value. Expected false/true or 0/1."));
        }
        const bool defaultValue =
                ServerRuntimeSettings::defaultValue(Setting::StatsEnabled).toBool();
        defaultPersistedValue = QString::number(defaultValue ? 1u : 0u);
        hasDefault = true;
        persistedValue = QString::number(normalized ? 1u : 0u);
        normalizedValue = QVariant::fromValue(normalized);

        const bool wasEnabled = previousValue.toBool();
        const bool nowEnabled = normalized;
        shouldCaptureStatsSnapshot = nowEnabled && !wasEnabled;
    } else if (trimmedKey == QLatin1String(ServerRuntimeSettings::key(Setting::StatsAdminsOnly))) {
        changedSetting = Setting::StatsAdminsOnly;
        hasChangedSetting = true;
        previousValue = previousSettingValue(changedSetting);

        bool normalized = true;
        if (!parseBool(&normalized)) {
            return ServerDatabaseError(Code::OperationFailed,
                                       kOp,
                                       trimmedKey,
                                       ServerDatabaseError::tr(
                                               "Invalid value. Expected false/true or 0/1."));
        }
        const bool defaultValue =
                ServerRuntimeSettings::defaultValue(Setting::StatsAdminsOnly).toBool();
        defaultPersistedValue = QString::number(defaultValue ? 1u : 0u);
        hasDefault = true;
        persistedValue = QString::number(normalized ? 1u : 0u);
        normalizedValue = QVariant::fromValue(normalized);
    } else if (trimmedKey == QLatin1String(ServerRuntimeSettings::key(Setting::DateTimeFormat))) {
        changedSetting = Setting::DateTimeFormat;
        hasChangedSetting = true;
        previousValue = previousSettingValue(changedSetting);

        QString normalizedFormat;
        if (!ServerDateTimeFormat::parseSettingValue(value.toString(), &normalizedFormat)) {
            return ServerDatabaseError(Code::OperationFailed,
                                       kOp,
                                       trimmedKey,
                                       ServerDatabaseError::tr(
                                               "Invalid value. Expected date/time format like "
                                               "dd.MM.yyyy HH:mm:ss or yyyy-MM-dd HH:mm, or "
                                               "default/auto."));
        }
        defaultPersistedValue =
                ServerRuntimeSettings::defaultValue(Setting::DateTimeFormat).toString();
        hasDefault = true;
        persistedValue = normalizedFormat;
        normalizedValue = QVariant::fromValue(normalizedFormat);
    } else if (trimmedKey
               == QLatin1String(
                       ServerRuntimeSettings::key(Setting::PendingAccessUserReplyMessage))) {
        changedSetting = Setting::PendingAccessUserReplyMessage;
        hasChangedSetting = true;
        previousValue = previousSettingValue(changedSetting);

        QString normalized = value.toString().trimmed();
        const QString normalizedToken = normalized.toLower();
        if (normalizedToken == QStringLiteral("default")
            || normalizedToken == QStringLiteral("auto")) {
            normalized.clear();
        }
        defaultPersistedValue =
                ServerRuntimeSettings::defaultValue(Setting::PendingAccessUserReplyMessage)
                        .toString();
        hasDefault = true;
        persistedValue = normalized;
        normalizedValue = QVariant::fromValue(normalized);
    } else {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   trimmedKey,
                                   ServerDatabaseError::tr("Unknown or non-runtime setting key."));
    }

    const bool valueChanged = hasChangedSetting && previousValue != normalizedValue;

    const bool isDefault = hasDefault && persistedValue == defaultPersistedValue;
    if (isDefault) {
        if (!d->deleteMetaValue(trimmedKey)) {
            return ServerDatabaseError(Code::PersistenceFailed, kOp, trimmedKey);
        }
    } else {
        // Persist to SQLite (meta table).
        if (!d->writeMetaValue(trimmedKey, persistedValue)) {
            return ServerDatabaseError(Code::PersistenceFailed, kOp, trimmedKey);
        }
    }

    ServerSettingEntry updated(trimmedKey, normalizedValue);
    d->settingsModel.upsertEntry(updated);
    if (changedSetting == Setting::MaxResourcesPerUser) {
        d->queueModel.setMaxResourcesPerUser(normalizedValue.toUInt());
    } else if (changedSetting == Setting::MaxOccupationDurationMinutes) {
        d->queueModel.setMaxOccupationDurationMinutes(normalizedValue.toUInt());
    } else if (changedSetting == Setting::StatsEnabled && d->stats) {
        d->stats->setEnabled(normalizedValue.toBool());
    }

    const QString logValue = isDefault ? QStringLiteral("<default>") : persistedValue;
    QDebug log = infoLog();
    log << "Server setting updated key=\"" << qPrintable(trimmedKey)
        << "\" value=" << qPrintable(logValue);
    d->appendActorToLog(log, actor);
    if (shouldCaptureStatsSnapshot && d->stats) {
        d->stats->captureInitialState(&d->userModel, &d->resourceModel, &d->queueModel);
    }
    if (valueChanged) {
        emit serverSettingChanged(changedSetting, normalizedValue);
    }
    return ServerDatabaseError();
}

ServerDatabaseError ServerDatabase::addOccupationDuration(int minutes,
                                                          const MessengerUserKey &actor)
{
    using Code = ServerDatabaseError::Code;
    using Op = ServerDatabaseError::Operation;
    static constexpr Op kOp = Op::AddOccupationDuration;

    if (!d->ready)
        return ServerDatabaseError(Code::DatabaseNotReady, kOp);

    if (!d->actorCanManageServerSettings(actor)) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   QString::number(minutes),
                                   ServerDatabaseError::tr(
                                           "Access denied. Server administrators only."));
    }

    if (minutes < 0) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   QString::number(minutes),
                                   ServerDatabaseError::tr(
                                           "Occupation duration must be a non-negative integer."));
    }

    const QList<int> current = d->durationModel.occupationDurations();
    if (current.contains(minutes)) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   QString::number(minutes),
                                   ServerDatabaseError::tr(
                                           "Occupation duration %1 minutes already exists.")
                                           .arg(QString::number(minutes)));
    }

    QList<int> updated = current;
    updated.append(minutes);
    if (!d->replaceOccupationDurations(updated))
        return ServerDatabaseError(Code::PersistenceFailed, kOp);

    QDebug log = infoLog();
    log << "Occupation duration added minutes=" << minutes;
    d->appendActorToLog(log, actor);
    return ServerDatabaseError();
}

ServerDatabaseError ServerDatabase::removeOccupationDuration(int minutes,
                                                             const MessengerUserKey &actor)
{
    using Code = ServerDatabaseError::Code;
    using Op = ServerDatabaseError::Operation;
    static constexpr Op kOp = Op::RemoveOccupationDuration;

    if (!d->ready)
        return ServerDatabaseError(Code::DatabaseNotReady, kOp);

    if (!d->actorCanManageServerSettings(actor)) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   QString::number(minutes),
                                   ServerDatabaseError::tr(
                                           "Access denied. Server administrators only."));
    }

    if (minutes < 0) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   QString::number(minutes),
                                   ServerDatabaseError::tr(
                                           "Occupation duration must be a non-negative integer."));
    }

    if (minutes == 0) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   QString::number(minutes),
                                   ServerDatabaseError::tr(
                                           "Indefinite occupation duration cannot be removed."));
    }

    const QList<int> current = d->durationModel.occupationDurations();
    if (!current.contains(minutes)) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   QString::number(minutes),
                                   ServerDatabaseError::tr(
                                           "Occupation duration %1 minutes does not exist.")
                                           .arg(QString::number(minutes)));
    }

    QList<int> updated = current;
    updated.removeAll(minutes);
    if (!d->replaceOccupationDurations(updated))
        return ServerDatabaseError(Code::PersistenceFailed, kOp);

    QDebug log = infoLog();
    log << "Occupation duration removed minutes=" << minutes;
    d->appendActorToLog(log, actor);
    return ServerDatabaseError();
}

ServerDatabaseError ServerDatabase::addResource(const Resource &resource,
                                                const MessengerUserKey &actor)
{
    using Code = ServerDatabaseError::Code;
    using Op = ServerDatabaseError::Operation;
    static constexpr Op kOp = Op::AddResource;

    if (!d->ready)
        return ServerDatabaseError(Code::DatabaseNotReady, kOp);

    const QString name = resource.name().trimmed();
    if (name.isEmpty())
        return ServerDatabaseError(Code::ResourceNameEmpty, kOp);

    if (!d->actorCanModifyResource(actor, resource)) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   name,
                                   ServerDatabaseError::tr("Access denied. Admins only."));
    }

    if (d->resourceModel.existsByName(name))
        return ServerDatabaseError(Code::ResourceAlreadyExists, kOp, name);

    Resource normalized = resource;
    if (normalized.uuid().isNull()) {
        for (int attempts = 0; attempts < 3; ++attempts) {
            normalized.setUuid(QUuid::createUuid());
            if (!d->resourceModel.existsByUuid(normalized.uuidString()))
                break;
        }
    }

    if (normalized.uuid().isNull() || d->resourceModel.existsByUuid(normalized.uuidString())) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   name,
                                   ServerDatabaseError::tr(
                                           "Failed to generate a unique resource identifier."));
    }

    if (!d->db.isOpen() && !d->db.open()) {
        return ServerDatabaseError(Code::DatabaseOpenFailed,
                                   kOp,
                                   name,
                                   QString(),
                                   d->db.lastError());
    }

    TransactionGuard tx(d->db);
    if (!tx.begin()) {
        return ServerDatabaseError(Code::TransactionBeginFailed,
                                   kOp,
                                   name,
                                   QString(),
                                   d->db.lastError());
    }

    if (!d->persistResourceSnapshot(
                normalized, QStringLiteral("Failed to persist resource \"%1\"").arg(name))) {
        tx.rollback();
        return ServerDatabaseError(Code::PersistenceFailed, kOp);
    }

    if (!tx.commit()) {
        tx.rollback();
        return ServerDatabaseError(Code::TransactionCommitFailed,
                                   kOp,
                                   name,
                                   QString(),
                                   d->db.lastError());
    }

    d->resourceModel.upsert(normalized);
    if (d->stats) {
        ensureStatsSnapshot(d);
        d->stats->recordResourceEvent(normalized, actor, QStringLiteral("add"));
    }
    QDebug log = infoLog();
    log << "Resource added " << qPrintable(logResourceText(normalized));
    d->appendActorToLog(log, actor);
    return ServerDatabaseError();
}

ServerDatabaseError ServerDatabase::updateResource(const Resource &resource,
                                                   const MessengerUserKey &actor)
{
    const Resource existing = d->resourceModel.resourceByName(resource.name().trimmed());
    Resource desired = resource;
    if (existing.isValid())
        desired.setStatus(existing.status());
    return replaceResource(resource.name(), desired, actor);
}

ServerDatabaseError ServerDatabase::replaceResource(const QString &currentName,
                                                    const Resource &resource,
                                                    const MessengerUserKey &actor)
{
    using Code = ServerDatabaseError::Code;
    using Op = ServerDatabaseError::Operation;
    static constexpr Op kOp = Op::UpdateResource;

    if (!d->ready)
        return ServerDatabaseError(Code::DatabaseNotReady, kOp);

    const QString currentTrimmed = currentName.trimmed();
    const QString name = resource.name().trimmed();
    if (currentTrimmed.isEmpty() || name.isEmpty())
        return ServerDatabaseError(Code::ResourceNameEmpty, kOp);

    const Resource existing = d->resourceModel.resourceByName(currentTrimmed);
    if (!existing.isValid())
        return ServerDatabaseError(Code::ResourceNotFound, kOp, currentTrimmed);

    if (!d->actorCanModifyResource(actor, existing)
        || !d->actorCanModifyResource(actor, resource)) {
        return ServerDatabaseError(Code::ResourceNotFound, kOp, currentTrimmed);
    }

    const Resource conflicting = d->resourceModel.resourceByName(name);
    if (conflicting.isValid() && conflicting.uuid() != existing.uuid()) {
        return ServerDatabaseError(Code::ResourceAlreadyExists, kOp, name);
    }

    Resource updated(existing.uuid(),
                     name,
                     resource.description(),
                     resource.startMessage(),
                     resource.status());
    updated.setGroups(resource.groups());

    QList<ResourceQueueEntry> removedEntries;
    const bool clearQueue = existing.status() != Resource::Status::TemporarilyUnavailable
                            && updated.status() == Resource::Status::TemporarilyUnavailable;
    const bool groupsChanged = existing.groups() != updated.groups();
    if (clearQueue || groupsChanged) {
        const QList<ResourceQueueEntry> entries = d->queueModel.items();
        for (const ResourceQueueEntry &entry : entries) {
            if (!entry.isValid() || entry.resourceUuid() != existing.uuidString())
                continue;
            if (clearQueue || !d->userHasAccessToResource(entry.user(), updated))
                removedEntries.append(entry);
        }
    }

    if (!d->db.isOpen() && !d->db.open()) {
        return ServerDatabaseError(Code::DatabaseOpenFailed,
                                   kOp,
                                   currentTrimmed,
                                   QString(),
                                   d->db.lastError());
    }

    TransactionGuard tx(d->db);
    if (!tx.begin()) {
        return ServerDatabaseError(Code::TransactionBeginFailed,
                                   kOp,
                                   currentTrimmed,
                                   QString(),
                                   d->db.lastError());
    }

    if (!d->upsertResourceRecord(updated)) {
        tx.rollback();
        return ServerDatabaseError(Code::PersistenceFailed, kOp);
    }

    if (clearQueue) {
        QSqlQuery removeQueue(d->db);
        removeQueue.prepare(
                QStringLiteral("DELETE FROM resource_queue WHERE resource_uuid = :uuid"));
        removeQueue.bindValue(QStringLiteral(":uuid"), existing.uuidString());
        if (!removeQueue.exec()) {
            tx.rollback();
            return ServerDatabaseError(Code::PersistenceFailed,
                                       kOp,
                                       currentTrimmed,
                                       QString(),
                                       removeQueue.lastError());
        }
    } else {
        for (const ResourceQueueEntry &entry : std::as_const(removedEntries)) {
            if (!d->deleteQueueEntry(entry.key())) {
                tx.rollback();
                return ServerDatabaseError(Code::PersistenceFailed,
                                           kOp,
                                           currentTrimmed,
                                           QString(),
                                           d->db.lastError());
            }
        }
    }

    if (!tx.commit()) {
        tx.rollback();
        return ServerDatabaseError(Code::TransactionCommitFailed,
                                   kOp,
                                   currentTrimmed,
                                   QString(),
                                   d->db.lastError());
    }

    d->resourceModel.upsert(updated);
    const qsizetype removedQueueEntriesCount = removedEntries.size();
    if (!removedEntries.isEmpty()) {
        QSet<ResourceQueueKey> removedKeys;
        for (const ResourceQueueEntry &entry : std::as_const(removedEntries))
            removedKeys.insert(entry.key());
        QList<ResourceQueueEntry> entries = d->queueModel.items();
        entries.erase(std::remove_if(entries.begin(),
                                     entries.end(),
                                     [&removedKeys](const ResourceQueueEntry &entry) {
                                         return removedKeys.contains(entry.key());
                                     }),
                      entries.end());
        d->queueModel.replaceAll(entries);
    }

    if (d->stats) {
        ensureStatsSnapshot(d);
        const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
        const QString removalReason = clearQueue ? QStringLiteral("status_temporarily_unavailable")
                                                 : QStringLiteral("resource_group_change");
        for (const ResourceQueueEntry &entry : std::as_const(removedEntries)) {
            const bool active = entry.isActiveAtUtc(nowUtc);
            const bool wasScheduled = isScheduledQueueEntry(entry);
            if (active) {
                d->stats->recordOccupationEvent(updated.name(),
                                                updated.uuidString(),
                                                entry.user(),
                                                actor,
                                                QStringLiteral("occupation_end"),
                                                wasScheduled,
                                                0,
                                                nowUtc,
                                                removalReason);
            } else {
                d->stats->recordScheduleEvent(updated.name(),
                                              updated.uuidString(),
                                              entry.user(),
                                              actor,
                                              QStringLiteral("schedule_remove"),
                                              entry.startAtUtc(),
                                              entry.endAtUtc(),
                                              nowUtc,
                                              removalReason);
            }
        }
        if (existing.name() != updated.name())
            d->stats->recordResourceEvent(updated, actor, QStringLiteral("rename"));
        else
            d->stats->recordResourceEvent(updated, actor, QStringLiteral("update"));
        if (existing.status() != updated.status())
            d->stats->recordResourceEvent(updated, actor, QStringLiteral("status_update"));
    }
    QDebug log = infoLog();
    log << "Resource replaced " << qPrintable(logResourceText(existing)) << " -> "
        << qPrintable(logResourceText(updated))
        << " removed_queue_entries=" << removedQueueEntriesCount;
    d->appendActorToLog(log, actor);
    return ServerDatabaseError();
}

ServerDatabaseError ServerDatabase::setResourceStatus(const QString &name,
                                                      Resource::Status status,
                                                      const MessengerUserKey &actor)
{
    using Code = ServerDatabaseError::Code;
    using Op = ServerDatabaseError::Operation;
    static constexpr Op kOp = Op::SetResourceStatus;

    if (!d->ready)
        return ServerDatabaseError(Code::DatabaseNotReady, kOp);

    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty())
        return ServerDatabaseError(Code::ResourceNameEmpty, kOp);

    const Resource existing = d->resourceModel.resourceByName(trimmedName);
    if (!existing.isValid())
        return ServerDatabaseError(Code::ResourceNotFound, kOp, trimmedName);

    if (!d->actorCanModifyResource(actor, existing)) {
        return ServerDatabaseError(Code::ResourceNotFound, kOp, trimmedName);
    }

    if (existing.status() == status)
        return ServerDatabaseError();

    QList<ResourceQueueEntry> removedEntries;
    if (status == Resource::Status::TemporarilyUnavailable && d->stats) {
        const QList<ResourceQueueEntry> entries = d->queueModel.items();
        for (const ResourceQueueEntry &entry : entries) {
            if (!entry.isValid())
                continue;
            if (entry.resourceUuid() != existing.uuidString())
                continue;
            removedEntries.append(entry);
        }
    }

    if (!d->db.isOpen() && !d->db.open()) {
        return ServerDatabaseError(Code::DatabaseOpenFailed,
                                   kOp,
                                   trimmedName,
                                   QString(),
                                   d->db.lastError());
    }

    TransactionGuard tx(d->db);
    if (!tx.begin()) {
        return ServerDatabaseError(Code::TransactionBeginFailed,
                                   kOp,
                                   trimmedName,
                                   QString(),
                                   d->db.lastError());
    }

    Resource updated(existing.uuid(),
                     existing.name(),
                     existing.description(),
                     existing.startMessage(),
                     status);
    updated.setGroups(existing.groups());
    if (!d->upsertResourceRecord(updated)) {
        tx.rollback();
        return ServerDatabaseError(Code::PersistenceFailed,
                                   kOp,
                                   trimmedName,
                                   QString(),
                                   d->db.lastError());
    }

    if (status == Resource::Status::TemporarilyUnavailable) {
        QSqlQuery removeQueue(d->db);
        removeQueue.prepare(
                QStringLiteral("DELETE FROM resource_queue WHERE resource_uuid = :uuid"));
        removeQueue.bindValue(QStringLiteral(":uuid"), existing.uuidString());
        if (!removeQueue.exec()) {
            tx.rollback();
            return ServerDatabaseError(Code::PersistenceFailed,
                                       kOp,
                                       trimmedName,
                                       QString(),
                                       removeQueue.lastError());
        }
    }

    if (!tx.commit()) {
        tx.rollback();
        return ServerDatabaseError(Code::TransactionCommitFailed,
                                   kOp,
                                   trimmedName,
                                   QString(),
                                   d->db.lastError());
    }

    d->resourceModel.upsert(updated);

    qsizetype removedQueueEntriesCount = 0;
    if (status == Resource::Status::TemporarilyUnavailable) {
        QList<ResourceQueueEntry> entries = d->queueModel.items();
        const qsizetype before = entries.size();
        entries.erase(std::remove_if(entries.begin(),
                                     entries.end(),
                                     [&existing](const ResourceQueueEntry &entry) {
                                         return entry.resourceUuid() == existing.uuidString();
                                     }),
                      entries.end());
        removedQueueEntriesCount = before - entries.size();
        d->queueModel.replaceAll(entries);
    }

    if (d->stats) {
        ensureStatsSnapshot(d);
        if (status == Resource::Status::TemporarilyUnavailable && !removedEntries.isEmpty()) {
            const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
            for (const ResourceQueueEntry &entry : std::as_const(removedEntries)) {
                const bool active = entry.isActiveAtUtc(nowUtc);
                const bool wasScheduled = isScheduledQueueEntry(entry);
                if (active) {
                    d->stats->recordOccupationEvent(existing.name(),
                                                    existing.uuidString(),
                                                    entry.user(),
                                                    actor,
                                                    QStringLiteral("occupation_end"),
                                                    wasScheduled,
                                                    0,
                                                    nowUtc,
                                                    QStringLiteral(
                                                            "status_temporarily_unavailable"));
                } else {
                    d->stats->recordScheduleEvent(existing.name(),
                                                  existing.uuidString(),
                                                  entry.user(),
                                                  actor,
                                                  QStringLiteral("schedule_remove"),
                                                  entry.startAtUtc(),
                                                  entry.endAtUtc(),
                                                  nowUtc,
                                                  QStringLiteral("status_temporarily_unavailable"));
                }
            }
        }
        d->stats->recordResourceEvent(updated, actor, QStringLiteral("status_update"));
    }

    QDebug log = infoLog();
    log << "Resource status updated " << qPrintable(logResourceText(updated)) << ' '
        << qPrintable(Resource::statusToToken(existing.status())) << " -> "
        << qPrintable(Resource::statusToToken(status))
        << " removed_queue_entries=" << removedQueueEntriesCount;
    d->appendActorToLog(log, actor);
    return ServerDatabaseError();
}

ServerDatabaseError ServerDatabase::deleteResource(const QString &name,
                                                   const MessengerUserKey &actor)
{
    using Code = ServerDatabaseError::Code;
    using Op = ServerDatabaseError::Operation;
    static constexpr Op kOp = Op::DeleteResource;

    if (!d->ready)
        return ServerDatabaseError(Code::DatabaseNotReady, kOp);

    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return ServerDatabaseError(Code::ResourceNameEmpty, kOp);

    const Resource existing = d->resourceModel.resourceByName(trimmed);
    if (!existing.isValid())
        return ServerDatabaseError(Code::ResourceNotFound, kOp, trimmed);

    if (!d->actorCanModifyResource(actor, existing)) {
        return ServerDatabaseError(Code::ResourceNotFound, kOp, trimmed);
    }

    QList<ResourceQueueEntry> removedEntries;
    if (d->stats) {
        const QList<ResourceQueueEntry> entries = d->queueModel.items();
        for (const ResourceQueueEntry &entry : entries) {
            if (!entry.isValid())
                continue;
            if (entry.resourceUuid() != existing.uuidString())
                continue;
            removedEntries.append(entry);
        }
    }

    if (!d->db.isOpen() && !d->db.open())
        return ServerDatabaseError(Code::DatabaseOpenFailed,
                                   kOp,
                                   trimmed,
                                   QString(),
                                   d->db.lastError());

    TransactionGuard tx(d->db);
    if (!tx.begin())
        return ServerDatabaseError(Code::TransactionBeginFailed,
                                   kOp,
                                   trimmed,
                                   QString(),
                                   d->db.lastError());

    // Remove queue entries first (SQLite foreign keys may be disabled at runtime).
    {
        QSqlQuery removeQueue(d->db);
        removeQueue.prepare(
                QStringLiteral("DELETE FROM resource_queue WHERE resource_uuid = :uuid"));
        removeQueue.bindValue(QStringLiteral(":uuid"), existing.uuidString());
        if (!removeQueue.exec()) {
            tx.rollback();
            return ServerDatabaseError(Code::PersistenceFailed,
                                       kOp,
                                       existing.name(),
                                       QString(),
                                       removeQueue.lastError());
        }
    }

    if (!d->deleteResourceRecord(existing.uuidString())) {
        tx.rollback();
        return ServerDatabaseError(Code::PersistenceFailed,
                                   kOp,
                                   existing.name(),
                                   QString(),
                                   d->db.lastError());
    }

    if (!tx.commit()) {
        tx.rollback();
        return ServerDatabaseError(Code::TransactionCommitFailed,
                                   kOp,
                                   existing.name(),
                                   QString(),
                                   d->db.lastError());
    }

    d->resourceModel.removeByUuid(existing.uuidString());

    QList<ResourceQueueEntry> entries = d->queueModel.items();
    const qsizetype before = entries.size();
    entries.erase(std::remove_if(entries.begin(),
                                 entries.end(),
                                 [&existing](const ResourceQueueEntry &entry) {
                                     return entry.resourceUuid() == existing.uuidString();
                                 }),
                  entries.end());
    const qsizetype removed = before - entries.size();
    d->queueModel.replaceAll(entries);

    QDebug log = infoLog();
    log << "Resource deleted " << qPrintable(logResourceText(existing))
        << " removed_queue_entries=" << removed;
    d->appendActorToLog(log, actor);
    if (d->stats) {
        ensureStatsSnapshot(d);
        if (!removedEntries.isEmpty()) {
            const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
            for (const ResourceQueueEntry &entry : std::as_const(removedEntries)) {
                const bool active = entry.isActiveAtUtc(nowUtc);
                const bool wasScheduled = isScheduledQueueEntry(entry);
                if (active) {
                    d->stats->recordOccupationEvent(existing.name(),
                                                    existing.uuidString(),
                                                    entry.user(),
                                                    actor,
                                                    QStringLiteral("occupation_end"),
                                                    wasScheduled,
                                                    0,
                                                    nowUtc,
                                                    QStringLiteral("delete_resource"));
                } else {
                    d->stats->recordScheduleEvent(existing.name(),
                                                  existing.uuidString(),
                                                  entry.user(),
                                                  actor,
                                                  QStringLiteral("schedule_remove"),
                                                  entry.startAtUtc(),
                                                  entry.endAtUtc(),
                                                  nowUtc,
                                                  QStringLiteral("delete_resource"));
                }
            }
        }
        d->stats->recordResourceEvent(existing, actor, QStringLiteral("delete"));
    }
    return ServerDatabaseError();
}

ServerDatabaseError ServerDatabase::renameResource(const QString &oldName,
                                                   const QString &newName,
                                                   const QString &description,
                                                   const QString &startMessage,
                                                   const MessengerUserKey &actor)
{
    const QString oldTrimmed = oldName.trimmed();
    const QString newTrimmed = newName.trimmed();
    const Resource existing = d->resourceModel.resourceByName(oldTrimmed);
    Resource updated(existing.uuid(),
                     newTrimmed,
                     description,
                     startMessage,
                     existing.isValid() ? existing.status() : Resource::Status::On);
    if (existing.isValid())
        updated.setGroups(existing.groups());
    return replaceResource(oldTrimmed, updated, actor);
}

ServerDatabaseError ServerDatabase::addUser(const User &user,
                                            const MessengerUserKey &actor,
                                            bool *removedFromBlacklistOut)
{
    using Code = ServerDatabaseError::Code;
    using Op = ServerDatabaseError::Operation;
    static constexpr Op kOp = Op::AddUser;

    if (removedFromBlacklistOut)
        *removedFromBlacklistOut = false;

    if (!d->ready)
        return ServerDatabaseError(Code::DatabaseNotReady, kOp);

    if (!user.isValid())
        return ServerDatabaseError(Code::UserInvalid, kOp);

    if (user.isFixedAdmin()) {
        return ServerDatabaseError(
                Code::OperationFailed,
                kOp,
                user.key().toString(),
                ServerDatabaseError::tr(
                        "Fixed admin role can only be assigned via server configuration."));
    }

    if (!d->actorCanAdministerGroups(actor, user.groups())) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   user.key().toString(),
                                   ServerDatabaseError::tr("Access denied. Admins only."));
    }

    if (d->userModel.exists(user.key())) {
        return ServerDatabaseError(Code::UserAlreadyExists, kOp, user.key().toString());
    }

    if (!d->blacklistModel.purgeExpiredEntries()) {
        return ServerDatabaseError(Code::PersistenceFailed,
                                   kOp,
                                   user.key().toString(),
                                   QString(),
                                   d->db.lastError());
    }
    if (d->blacklistModel.exists(user.key())
        && !d->actorCanManageBlacklistTarget(actor, user.key())) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   user.key().toString(),
                                   ServerDatabaseError::tr(
                                           "Access denied. Only a server administrator can "
                                           "activate a blacklisted user."));
    }

    if (!d->db.isOpen() && !d->db.open()) {
        return ServerDatabaseError(Code::DatabaseOpenFailed,
                                   kOp,
                                   user.key().toString(),
                                   QString(),
                                   d->db.lastError());
    }

    TransactionGuard tx(d->db);
    if (!tx.begin()) {
        return ServerDatabaseError(Code::TransactionBeginFailed,
                                   kOp,
                                   user.key().toString(),
                                   QString(),
                                   d->db.lastError());
    }

    const bool hadBlacklistEntry = d->blacklistModel.exists(user.key());
    if (hadBlacklistEntry) {
        if (!d->deleteBlacklistRecord(user.key())) {
            tx.rollback();
            return ServerDatabaseError(Code::PersistenceFailed,
                                       kOp,
                                       user.key().toString(),
                                       QString(),
                                       d->db.lastError());
        }
    }

    // Clear pending-access record if present (an active user should not stay in this set).
    const bool hadPendingAccessEntry = d->pendingAccessUserModel.exists(user.key());
    if (hadPendingAccessEntry) {
        if (!d->deletePendingAccessUserRecord(user.key())) {
            tx.rollback();
            return ServerDatabaseError(Code::PersistenceFailed,
                                       kOp,
                                       user.key().toString(),
                                       QString(),
                                       d->db.lastError());
        }
    }

    if (!d->upsertUserRecord(user) || !d->persistUserSettingsRecord(user)) {
        tx.rollback();
        return ServerDatabaseError(Code::PersistenceFailed,
                                   kOp,
                                   user.key().toString(),
                                   QString(),
                                   d->db.lastError());
    }

    if (!tx.commit()) {
        tx.rollback();
        return ServerDatabaseError(Code::TransactionCommitFailed,
                                   kOp,
                                   user.key().toString(),
                                   QString(),
                                   d->db.lastError());
    }

    if (hadBlacklistEntry) {
        d->blacklistModel.removeByKey(user.key());
        if (removedFromBlacklistOut)
            *removedFromBlacklistOut = true;
    }

    d->pendingAccessUserModel.removeByKey(user.key());
    d->userModel.upsert(user);
    if (d->stats) {
        ensureStatsSnapshot(d);
        d->stats->recordUserEvent(user, actor, QStringLiteral("add"));
    }

    QDebug log = infoLog();
    const QString addedTimeZone = user.timezone().hasCustomOffset()
                                          ? user.timezone().toStorageValue()
                                          : QStringLiteral("default");
    log << "User added " << qPrintable(logUserText(d, user)) << " ("
        << (user.isFixedAdmin() ? "fixed-admin" : (user.isAdmin() ? "admin" : "user"))
        << ") locale=" << user.locale() << " timezone=" << addedTimeZone
        << " removed_from_blacklist=" << (hadBlacklistEntry ? "yes" : "no")
        << " removed_from_pending_access=" << (hadPendingAccessEntry ? "yes" : "no");
    d->appendActorToLog(log, actor, user.key());
    emit userChanged(user.key());
    return ServerDatabaseError();
}

ServerDatabaseError ServerDatabase::updateUser(const User &user, const MessengerUserKey &actor)
{
    using Code = ServerDatabaseError::Code;
    using Op = ServerDatabaseError::Operation;
    static constexpr Op kOp = Op::UpdateUser;

    if (!d->ready)
        return ServerDatabaseError(Code::DatabaseNotReady, kOp);

    if (!user.isValid())
        return ServerDatabaseError(Code::UserInvalid, kOp);

    if (!d->userModel.exists(user.key())) {
        return ServerDatabaseError(Code::UserNotFound, kOp, user.key().toString());
    }

    const User existing = d->userModel.userByKey(user.key());

    if (!d->actorCanModifyUser(actor, user.key())) {
        return ServerDatabaseError(Code::UserNotFound, kOp, user.key().toString());
    }

    if (!existing.isFixedAdmin() && user.isFixedAdmin()) {
        return ServerDatabaseError(
                Code::OperationFailed,
                kOp,
                user.key().toString(),
                ServerDatabaseError::tr(
                        "Fixed admin role can only be assigned via server configuration."));
    }

    User normalized = user;
    if (existing.isFixedAdmin()) {
        if (!user.isAdmin()) {
            return ServerDatabaseError(
                    Code::OperationFailed,
                    kOp,
                    user.key().toString(),
                    MessengerUserFacingErrors::roleCannotBeChangedForFixedAdmin());
        }
        // Preserve fixed-admin role even if the caller only provides "admin=true".
        normalized.setRole(User::Role::FixedAdminRole);
    }

    const bool actorIsAdmin = d->isActorAdmin(actor);
    if (actorIsAdmin && existing.groupRoles() != normalized.groupRoles()
        && !d->actorCanAdministerGroups(actor, existing.groups())) {
        return ServerDatabaseError(Code::UserNotFound, kOp, user.key().toString());
    }
    if (actorIsAdmin && !d->isActorFixedAdmin(actor)
        && !d->actorCanAdministerGroups(actor, normalized.groups())) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   user.key().toString(),
                                   ServerDatabaseError::tr("Access denied. Admins only."));
    }
    if (!actorIsAdmin) {
        if (existing.isValid()) {
            const bool restrictedChange = existing.isAdmin() != normalized.isAdmin()
                                          || existing.firstName() != normalized.firstName()
                                          || existing.lastName() != normalized.lastName()
                                          || existing.groupRoles() != normalized.groupRoles();
            if (restrictedChange) {
                return ServerDatabaseError(Code::OperationFailed,
                                           kOp,
                                           user.key().toString(),
                                           ServerDatabaseError::tr("Access denied."));
            }
        }
    }

    if (actor.messenger() != MessengerType::LocalCli
        && existing.roleForGroup(Group::root()) == User::Role::AdminRole
        && normalized.roleForGroup(Group::root()) != User::Role::AdminRole) {
        bool anotherServerAdminExists = false;
        const QList<User> users = d->userModel.items();
        for (const User &candidate : users) {
            if (!candidate.isValid() || candidate.key() == existing.key())
                continue;
            if (candidate.isFixedAdmin()
                || candidate.roleForGroup(Group::root()) == User::Role::AdminRole) {
                anotherServerAdminExists = true;
                break;
            }
        }
        if (!anotherServerAdminExists) {
            return ServerDatabaseError(Code::OperationFailed,
                                       kOp,
                                       user.key().toString(),
                                       ServerDatabaseError::tr(
                                               "Cannot demote the last server administrator."));
        }
    }

    QList<ResourceQueueEntry> removedQueueEntries;
    if (existing.groupRoles() != normalized.groupRoles()) {
        const QList<ResourceQueueEntry> entries = d->queueModel.items();
        for (const ResourceQueueEntry &entry : entries) {
            if (!entry.isValid() || entry.user() != normalized.key())
                continue;

            const Resource resource = d->resourceModel.resourceByUuid(entry.resourceUuid());
            if (!resource.isValid()) {
                return ServerDatabaseError(
                        Code::OperationFailed,
                        kOp,
                        user.key().toString(),
                        ServerDatabaseError::tr(
                                "Cannot update groups while the user has an unknown queue "
                                "resource."));
            }

            bool remainsVisible = false;
            for (const Group &group : resource.groups()) {
                if (normalized.hasAccessToGroup(group)) {
                    remainsVisible = true;
                    break;
                }
            }
            if (remainsVisible)
                continue;

            if (!d->actorCanModifyResource(actor, resource)) {
                return ServerDatabaseError(
                        Code::OperationFailed,
                        kOp,
                        user.key().toString(),
                        ServerDatabaseError::tr(
                                "Access denied. The group change would remove queue entries for "
                                "resources outside your administered groups."));
            }
            removedQueueEntries.append(entry);
        }
    }

    if (!d->db.isOpen() && !d->db.open()) {
        return ServerDatabaseError(Code::DatabaseOpenFailed,
                                   kOp,
                                   user.key().toString(),
                                   QString(),
                                   d->db.lastError());
    }

    TransactionGuard tx(d->db);
    if (!tx.begin()) {
        return ServerDatabaseError(Code::TransactionBeginFailed,
                                   kOp,
                                   user.key().toString(),
                                   QString(),
                                   d->db.lastError());
    }

    bool persisted = d->upsertUserRecord(normalized) && d->persistUserSettingsRecord(normalized);
    for (const ResourceQueueEntry &entry : std::as_const(removedQueueEntries)) {
        if (persisted && !d->deleteQueueEntry(entry.key()))
            persisted = false;
    }
    if (!persisted) {
        tx.rollback();
        return ServerDatabaseError(Code::PersistenceFailed,
                                   kOp,
                                   user.key().toString(),
                                   QString(),
                                   d->db.lastError());
    }

    if (!tx.commit()) {
        tx.rollback();
        return ServerDatabaseError(Code::TransactionCommitFailed,
                                   kOp,
                                   user.key().toString(),
                                   QString(),
                                   d->db.lastError());
    }

    d->userModel.upsert(normalized);
    if (!removedQueueEntries.isEmpty()) {
        QSet<ResourceQueueKey> removedKeys;
        for (const ResourceQueueEntry &entry : std::as_const(removedQueueEntries))
            removedKeys.insert(entry.key());
        QList<ResourceQueueEntry> entries = d->queueModel.items();
        entries.erase(std::remove_if(entries.begin(),
                                     entries.end(),
                                     [&removedKeys](const ResourceQueueEntry &entry) {
                                         return removedKeys.contains(entry.key());
                                     }),
                      entries.end());
        d->queueModel.replaceAll(entries);
    }
    if (d->stats) {
        ensureStatsSnapshot(d);
        const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
        for (const ResourceQueueEntry &entry : std::as_const(removedQueueEntries)) {
            const Resource resource = d->resourceModel.resourceByUuid(entry.resourceUuid());
            const bool active = entry.isActiveAtUtc(nowUtc);
            const bool wasScheduled = isScheduledQueueEntry(entry);
            if (active) {
                d->stats->recordOccupationEvent(resource.name(),
                                                entry.resourceUuid(),
                                                entry.user(),
                                                actor,
                                                QStringLiteral("occupation_end"),
                                                wasScheduled,
                                                0,
                                                nowUtc,
                                                QStringLiteral("user_group_change"));
            } else {
                d->stats->recordScheduleEvent(resource.name(),
                                              entry.resourceUuid(),
                                              entry.user(),
                                              actor,
                                              QStringLiteral("schedule_remove"),
                                              entry.startAtUtc(),
                                              entry.endAtUtc(),
                                              nowUtc,
                                              QStringLiteral("user_group_change"));
            }
        }
        d->stats->recordUserEvent(normalized, actor, QStringLiteral("update"));
    }

    QDebug log = infoLog();
    const QString updatedTimeZone = normalized.timezone().hasCustomOffset()
                                            ? normalized.timezone().toStorageValue()
                                            : QStringLiteral("default");
    log << "User updated " << qPrintable(logUserText(d, normalized)) << " ("
        << (normalized.isFixedAdmin() ? "fixed-admin" : (normalized.isAdmin() ? "admin" : "user"))
        << ") locale=" << normalized.locale() << " timezone=" << updatedTimeZone;
    if (!removedQueueEntries.isEmpty())
        log << " removed_queue_entries=" << removedQueueEntries.size();
    d->appendActorToLog(log, actor, normalized.key());
    emit userChanged(normalized.key());
    return ServerDatabaseError();
}

ServerDatabaseError ServerDatabase::deleteUser(const MessengerUserKey &user,
                                               const MessengerUserKey &actor)
{
    using Code = ServerDatabaseError::Code;
    using Op = ServerDatabaseError::Operation;
    static constexpr Op kOp = Op::DeleteUser;

    if (!d->ready)
        return ServerDatabaseError(Code::DatabaseNotReady, kOp);

    if (!user.isValid())
        return ServerDatabaseError(Code::UserInvalid, kOp);

    if (!d->isActorAdmin(actor)) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   user.toString(),
                                   ServerDatabaseError::tr("Access denied. Admins only."));
    }

    if (!d->userModel.exists(user)) {
        return ServerDatabaseError(Code::UserNotFound, kOp, user.toString());
    }

    const User existing = d->userModel.userByKey(user);
    if (existing.isFixedAdmin()) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   user.toString(),
                                   ServerDatabaseError::tr("Cannot delete a fixed admin user."));
    }

    if (!d->actorCanAdministerUser(actor, existing)) {
        return ServerDatabaseError(Code::UserNotFound, kOp, user.toString());
    }

    if (actor.messenger() != MessengerType::LocalCli
        && existing.roleForGroup(Group::root()) == User::Role::AdminRole) {
        bool anotherServerAdminExists = false;
        const QList<User> users = d->userModel.items();
        for (const User &candidate : users) {
            if (!candidate.isValid() || candidate.key() == existing.key())
                continue;
            if (candidate.isFixedAdmin()
                || candidate.roleForGroup(Group::root()) == User::Role::AdminRole) {
                anotherServerAdminExists = true;
                break;
            }
        }
        if (!anotherServerAdminExists) {
            return ServerDatabaseError(Code::OperationFailed,
                                       kOp,
                                       user.toString(),
                                       ServerDatabaseError::tr(
                                               "Cannot delete the last server administrator."));
        }
    }

    if (!d->actorCanAdministerQueueEntriesForUser(actor, user)) {
        return ServerDatabaseError(
                Code::OperationFailed,
                kOp,
                user.toString(),
                ServerDatabaseError::tr(
                        "Access denied. The user has queue entries for resources outside your "
                        "administered groups."));
    }

    const bool wasBlacklisted = d->blacklistModel.exists(user);
    const bool wasPendingAccess = d->pendingAccessUserModel.exists(user);
    QList<ResourceQueueEntry> removedEntries;
    if (d->stats) {
        const QList<ResourceQueueEntry> entries = d->queueModel.items();
        for (const ResourceQueueEntry &entry : entries) {
            if (!entry.isValid())
                continue;
            if (entry.user() != user)
                continue;
            removedEntries.append(entry);
        }
    }

    if (!d->db.isOpen() && !d->db.open()) {
        return ServerDatabaseError(Code::DatabaseOpenFailed,
                                   kOp,
                                   user.toString(),
                                   QString(),
                                   d->db.lastError());
    }

    TransactionGuard tx(d->db);
    if (!tx.begin()) {
        return ServerDatabaseError(Code::TransactionBeginFailed,
                                   kOp,
                                   user.toString(),
                                   QString(),
                                   d->db.lastError());
    }

    if (!d->deleteQueueEntriesForUser(user) || (wasBlacklisted && !d->deleteBlacklistRecord(user))
        || !d->deletePendingAccessUserRecord(user) || !d->deleteUserSettings(user)
        || !d->deleteUserRecord(user)) {
        tx.rollback();
        return ServerDatabaseError(Code::PersistenceFailed,
                                   kOp,
                                   user.toString(),
                                   QString(),
                                   d->db.lastError());
    }

    if (!tx.commit()) {
        tx.rollback();
        return ServerDatabaseError(Code::TransactionCommitFailed,
                                   kOp,
                                   user.toString(),
                                   QString(),
                                   d->db.lastError());
    }

    d->blacklistModel.removeByKey(user);
    d->pendingAccessUserModel.removeByKey(user);
    d->userModel.removeByKey(user);

    QList<ResourceQueueEntry> entries = d->queueModel.items();
    const qsizetype before = entries.size();
    entries.erase(std::remove_if(entries.begin(),
                                 entries.end(),
                                 [&user](const ResourceQueueEntry &entry) {
                                     return entry.user() == user;
                                 }),
                  entries.end());
    const qsizetype removedQueueEntries = before - entries.size();
    d->queueModel.replaceAll(entries);

    QDebug log = infoLog();
    log << "User deleted " << qPrintable(logUserText(d, existing))
        << " removed_queue_entries=" << removedQueueEntries
        << " removed_blacklist=" << (wasBlacklisted ? "yes" : "no")
        << " removed_pending_access=" << (wasPendingAccess ? "yes" : "no");
    d->appendActorToLog(log, actor, user);
    if (d->stats) {
        ensureStatsSnapshot(d);
        if (!removedEntries.isEmpty()) {
            const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
            for (const ResourceQueueEntry &entry : std::as_const(removedEntries)) {
                const Resource resource = d->resourceModel.resourceByUuid(entry.resourceUuid());
                const QString resourceName = resource.isValid() ? resource.name() : QString();
                const bool active = entry.isActiveAtUtc(nowUtc);
                const bool wasScheduled = isScheduledQueueEntry(entry);
                if (active) {
                    d->stats->recordOccupationEvent(resourceName,
                                                    entry.resourceUuid(),
                                                    entry.user(),
                                                    actor,
                                                    QStringLiteral("occupation_end"),
                                                    wasScheduled,
                                                    0,
                                                    nowUtc,
                                                    QStringLiteral("delete_user"));
                } else {
                    d->stats->recordScheduleEvent(resourceName,
                                                  entry.resourceUuid(),
                                                  entry.user(),
                                                  actor,
                                                  QStringLiteral("schedule_remove"),
                                                  entry.startAtUtc(),
                                                  entry.endAtUtc(),
                                                  nowUtc,
                                                  QStringLiteral("delete_user"));
                }
            }
        }
        d->stats->recordUserEvent(existing, actor, QStringLiteral("delete"));
    }
    emit userChanged(user);
    return ServerDatabaseError();
}

ServerDatabaseError ServerDatabase::ensureFixedAdmin(const MessengerUserKey &user)
{
    using Code = ServerDatabaseError::Code;
    using Op = ServerDatabaseError::Operation;
    static constexpr Op kOp = Op::EnsureFixedAdmin;

    if (!d->ready)
        return ServerDatabaseError(Code::DatabaseNotReady, kOp);

    if (!user.isValid())
        return ServerDatabaseError(Code::UserInvalid, kOp);

    const User existing = d->userModel.userByKey(user);
    if (existing.isValid() && existing.isFixedAdmin())
        return ServerDatabaseError();

    if (!d->db.isOpen() && !d->db.open()) {
        return ServerDatabaseError(Code::DatabaseOpenFailed,
                                   kOp,
                                   user.toString(),
                                   QString(),
                                   d->db.lastError());
    }

    TransactionGuard tx(d->db);
    if (!tx.begin()) {
        return ServerDatabaseError(Code::TransactionBeginFailed,
                                   kOp,
                                   user.toString(),
                                   QString(),
                                   d->db.lastError());
    }

    User candidate = existing.isValid()
                             ? existing
                             : User(user, QString(), QString(), User::Role::FixedAdminRole);
    candidate.setKey(user);
    // The fixed-admin marker is process-local; persistence normalizes it to a regular admin.
    candidate.setRole(User::Role::FixedAdminRole);

    if (!d->upsertUserRecord(candidate) || !d->persistUserSettingsRecord(candidate)) {
        tx.rollback();
        return ServerDatabaseError(Code::PersistenceFailed,
                                   kOp,
                                   user.toString(),
                                   QString(),
                                   d->db.lastError());
    }

    if (!tx.commit()) {
        tx.rollback();
        return ServerDatabaseError(Code::TransactionCommitFailed,
                                   kOp,
                                   user.toString(),
                                   QString(),
                                   d->db.lastError());
    }

    d->userModel.upsert(candidate);
    if (d->stats) {
        ensureStatsSnapshot(d);
        const QString eventType =
                existing.isValid() ? QStringLiteral("update") : QStringLiteral("add");
        d->stats->recordUserEvent(candidate, MessengerUserKey(), eventType);
    }

    return ServerDatabaseError();
}

bool ServerDatabase::isBlacklisted(const MessengerUserKey &user, BlacklistEntry *entryOut)
{
    if (!d->ready)
        return false;
    return d->blacklistModel.isBlacklisted(user, entryOut);
}

ServerDatabaseError ServerDatabase::saveBlacklistEntry(const BlacklistEntry &entry,
                                                       const MessengerUserKey &actor)
{
    using Code = ServerDatabaseError::Code;
    using Op = ServerDatabaseError::Operation;
    static constexpr Op kOp = Op::SaveBlacklistEntry;

    if (!d->ready)
        return ServerDatabaseError(Code::DatabaseNotReady, kOp);

    if (!entry.isValid())
        return ServerDatabaseError(Code::BlacklistEntryInvalid, kOp);

    if (!d->actorCanManageBlacklistTarget(actor, entry.user())) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   entry.user().toString(),
                                   ServerDatabaseError::tr("Access denied. Admins only."));
    }

    const bool existed = d->blacklistModel.exists(entry.user());
    if (!d->upsertBlacklistRecord(entry)) {
        return ServerDatabaseError(Code::PersistenceFailed,
                                   kOp,
                                   entry.user().toString(),
                                   QString(),
                                   d->db.lastError());
    }

    d->blacklistModel.upsert(entry);

    const QString untilText = entry.blockedUntilUtc().isValid()
                                      ? entry.blockedUntilUtc().toString(Qt::ISODate)
                                      : QStringLiteral("<permanent>");
    QDebug log = infoLog();
    log << "Blacklist entry " << (existed ? "updated" : "added") << ' '
        << qPrintable(logUserText(d, entry.user())) << " until=" << untilText
        << " reason=" << entry.reason();
    d->appendActorToLog(log, actor, entry.user());
    return ServerDatabaseError();
}

ServerDatabaseError ServerDatabase::removeBlacklistEntry(const MessengerUserKey &user,
                                                         const MessengerUserKey &actor)
{
    using Code = ServerDatabaseError::Code;
    using Op = ServerDatabaseError::Operation;
    static constexpr Op kOp = Op::RemoveBlacklistEntry;

    if (!d->ready)
        return ServerDatabaseError(Code::DatabaseNotReady, kOp);

    if (!user.isValid())
        return ServerDatabaseError(Code::UserInvalid, kOp);

    if (!d->actorCanManageBlacklistTarget(actor, user)) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   user.toString(),
                                   ServerDatabaseError::tr("Access denied. Admins only."));
    }

    if (!d->blacklistModel.purgeExpiredEntries()) {
        return ServerDatabaseError(Code::PersistenceFailed,
                                   kOp,
                                   user.toString(),
                                   QString(),
                                   d->db.lastError());
    }

    const BlacklistEntry existing = d->blacklistModel.entryByKey(user);
    if (!existing.isValid())
        return ServerDatabaseError(Code::UserNotBlacklisted, kOp, user.toString());

    if (!d->deleteBlacklistRecord(user)) {
        return ServerDatabaseError(Code::PersistenceFailed,
                                   kOp,
                                   user.toString(),
                                   QString(),
                                   d->db.lastError());
    }

    d->blacklistModel.removeByKey(user);

    const QString untilText = existing.blockedUntilUtc().isValid()
                                      ? existing.blockedUntilUtc().toString(Qt::ISODate)
                                      : QStringLiteral("<permanent>");
    QDebug log = infoLog();
    log << "Blacklist entry removed " << qPrintable(logUserText(d, user)) << " until=" << untilText
        << " reason=" << existing.reason();
    d->appendActorToLog(log, actor, user);
    return ServerDatabaseError();
}

ServerDatabaseError ServerDatabase::banUser(const BlacklistEntry &entry,
                                            const MessengerUserKey &actor,
                                            int *removedQueueEntriesOut)
{
    using Code = ServerDatabaseError::Code;
    using Op = ServerDatabaseError::Operation;
    static constexpr Op kOp = Op::BanUser;

    if (removedQueueEntriesOut)
        *removedQueueEntriesOut = 0;

    if (!d->ready)
        return ServerDatabaseError(Code::DatabaseNotReady, kOp);

    if (!entry.isValid())
        return ServerDatabaseError(Code::BlacklistEntryInvalid, kOp);

    if (!d->actorCanManageBlacklistTarget(actor, entry.user())) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   entry.user().toString(),
                                   ServerDatabaseError::tr("Access denied. Admins only."));
    }

    if (!d->actorCanAdministerQueueEntriesForUser(actor, entry.user())) {
        return ServerDatabaseError(
                Code::OperationFailed,
                kOp,
                entry.user().toString(),
                ServerDatabaseError::tr(
                        "Access denied. The user has queue entries for resources outside your "
                        "administered groups."));
    }

    QList<ResourceQueueEntry> removedEntries;
    if (d->stats) {
        const QList<ResourceQueueEntry> currentEntries = d->queueModel.items();
        for (const ResourceQueueEntry &queueEntry : currentEntries) {
            if (!queueEntry.isValid())
                continue;
            if (queueEntry.user() != entry.user())
                continue;
            removedEntries.append(queueEntry);
        }
    }

    if (!d->db.isOpen() && !d->db.open()) {
        return ServerDatabaseError(Code::DatabaseOpenFailed,
                                   kOp,
                                   entry.user().toString(),
                                   QString(),
                                   d->db.lastError());
    }

    TransactionGuard tx(d->db);
    if (!tx.begin()) {
        return ServerDatabaseError(Code::TransactionBeginFailed,
                                   kOp,
                                   entry.user().toString(),
                                   QString(),
                                   d->db.lastError());
    }

    if (!d->upsertBlacklistRecord(entry) || !d->deleteQueueEntriesForUser(entry.user())) {
        tx.rollback();
        return ServerDatabaseError(Code::PersistenceFailed,
                                   kOp,
                                   entry.user().toString(),
                                   QString(),
                                   d->db.lastError());
    }

    if (!tx.commit()) {
        tx.rollback();
        return ServerDatabaseError(Code::TransactionCommitFailed,
                                   kOp,
                                   entry.user().toString(),
                                   QString(),
                                   d->db.lastError());
    }

    d->blacklistModel.upsert(entry);

    QList<ResourceQueueEntry> entries = d->queueModel.items();
    const qsizetype before = entries.size();
    entries.erase(std::remove_if(entries.begin(),
                                 entries.end(),
                                 [&entry](const ResourceQueueEntry &e) {
                                     return e.user() == entry.user();
                                 }),
                  entries.end());
    const qsizetype removed = before - entries.size();
    if (removedQueueEntriesOut)
        *removedQueueEntriesOut = static_cast<int>(removed);
    d->queueModel.replaceAll(entries);

    const QString untilText = entry.blockedUntilUtc().isValid()
                                      ? entry.blockedUntilUtc().toString(Qt::ISODate)
                                      : QStringLiteral("<permanent>");
    QDebug log = infoLog();
    log << "User banned " << qPrintable(logUserText(d, entry.user())) << " until=" << untilText
        << " reason=" << entry.reason() << " removed_queue_entries=" << removed;
    d->appendActorToLog(log, actor, entry.user());
    if (d->stats) {
        ensureStatsSnapshot(d);
        if (!removedEntries.isEmpty()) {
            const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
            for (const ResourceQueueEntry &queueEntry : std::as_const(removedEntries)) {
                const Resource resource =
                        d->resourceModel.resourceByUuid(queueEntry.resourceUuid());
                const QString resourceName = resource.isValid() ? resource.name() : QString();
                const bool active = queueEntry.isActiveAtUtc(nowUtc);
                const bool wasScheduled = isScheduledQueueEntry(queueEntry);
                if (active) {
                    d->stats->recordOccupationEvent(resourceName,
                                                    queueEntry.resourceUuid(),
                                                    queueEntry.user(),
                                                    actor,
                                                    QStringLiteral("occupation_end"),
                                                    wasScheduled,
                                                    0,
                                                    nowUtc,
                                                    QStringLiteral("ban_user"));
                } else {
                    d->stats->recordScheduleEvent(resourceName,
                                                  queueEntry.resourceUuid(),
                                                  queueEntry.user(),
                                                  actor,
                                                  QStringLiteral("schedule_remove"),
                                                  queueEntry.startAtUtc(),
                                                  queueEntry.endAtUtc(),
                                                  nowUtc,
                                                  QStringLiteral("ban_user"));
                }
            }
        }
    }
    return ServerDatabaseError();
}

ServerDatabaseError ServerDatabase::unbanUser(const MessengerUserKey &user,
                                              const MessengerUserKey &actor,
                                              BlacklistEntry *previousEntryOut)
{
    using Code = ServerDatabaseError::Code;
    using Op = ServerDatabaseError::Operation;
    static constexpr Op kOp = Op::UnbanUser;

    if (previousEntryOut)
        *previousEntryOut = BlacklistEntry();

    if (!d->ready)
        return ServerDatabaseError(Code::DatabaseNotReady, kOp);

    if (!user.isValid())
        return ServerDatabaseError(Code::UserInvalid, kOp);

    if (!d->actorCanManageBlacklistTarget(actor, user)) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   user.toString(),
                                   ServerDatabaseError::tr("Access denied. Admins only."));
    }

    if (!d->blacklistModel.purgeExpiredEntries()) {
        return ServerDatabaseError(Code::PersistenceFailed,
                                   kOp,
                                   user.toString(),
                                   QString(),
                                   d->db.lastError());
    }

    const BlacklistEntry existing = d->blacklistModel.entryByKey(user);
    if (!existing.isValid())
        return ServerDatabaseError(Code::UserNotBlacklisted, kOp, user.toString());

    if (!d->deleteBlacklistRecord(user)) {
        return ServerDatabaseError(Code::PersistenceFailed,
                                   kOp,
                                   user.toString(),
                                   QString(),
                                   d->db.lastError());
    }

    d->blacklistModel.removeByKey(user);
    if (previousEntryOut)
        *previousEntryOut = existing;

    const QString untilText = existing.blockedUntilUtc().isValid()
                                      ? existing.blockedUntilUtc().toString(Qt::ISODate)
                                      : QStringLiteral("<permanent>");
    QDebug log = infoLog();
    log << "User unbanned " << qPrintable(logUserText(d, user)) << " previous_until=" << untilText
        << " previous_reason=" << existing.reason();
    d->appendActorToLog(log, actor, user);
    return ServerDatabaseError();
}

ServerDatabaseError ServerDatabase::occupyResourceUser(const QString &resourceName,
                                                       const MessengerUserKey &user,
                                                       int durationMinutes,
                                                       const QList<int> &allowedDurations,
                                                       const MessengerUserKey &actor)
{
    using Code = ServerDatabaseError::Code;
    using Op = ServerDatabaseError::Operation;
    static constexpr Op kOp = Op::Enqueue;

    if (!d->ready)
        return ServerDatabaseError(Code::DatabaseNotReady, kOp);

    if (!d->actorHasActiveAccess(actor)) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   actor.toString(),
                                   ServerDatabaseError::tr(
                                           "Access denied. Access is not activated."));
    }

    if (!user.isValid())
        return ServerDatabaseError(Code::UserInvalid, kOp);

    const ServerDatabaseError availabilityError =
            validateResourceAvailabilityForQueueOperation(d, resourceName, actor, kOp);
    if (availabilityError)
        return availabilityError;

    const Resource resource = d->resourceModel.resourceByName(resourceName.trimmed());
    if (!d->userHasAccessToResource(user, resource)) {
        return ServerDatabaseError(Code::ResourceNotFound, kOp, resourceName.trimmed());
    }
    if (actor != user
        && (!d->actorCanModifyUser(actor, user) || !d->actorCanModifyResource(actor, resource))) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   user.toString(),
                                   ServerDatabaseError::tr("Access denied. Admins only."));
    }

    QString errorText;
    if (!d->queueModel.occupyResourceUser(
                resourceName, user, durationMinutes, allowedDurations, actor, &errorText)) {
        return ServerDatabaseError(Code::OperationFailed, kOp, resourceName, errorText);
    }

    QDebug log = infoLog();
    log << "Queue enqueue " << qPrintable(logResourceText(d, resourceName)) << " on "
        << qPrintable(logUserText(d, user)) << " duration=" << durationMinutes;
    d->appendActorToLog(log, actor, user);

    return ServerDatabaseError();
}

ServerDatabaseError ServerDatabase::scheduleResourceUser(const QString &resourceName,
                                                         const MessengerUserKey &user,
                                                         const QDateTime &startUtc,
                                                         const QDateTime &endUtc,
                                                         const MessengerUserKey &actor)
{
    using Code = ServerDatabaseError::Code;
    using Op = ServerDatabaseError::Operation;
    static constexpr Op kOp = Op::Schedule;

    if (!d->ready)
        return ServerDatabaseError(Code::DatabaseNotReady, kOp);

    if (!d->actorHasActiveAccess(actor)) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   actor.toString(),
                                   ServerDatabaseError::tr(
                                           "Access denied. Access is not activated."));
    }

    if (!user.isValid())
        return ServerDatabaseError(Code::UserInvalid, kOp);

    const ServerDatabaseError availabilityError =
            validateResourceAvailabilityForQueueOperation(d, resourceName, actor, kOp);
    if (availabilityError)
        return availabilityError;

    const Resource resource = d->resourceModel.resourceByName(resourceName.trimmed());
    if (!d->userHasAccessToResource(user, resource)) {
        return ServerDatabaseError(Code::ResourceNotFound, kOp, resourceName.trimmed());
    }
    if (actor != user
        && (!d->actorCanModifyUser(actor, user) || !d->actorCanModifyResource(actor, resource))) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   user.toString(),
                                   ServerDatabaseError::tr("Access denied. Admins only."));
    }

    QString errorText;
    if (!d->queueModel
                 .scheduleResourceUser(resourceName, user, startUtc, endUtc, actor, &errorText)) {
        return ServerDatabaseError(Code::OperationFailed, kOp, resourceName, errorText);
    }

    QDebug log = infoLog();
    log << "Queue schedule " << qPrintable(logResourceText(d, resourceName)) << " on "
        << qPrintable(logUserText(d, user)) << " start=" << startUtc.toString(Qt::ISODate)
        << " end=" << endUtc.toString(Qt::ISODate);
    d->appendActorToLog(log, actor, user);
    if (d->stats) {
        ensureStatsSnapshot(d);
        const Resource resource = d->resourceModel.resourceByName(resourceName);
        const QString resourceUuid = resource.isValid() ? resource.uuidString() : QString();
        d->stats->recordScheduleEvent(resourceName,
                                      resourceUuid,
                                      user,
                                      actor,
                                      QStringLiteral("schedule_set"),
                                      startUtc,
                                      endUtc,
                                      QDateTime::currentDateTimeUtc());
    }
    return ServerDatabaseError();
}

ServerDatabaseError ServerDatabase::removeUserSchedulesForResource(const QString &resourceName,
                                                                   const MessengerUserKey &user,
                                                                   const MessengerUserKey &actor)
{
    using Code = ServerDatabaseError::Code;
    using Op = ServerDatabaseError::Operation;
    static constexpr Op kOp = Op::Unschedule;

    if (!d->ready)
        return ServerDatabaseError(Code::DatabaseNotReady, kOp);

    if (!d->actorHasActiveAccess(actor)) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   actor.toString(),
                                   ServerDatabaseError::tr(
                                           "Access denied. Access is not activated."));
    }

    if (!user.isValid())
        return ServerDatabaseError(Code::UserInvalid, kOp);

    const ServerDatabaseError availabilityError =
            validateResourceAvailabilityForQueueOperation(d, resourceName, actor, kOp);
    if (availabilityError)
        return availabilityError;

    const Resource targetResource = d->resourceModel.resourceByName(resourceName.trimmed());
    if (actor != user
        && (!d->actorCanModifyUser(actor, user)
            || !d->actorCanModifyResource(actor, targetResource))) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   user.toString(),
                                   ServerDatabaseError::tr("Access denied. Admins only."));
    }

    QList<ResourceQueueEntry> removedEntries;
    QString resourceUuid;
    if (d->stats) {
        const Resource resource = d->resourceModel.resourceByName(resourceName);
        resourceUuid = resource.isValid() ? resource.uuidString() : QString();
        if (!resourceUuid.isEmpty()) {
            const QList<ResourceQueueEntry> entries = d->queueModel.items();
            for (const ResourceQueueEntry &entry : entries) {
                if (!entry.isValid())
                    continue;
                if (entry.user() != user)
                    continue;
                if (entry.resourceUuid() != resourceUuid)
                    continue;
                removedEntries.append(entry);
            }
        }
    }

    QString errorText;
    if (!d->queueModel.removeUserSchedulesForResource(resourceName, user, &errorText)) {
        return ServerDatabaseError(Code::OperationFailed, kOp, resourceName, errorText);
    }

    QDebug log = infoLog();
    log << "Queue unschedule " << qPrintable(logResourceText(d, resourceName)) << " on "
        << qPrintable(logUserText(d, user));
    d->appendActorToLog(log, actor, user);
    if (d->stats) {
        ensureStatsSnapshot(d);
        if (!removedEntries.isEmpty()) {
            const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
            for (const ResourceQueueEntry &entry : std::as_const(removedEntries)) {
                const bool active = entry.isActiveAtUtc(nowUtc);
                const bool wasScheduled = isScheduledQueueEntry(entry);
                if (active) {
                    d->stats->recordOccupationEvent(resourceName,
                                                    resourceUuid,
                                                    entry.user(),
                                                    actor,
                                                    QStringLiteral("occupation_end"),
                                                    wasScheduled,
                                                    0,
                                                    nowUtc,
                                                    QStringLiteral("unschedule"));
                } else {
                    d->stats->recordScheduleEvent(resourceName,
                                                  resourceUuid,
                                                  entry.user(),
                                                  actor,
                                                  QStringLiteral("schedule_remove"),
                                                  entry.startAtUtc(),
                                                  entry.endAtUtc(),
                                                  nowUtc);
                }
            }
        }
    }
    return ServerDatabaseError();
}

ServerDatabaseError ServerDatabase::releaseUserOccupationForResource(const QString &resourceName,
                                                                     const MessengerUserKey &user,
                                                                     const MessengerUserKey &actor)
{
    using Code = ServerDatabaseError::Code;
    using Op = ServerDatabaseError::Operation;
    static constexpr Op kOp = Op::Release;

    if (!d->ready)
        return ServerDatabaseError(Code::DatabaseNotReady, kOp);

    if (!d->actorHasActiveAccess(actor)) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   actor.toString(),
                                   ServerDatabaseError::tr(
                                           "Access denied. Access is not activated."));
    }

    if (!user.isValid())
        return ServerDatabaseError(Code::UserInvalid, kOp);

    const ServerDatabaseError availabilityError =
            validateResourceAvailabilityForQueueOperation(d, resourceName, actor, kOp);
    if (availabilityError)
        return availabilityError;

    const Resource resource = d->resourceModel.resourceByName(resourceName.trimmed());
    if (actor != user
        && (!d->actorCanModifyUser(actor, user) || !d->actorCanModifyResource(actor, resource))) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   user.toString(),
                                   ServerDatabaseError::tr("Access denied. Admins only."));
    }

    QString errorText;
    if (!d->queueModel.releaseUserOccupationForResource(resourceName, user, actor, &errorText)) {
        return ServerDatabaseError(Code::OperationFailed, kOp, resourceName, errorText);
    }

    QDebug log = infoLog();
    log << "Queue release " << qPrintable(logResourceText(d, resourceName)) << " on "
        << qPrintable(logUserText(d, user));
    d->appendActorToLog(log, actor, user);

    return ServerDatabaseError();
}

ServerDatabaseError ServerDatabase::removeResourceQueueEntries(const QString &resourceName,
                                                               const MessengerUserKey &actor,
                                                               int *removedCountOut)
{
    using Code = ServerDatabaseError::Code;
    using Op = ServerDatabaseError::Operation;
    static constexpr Op kOp = Op::RemoveResourceQueue;

    if (removedCountOut)
        *removedCountOut = 0;

    if (!d->ready)
        return ServerDatabaseError(Code::DatabaseNotReady, kOp);

    if (!d->actorHasActiveAccess(actor)) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   actor.toString(),
                                   ServerDatabaseError::tr(
                                           "Access denied. Access is not activated."));
    }

    const ServerDatabaseError availabilityError =
            validateResourceAvailabilityForQueueOperation(d, resourceName, actor, kOp);
    if (availabilityError)
        return availabilityError;

    const Resource resource = d->resourceModel.resourceByName(resourceName.trimmed());
    if (!d->actorCanModifyResource(actor, resource)) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   resourceName,
                                   ServerDatabaseError::tr("Access denied. Admins only."));
    }

    QList<ResourceQueueEntry> removedEntries;
    if (d->stats) {
        const QString resourceUuid = resource.uuidString();
        const QList<ResourceQueueEntry> entries = d->queueModel.items();
        for (const ResourceQueueEntry &entry : entries) {
            if (!entry.isValid())
                continue;
            if (entry.resourceUuid() != resourceUuid)
                continue;
            removedEntries.append(entry);
        }
    }

    QString errorText;
    int removedCount = 0;
    if (!d->queueModel.removeResourceEntriesForResource(resourceName, &removedCount, &errorText)) {
        if (removedCountOut)
            *removedCountOut = removedCount;
        return ServerDatabaseError(Code::OperationFailed, kOp, resourceName, errorText);
    }

    if (removedCountOut)
        *removedCountOut = removedCount;

    QDebug log = infoLog();
    log << "Queue remove resource " << qPrintable(logResourceText(d, resourceName))
        << " removed_entries=" << removedCount;
    d->appendActorToLog(log, actor);
    if (d->stats) {
        ensureStatsSnapshot(d);
        if (!removedEntries.isEmpty()) {
            const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
            for (const ResourceQueueEntry &entry : std::as_const(removedEntries)) {
                const bool active = entry.isActiveAtUtc(nowUtc);
                const bool wasScheduled = isScheduledQueueEntry(entry);
                if (active) {
                    d->stats->recordOccupationEvent(resource.name(),
                                                    resource.uuidString(),
                                                    entry.user(),
                                                    actor,
                                                    QStringLiteral("occupation_end"),
                                                    wasScheduled,
                                                    0,
                                                    nowUtc,
                                                    QStringLiteral("clear_resource_queue"));
                } else {
                    d->stats->recordScheduleEvent(resource.name(),
                                                  resource.uuidString(),
                                                  entry.user(),
                                                  actor,
                                                  QStringLiteral("schedule_remove"),
                                                  entry.startAtUtc(),
                                                  entry.endAtUtc(),
                                                  nowUtc,
                                                  QStringLiteral("clear_resource_queue"));
                }
            }
        }
    }
    return ServerDatabaseError();
}

ServerDatabaseError ServerDatabase::removeUserFromQueues(const MessengerUserKey &user,
                                                         const MessengerUserKey &actor,
                                                         int *removedCountOut)
{
    using Code = ServerDatabaseError::Code;
    using Op = ServerDatabaseError::Operation;
    static constexpr Op kOp = Op::RemoveUserQueues;

    if (!d->ready)
        return ServerDatabaseError(Code::DatabaseNotReady, kOp);

    if (!d->actorHasActiveAccess(actor)) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   actor.toString(),
                                   ServerDatabaseError::tr(
                                           "Access denied. Access is not activated."));
    }

    if (!user.isValid())
        return ServerDatabaseError(Code::UserInvalid, kOp);

    if (actor != user
        && (!d->actorCanModifyUser(actor, user)
            || !d->actorCanAdministerQueueEntriesForUser(actor, user))) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   user.toString(),
                                   ServerDatabaseError::tr("Access denied. Admins only."));
    }

    QList<ResourceQueueEntry> removedEntries;
    if (d->stats) {
        const QList<ResourceQueueEntry> entries = d->queueModel.items();
        for (const ResourceQueueEntry &entry : entries) {
            if (!entry.isValid())
                continue;
            if (entry.user() != user)
                continue;
            removedEntries.append(entry);
        }
    }

    QString errorText;
    int removedCount = 0;
    if (!d->queueModel.removeUserFromQueues(user, &removedCount, &errorText)) {
        if (removedCountOut)
            *removedCountOut = removedCount;
        return ServerDatabaseError(Code::OperationFailed, kOp, user.toString(), errorText);
    }

    if (removedCountOut)
        *removedCountOut = removedCount;

    QDebug log = infoLog();
    log << "Queue remove user " << qPrintable(logUserText(d, user))
        << " removed_entries=" << removedCount;
    d->appendActorToLog(log, actor, user);
    if (d->stats) {
        ensureStatsSnapshot(d);
        if (!removedEntries.isEmpty()) {
            const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
            for (const ResourceQueueEntry &entry : std::as_const(removedEntries)) {
                const Resource resource = d->resourceModel.resourceByUuid(entry.resourceUuid());
                const QString resourceName = resource.isValid() ? resource.name() : QString();
                const bool active = entry.isActiveAtUtc(nowUtc);
                const bool wasScheduled = isScheduledQueueEntry(entry);
                if (active) {
                    d->stats->recordOccupationEvent(resourceName,
                                                    entry.resourceUuid(),
                                                    entry.user(),
                                                    actor,
                                                    QStringLiteral("occupation_end"),
                                                    wasScheduled,
                                                    0,
                                                    nowUtc,
                                                    QStringLiteral("remove_user"));
                } else {
                    d->stats->recordScheduleEvent(resourceName,
                                                  entry.resourceUuid(),
                                                  entry.user(),
                                                  actor,
                                                  QStringLiteral("schedule_remove"),
                                                  entry.startAtUtc(),
                                                  entry.endAtUtc(),
                                                  nowUtc,
                                                  QStringLiteral("remove_user"));
                }
            }
        }
    }
    return ServerDatabaseError();
}

ServerDatabaseError ServerDatabase::recordPendingAccessUser(const PendingAccessUserEntry &entry,
                                                            const MessengerUserKey &actor)
{
    using Code = ServerDatabaseError::Code;
    using Op = ServerDatabaseError::Operation;
    static constexpr Op kOp = Op::RecordPendingAccessUser;

    if (!d->ready)
        return ServerDatabaseError(Code::DatabaseNotReady, kOp);

    if (!d->pendingAccessUserModel.recordUser(entry)) {
        return ServerDatabaseError(Code::PersistenceFailed, kOp);
    }

    const MessengerUserKey user = entry.user();
    const QDateTime lastMessageAt = entry.lastMessageAtUtc();
    QDebug log = infoLog();
    log << "Pending-access user recorded " << user.toString() << " name=" << entry.firstName()
        << ' ' << entry.lastName() << " last_message_at=" << lastMessageAt.toString(Qt::ISODate);
    d->appendActorToLog(log, actor, user);
    return ServerDatabaseError();
}

ServerDatabaseError ServerDatabase::clearPendingAccessUser(const MessengerUserKey &user,
                                                           const MessengerUserKey &actor)
{
    using Code = ServerDatabaseError::Code;
    using Op = ServerDatabaseError::Operation;
    static constexpr Op kOp = Op::ClearPendingAccessUser;

    if (!d->ready)
        return ServerDatabaseError(Code::DatabaseNotReady, kOp);

    if (!user.isValid())
        return ServerDatabaseError(Code::UserInvalid, kOp);

    if (!d->actorCanManageServerSettings(actor)) {
        return ServerDatabaseError(Code::OperationFailed,
                                   kOp,
                                   user.toString(),
                                   ServerDatabaseError::tr(
                                           "Access denied. Server administrators only."));
    }

    if (!d->pendingAccessUserModel.clearUser(user)) {
        return ServerDatabaseError(Code::PersistenceFailed, kOp);
    }

    QDebug log = infoLog();
    log << "Pending-access user cleared " << user.toString();
    d->appendActorToLog(log, actor, user);
    return ServerDatabaseError();
}
