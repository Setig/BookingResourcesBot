#include "ServerStats.h"
#include "ServerStats_p.h"

#include "User.h"
#include "Resource.h"
#include "UserModel.h"
#include "ResourceModel.h"
#include "ResourceQueueModel.h"
#include "ResourceQueueTypes.h"
#include "MessengerTextRender.h"
#include "ServerDateTimeFormat.h"
#include "ServerStatsReportWorker.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

#include <QDir>
#include <QSet>
#include <QFile>
#include <QDate>
#include <QHash>
#include <QTime>
#include <QList>
#include <QUuid>
#include <QVector>
#include <QThread>
#include <QTimer>
#include <QPointer>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>
#include <QTextStream>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QCoreApplication>
#include <QRegularExpression>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#  include <QStringConverter>
#endif

Q_LOGGING_CATEGORY(lcServerStats, "booking.server.stats")

namespace {

constexpr int kSchemaVersion = 2;
constexpr int kTopResourcesLimit = 10;
constexpr int kMaxTableRows = 100;
constexpr int kReportGenerationTimeoutMs = 30 * 1000;
constexpr int kReportShutdownWaitMs = 5000;
constexpr int kStatsBusyTimeoutMs = 5000;
constexpr int kMaxConcurrentReports = 1;
constexpr qsizetype kMaxReportEvents = 250000;

QString statsFileNameForMonth(const QDate &utcMonth)
{
    const int year = utcMonth.year();
    const int month = utcMonth.month();
    return QStringLiteral("resources_bot_stats_%1_%2.sqlite")
            .arg(year, 4, 10, QLatin1Char('0'))
            .arg(month, 2, 10, QLatin1Char('0'));
}

QString csvEscape(const QString &value)
{
    QString out = value;
    if (!out.isEmpty()
        && (out.at(0) == QLatin1Char('=') || out.at(0) == QLatin1Char('+')
            || out.at(0) == QLatin1Char('-') || out.at(0) == QLatin1Char('@'))) {
        out.prepend(QLatin1Char('\''));
    }
    out.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    if (out.contains(QLatin1Char(',')) || out.contains(QLatin1Char('"'))
        || out.contains(QLatin1Char('\n')) || out.contains(QLatin1Char('\r'))) {
        out.prepend(QLatin1Char('"'));
        out.append(QLatin1Char('"'));
    }
    return out;
}

QString serializeGroups(const QSet<Group> &groups)
{
    if (groups.isEmpty())
        return QString();

    QJsonArray values;
    QStringList paths;
    for (const Group &group : groups)
        paths.append(QString(group));
    paths.sort(Qt::CaseSensitive);
    for (const QString &path : paths)
        values.append(path);
    return QString::fromUtf8(QJsonDocument(values).toJson(QJsonDocument::Compact));
}

QSet<Group> deserializeGroups(const QString &value)
{
    QSet<Group> groups;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(value.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray())
        return groups;
    const QJsonArray values = document.array();
    for (const QJsonValue &entry : values) {
        if (entry.isString())
            groups.insert(Group(entry.toString()));
    }
    return groups;
}

bool tableHasColumn(QSqlDatabase &db, const QString &table, const QString &column)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table)))
        return false;
    while (query.next()) {
        if (query.value(1).toString().compare(column, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

bool groupsCoveredByRoots(const QSet<Group> &groups, const QSet<Group> &roots, bool requireAll)
{
    if (groups.isEmpty() || roots.isEmpty())
        return false;

    bool anyCovered = false;
    for (const Group &group : groups) {
        bool covered = false;
        for (const Group &root : roots) {
            if (root.contains(group)) {
                covered = true;
                anyCovered = true;
                break;
            }
        }
        if (requireAll && !covered)
            return false;
    }
    return requireAll || anyCovered;
}

struct ReportEvent
{
    qint64 rowId = 0;
    qint64 eventSeconds = 0;
    QString eventType;
    QString resourceUuid;
    QString resourceName;
    QString userKey;
    QString initiatorKey;
    bool scheduled = false;
    int durationMinutes = 0;
    qint64 scheduleStartSeconds = 0;
    qint64 scheduleEndSeconds = 0;
    bool isSnapshot = false;
    QString note;
    QString resourceGroups;
    QString userGroups;
    QString initiatorGroups;
};

bool userKeyAllowedByScope(const ServerStats::ReportRequest &request,
                           const QString &userKey,
                           const QString &serializedGroups,
                           const QSet<Group> &authorizedRoots)
{
    bool explicitlyAllowed = false;
    for (const MessengerUserKey &allowed : request.allowedUsers) {
        if (allowed.toString() == userKey) {
            explicitlyAllowed = true;
            break;
        }
    }
    if (!explicitlyAllowed)
        return false;
    return groupsCoveredByRoots(deserializeGroups(serializedGroups),
                                authorizedRoots,
                                request.administrativeScope);
}

bool eventAllowedByScope(const ServerStats::ReportRequest &request, ReportEvent *event)
{
    if (!event)
        return false;
    if (!request.scopeRestricted)
        return true;

    QSet<Group> authorizedRoots;
    for (const QString &path : request.authorizedGroupRoots)
        authorizedRoots.insert(Group(path));

    bool hasScopedSubject = false;
    if (!event->resourceUuid.trimmed().isEmpty() || !event->resourceName.trimmed().isEmpty()) {
        hasScopedSubject = true;
        bool resourceAllowed = false;
        for (const QString &uuid : request.allowedResourceUuids) {
            if (uuid.trimmed() == event->resourceUuid.trimmed()) {
                resourceAllowed = true;
                break;
            }
        }
        if (!resourceAllowed)
            return false;
        if (!groupsCoveredByRoots(deserializeGroups(event->resourceGroups),
                                  authorizedRoots,
                                  request.administrativeScope)) {
            return false;
        }
    }

    if (!event->userKey.trimmed().isEmpty()) {
        hasScopedSubject = true;
        if (!userKeyAllowedByScope(request, event->userKey, event->userGroups, authorizedRoots)) {
            return false;
        }
    }

    if (!event->initiatorKey.trimmed().isEmpty()
        && !userKeyAllowedByScope(request,
                                  event->initiatorKey,
                                  event->initiatorGroups,
                                  authorizedRoots)) {
        event->initiatorKey.clear();
    }

    return hasScopedSubject;
}

struct SessionSpan
{
    QString resourceKey;
    QString resourceName;
    QString resourceUuid;
    QString userKey;
    qint64 startSeconds = 0;
    qint64 endSeconds = 0;
};

struct ActiveQueueOccupation
{
    QString resourceUuid;
    QString resourceName;
    QString userKey;
    qint64 startSeconds = 0;
};

struct ResourceSummaryRow
{
    QString resourceName;
    qint64 occupiedSeconds = 0;
    int sessions = 0;
};

struct UserSummaryRow
{
    qint64 occupiedSeconds = 0;
    int sessions = 0;
    QSet<QString> uniqueResourceKeys;
};

struct UserNameSnapshot
{
    qint64 eventSeconds = 0;
    QString firstName;
    QString lastName;
};

struct InitiatorSummaryRow
{
    int total = 0;
    int occupationStart = 0;
    int occupationEnd = 0;
    int scheduleSet = 0;
    int scheduleRemove = 0;
    int other = 0;
};

QString reportKindSuffix(ServerStats::ReportRequest::ReportKind kind)
{
    switch (kind) {
    case ServerStats::ReportRequest::ReportKind::UtilizationSummary:
        return QStringLiteral("utilization");
    case ServerStats::ReportRequest::ReportKind::UserActivitySummary:
        return QStringLiteral("user_activity");
    case ServerStats::ReportRequest::ReportKind::TopResources:
        return QStringLiteral("top_resources");
    case ServerStats::ReportRequest::ReportKind::PeakHours:
        return QStringLiteral("peak_hours");
    case ServerStats::ReportRequest::ReportKind::ScheduledVsActual:
        return QStringLiteral("plan_vs_actual");
    case ServerStats::ReportRequest::ReportKind::WeekdayPattern:
        return QStringLiteral("weekday_pattern");
    case ServerStats::ReportRequest::ReportKind::SessionDurationDistribution:
        return QStringLiteral("duration_distribution");
    case ServerStats::ReportRequest::ReportKind::InitiatorAnalysis:
        return QStringLiteral("initiator_analysis");
    case ServerStats::ReportRequest::ReportKind::EventsLog:
        return QStringLiteral("events");
    }

    return QStringLiteral("report");
}

QDate earliestStatsMonth(const QString &statsDirPath)
{
    const QDir dir(statsDirPath);
    if (!dir.exists())
        return QDate();

    const QStringList files = dir.entryList({ QStringLiteral("resources_bot_stats_*.sqlite") },
                                            QDir::Files,
                                            QDir::Name);
    const QRegularExpression rx(QStringLiteral("^resources_bot_stats_(\\d{4})_(\\d{2})\\.sqlite$"));

    QDate earliest;
    for (const QString &fileName : files) {
        const QRegularExpressionMatch match = rx.match(fileName);
        if (!match.hasMatch())
            continue;

        bool yearOk = false;
        bool monthOk = false;
        const int year = match.captured(1).toInt(&yearOk);
        const int month = match.captured(2).toInt(&monthOk);
        if (!yearOk || !monthOk)
            continue;

        const QDate monthDate(year, month, 1);
        if (!monthDate.isValid())
            continue;
        if (!earliest.isValid() || monthDate < earliest)
            earliest = monthDate;
    }

    return earliest;
}

QString normalizeResourceKey(const QString &resourceUuid, const QString &resourceName)
{
    const QString uuid = resourceUuid.trimmed();
    if (!uuid.isEmpty())
        return QStringLiteral("uuid:%1").arg(uuid);

    const QString name = resourceName.trimmed().toLower();
    if (!name.isEmpty())
        return QStringLiteral("name:%1").arg(name);

    return QString();
}

QString displayResourceName(const QString &resourceName, const QString &resourceUuid)
{
    const QString trimmedName = resourceName.trimmed();
    if (!trimmedName.isEmpty())
        return trimmedName;
    return resourceUuid.trimmed();
}

QString safeResourceName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (!trimmed.isEmpty())
        return trimmed;
    return QCoreApplication::translate("ServerStats", "unknown");
}

bool isInRange(qint64 eventSeconds, qint64 startSeconds, qint64 endSeconds)
{
    return eventSeconds >= startSeconds && eventSeconds <= endSeconds;
}

QString formatHoursValue(qint64 seconds)
{
    return QString::number(static_cast<double>(seconds) / 3600.0, 'f', 2);
}

QString formatMinutesValue(double minutes)
{
    return QString::number(minutes, 'f', 2);
}

QString formatPercentValue(double value)
{
    return QString::number(value, 'f', 2);
}

QString formatReportDateTimeValue(qint64 epochSeconds, const QTimeZone &timeZone)
{
    if (epochSeconds <= 0)
        return QString();

    const QDateTime utcDateTime = QDateTime::fromSecsSinceEpoch(epochSeconds, QTimeZone::utc());
    if (!utcDateTime.isValid())
        return QString();

    return utcDateTime.toTimeZone(timeZone).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString localizedRankingTypeValue(const QString &value)
{
    if (value == QStringLiteral("hours"))
        return QCoreApplication::translate("ServerStats", "hours");
    if (value == QStringLiteral("sessions"))
        return QCoreApplication::translate("ServerStats", "sessions");
    return value;
}

QString localizedInitiatorTypeValue(const QString &value)
{
    if (value == QStringLiteral("admin"))
        return QCoreApplication::translate("ServerStats", "admin");
    if (value == QStringLiteral("user"))
        return QCoreApplication::translate("ServerStats", "user");
    if (value == QStringLiteral("system"))
        return QCoreApplication::translate("ServerStats", "system");
    return value;
}

QString displayUserNameForStats(const QString &userKey,
                                const QHash<QString, UserNameSnapshot> &userNamesByKey)
{
    const QString normalizedKey = userKey.trimmed();
    if (normalizedKey.isEmpty())
        return normalizedKey;

    QString firstName;
    QString lastName;
    const QHash<QString, UserNameSnapshot>::const_iterator snapshotIt =
            userNamesByKey.constFind(normalizedKey);
    if (snapshotIt != userNamesByKey.cend()) {
        firstName = snapshotIt->firstName;
        lastName = snapshotIt->lastName;
    }

    bool keyParsed = false;
    const MessengerUserKey key = MessengerUserKey::fromString(normalizedKey, &keyParsed);
    if (keyParsed && key.isValid()) {
        const User user(key, firstName, lastName, User::Role::UserRole);
        const QString display = MessengerTextRender::formatUserDisplayName(user, false).trimmed();
        if (!display.isEmpty())
            return display;
    }

    const QString fallbackName =
            QStringLiteral("%1 %2").arg(firstName.trimmed(), lastName.trimmed()).trimmed();
    if (!fallbackName.isEmpty())
        return fallbackName;

    return normalizedKey;
}

QTimeZone effectiveReportTimeZone(const UserTimeZone &timeZone)
{
    return ServerDateTimeFormat::effectiveTimeZone(timeZone);
}

QString reportTimeZoneLabel(const QTimeZone &timeZone, const QDateTime &referenceUtc)
{
    if (!timeZone.isValid())
        return QStringLiteral("UTC");

    const QDateTime effectiveReferenceUtc =
            referenceUtc.isValid() ? referenceUtc.toUTC() : QDateTime::currentDateTimeUtc();
    const int offsetSeconds = timeZone.offsetFromUtc(effectiveReferenceUtc);
    const int offsetMinutes = offsetSeconds / 60;
    if (!UserTimeZone::isOffsetInRange(offsetMinutes))
        return QStringLiteral("UTC");

    return UserTimeZone::formatOffset(static_cast<short>(offsetMinutes));
}

void addSecondsToHourBuckets(qint64 fromSeconds,
                             qint64 toSeconds,
                             std::array<qint64, 24> *buckets,
                             const QTimeZone &timeZone)
{
    if (!buckets || toSeconds <= fromSeconds)
        return;

    qint64 cursor = fromSeconds;
    while (cursor < toSeconds) {
        const QDateTime cursorLocal = QDateTime::fromSecsSinceEpoch(cursor, timeZone);
        if (!cursorLocal.isValid())
            break;

        const int hour = cursorLocal.time().hour();
        const QDateTime hourStartLocal(cursorLocal.date(), QTime(hour, 0), timeZone);
        qint64 nextHourSeconds = hourStartLocal.addSecs(60 * 60).toSecsSinceEpoch();
        if (nextHourSeconds <= cursor)
            nextHourSeconds = cursor + (60 * 60);
        const qint64 segmentEnd = qMin(toSeconds, nextHourSeconds);
        if (segmentEnd <= cursor)
            break;

        (*buckets)[hour] += (segmentEnd - cursor);
        cursor = segmentEnd;
    }
}

void addSecondsToWeekdayBuckets(qint64 fromSeconds,
                                qint64 toSeconds,
                                std::array<qint64, 7> *buckets,
                                const QTimeZone &timeZone)
{
    if (!buckets || toSeconds <= fromSeconds)
        return;

    qint64 cursor = fromSeconds;
    while (cursor < toSeconds) {
        const QDateTime cursorLocal = QDateTime::fromSecsSinceEpoch(cursor, timeZone);
        if (!cursorLocal.isValid())
            break;

        const int weekday = qBound(1, cursorLocal.date().dayOfWeek(), 7);
        const int bucketIndex = weekday - 1;
        const QDateTime dayStartLocal(cursorLocal.date(), QTime(0, 0), timeZone);
        qint64 nextDaySeconds = dayStartLocal.addDays(1).toSecsSinceEpoch();
        if (nextDaySeconds <= cursor)
            nextDaySeconds = cursor + (24 * 60 * 60);
        const qint64 segmentEnd = qMin(toSeconds, nextDaySeconds);
        if (segmentEnd <= cursor)
            break;

        (*buckets)[bucketIndex] += (segmentEnd - cursor);
        cursor = segmentEnd;
    }
}

QString weekdayName(int bucketIndex)
{
    switch (qBound(1, bucketIndex + 1, 7)) {
    case 1:
        return QCoreApplication::translate("ServerStats", "Monday");
    case 2:
        return QCoreApplication::translate("ServerStats", "Tuesday");
    case 3:
        return QCoreApplication::translate("ServerStats", "Wednesday");
    case 4:
        return QCoreApplication::translate("ServerStats", "Thursday");
    case 5:
        return QCoreApplication::translate("ServerStats", "Friday");
    case 6:
        return QCoreApplication::translate("ServerStats", "Saturday");
    case 7:
        return QCoreApplication::translate("ServerStats", "Sunday");
    }
    return QCoreApplication::translate("ServerStats", "Unknown");
}

double percentileSeconds(const QVector<qint64> &sortedSeconds, double percentile)
{
    if (sortedSeconds.isEmpty())
        return 0.0;

    const double bounded = qBound(0.0, percentile, 1.0);
    const double position = bounded * static_cast<double>(sortedSeconds.size() - 1);
    const int lowerIndex = static_cast<int>(std::floor(position));
    const int upperIndex = static_cast<int>(std::ceil(position));
    if (lowerIndex == upperIndex)
        return static_cast<double>(sortedSeconds.at(lowerIndex));

    const double lower = static_cast<double>(sortedSeconds.at(lowerIndex));
    const double upper = static_cast<double>(sortedSeconds.at(upperIndex));
    const double weight = position - static_cast<double>(lowerIndex);
    return lower + (upper - lower) * weight;
}

} // namespace

ServerStatsPrivate::ServerStatsPrivate(ServerStats *qPtr, const QString &baseDatabasePath)
    : q(qPtr)
    , baseDbPath(baseDatabasePath)
    , enabled(false)
    , initialSnapshotCaptured(false)
    , activeReportCount(0)
{
    // do nothing
}

ServerStatsPrivate::~ServerStatsPrivate()
{
    closeDatabase();
}

qint64 ServerStatsPrivate::toEpochSeconds(const QDateTime &utcTime)
{
    if (!utcTime.isValid())
        return 0;
    return utcTime.toUTC().toSecsSinceEpoch();
}

QString ServerStatsPrivate::monthKey(const QDate &utcMonth) const
{
    return QStringLiteral("%1_%2")
            .arg(utcMonth.year(), 4, 10, QLatin1Char('0'))
            .arg(utcMonth.month(), 2, 10, QLatin1Char('0'));
}

QString ServerStatsPrivate::statsDirectory() const
{
    if (baseDbPath.trimmed().isEmpty())
        return QString();
    QFileInfo info(baseDbPath);
    const QString baseDir = info.absolutePath();
    return QDir(baseDir).filePath(QStringLiteral("stats"));
}

QString ServerStatsPrivate::statsFilePathForMonth(const QDate &utcMonth) const
{
    const QString dirPath = statsDirectory();
    if (dirPath.isEmpty())
        return QString();
    return QDir(dirPath).filePath(statsFileNameForMonth(utcMonth));
}

void ServerStatsPrivate::closeDatabase()
{
    if (db.isOpen())
        db.close();
    if (!connectionName.isEmpty()) {
        const QString name = connectionName;
        connectionName.clear();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }
    currentMonthKey.clear();
}

bool ServerStatsPrivate::ensureDatabaseForUtc(const QDateTime &utcTime)
{
    if (!utcTime.isValid())
        return false;

    const QDate utcDate = utcTime.toUTC().date();
    const QString targetMonthKey = monthKey(utcDate);
    if (!db.isOpen() || currentMonthKey != targetMonthKey) {
        closeDatabase();

        const QString dirPath = statsDirectory();
        if (dirPath.isEmpty())
            return false;

        QDir dir(dirPath);
        if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
            qCWarning(lcServerStats) << "Failed to create stats directory:" << dirPath;
            return false;
        }

        const QString dbPath = statsFilePathForMonth(utcDate);
        if (dbPath.isEmpty())
            return false;

        connectionName = QStringLiteral("booking_stats_%1_%2")
                                 .arg(reinterpret_cast<quintptr>(this), 0, 16, QLatin1Char('0'))
                                 .arg(targetMonthKey);
        db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(dbPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=%1").arg(kStatsBusyTimeoutMs));
        if (!db.open()) {
            qCWarning(lcServerStats).nospace() << "Failed to open stats database at " << dbPath
                                               << ": " << db.lastError().text();
            closeDatabase();
            return false;
        }

        QSqlQuery pragmaQuery(db);
        if (!pragmaQuery.exec(QStringLiteral("PRAGMA journal_mode=WAL"))
            || !pragmaQuery.exec(
                    QStringLiteral("PRAGMA busy_timeout=%1").arg(kStatsBusyTimeoutMs))) {
            qCWarning(lcServerStats).nospace()
                    << "Failed to configure stats database concurrency at " << dbPath << ": "
                    << pragmaQuery.lastError().text();
            closeDatabase();
            return false;
        }

        currentMonthKey = targetMonthKey;
    }

    return ensureSchema();
}

bool ServerStatsPrivate::ensureSchema()
{
    if (!ensureMetaTable())
        return false;

    const QString versionKey = QStringLiteral("schema_version");
    const QString stored = readMetaValue(versionKey);
    int current = 0;
    if (!stored.isEmpty()) {
        bool ok = false;
        current = stored.toInt(&ok);
        if (!ok) {
            qCWarning(lcServerStats) << "Unexpected stats schema version:" << stored;
            return false;
        }
        if (current > kSchemaVersion) {
            qCWarning(lcServerStats) << "Stats schema version is newer than supported:" << stored;
            return false;
        }
    }

    if (!ensureEventsTable() || !ensureUsersTable() || !ensureResourcesTable())
        return false;

    if (stored.isEmpty() || current < kSchemaVersion)
        return writeMetaValue(versionKey, QString::number(kSchemaVersion));
    return true;
}

bool ServerStatsPrivate::ensureMetaTable()
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS meta ("
                                   "key TEXT PRIMARY KEY,"
                                   "value TEXT NOT NULL)"))) {
        qCWarning(lcServerStats).nospace()
                << "Failed to create stats meta table: " << query.lastError().text();
        return false;
    }
    return true;
}

