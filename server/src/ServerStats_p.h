#ifndef SERVERSTATS_P_H
#define SERVERSTATS_P_H

#include "ServerStats.h"
#include "Group.h"

#include <QHash>
#include <QSet>
#include <QSqlDatabase>

class QSqlQuery;
class QThread;

class ServerStatsPrivate
{
public:
    explicit ServerStatsPrivate(ServerStats *q, const QString &baseDatabasePath);
    ~ServerStatsPrivate();

    bool ensureDatabaseForUtc(const QDateTime &utcTime);
    bool ensureSchema();
    bool ensureMetaTable();
    bool ensureEventsTable();
    bool ensureUsersTable();
    bool ensureResourcesTable();
    bool ensureColumn(const QString &table, const QString &column, const QString &definition);
    QString readMetaValue(const QString &key) const;
    bool writeMetaValue(const QString &key, const QString &value) const;

    void closeDatabase();
    QString statsDirectory() const;
    QString statsFilePathForMonth(const QDate &utcMonth) const;
    QString monthKey(const QDate &utcMonth) const;

    static qint64 toEpochSeconds(const QDateTime &utcTime);

    bool insertEvent(const QDateTime &eventAtUtc,
                     const QString &eventType,
                     const QString &resourceUuid,
                     const QString &resourceName,
                     const QString &userKey,
                     const QString &initiatorKey,
                     bool scheduled,
                     int durationMinutes,
                     const QDateTime &scheduleStartUtc,
                     const QDateTime &scheduleEndUtc,
                     const QString &note,
                     bool isSnapshot);

    bool insertUserEvent(const QDateTime &eventAtUtc,
                         const QString &eventType,
                         const User &user,
                         const MessengerUserKey &actor,
                         bool isSnapshot);

    bool insertResourceEvent(const QDateTime &eventAtUtc,
                             const QString &eventType,
                             const Resource &resource,
                             const MessengerUserKey &actor,
                             bool isSnapshot);

    ServerStats *q;
    QString baseDbPath;
    bool enabled;
    bool initialSnapshotCaptured;
    int activeReportCount;
    QSet<QThread *> activeReportThreads;
    QString currentMonthKey;
    QString connectionName;
    QSqlDatabase db;
    QHash<QString, QSet<Group>> resourceGroupsByUuid;
    QHash<QString, QSet<Group>> userGroupsByKey;
};

#endif // SERVERSTATS_P_H