bool ServerStatsPrivate::ensureColumn(const QString &table,
                                      const QString &column,
                                      const QString &definition)
{
    if (tableHasColumn(db, table, column))
        return true;

    QSqlQuery query(db);
    if (!query.exec(
                QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3").arg(table, column, definition))) {
        qCWarning(lcServerStats).nospace() << "Failed to add stats column " << table << '.'
                                           << column << ": " << query.lastError().text();
        return false;
    }
    return true;
}

bool ServerStatsPrivate::ensureEventsTable()
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS events ("
                                   "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                   "event_at INTEGER NOT NULL,"
                                   "event_type TEXT NOT NULL,"
                                   "resource_uuid TEXT,"
                                   "resource_name TEXT,"
                                   "user_key TEXT,"
                                   "initiator_key TEXT,"
                                   "scheduled INTEGER NOT NULL DEFAULT 0,"
                                   "duration_minutes INTEGER NOT NULL DEFAULT 0,"
                                   "schedule_start INTEGER NOT NULL DEFAULT 0,"
                                   "schedule_end INTEGER NOT NULL DEFAULT 0,"
                                   "is_snapshot INTEGER NOT NULL DEFAULT 0,"
                                   "resource_groups TEXT,"
                                   "user_groups TEXT,"
                                   "initiator_groups TEXT,"
                                   "note TEXT)"))) {
        qCWarning(lcServerStats).nospace()
                << "Failed to create stats events table: " << query.lastError().text();
        return false;
    }

    if (!ensureColumn(QStringLiteral("events"),
                      QStringLiteral("resource_groups"),
                      QStringLiteral("TEXT"))
        || !ensureColumn(QStringLiteral("events"),
                         QStringLiteral("user_groups"),
                         QStringLiteral("TEXT"))
        || !ensureColumn(QStringLiteral("events"),
                         QStringLiteral("initiator_groups"),
                         QStringLiteral("TEXT"))) {
        return false;
    }

    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_stats_events_time "
                                   "ON events(event_at)"))) {
        qCWarning(lcServerStats).nospace()
                << "Failed to create stats events index: " << query.lastError().text();
        return false;
    }

    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_stats_events_resource "
                                   "ON events(resource_uuid, resource_name)"))) {
        qCWarning(lcServerStats).nospace()
                << "Failed to create stats events resource index: " << query.lastError().text();
        return false;
    }
    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_stats_events_uuid_time "
                                   "ON events(resource_uuid, event_at)"))
        || !query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_stats_events_name_time "
                                      "ON events(resource_name, event_at)"))) {
        qCWarning(lcServerStats).nospace()
                << "Failed to create stats resource lookup index: " << query.lastError().text();
        return false;
    }

    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_stats_events_user "
                                   "ON events(user_key)"))) {
        qCWarning(lcServerStats).nospace()
                << "Failed to create stats events user index: " << query.lastError().text();
        return false;
    }

    return true;
}

bool ServerStatsPrivate::ensureUsersTable()
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS users_history ("
                                   "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                   "event_at INTEGER NOT NULL,"
                                   "event_type TEXT NOT NULL,"
                                   "messenger_type TEXT NOT NULL,"
                                   "user_id TEXT NOT NULL,"
                                   "first_name TEXT,"
                                   "last_name TEXT,"
                                   "role INTEGER NOT NULL DEFAULT 0,"
                                   "locale TEXT,"
                                   "groups TEXT,"
                                   "actor_messenger TEXT,"
                                   "actor_user_id TEXT,"
                                   "is_snapshot INTEGER NOT NULL DEFAULT 0)"))) {
        qCWarning(lcServerStats).nospace()
                << "Failed to create stats users table: " << query.lastError().text();
        return false;
    }

    if (!ensureColumn(QStringLiteral("users_history"),
                      QStringLiteral("groups"),
                      QStringLiteral("TEXT"))) {
        return false;
    }

    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_stats_users_key "
                                   "ON users_history(messenger_type, user_id)"))) {
        qCWarning(lcServerStats).nospace()
                << "Failed to create stats users index: " << query.lastError().text();
        return false;
    }

    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_stats_users_time "
                                   "ON users_history(event_at)"))) {
        qCWarning(lcServerStats).nospace()
                << "Failed to create stats users time index: " << query.lastError().text();
        return false;
    }

    return true;
}

bool ServerStatsPrivate::ensureResourcesTable()
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS resources_history ("
                                   "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                   "event_at INTEGER NOT NULL,"
                                   "event_type TEXT NOT NULL,"
                                   "resource_uuid TEXT NOT NULL,"
                                   "name TEXT,"
                                   "description TEXT,"
                                   "start_message TEXT,"
                                   "groups TEXT,"
                                   "actor_messenger TEXT,"
                                   "actor_user_id TEXT,"
                                   "is_snapshot INTEGER NOT NULL DEFAULT 0)"))) {
        qCWarning(lcServerStats).nospace()
                << "Failed to create stats resources table: " << query.lastError().text();
        return false;
    }

    if (!ensureColumn(QStringLiteral("resources_history"),
                      QStringLiteral("groups"),
                      QStringLiteral("TEXT"))) {
        return false;
    }

    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_stats_resources_uuid "
                                   "ON resources_history(resource_uuid)"))) {
        qCWarning(lcServerStats).nospace()
                << "Failed to create stats resources index: " << query.lastError().text();
        return false;
    }

    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_stats_resources_time "
                                   "ON resources_history(event_at)"))) {
        qCWarning(lcServerStats).nospace()
                << "Failed to create stats resources time index: " << query.lastError().text();
        return false;
    }

    return true;
}

QString ServerStatsPrivate::readMetaValue(const QString &key) const
{
    if (!db.isOpen())
        return QString();

    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT value FROM meta WHERE key = :key"));
    query.bindValue(QStringLiteral(":key"), key);
    if (!query.exec()) {
        qCWarning(lcServerStats).nospace()
                << "Failed to read stats meta value: " << query.lastError().text();
        return QString();
    }
    if (!query.next())
        return QString();
    return query.value(0).toString();
}

bool ServerStatsPrivate::writeMetaValue(const QString &key, const QString &value) const
{
    if (!db.isOpen())
        return false;

    QSqlQuery query(db);
    query.prepare(QStringLiteral("UPDATE meta SET value = :value WHERE key = :key"));
    query.bindValue(QStringLiteral(":value"), value);
    query.bindValue(QStringLiteral(":key"), key);
    if (!query.exec()) {
        qCWarning(lcServerStats).nospace()
                << "Failed to update stats meta value: " << query.lastError().text();
        return false;
    }

    if (query.numRowsAffected() > 0)
        return true;

    QSqlQuery insert(db);
    insert.prepare(QStringLiteral("INSERT INTO meta(key, value) VALUES (:key, :value)"));
    insert.bindValue(QStringLiteral(":key"), key);
    insert.bindValue(QStringLiteral(":value"), value);
    if (!insert.exec()) {
        qCWarning(lcServerStats).nospace()
                << "Failed to insert stats meta value: " << insert.lastError().text();
        return false;
    }
    return true;
}

bool ServerStatsPrivate::insertEvent(const QDateTime &eventAtUtc,
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
                                     bool isSnapshot)
{
    if (!db.isOpen())
        return false;

    QSqlQuery query(db);
    query.prepare(QStringLiteral("INSERT INTO events("
                                 "event_at, event_type, resource_uuid, resource_name, "
                                 "user_key, initiator_key, scheduled, duration_minutes, "
                                 "schedule_start, schedule_end, is_snapshot, resource_groups, "
                                 "user_groups, initiator_groups, note) "
                                 "VALUES (:event_at, :event_type, :resource_uuid, :resource_name, "
                                 ":user_key, :initiator_key, :scheduled, :duration_minutes, "
                                 ":schedule_start, :schedule_end, :is_snapshot, :resource_groups, "
                                 ":user_groups, :initiator_groups, :note)"));

    const qint64 eventSeconds = toEpochSeconds(eventAtUtc);
    const qint64 startSeconds = toEpochSeconds(scheduleStartUtc);
    const qint64 endSeconds = toEpochSeconds(scheduleEndUtc);

    query.bindValue(QStringLiteral(":event_at"), eventSeconds);
    query.bindValue(QStringLiteral(":event_type"), eventType);
    query.bindValue(QStringLiteral(":resource_uuid"), resourceUuid);
    query.bindValue(QStringLiteral(":resource_name"), resourceName);
    query.bindValue(QStringLiteral(":user_key"), userKey);
    query.bindValue(QStringLiteral(":initiator_key"), initiatorKey);
    query.bindValue(QStringLiteral(":scheduled"), scheduled ? 1 : 0);
    query.bindValue(QStringLiteral(":duration_minutes"), durationMinutes);
    query.bindValue(QStringLiteral(":schedule_start"), startSeconds);
    query.bindValue(QStringLiteral(":schedule_end"), endSeconds);
    query.bindValue(QStringLiteral(":is_snapshot"), isSnapshot ? 1 : 0);
    query.bindValue(QStringLiteral(":resource_groups"),
                    serializeGroups(resourceGroupsByUuid.value(resourceUuid)));
    query.bindValue(QStringLiteral(":user_groups"),
                    serializeGroups(userGroupsByKey.value(userKey)));
    query.bindValue(QStringLiteral(":initiator_groups"),
                    serializeGroups(userGroupsByKey.value(initiatorKey)));
    query.bindValue(QStringLiteral(":note"), note);

    if (!query.exec()) {
        qCWarning(lcServerStats).nospace()
                << "Failed to insert stats event: " << query.lastError().text();
        return false;
    }

    return true;
}

bool ServerStatsPrivate::insertUserEvent(const QDateTime &eventAtUtc,
                                         const QString &eventType,
                                         const User &user,
                                         const MessengerUserKey &actor,
                                         bool isSnapshot)
{
    if (!db.isOpen())
        return false;

    const MessengerUserKey key = user.key();
    if (!key.isValid())
        return false;

    QSqlQuery query(db);
    query.prepare(QStringLiteral("INSERT INTO users_history("
                                 "event_at, event_type, messenger_type, user_id, "
                                 "first_name, last_name, role, locale, groups, "
                                 "actor_messenger, actor_user_id, is_snapshot) "
                                 "VALUES (:event_at, :event_type, :messenger, :user_id, "
                                 ":first_name, :last_name, :role, :locale, :groups, "
                                 ":actor_messenger, :actor_user_id, :is_snapshot)"));

    query.bindValue(QStringLiteral(":event_at"), toEpochSeconds(eventAtUtc));
    query.bindValue(QStringLiteral(":event_type"), eventType);
    query.bindValue(QStringLiteral(":messenger"), messengerTypeToString(key.messenger()));
    query.bindValue(QStringLiteral(":user_id"), key.userId());
    query.bindValue(QStringLiteral(":first_name"), user.firstName());
    query.bindValue(QStringLiteral(":last_name"), user.lastName());
    query.bindValue(QStringLiteral(":role"), static_cast<int>(user.role()));
    query.bindValue(QStringLiteral(":locale"), user.locale());
    query.bindValue(QStringLiteral(":groups"), serializeGroups(user.groups()));
    query.bindValue(QStringLiteral(":actor_messenger"),
                    actor.isValid() ? messengerTypeToString(actor.messenger()) : QString());
    query.bindValue(QStringLiteral(":actor_user_id"), actor.isValid() ? actor.userId() : QString());
    query.bindValue(QStringLiteral(":is_snapshot"), isSnapshot ? 1 : 0);

    if (!query.exec()) {
        qCWarning(lcServerStats).nospace()
                << "Failed to insert stats user event: " << query.lastError().text();
        return false;
    }
    return true;
}

bool ServerStatsPrivate::insertResourceEvent(const QDateTime &eventAtUtc,
                                             const QString &eventType,
                                             const Resource &resource,
                                             const MessengerUserKey &actor,
                                             bool isSnapshot)
{
    if (!db.isOpen())
        return false;

    if (!resource.isValid())
        return false;

    QSqlQuery query(db);
    query.prepare(QStringLiteral("INSERT INTO resources_history("
                                 "event_at, event_type, resource_uuid, "
                                 "name, description, start_message, groups, "
                                 "actor_messenger, actor_user_id, is_snapshot) "
                                 "VALUES (:event_at, :event_type, :resource_uuid, "
                                 ":name, :description, :start_message, :groups, "
                                 ":actor_messenger, :actor_user_id, :is_snapshot)"));

    query.bindValue(QStringLiteral(":event_at"), toEpochSeconds(eventAtUtc));
    query.bindValue(QStringLiteral(":event_type"), eventType);
    query.bindValue(QStringLiteral(":resource_uuid"), resource.uuidString());
    query.bindValue(QStringLiteral(":name"), resource.name());
    query.bindValue(QStringLiteral(":description"), resource.description());
    query.bindValue(QStringLiteral(":start_message"), resource.startMessage());
    query.bindValue(QStringLiteral(":groups"), serializeGroups(resource.groups()));
    query.bindValue(QStringLiteral(":actor_messenger"),
                    actor.isValid() ? messengerTypeToString(actor.messenger()) : QString());
    query.bindValue(QStringLiteral(":actor_user_id"), actor.isValid() ? actor.userId() : QString());
    query.bindValue(QStringLiteral(":is_snapshot"), isSnapshot ? 1 : 0);

    if (!query.exec()) {
        qCWarning(lcServerStats).nospace()
                << "Failed to insert stats resource event: " << query.lastError().text();
        return false;
    }
    return true;
}

ServerStats::ServerStats(const QString &baseDatabasePath, QObject *parent)
    : QObject(parent)
    , d(new ServerStatsPrivate(this, baseDatabasePath))
{
    // do nothing
}

ServerStats::~ServerStats()
{
    const QSet<QThread *> activeThreads = d->activeReportThreads;
    for (QThread *thread : activeThreads) {
        if (thread)
            thread->requestInterruption();
    }
    for (QThread *thread : activeThreads) {
        if (!thread || !thread->isRunning())
            continue;
        thread->quit();
        if (!thread->wait(kReportShutdownWaitMs)) {
            qCWarning(lcServerStats)
                    << "Statistics report thread did not stop before shutdown timeout.";
        }
    }
    d->activeReportThreads.clear();
    delete d;
}

void ServerStats::setEnabled(bool enabled)
{
    if (d->enabled == enabled)
        return;
    d->enabled = enabled;
    if (!enabled)
        d->initialSnapshotCaptured = false;
}

bool ServerStats::isEnabled() const
{
    return d->enabled;
}

QString ServerStats::baseDatabasePath() const
{
    return d->baseDbPath;
}

QString ServerStats::statsDirectory() const
{
    return d->statsDirectory();
}

QString ServerStats::statsFilePathForMonth(const QDate &utcMonth) const
{
    return d->statsFilePathForMonth(utcMonth);
}

void ServerStats::captureInitialState(const UserModel *users,
                                      const ResourceModel *resources,
                                      const ResourceQueueModel *queue)
{
    if (!d->enabled)
        return;
    if (!users || !resources || !queue)
        return;

    for (const User &user : users->items()) {
        if (user.isValid())
            d->userGroupsByKey.insert(user.key().toString(), user.groups());
    }
    for (const Resource &resource : resources->items()) {
        if (resource.isValid())
            d->resourceGroupsByUuid.insert(resource.uuidString(), resource.groups());
    }
    if (d->initialSnapshotCaptured)
        return;

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    if (!d->ensureDatabaseForUtc(nowUtc))
        return;

    const QString snapshotKey = QStringLiteral("initial_snapshot_at");
    if (!d->db.transaction()) {
        qCWarning(lcServerStats) << "Failed to start statistics snapshot transaction:"
                                 << d->db.lastError().text();
        return;
    }

    bool snapshotSaved = true;

    for (const User &user : users->items()) {
        if (!user.isValid())
            continue;
        if (!d->insertUserEvent(nowUtc,
                                QStringLiteral("snapshot"),
                                user,
                                MessengerUserKey(),
                                true)) {
            snapshotSaved = false;
        }
    }

    for (const Resource &resource : resources->items()) {
        if (!resource.isValid())
            continue;
        if (!d->insertResourceEvent(nowUtc,
                                    QStringLiteral("snapshot"),
                                    resource,
                                    MessengerUserKey(),
                                    true)) {
            snapshotSaved = false;
        }
    }

    QHash<QString, QString> resourceNameCache;
    const auto resourceNameForUuid = [&](const QString &uuid) -> QString {
        const QHash<QString, QString>::const_iterator it = resourceNameCache.constFind(uuid);
        if (it != resourceNameCache.cend())
            return it.value();
        const Resource resource = resources->resourceByUuid(uuid);
        const QString name = resource.isValid() ? resource.name() : QString();
        resourceNameCache.insert(uuid, name);
        return name;
    };

    const QList<ResourceQueueEntry> entries = queue->items();
    for (const ResourceQueueEntry &entry : entries) {
        if (!entry.isValid())
            continue;
        const QString resourceUuid = entry.resourceUuid();
        const QString resourceName = resourceNameForUuid(resourceUuid);
        const QString userKey = entry.user().toString();
        const bool active = entry.isActiveAtUtc(nowUtc);
        if (active) {
            if (!d->insertEvent(nowUtc,
                                QStringLiteral("occupation_snapshot"),
                                resourceUuid,
                                resourceName,
                                userKey,
                                QString(),
                                false,
                                0,
                                QDateTime(),
                                QDateTime(),
                                QString(),
                                true)) {
                snapshotSaved = false;
            }
        } else if (entry.startAtUtc().isValid() && entry.startAtUtc().toUTC() > nowUtc) {
            if (!d->insertEvent(nowUtc,
                                QStringLiteral("schedule_snapshot"),
                                resourceUuid,
                                resourceName,
                                userKey,
                                QString(),
                                true,
                                0,
                                entry.startAtUtc(),
                                entry.endAtUtc(),
                                QString(),
                                true)) {
                snapshotSaved = false;
            }
        }
    }

    if (snapshotSaved) {
        snapshotSaved = d->writeMetaValue(snapshotKey, nowUtc.toUTC().toString(Qt::ISODateWithMs));
    }
    if (!snapshotSaved || !d->db.commit()) {
        if (!snapshotSaved) {
            qCWarning(lcServerStats) << "Failed to capture initial statistics snapshot.";
        } else {
            qCWarning(lcServerStats)
                    << "Failed to commit statistics snapshot:" << d->db.lastError().text();
        }
        if (!d->db.rollback()) {
            qCWarning(lcServerStats)
                    << "Failed to rollback statistics snapshot:" << d->db.lastError().text();
        }
        return;
    }

    d->initialSnapshotCaptured = true;
}

void ServerStats::recordUserEvent(const User &user,
                                  const MessengerUserKey &actor,
                                  const QString &eventType,
                                  bool isSnapshot)
{
    if (!d->enabled)
        return;
    if (!user.isValid())
        return;

    d->userGroupsByKey.insert(user.key().toString(), user.groups());
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    if (!d->ensureDatabaseForUtc(nowUtc))
        return;

    d->insertUserEvent(nowUtc, eventType, user, actor, isSnapshot);
}

void ServerStats::recordResourceEvent(const Resource &resource,
                                      const MessengerUserKey &actor,
                                      const QString &eventType,
                                      bool isSnapshot)
{
    if (!d->enabled)
        return;
    if (!resource.isValid())
        return;

    d->resourceGroupsByUuid.insert(resource.uuidString(), resource.groups());
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    if (!d->ensureDatabaseForUtc(nowUtc))
        return;

    d->insertResourceEvent(nowUtc, eventType, resource, actor, isSnapshot);
}

void ServerStats::recordOccupationEvent(const QString &resourceName,
                                        const QString &resourceUuid,
                                        const MessengerUserKey &user,
                                        const MessengerUserKey &initiator,
                                        const QString &eventType,
                                        bool scheduled,
                                        int durationMinutes,
                                        const QDateTime &eventAtUtc,
                                        const QString &note)
{
    if (!d->enabled)
        return;
    if (!eventAtUtc.isValid())
        return;

    if (!d->ensureDatabaseForUtc(eventAtUtc))
        return;

    d->insertEvent(eventAtUtc,
                   eventType,
                   resourceUuid,
                   resourceName,
                   user.toString(),
                   initiator.toString(),
                   scheduled,
                   durationMinutes,
                   QDateTime(),
                   QDateTime(),
                   note,
                   false);
}

void ServerStats::recordScheduleEvent(const QString &resourceName,
                                      const QString &resourceUuid,
                                      const MessengerUserKey &user,
                                      const MessengerUserKey &initiator,
                                      const QString &eventType,
                                      const QDateTime &scheduleStartUtc,
                                      const QDateTime &scheduleEndUtc,
                                      const QDateTime &eventAtUtc,
                                      const QString &note,
                                      bool isSnapshot)
{
    if (!d->enabled)
        return;
    if (!eventAtUtc.isValid())
        return;

    if (!d->ensureDatabaseForUtc(eventAtUtc))
        return;

    d->insertEvent(eventAtUtc,
                   eventType,
                   resourceUuid,
                   resourceName,
                   user.toString(),
                   initiator.toString(),
                   true,
                   0,
                   scheduleStartUtc,
                   scheduleEndUtc,
                   note,
                   isSnapshot);
}

void ServerStats::configureActorScope(ReportRequest *request, const User &actor, bool unrestricted)
{
    if (!request)
        return;

    request->scopeRestricted = !unrestricted;
    request->administrativeScope = !unrestricted && actor.isAdmin();
    request->authorizedGroupRoots.clear();
    request->allowedResourceUuids.clear();
    request->allowedUsers.clear();

    if (unrestricted)
        return;
    if (!actor.isValid())
        return;

    const QSet<Group> roots = actor.isAdmin() ? actor.administeredGroupRoots() : actor.groups();
    for (const Group &group : roots)
        request->authorizedGroupRoots.append(QString(group));
    request->authorizedGroupRoots.sort(Qt::CaseSensitive);

    if (!actor.isAdmin() && actor.key().isValid())
        request->allowedUsers.append(actor.key());
}

bool ServerStats::actorScopeStillValid(const ReportRequest &request,
                                       const User &actor,
                                       bool unrestricted,
                                       const ResourceModel *resources,
                                       const UserModel *users)
{
    if (!request.scopeRestricted)
        return unrestricted;
    if (unrestricted || !actor.isValid() || !resources || !users)
        return false;

    ReportRequest currentScope;
    configureActorScope(&currentScope, actor, false);
    if (currentScope.administrativeScope != request.administrativeScope)
        return false;
    QSet<QString> currentRoots;
    for (const QString &root : currentScope.authorizedGroupRoots)
        currentRoots.insert(root);
    QSet<QString> requestedRoots;
    for (const QString &root : request.authorizedGroupRoots)
        requestedRoots.insert(root);
    if (currentRoots != requestedRoots) {
        return false;
    }

    for (const QString &uuid : request.allowedResourceUuids) {
        const Resource resource = resources->resourceByUuid(uuid);
        if (!resource.isValid())
            return false;
        if (actor.isAdmin()) {
            if (!actor.canAdministerGroups(resource.groups()))
                return false;
        } else {
            if (resource.status() == Resource::Status::Off)
                return false;
            bool visible = false;
            for (const Group &group : resource.groups()) {
                if (actor.hasAccessToGroup(group)) {
                    visible = true;
                    break;
                }
            }
            if (!visible)
                return false;
        }
    }

    for (const MessengerUserKey &key : request.allowedUsers) {
        const User target = users->userByKey(key);
        if (!target.isValid())
            return false;
        if (actor.isAdmin()) {
            if (!actor.canAdministerGroups(target.groups()))
                return false;
        } else if (target.key() != actor.key()) {
            return false;
        }
    }

    return true;
}

ServerStats::ReportResult ServerStats::generateReport(const QString &baseDatabasePath,
                                                      const ReportRequest &request,
                                                      const QString &outputDirectory,
                                                      const CancellationCheck &cancellationCheck)
{
    ReportResult result;
    result.format = request.format;
    QString csvPath;
    QFile csvFile;

    const auto cancellationRequested = [&cancellationCheck]() {
        return cancellationCheck && cancellationCheck();
    };
    const auto cancelledReport = [&result, &csvPath, &csvFile]() {
        if (csvFile.isOpen())
            csvFile.close();
        if (!csvPath.isEmpty())
            QFile::remove(csvPath);
        result.ok = false;
        result.errorText = ServerStats::tr("Report generation was cancelled.");
        result.tableText.clear();
        result.filePath.clear();
        return result;
    };

    if (!request.startUtc.isValid() || !request.endUtc.isValid()) {
        result.errorText = tr("Invalid time range.");
        return result;
    }
    if (request.startUtc.toUTC() > request.endUtc.toUTC()) {
        result.errorText = tr("Invalid time range.");
        return result;
    }

    const QTimeZone reportTimeZone = effectiveReportTimeZone(request.timeZone);

    const QFileInfo baseInfo(baseDatabasePath);
    const QString baseDir = baseInfo.absolutePath();
    const QString statsDir = QDir(baseDir).filePath(QStringLiteral("stats"));
    if (statsDir.isEmpty()) {
        result.errorText = tr("Stats directory is not available.");
        return result;
    }
    QString reportOutputDir = outputDirectory.trimmed();
    if (reportOutputDir.isEmpty())
        reportOutputDir = statsDir;

    const qint64 startSeconds = ServerStatsPrivate::toEpochSeconds(request.startUtc);
    const qint64 requestedEndSeconds = ServerStatsPrivate::toEpochSeconds(request.endUtc);
    qint64 reportEndSeconds = requestedEndSeconds;
    if (reportEndSeconds < startSeconds)
        reportEndSeconds = startSeconds;

    const QDate reportEndDate =
            QDateTime::fromSecsSinceEpoch(reportEndSeconds, QTimeZone::utc()).date();
    const QDate startMonth(request.startUtc.toUTC().date().year(),
                           request.startUtc.toUTC().date().month(),
                           1);
    const QDate endMonth(reportEndDate.year(), reportEndDate.month(), 1);

    QDate queryStartMonth = startMonth.addMonths(-1);
    const QDate earliestMonth = earliestStatsMonth(statsDir);
    if (earliestMonth.isValid() && earliestMonth < queryStartMonth)
        queryStartMonth = earliestMonth;

    QSet<QString> requestedResourceUuids;
    for (const QString &uuid : request.resourceUuids) {
        const QString normalized = uuid.trimmed();
        if (!normalized.isEmpty())
            requestedResourceUuids.insert(normalized);
    }

    QSet<QString> requestedResourceNames;
    for (const QString &name : request.resourceNames) {
        const QString normalized = name.trimmed();
        if (!normalized.isEmpty())
            requestedResourceNames.insert(normalized);
    }

    QSet<QString> requestedUserKeys;
    for (const MessengerUserKey &userKey : request.users) {
        const QString normalized = userKey.toString().trimmed();
        if (!normalized.isEmpty())
            requestedUserKeys.insert(normalized);
    }

    const bool requiresUserDisplayNames =
            request.reportKind == ReportRequest::ReportKind::UserActivitySummary
            || request.reportKind == ReportRequest::ReportKind::EventsLog;

    QList<ReportEvent> events;
    QHash<QString, ReportEvent> openOccupationsAtRangeStart;
    QHash<QString, UserNameSnapshot> userNamesByKey;
    QDate current = queryStartMonth;
    int connectionIndex = 0;
    while (current.isValid() && current <= endMonth) {
        if (cancellationRequested())
            return cancelledReport();

        const QString filePath = QDir(statsDir).filePath(statsFileNameForMonth(current));
        if (!QFile::exists(filePath)) {
            current = current.addMonths(1);
            continue;
        }

        const QString connectionName =
                QStringLiteral("stats_report_%1_%2")
                        .arg(connectionIndex++)
                        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(filePath);
        db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=%1").arg(kStatsBusyTimeoutMs));
        if (!db.open()) {
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(connectionName);
            result.errorText = tr("Failed to read statistics data.");
            return result;
        }

        QString where;
        QHash<QString, QVariant> bindings;
        where = QStringLiteral("event_at <= :end");
        bindings.insert(QStringLiteral(":end"), reportEndSeconds);

        QStringList resourceConditions;
        if (!request.resourceUuids.isEmpty()) {
            QStringList placeholders;
            int i = 0;
            for (const QString &uuid : request.resourceUuids) {
                const QString key = QStringLiteral(":ru%1").arg(i++);
                placeholders << key;
                bindings.insert(key, uuid);
            }
            resourceConditions << QStringLiteral("resource_uuid IN (%1)")
                                          .arg(placeholders.join(QStringLiteral(", ")));
        }
        if (!request.resourceNames.isEmpty()) {
            QStringList placeholders;
            int i = 0;
            for (const QString &name : request.resourceNames) {
                const QString key = QStringLiteral(":rn%1").arg(i++);
                placeholders << key;
                bindings.insert(key, name);
            }
            const QString namesCondition = QStringLiteral("resource_name IN (%1)")
                                                   .arg(placeholders.join(QStringLiteral(", ")));
            if (request.resourceUuids.isEmpty()) {
                resourceConditions << namesCondition;
            } else {
                resourceConditions
                        << QStringLiteral("((resource_uuid IS NULL OR resource_uuid = '') AND %1)")
                                   .arg(namesCondition);
            }
        }
        if (!resourceConditions.isEmpty()) {
            where += QStringLiteral(" AND (%1)")
                             .arg(resourceConditions.join(QStringLiteral(" OR ")));
        }

        if (!request.users.isEmpty()) {
            QStringList placeholders;
            int i = 0;
            for (const MessengerUserKey &userKey : request.users) {
                const QString key = QStringLiteral(":u%1").arg(i++);
                placeholders << key;
                bindings.insert(key, userKey.toString());
            }
            where += QStringLiteral(" AND user_key IN (%1)")
                             .arg(placeholders.join(QStringLiteral(", ")));
        }

        const bool hasScopeColumns =
                tableHasColumn(db, QStringLiteral("events"), QStringLiteral("resource_groups"))
                && tableHasColumn(db, QStringLiteral("events"), QStringLiteral("user_groups"))
                && tableHasColumn(db, QStringLiteral("events"), QStringLiteral("initiator_groups"));
        const QString scopeColumns =
                hasScopeColumns
                        ? QStringLiteral(", resource_groups, user_groups, initiator_groups ")
                        : QStringLiteral(", NULL, NULL, NULL ");

        QSqlQuery query(db);
        query.setForwardOnly(true);
        const QString sql =
                QStringLiteral(
                        "SELECT id, event_at, event_type, resource_uuid, resource_name, user_key, "
                        "initiator_key, scheduled, duration_minutes, schedule_start, schedule_end, "
                        "is_snapshot, note %1"
                        "FROM events WHERE %2 ORDER BY event_at ASC, id ASC")
                        .arg(scopeColumns, where);
        if (!query.prepare(sql)) {
            query = QSqlQuery();
            db.close();
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(connectionName);
            result.errorText = tr("Failed to read statistics data.");
            return result;
        }
        for (auto it = bindings.cbegin(); it != bindings.cend(); ++it)
            query.bindValue(it.key(), it.value());

        if (!query.exec()) {
            query = QSqlQuery();
            db.close();
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(connectionName);
            result.errorText = tr("Failed to read statistics data.");
            return result;
        }

        bool readCancelled = false;
        bool eventLimitExceeded = false;
        while (query.next()) {
            if (cancellationRequested()) {
                readCancelled = true;
                break;
            }

            ReportEvent event;
            event.rowId = query.value(0).toLongLong();
            event.eventSeconds = query.value(1).toLongLong();
            event.eventType = query.value(2).toString();
            event.resourceUuid = query.value(3).toString();
            event.resourceName = query.value(4).toString();
            event.userKey = query.value(5).toString();
            event.initiatorKey = query.value(6).toString();
            event.scheduled = query.value(7).toInt() != 0;
            event.durationMinutes = query.value(8).toInt();
            event.scheduleStartSeconds = query.value(9).toLongLong();
            event.scheduleEndSeconds = query.value(10).toLongLong();
            event.isSnapshot = query.value(11).toInt() != 0;
            event.note = query.value(12).toString();
            event.resourceGroups = query.value(13).toString();
            event.userGroups = query.value(14).toString();
            event.initiatorGroups = query.value(15).toString();
            if (!eventAllowedByScope(request, &event))
                continue;

            if (event.eventSeconds < startSeconds) {
                const QString resourceKey =
                        normalizeResourceKey(event.resourceUuid, event.resourceName);
                if (resourceKey.isEmpty())
                    continue;
                if (event.eventType == QStringLiteral("occupation_start")
                    || event.eventType == QStringLiteral("occupation_snapshot")) {
                    openOccupationsAtRangeStart.insert(resourceKey, event);
                } else if (event.eventType == QStringLiteral("occupation_end")) {
                    openOccupationsAtRangeStart.remove(resourceKey);
                }
                continue;
            }

            if (events.size() >= kMaxReportEvents) {
                eventLimitExceeded = true;
                break;
            }
            events.append(event);
        }

        if (readCancelled || eventLimitExceeded) {
            query = QSqlQuery();
            db.close();
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(connectionName);
            if (readCancelled)
                return cancelledReport();
            result.errorText = tr("The requested report range contains too many events.");
            return result;
        }

        bool userHistoryReadFailed = false;
        if (requiresUserDisplayNames) {
            QSet<Group> authorizedRoots;
            if (request.scopeRestricted) {
                for (const QString &path : request.authorizedGroupRoots)
                    authorizedRoots.insert(Group(path));
            }

            QSqlQuery usersQuery(db);
            usersQuery.setForwardOnly(true);
            QString usersWhere = QStringLiteral("event_at <= :end");
            QHash<QString, QVariant> usersBindings;
            usersBindings.insert(QStringLiteral(":end"), reportEndSeconds);

            if (!request.users.isEmpty()) {
                QStringList placeholders;
                int i = 0;
                for (const MessengerUserKey &userKey : request.users) {
                    const QString key = userKey.toString().trimmed();
                    if (key.isEmpty())
                        continue;
                    const QString placeholder = QStringLiteral(":uk%1").arg(i++);
                    placeholders << placeholder;
                    usersBindings.insert(placeholder, key);
                }
                if (!placeholders.isEmpty()) {
                    usersWhere += QStringLiteral(" AND (messenger_type || ':' || user_id) IN (%1)")
                                          .arg(placeholders.join(QStringLiteral(", ")));
                }
            }

            const bool hasUserScopeColumn =
                    tableHasColumn(db, QStringLiteral("users_history"), QStringLiteral("groups"));
            const QString userScopeColumn =
                    hasUserScopeColumn ? QStringLiteral(", groups ") : QStringLiteral(", NULL ");
            const QString usersSql =
                    QStringLiteral("SELECT event_at, messenger_type, user_id, first_name, "
                                   "last_name %1"
                                   "FROM users_history WHERE %2 ORDER BY event_at ASC, id ASC")
                            .arg(userScopeColumn, usersWhere);
            if (!usersQuery.prepare(usersSql)) {
                userHistoryReadFailed = true;
            } else {
                for (auto it = usersBindings.cbegin(); it != usersBindings.cend(); ++it)
                    usersQuery.bindValue(it.key(), it.value());
                if (!usersQuery.exec()) {
                    userHistoryReadFailed = true;
                } else {
                    while (usersQuery.next()) {
                        if (cancellationRequested()) {
                            readCancelled = true;
                            break;
                        }
                        const qint64 eventAtSeconds = usersQuery.value(0).toLongLong();
                        const QString messengerType = usersQuery.value(1).toString().trimmed();
                        const QString userId = usersQuery.value(2).toString().trimmed();
                        if (messengerType.isEmpty() || userId.isEmpty())
                            continue;

                        const QString userKey = QStringLiteral("%1:%2").arg(messengerType, userId);
                        if (request.scopeRestricted
                            && !userKeyAllowedByScope(request,
                                                      userKey,
                                                      usersQuery.value(5).toString(),
                                                      authorizedRoots)) {
                            continue;
                        }
                        const QHash<QString, UserNameSnapshot>::const_iterator snapshotIt =
                                userNamesByKey.constFind(userKey);
                        if (snapshotIt != userNamesByKey.cend()
                            && eventAtSeconds < snapshotIt->eventSeconds) {
                            continue;
                        }

                        UserNameSnapshot snapshot;
                        snapshot.eventSeconds = eventAtSeconds;
                        snapshot.firstName = usersQuery.value(3).toString();
                        snapshot.lastName = usersQuery.value(4).toString();
                        userNamesByKey.insert(userKey, snapshot);
                    }
                }
            }
        }

        query = QSqlQuery();
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
        if (readCancelled)
            return cancelledReport();
        if (userHistoryReadFailed) {
            result.errorText = tr("Failed to read statistics data.");
            return result;
        }
        current = current.addMonths(1);
    }

    qint64 syntheticRowId = std::numeric_limits<qint64>::min();
    for (ReportEvent event : openOccupationsAtRangeStart) {
        if (events.size() >= kMaxReportEvents) {
            result.errorText = tr("The requested report range contains too many events.");
            return result;
        }
        event.rowId = syntheticRowId++;
        event.eventSeconds = startSeconds;
        event.eventType = QStringLiteral("occupation_snapshot");
        event.isSnapshot = true;
        events.append(event);
    }
    openOccupationsAtRangeStart.clear();

    if (cancellationRequested())
        return cancelledReport();
    std::sort(events.begin(), events.end(), [](const ReportEvent &lhs, const ReportEvent &rhs) {
        if (lhs.eventSeconds != rhs.eventSeconds)
            return lhs.eventSeconds < rhs.eventSeconds;
        return lhs.rowId < rhs.rowId;
    });

    QList<ActiveQueueOccupation> activeQueueOccupations;
    QSet<QString> allowedResourceUuids;
    for (const QString &uuid : request.allowedResourceUuids) {
        const QString normalized = uuid.trimmed();
        if (!normalized.isEmpty())
            allowedResourceUuids.insert(normalized);
    }
    QSet<QString> allowedUserKeys;
    for (const MessengerUserKey &key : request.allowedUsers) {
        const QString normalized = key.toString().trimmed();
        if (!normalized.isEmpty())
            allowedUserKeys.insert(normalized);
    }

    bool liveQueueReadCancelled = false;
    if (!baseDatabasePath.trimmed().isEmpty() && QFile::exists(baseDatabasePath)) {
        const QString connectionName =
                QStringLiteral("stats_live_queue_%1")
                        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(baseDatabasePath);
        db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=%1").arg(kStatsBusyTimeoutMs));
        if (db.open()) {
            QSqlQuery query(db);
            query.setForwardOnly(true);
            query.prepare(QStringLiteral("SELECT q.resource_uuid, r.name, q.messenger_type, "
                                         "q.user_id, q.start_at "
                                         "FROM resource_queue q "
                                         "LEFT JOIN resources r ON r.uuid = q.resource_uuid "
                                         "WHERE q.start_at > 0 AND q.start_at <= :at "
                                         "AND (q.end_at = 0 OR q.end_at > :at) "
                                         "ORDER BY q.start_at ASC"));
            query.bindValue(QStringLiteral(":at"), reportEndSeconds);
            if (query.exec()) {
                const bool hasResourceFilters =
                        !requestedResourceUuids.isEmpty() || !requestedResourceNames.isEmpty();
                const bool hasUserFilters = !requestedUserKeys.isEmpty();
                while (query.next()) {
                    if (cancellationRequested()) {
                        liveQueueReadCancelled = true;
                        break;
                    }
                    const QString resourceUuid = query.value(0).toString().trimmed();
                    const QString resourceName = query.value(1).toString().trimmed();
                    const QString messengerType = query.value(2).toString().trimmed();
                    const QString userId = query.value(3).toString().trimmed();
                    const qint64 startAtSeconds = query.value(4).toLongLong();
                    if (resourceUuid.isEmpty() || messengerType.isEmpty() || userId.isEmpty()
                        || startAtSeconds <= 0) {
                        continue;
                    }

                    if (hasResourceFilters && !requestedResourceUuids.contains(resourceUuid)
                        && !requestedResourceNames.contains(resourceName)) {
                        continue;
                    }

                    const QString userKey =
                            QStringLiteral("%1:%2").arg(messengerType, userId).trimmed();
                    if (hasUserFilters && !requestedUserKeys.contains(userKey))
                        continue;
                    if (request.scopeRestricted
                        && (!allowedResourceUuids.contains(resourceUuid)
                            || !allowedUserKeys.contains(userKey))) {
                        continue;
                    }

                    ActiveQueueOccupation occupation;
                    occupation.resourceUuid = resourceUuid;
                    occupation.resourceName = resourceName;
                    occupation.userKey = userKey;
                    occupation.startSeconds = startAtSeconds;
                    activeQueueOccupations.append(occupation);
                }
            }
        }
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    }
    if (liveQueueReadCancelled)
        return cancelledReport();

    QTextStream csvStream;
    QStringList tableLines;
    QStringList tableHeaderColumns;
    QList<QStringList> tableRows;
    QVector<int> tableColumnWidths;
    if (request.format == ReportRequest::OutputFormat::Csv) {
        QDir dir(reportOutputDir);
        if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
            result.errorText = tr("Failed to create stats directory.");
            return result;
        }
        const QString startStamp = request.startUtc.toUTC()
                                           .toTimeZone(reportTimeZone)
                                           .toString(QStringLiteral("yyyyMMdd_HHmmss"));
        const QString endStamp = request.endUtc.toUTC()
                                         .toTimeZone(reportTimeZone)
                                         .toString(QStringLiteral("yyyyMMdd_HHmmss"));
        const QString generatedStamp = QDateTime::currentDateTimeUtc()
                                               .toTimeZone(reportTimeZone)
                                               .toString(QStringLiteral("yyyyMMdd_HHmmss"));
        const QString reportToken = reportKindSuffix(request.reportKind);
        const QString uniqueSuffix = QUuid::createUuid().toString(QUuid::WithoutBraces);
        csvPath = dir.filePath(
                QStringLiteral("stats_report_%1_%2_to_%3_generated_%4_%5.csv")
                        .arg(reportToken, startStamp, endStamp, generatedStamp, uniqueSuffix));
        csvFile.setFileName(csvPath);
        if (!csvFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            result.errorText = tr("Failed to create CSV report file.");
            return result;
        }
        csvStream.setDevice(&csvFile);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        csvStream.setEncoding(QStringConverter::Utf8);
#else
        csvStream.setCodec("UTF-8");
#endif
    }

    auto writeCsvRow = [&](const QStringList &columns) {
        QStringList escaped;
        escaped.reserve(columns.size());
        for (const QString &column : columns)
            escaped << csvEscape(column);
        csvStream << escaped.join(QStringLiteral(",")) << '\n';
    };

    auto ensureTableWidthSize = [&](int columnCount) {
        if (columnCount <= tableColumnWidths.size())
            return;

        const int oldSize = tableColumnWidths.size();
        tableColumnWidths.resize(columnCount);
        for (int i = oldSize; i < columnCount; ++i)
            tableColumnWidths[i] = 0;
    };

    auto extendTableWidths = [&](const QStringList &columns) {
        ensureTableWidthSize(columns.size());
        for (int i = 0; i < columns.size(); ++i) {
            const int width = qMax(3, columns.at(i).size());
            if (width > tableColumnWidths[i])
                tableColumnWidths[i] = width;
        }
    };

    auto tableLineForColumns = [&](const QStringList &columns) -> QString {
        const int columnCount = tableColumnWidths.size();
        QStringList padded;
        padded.reserve(columnCount);
        for (int i = 0; i < columnCount; ++i) {
            const QString value = (i < columns.size()) ? columns.at(i) : QString();
            padded << value.leftJustified(tableColumnWidths.at(i), QLatin1Char(' '));
        }
        return padded.join(QStringLiteral(" | "));
    };

    auto tableSeparatorLine = [&]() -> QString {
        QStringList separators;
        separators.reserve(tableColumnWidths.size());
        for (int width : std::as_const(tableColumnWidths))
            separators << QString(qMax(3, width), QLatin1Char('-'));
        return separators.join(QStringLiteral("-+-"));
    };

    auto writeHeaderRow = [&](const QStringList &columns) {
        if (request.format == ReportRequest::OutputFormat::Csv) {
            writeCsvRow(columns);
            return;
        }
        tableHeaderColumns = columns;
        tableRows.clear();
        tableColumnWidths.clear();
        extendTableWidths(columns);
    };

    auto writeDataRow = [&](const QStringList &columns) {
        result.totalRows += 1;
        if (request.format == ReportRequest::OutputFormat::Csv) {
            writeCsvRow(columns);
            return;
        }

        if (result.rowCount < kMaxTableRows) {
            tableRows.append(columns);
            extendTableWidths(columns);
            result.rowCount += 1;
        } else {
            result.truncated = true;
        }
    };

    {
        struct OpenSession
        {
            QString resourceKey;
            QString resourceName;
            QString resourceUuid;
            QString userKey;
            qint64 startSeconds = 0;
        };

        const qint64 rangeEndSecondsExclusive =
                (reportEndSeconds <= startSeconds) ? (startSeconds + 1) : reportEndSeconds;

        QList<SessionSpan> sessions;
        QHash<QString, OpenSession> openByResource;
        QHash<QString, QString> resourceNameByKey;

        auto appendSession = [&sessions](const OpenSession &open, qint64 endValueSeconds) {
            if (open.resourceKey.trimmed().isEmpty())
                return;
            if (endValueSeconds <= open.startSeconds)
                return;
            SessionSpan span;
            span.resourceKey = open.resourceKey;
            span.resourceName = open.resourceName;
            span.resourceUuid = open.resourceUuid;
            span.userKey = open.userKey;
            span.startSeconds = open.startSeconds;
            span.endSeconds = endValueSeconds;
            sessions.append(span);
        };

        for (const ReportEvent &event : std::as_const(events)) {
            if (cancellationRequested())
                return cancelledReport();
            const QString resourceKey =
                    normalizeResourceKey(event.resourceUuid, event.resourceName);
            if (!resourceKey.isEmpty()) {
                const QString displayName =
                        displayResourceName(event.resourceName, event.resourceUuid);
                if (!displayName.trimmed().isEmpty())
                    resourceNameByKey.insert(resourceKey, displayName.trimmed());
            }

            const bool occupationSnapshot =
                    event.eventType == QStringLiteral("occupation_snapshot");
            const bool occupationStart =
                    event.eventType == QStringLiteral("occupation_start") || occupationSnapshot;
            if (occupationStart) {
                if (resourceKey.isEmpty())
                    continue;
                if (occupationSnapshot && openByResource.contains(resourceKey)
                    && openByResource.value(resourceKey).userKey == event.userKey) {
                    continue;
                }
                if (openByResource.contains(resourceKey))
                    appendSession(openByResource.value(resourceKey), event.eventSeconds);

                OpenSession open;
                open.resourceKey = resourceKey;
                open.resourceName = event.resourceName;
                open.resourceUuid = event.resourceUuid;
                open.userKey = event.userKey;
                open.startSeconds = event.eventSeconds;
                openByResource.insert(resourceKey, open);
            } else if (event.eventType == QStringLiteral("occupation_end")) {
                if (resourceKey.isEmpty())
                    continue;
                if (!openByResource.contains(resourceKey))
                    continue;
                appendSession(openByResource.take(resourceKey), event.eventSeconds);
            }
        }

        const QList<OpenSession> dangling = openByResource.values();
        for (const OpenSession &open : dangling)
            appendSession(open, rangeEndSecondsExclusive);

        QSet<QString> coveredResourceKeysAtRangeEnd;
        for (const SessionSpan &span : std::as_const(sessions)) {
            if (cancellationRequested())
                return cancelledReport();
            const QString key = span.resourceKey.trimmed();
            if (key.isEmpty())
                continue;
            if (span.startSeconds >= rangeEndSecondsExclusive)
                continue;
            if (span.endSeconds < rangeEndSecondsExclusive)
                continue;
            coveredResourceKeysAtRangeEnd.insert(key);
        }

        for (const ActiveQueueOccupation &occupation : std::as_const(activeQueueOccupations)) {
            const QString resourceKey =
                    normalizeResourceKey(occupation.resourceUuid, occupation.resourceName);
            if (resourceKey.isEmpty())
                continue;
            if (coveredResourceKeysAtRangeEnd.contains(resourceKey))
                continue;
            if (occupation.startSeconds >= rangeEndSecondsExclusive)
                continue;

            SessionSpan span;
            span.resourceKey = resourceKey;
            span.resourceName = occupation.resourceName;
            span.resourceUuid = occupation.resourceUuid;
            span.userKey = occupation.userKey;
            span.startSeconds = occupation.startSeconds;
            span.endSeconds = rangeEndSecondsExclusive;
            sessions.append(span);
            coveredResourceKeysAtRangeEnd.insert(resourceKey);

            if (!occupation.resourceName.trimmed().isEmpty())
                resourceNameByKey.insert(resourceKey, occupation.resourceName.trimmed());
        }

        QHash<QString, QString> selectedResourceNameByKey;
        for (const QString &name : request.resourceNames) {
            const QString key = normalizeResourceKey(QString(), name);
            if (!key.isEmpty())
                selectedResourceNameByKey.insert(key, name.trimmed());
        }
        for (const QString &uuid : request.resourceUuids) {
            const QString key = normalizeResourceKey(uuid, QString());
            if (!key.isEmpty())
                selectedResourceNameByKey.insert(key, uuid.trimmed());
        }

        QHash<QString, ResourceSummaryRow> resourceRows;
        QHash<QString, UserSummaryRow> userRows;
        std::array<qint64, 24> occupiedByHour = {};
        std::array<qint64, 24> coverageByHour = {};
        std::array<qint64, 7> occupiedByWeekday = {};
        std::array<qint64, 7> coverageByWeekday = {};
        QVector<qint64> sessionDurationsSeconds;
        qint64 totalOccupiedSeconds = 0;

        for (const SessionSpan &span : std::as_const(sessions)) {
            if (cancellationRequested())
                return cancelledReport();
            const qint64 clippedStart = qMax(span.startSeconds, startSeconds);
            const qint64 clippedEnd = qMin(span.endSeconds, rangeEndSecondsExclusive);
            if (clippedEnd <= clippedStart)
                continue;

            const qint64 durationSeconds = clippedEnd - clippedStart;
            const QString resourceKey = span.resourceKey.trimmed();
            const QString displayName =
                    safeResourceName(displayResourceName(span.resourceName, span.resourceUuid));
            if (!resourceKey.isEmpty()) {
                ResourceSummaryRow &resourceRow = resourceRows[resourceKey];
                if (resourceRow.resourceName.trimmed().isEmpty())
                    resourceRow.resourceName = displayName;
                resourceRow.occupiedSeconds += durationSeconds;
                resourceRow.sessions += 1;
            }

            const QString userKey = span.userKey.trimmed();
            if (!userKey.isEmpty()) {
                UserSummaryRow &userRow = userRows[userKey];
                userRow.occupiedSeconds += durationSeconds;
                userRow.sessions += 1;
                if (!resourceKey.isEmpty())
                    userRow.uniqueResourceKeys.insert(resourceKey);
            }

            totalOccupiedSeconds += durationSeconds;
            sessionDurationsSeconds.append(durationSeconds);
            addSecondsToHourBuckets(clippedStart, clippedEnd, &occupiedByHour, reportTimeZone);
            addSecondsToWeekdayBuckets(clippedStart,
                                       clippedEnd,
                                       &occupiedByWeekday,
                                       reportTimeZone);
        }

        for (auto it = selectedResourceNameByKey.cbegin(); it != selectedResourceNameByKey.cend();
             ++it) {
            if (resourceRows.contains(it.key()))
                continue;
            ResourceSummaryRow row;
            row.resourceName = safeResourceName(it.value());
            resourceRows.insert(it.key(), row);
        }
        for (const MessengerUserKey &user : request.users) {
            const QString key = user.toString().trimmed();
            if (!key.isEmpty() && !userRows.contains(key))
                userRows.insert(key, UserSummaryRow());
        }

        for (auto it = resourceRows.begin(); it != resourceRows.end(); ++it) {
            if (!it->resourceName.trimmed().isEmpty())
                continue;
            const QString knownName =
                    resourceNameByKey.value(it.key(), selectedResourceNameByKey.value(it.key()));
            it->resourceName = safeResourceName(knownName);
        }

        addSecondsToHourBuckets(startSeconds,
                                rangeEndSecondsExclusive,
                                &coverageByHour,
                                reportTimeZone);
        addSecondsToWeekdayBuckets(startSeconds,
                                   rangeEndSecondsExclusive,
                                   &coverageByWeekday,
                                   reportTimeZone);

        const qint64 rangeDurationSeconds =
                qMax<qint64>(1, rangeEndSecondsExclusive - startSeconds);
        int resourceUniverseCount = resourceRows.size();
        if (resourceUniverseCount <= 0)
            resourceUniverseCount = 1;

        int scheduledCount = 0;
        int actualStartedCount = 0;
        int actualCompletedCount = 0;
        int cancelledCount = 0;

        QHash<QString, InitiatorSummaryRow> initiatorRows;
        initiatorRows.insert(QStringLiteral("admin"), InitiatorSummaryRow());
        initiatorRows.insert(QStringLiteral("user"), InitiatorSummaryRow());
        initiatorRows.insert(QStringLiteral("system"), InitiatorSummaryRow());

        for (const ReportEvent &event : std::as_const(events)) {
            if (cancellationRequested())
                return cancelledReport();
            if (!isInRange(event.eventSeconds, startSeconds, reportEndSeconds))
                continue;
            if (event.isSnapshot)
                continue;

            if (event.eventType == QStringLiteral("schedule_set"))
                scheduledCount += 1;
            if (event.eventType == QStringLiteral("occupation_start") && event.scheduled)
                actualStartedCount += 1;
            if (event.eventType == QStringLiteral("occupation_end") && event.scheduled)
                actualCompletedCount += 1;
            if (event.eventType == QStringLiteral("schedule_remove"))
                cancelledCount += 1;

            const QString initiator = event.initiatorKey.trimmed();
            const QString eventUser = event.userKey.trimmed();
            QString initiatorType;
            if (initiator.isEmpty()) {
                initiatorType = QStringLiteral("system");
            } else if (!eventUser.isEmpty() && initiator == eventUser) {
                initiatorType = QStringLiteral("user");
            } else {
                initiatorType = QStringLiteral("admin");
            }

            InitiatorSummaryRow &initiatorRow = initiatorRows[initiatorType];
            initiatorRow.total += 1;
            if (event.eventType == QStringLiteral("occupation_start")) {
                initiatorRow.occupationStart += 1;
            } else if (event.eventType == QStringLiteral("occupation_end")) {
                initiatorRow.occupationEnd += 1;
            } else if (event.eventType == QStringLiteral("schedule_set")) {
                initiatorRow.scheduleSet += 1;
            } else if (event.eventType == QStringLiteral("schedule_remove")) {
                initiatorRow.scheduleRemove += 1;
            } else {
                initiatorRow.other += 1;
            }
        }

        std::sort(sessionDurationsSeconds.begin(), sessionDurationsSeconds.end());

        switch (request.reportKind) {
        case ReportRequest::ReportKind::UtilizationSummary: {
            writeHeaderRow(
                    { QCoreApplication::translate("ServerStats", "Resource"),
                      QCoreApplication::translate("ServerStats", "Utilization (%)"),
                      QCoreApplication::translate("ServerStats", "Occupied hours"),
                      QCoreApplication::translate("ServerStats", "Sessions"),
                      QCoreApplication::translate("ServerStats", "Average duration (min)") });

            QStringList resourceKeys = resourceRows.keys();
            std::sort(resourceKeys.begin(),
                      resourceKeys.end(),
                      [&resourceRows](const QString &lhs, const QString &rhs) {
                          return QString::localeAwareCompare(
                                         safeResourceName(resourceRows.value(lhs).resourceName),
                                         safeResourceName(resourceRows.value(rhs).resourceName))
                                 < 0;
                      });

            for (const QString &key : std::as_const(resourceKeys)) {
                const ResourceSummaryRow row = resourceRows.value(key);
                const double utilizationPercent = (100.0 * static_cast<double>(row.occupiedSeconds))
                                                  / static_cast<double>(rangeDurationSeconds);
                const double avgDurationMinutes =
                        row.sessions > 0 ? (static_cast<double>(row.occupiedSeconds) / 60.0
                                            / static_cast<double>(row.sessions))
                                         : 0.0;
                writeDataRow({ safeResourceName(row.resourceName),
                               formatPercentValue(utilizationPercent),
                               formatHoursValue(row.occupiedSeconds),
                               QString::number(row.sessions),
                               formatMinutesValue(avgDurationMinutes) });
            }
            break;
        }
        case ReportRequest::ReportKind::UserActivitySummary: {
            writeHeaderRow({ QCoreApplication::translate("ServerStats", "User"),
                             QCoreApplication::translate("ServerStats", "Sessions"),
                             QCoreApplication::translate("ServerStats", "Occupied hours"),
                             QCoreApplication::translate("ServerStats", "Unique resources") });

            QStringList userKeys = userRows.keys();
            std::sort(userKeys.begin(),
                      userKeys.end(),
                      [&userNamesByKey](const QString &lhs, const QString &rhs) {
                          const QString lhsDisplay = displayUserNameForStats(lhs, userNamesByKey);
                          const QString rhsDisplay = displayUserNameForStats(rhs, userNamesByKey);
                          const int displayCompare =
                                  QString::localeAwareCompare(lhsDisplay, rhsDisplay);
                          if (displayCompare != 0)
                              return displayCompare < 0;
                          return QString::localeAwareCompare(lhs, rhs) < 0;
                      });

            for (const QString &userKey : std::as_const(userKeys)) {
                const UserSummaryRow row = userRows.value(userKey);
                writeDataRow({ displayUserNameForStats(userKey, userNamesByKey),
                               QString::number(row.sessions),
                               formatHoursValue(row.occupiedSeconds),
                               QString::number(row.uniqueResourceKeys.size()) });
            }
            break;
        }
        case ReportRequest::ReportKind::TopResources: {
            struct RankedResource
            {
                QString resourceName;
                qint64 occupiedSeconds = 0;
                int sessions = 0;
            };

            QList<RankedResource> ranked;
            for (auto it = resourceRows.cbegin(); it != resourceRows.cend(); ++it) {
                const ResourceSummaryRow &row = it.value();
                if (row.sessions <= 0 && row.occupiedSeconds <= 0)
                    continue;
                RankedResource item;
                item.resourceName = safeResourceName(row.resourceName);
                item.occupiedSeconds = row.occupiedSeconds;
                item.sessions = row.sessions;
                ranked.append(item);
            }

            writeHeaderRow({ QCoreApplication::translate("ServerStats", "Ranking type"),
                             QCoreApplication::translate("ServerStats", "Rank"),
                             QCoreApplication::translate("ServerStats", "Resource"),
                             QCoreApplication::translate("ServerStats", "Occupied hours"),
                             QCoreApplication::translate("ServerStats", "Sessions") });

            QList<RankedResource> byHours = ranked;
            std::sort(byHours.begin(),
                      byHours.end(),
                      [](const RankedResource &lhs, const RankedResource &rhs) {
                          if (lhs.occupiedSeconds != rhs.occupiedSeconds)
                              return lhs.occupiedSeconds > rhs.occupiedSeconds;
                          if (lhs.sessions != rhs.sessions)
                              return lhs.sessions > rhs.sessions;
                          return QString::localeAwareCompare(lhs.resourceName, rhs.resourceName)
                                 < 0;
                      });
            const int hoursLimit = qMin(kTopResourcesLimit, byHours.size());
            for (int i = 0; i < hoursLimit; ++i) {
                const RankedResource &item = byHours.at(i);
                writeDataRow({ localizedRankingTypeValue(QStringLiteral("hours")),
                               QString::number(i + 1),
                               item.resourceName,
                               formatHoursValue(item.occupiedSeconds),
                               QString::number(item.sessions) });
            }

            QList<RankedResource> bySessions = ranked;
            std::sort(bySessions.begin(),
                      bySessions.end(),
                      [](const RankedResource &lhs, const RankedResource &rhs) {
                          if (lhs.sessions != rhs.sessions)
                              return lhs.sessions > rhs.sessions;
                          if (lhs.occupiedSeconds != rhs.occupiedSeconds)
                              return lhs.occupiedSeconds > rhs.occupiedSeconds;
                          return QString::localeAwareCompare(lhs.resourceName, rhs.resourceName)
                                 < 0;
                      });
            const int sessionsLimit = qMin(kTopResourcesLimit, bySessions.size());
            for (int i = 0; i < sessionsLimit; ++i) {
                const RankedResource &item = bySessions.at(i);
                writeDataRow({ localizedRankingTypeValue(QStringLiteral("sessions")),
                               QString::number(i + 1),
                               item.resourceName,
                               formatHoursValue(item.occupiedSeconds),
                               QString::number(item.sessions) });
            }
            break;
        }
        case ReportRequest::ReportKind::PeakHours: {
            writeHeaderRow({ QCoreApplication::translate("ServerStats", "Hour (%1)")
                                     .arg(reportTimeZoneLabel(reportTimeZone, request.startUtc)),
                             QCoreApplication::translate("ServerStats", "Occupied hours"),
                             QCoreApplication::translate("ServerStats", "Load share (%)"),
                             QCoreApplication::translate("ServerStats", "Utilization (%)") });

            for (int hour = 0; hour < 24; ++hour) {
                const qint64 occupied = occupiedByHour.at(hour);
                const qint64 coverage = coverageByHour.at(hour);
                const double loadSharePercent =
                        totalOccupiedSeconds > 0 ? (100.0 * static_cast<double>(occupied)
                                                    / static_cast<double>(totalOccupiedSeconds))
                                                 : 0.0;
                const double utilizationPercent =
                        coverage > 0 ? (100.0 * static_cast<double>(occupied)
                                        / (static_cast<double>(coverage)
                                           * static_cast<double>(resourceUniverseCount)))
                                     : 0.0;
                writeDataRow({ QStringLiteral("%1").arg(hour, 2, 10, QLatin1Char('0')),
                               formatHoursValue(occupied),
                               formatPercentValue(loadSharePercent),
                               formatPercentValue(utilizationPercent) });
            }
            break;
        }
        case ReportRequest::ReportKind::ScheduledVsActual: {
            writeHeaderRow({ QCoreApplication::translate("ServerStats", "Scheduled"),
                             QCoreApplication::translate("ServerStats", "Actual started"),
                             QCoreApplication::translate("ServerStats", "Actual completed"),
                             QCoreApplication::translate("ServerStats", "Cancelled/removed"),
                             QCoreApplication::translate("ServerStats", "Start rate (%)"),
                             QCoreApplication::translate("ServerStats", "Completion rate (%)") });

            const double startRate = scheduledCount > 0
                                             ? (100.0 * static_cast<double>(actualStartedCount)
                                                / static_cast<double>(scheduledCount))
                                             : 0.0;
            const double completionRate =
                    scheduledCount > 0 ? (100.0 * static_cast<double>(actualCompletedCount)
                                          / static_cast<double>(scheduledCount))
                                       : 0.0;
            writeDataRow({ QString::number(scheduledCount),
                           QString::number(actualStartedCount),
                           QString::number(actualCompletedCount),
                           QString::number(cancelledCount),
                           formatPercentValue(startRate),
                           formatPercentValue(completionRate) });
            break;
        }
        case ReportRequest::ReportKind::WeekdayPattern: {
            writeHeaderRow({ QCoreApplication::translate("ServerStats", "Weekday"),
                             QCoreApplication::translate("ServerStats", "Occupied hours"),
                             QCoreApplication::translate("ServerStats", "Load share (%)"),
                             QCoreApplication::translate("ServerStats", "Utilization (%)") });

            for (int i = 0; i < 7; ++i) {
                const qint64 occupied = occupiedByWeekday.at(i);
                const qint64 coverage = coverageByWeekday.at(i);
                const double loadSharePercent =
                        totalOccupiedSeconds > 0 ? (100.0 * static_cast<double>(occupied)
                                                    / static_cast<double>(totalOccupiedSeconds))
                                                 : 0.0;
                const double utilizationPercent =
                        coverage > 0 ? (100.0 * static_cast<double>(occupied)
                                        / (static_cast<double>(coverage)
                                           * static_cast<double>(resourceUniverseCount)))
                                     : 0.0;
                writeDataRow({ weekdayName(i),
                               formatHoursValue(occupied),
                               formatPercentValue(loadSharePercent),
                               formatPercentValue(utilizationPercent) });
            }
            break;
        }
        case ReportRequest::ReportKind::SessionDurationDistribution: {
            writeHeaderRow({ QCoreApplication::translate("ServerStats", "Total sessions"),
                             QCoreApplication::translate("ServerStats", "Average duration (min)"),
                             QCoreApplication::translate("ServerStats", "Median (min)"),
                             QCoreApplication::translate("ServerStats", "P75 (min)"),
                             QCoreApplication::translate("ServerStats", "P90 (min)"),
                             QCoreApplication::translate("ServerStats", "P95 (min)"),
                             QCoreApplication::translate("ServerStats", "Short (<=30 min)"),
                             QCoreApplication::translate("ServerStats", "Medium (30-120 min)"),
                             QCoreApplication::translate("ServerStats", "Long (>120 min)") });

            if (sessionDurationsSeconds.isEmpty()) {
                writeDataRow({ QStringLiteral("0"),
                               QStringLiteral("0.00"),
                               QStringLiteral("0.00"),
                               QStringLiteral("0.00"),
                               QStringLiteral("0.00"),
                               QStringLiteral("0.00"),
                               QStringLiteral("0"),
                               QStringLiteral("0"),
                               QStringLiteral("0") });
                break;
            }

            int shortCount = 0;
            int mediumCount = 0;
            int longCount = 0;
            for (const qint64 durationSeconds : std::as_const(sessionDurationsSeconds)) {
                if (durationSeconds <= 30 * 60) {
                    shortCount += 1;
                } else if (durationSeconds <= 120 * 60) {
                    mediumCount += 1;
                } else {
                    longCount += 1;
                }
            }

            const double averageMinutes = static_cast<double>(totalOccupiedSeconds) / 60.0
                                          / static_cast<double>(sessionDurationsSeconds.size());
            const double medianMinutes = percentileSeconds(sessionDurationsSeconds, 0.50) / 60.0;
            const double p75Minutes = percentileSeconds(sessionDurationsSeconds, 0.75) / 60.0;
            const double p90Minutes = percentileSeconds(sessionDurationsSeconds, 0.90) / 60.0;
            const double p95Minutes = percentileSeconds(sessionDurationsSeconds, 0.95) / 60.0;

            writeDataRow({ QString::number(sessionDurationsSeconds.size()),
                           formatMinutesValue(averageMinutes),
                           formatMinutesValue(medianMinutes),
                           formatMinutesValue(p75Minutes),
                           formatMinutesValue(p90Minutes),
                           formatMinutesValue(p95Minutes),
                           QString::number(shortCount),
                           QString::number(mediumCount),
                           QString::number(longCount) });
            break;
        }
        case ReportRequest::ReportKind::InitiatorAnalysis: {
            writeHeaderRow({ QCoreApplication::translate("ServerStats", "Initiator type"),
                             QCoreApplication::translate("ServerStats", "Operations total"),
                             QCoreApplication::translate("ServerStats", "Occupation start"),
                             QCoreApplication::translate("ServerStats", "Occupation end"),
                             QCoreApplication::translate("ServerStats", "Schedule set"),
                             QCoreApplication::translate("ServerStats", "Schedule removed"),
                             QCoreApplication::translate("ServerStats", "Other") });

            const QStringList types = { QStringLiteral("admin"),
                                        QStringLiteral("user"),
                                        QStringLiteral("system") };
            for (const QString &type : types) {
                const InitiatorSummaryRow row = initiatorRows.value(type);
                writeDataRow({ localizedInitiatorTypeValue(type),
                               QString::number(row.total),
                               QString::number(row.occupationStart),
                               QString::number(row.occupationEnd),
                               QString::number(row.scheduleSet),
                               QString::number(row.scheduleRemove),
                               QString::number(row.other) });
            }
            break;
        }
        case ReportRequest::ReportKind::EventsLog: {
            const QString timeZoneLabel = reportTimeZoneLabel(reportTimeZone, request.startUtc);
            writeHeaderRow({ QCoreApplication::translate("ServerStats", "Event time (%1)")
                                     .arg(timeZoneLabel),
                             QCoreApplication::translate("ServerStats", "Event type"),
                             QCoreApplication::translate("ServerStats", "Resource"),
                             QCoreApplication::translate("ServerStats", "User"),
                             QCoreApplication::translate("ServerStats", "Initiator"),
                             QCoreApplication::translate("ServerStats", "Scheduled"),
                             QCoreApplication::translate("ServerStats", "Duration (min)"),
                             QCoreApplication::translate("ServerStats", "Schedule start (%1)")
                                     .arg(timeZoneLabel),
                             QCoreApplication::translate("ServerStats", "Schedule end (%1)")
                                     .arg(timeZoneLabel),
                             QCoreApplication::translate("ServerStats", "Snapshot"),
                             QCoreApplication::translate("ServerStats", "Note") });

            for (const ReportEvent &event : std::as_const(events)) {
                if (cancellationRequested())
                    return cancelledReport();
                if (!isInRange(event.eventSeconds, startSeconds, reportEndSeconds))
                    continue;

                const QString initiatorDisplay =
                        event.initiatorKey.trimmed().isEmpty()
                                ? localizedInitiatorTypeValue(QStringLiteral("system"))
                                : displayUserNameForStats(event.initiatorKey, userNamesByKey);

                writeDataRow(
                        { formatReportDateTimeValue(event.eventSeconds, reportTimeZone),
                          event.eventType,
                          safeResourceName(
                                  displayResourceName(event.resourceName, event.resourceUuid)),
                          displayUserNameForStats(event.userKey, userNamesByKey),
                          initiatorDisplay,
                          QString::number(event.scheduled ? 1 : 0),
                          QString::number(event.durationMinutes),
                          formatReportDateTimeValue(event.scheduleStartSeconds, reportTimeZone),
                          formatReportDateTimeValue(event.scheduleEndSeconds, reportTimeZone),
                          QString::number(event.isSnapshot ? 1 : 0),
                          event.note });
            }
            break;
        }
        }
    }

    if (request.format == ReportRequest::OutputFormat::Csv) {
        csvFile.flush();
        csvFile.close();
        result.rowCount = result.totalRows;
        result.filePath = csvPath;
    } else {
        if (!tableHeaderColumns.isEmpty()) {
            tableLines << tableLineForColumns(tableHeaderColumns);
            tableLines << tableSeparatorLine();
            for (const QStringList &row : std::as_const(tableRows))
                tableLines << tableLineForColumns(row);
        }
        if (result.truncated && result.totalRows > result.rowCount) {
            tableLines << QStringLiteral("... (%1/%2 rows)")
                                  .arg(QString::number(result.rowCount),
                                       QString::number(result.totalRows));
        }
        result.tableText = tableLines.join(QStringLiteral("\n"));
        result.filePath.clear();
    }

    result.ok = result.errorText.isEmpty();
    return result;
}

QString ServerStats::reportKindToken(ReportRequest::ReportKind kind)
{
    switch (kind) {
    case ReportRequest::ReportKind::UtilizationSummary:
        return QStringLiteral("utilization");
    case ReportRequest::ReportKind::UserActivitySummary:
        return QStringLiteral("user_activity");
    case ReportRequest::ReportKind::TopResources:
        return QStringLiteral("top_resources");
    case ReportRequest::ReportKind::PeakHours:
        return QStringLiteral("peak_hours");
    case ReportRequest::ReportKind::ScheduledVsActual:
        return QStringLiteral("plan_vs_actual");
    case ReportRequest::ReportKind::WeekdayPattern:
        return QStringLiteral("weekday_pattern");
    case ReportRequest::ReportKind::SessionDurationDistribution:
        return QStringLiteral("duration_distribution");
    case ReportRequest::ReportKind::InitiatorAnalysis:
        return QStringLiteral("initiators");
    case ReportRequest::ReportKind::EventsLog:
        return QStringLiteral("events");
    }

    return QStringLiteral("utilization");
}

bool ServerStats::reportKindFromToken(const QString &token, ReportRequest::ReportKind *kindOut)
{
    if (!kindOut)
        return false;

    const QString normalized = token.trimmed().toLower();
    if (normalized.isEmpty() || normalized == QStringLiteral("utilization")
        || normalized == QStringLiteral("utilization_summary")) {
        *kindOut = ReportRequest::ReportKind::UtilizationSummary;
        return true;
    }
    if (normalized == QStringLiteral("user_activity")
        || normalized == QStringLiteral("user_activity_summary")) {
        *kindOut = ReportRequest::ReportKind::UserActivitySummary;
        return true;
    }
    if (normalized == QStringLiteral("top_resources") || normalized == QStringLiteral("top")) {
        *kindOut = ReportRequest::ReportKind::TopResources;
        return true;
    }
    if (normalized == QStringLiteral("peak_hours") || normalized == QStringLiteral("peak")) {
        *kindOut = ReportRequest::ReportKind::PeakHours;
        return true;
    }
    if (normalized == QStringLiteral("plan_vs_actual")
        || normalized == QStringLiteral("scheduled_vs_actual")) {
        *kindOut = ReportRequest::ReportKind::ScheduledVsActual;
        return true;
    }
    if (normalized == QStringLiteral("weekday_pattern")) {
        *kindOut = ReportRequest::ReportKind::WeekdayPattern;
        return true;
    }
    if (normalized == QStringLiteral("duration_distribution")
        || normalized == QStringLiteral("session_duration_distribution")) {
        *kindOut = ReportRequest::ReportKind::SessionDurationDistribution;
        return true;
    }
    if (normalized == QStringLiteral("initiators")
        || normalized == QStringLiteral("initiator_analysis")) {
        *kindOut = ReportRequest::ReportKind::InitiatorAnalysis;
        return true;
    }
    if (normalized == QStringLiteral("events") || normalized == QStringLiteral("event_log")
        || normalized == QStringLiteral("events_log")) {
        *kindOut = ReportRequest::ReportKind::EventsLog;
        return true;
    }

    return false;
}

QString ServerStats::reportKindUsageText()
{
    return QStringLiteral("utilization|user_activity|top_resources|peak_hours|"
                          "plan_vs_actual|weekday_pattern|duration_distribution|initiators|"
                          "events");
}

void ServerStats::startReportAsync(const ReportRequest &request,
                                   QObject *receiver,
                                   const std::function<void(const ReportResult &)> &callback) const
{
    if (!receiver || !callback)
        return;

    if (d->activeReportCount >= kMaxConcurrentReports) {
        ReportResult result;
        result.format = request.format;
        result.errorText = tr("Another statistics report is already being generated.");
        QTimer::singleShot(0, receiver, [callback, result]() { callback(result); });
        return;
    }

    ++d->activeReportCount;
    QThread *thread = new QThread;
    d->activeReportThreads.insert(thread);
    ServerStatsReportWorker *worker =
            new ServerStatsReportWorker(d->baseDbPath, request, statsDirectory());
    worker->moveToThread(thread);
    std::shared_ptr<std::atomic_bool> completionHandled = std::make_shared<std::atomic_bool>(false);
    std::shared_ptr<bool> activeSlotReleased = std::make_shared<bool>(false);
    const auto releaseActiveSlot = [this, activeSlotReleased, thread]() {
        if (*activeSlotReleased)
            return;
        *activeSlotReleased = true;
        d->activeReportThreads.remove(thread);
        d->activeReportCount = qMax(0, d->activeReportCount - 1);
    };

    QObject::connect(thread, &QThread::started, worker, &ServerStatsReportWorker::run);
    QObject::connect(worker,
                     &ServerStatsReportWorker::finished,
                     this,
                     releaseActiveSlot,
                     Qt::QueuedConnection);
    QObject::connect(
            worker,
            &ServerStatsReportWorker::finished,
            worker,
            [completionHandled](int, const QString &, const QString &filePath) {
                if (completionHandled->load() && !filePath.trimmed().isEmpty())
                    QFile::remove(filePath);
            },
            Qt::DirectConnection);
    QObject::connect(
            worker,
            &ServerStatsReportWorker::finished,
            receiver,
            [callback, completionHandled](int format,
                                          const QString &tableText,
                                          const QString &filePath,
                                          const QString &errorText,
                                          int rowCount,
                                          int totalRows,
                                          bool truncated) {
                bool expected = false;
                if (!completionHandled->compare_exchange_strong(expected, true)) {
                    if (!filePath.trimmed().isEmpty())
                        QFile::remove(filePath);
                    return;
                }

                ReportResult result;
                result.ok = errorText.isEmpty();
                result.format = static_cast<ReportRequest::OutputFormat>(format);
                result.errorText = errorText;
                result.tableText = tableText;
                result.filePath = filePath;
                result.rowCount = rowCount;
                result.totalRows = totalRows;
                result.truncated = truncated;
                callback(result);
            },
            Qt::QueuedConnection);

    QTimer *timeoutTimer = new QTimer(receiver);
    timeoutTimer->setSingleShot(true);
    const QPointer<QThread> threadGuard(thread);
    const ReportRequest::OutputFormat requestedFormat = request.format;
    QObject::connect(timeoutTimer,
                     &QTimer::timeout,
                     receiver,
                     [callback, completionHandled, threadGuard, requestedFormat]() {
                         bool expected = false;
                         if (!completionHandled->compare_exchange_strong(expected, true))
                             return;

                         ReportResult result;
                         result.ok = false;
                         result.format = requestedFormat;
                         result.errorText =
                                 QCoreApplication::translate(
                                         "ServerStats",
                                         "Failed to build report: internal time limit (%1 seconds) "
                                         "exceeded.")
                                         .arg(QString::number(kReportGenerationTimeoutMs / 1000));
                         callback(result);

                         if (threadGuard && threadGuard->isRunning()) {
                             threadGuard->requestInterruption();
                             threadGuard->quit();
                         }
                     });

    QObject::connect(worker, &ServerStatsReportWorker::finished, timeoutTimer, &QTimer::stop);
    QObject::connect(thread, &QThread::finished, timeoutTimer, &QObject::deleteLater);
    QObject::connect(worker, &ServerStatsReportWorker::finished, thread, &QThread::quit);
    QObject::connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    QObject::connect(thread, &QThread::finished, this, releaseActiveSlot);
    QObject::connect(
            receiver,
            &QObject::destroyed,
            thread,
            [thread, completionHandled]() {
                completionHandled->store(true);
                thread->requestInterruption();
                thread->quit();
            },
            Qt::DirectConnection);

    timeoutTimer->start(kReportGenerationTimeoutMs);
    thread->start();
}
