#ifndef SERVERSTATS_H
#define SERVERSTATS_H

#include <QObject>

#include <functional>

#include <QString>
#include <QDateTime>
#include <QStringList>

#include "UserTimeZone.h"
#include "MessengerUserKey.h"

class User;
class Resource;
class UserModel;
class ResourceModel;
class ResourceQueueModel;

class ServerStatsPrivate;

/**
 * @brief Statistics storage and report runner for server events.
 */
class ServerStats final : public QObject
{
    Q_OBJECT

public:
    struct ReportRequest
    {
        enum class FilterType {
            Resources = 0,
            Users
        };

        enum class OutputFormat {
            Table = 0,
            Csv
        };

        enum class ReportKind {
            UtilizationSummary = 0,
            UserActivitySummary,
            TopResources,
            PeakHours,
            ScheduledVsActual,
            WeekdayPattern,
            SessionDurationDistribution,
            InitiatorAnalysis,
            EventsLog
        };

        FilterType filter = FilterType::Resources;
        OutputFormat format = OutputFormat::Table;
        ReportKind reportKind = ReportKind::UtilizationSummary;
        QStringList resourceNames;
        QStringList resourceUuids;
        QList<MessengerUserKey> users;
        QDateTime startUtc;
        QDateTime endUtc;
        UserTimeZone timeZone;
        bool scopeRestricted = false;
        bool administrativeScope = false;
        QStringList authorizedGroupRoots;
        QStringList allowedResourceUuids;
        QList<MessengerUserKey> allowedUsers;
    };

    struct ReportResult
    {
        bool ok = false;
        ReportRequest::OutputFormat format = ReportRequest::OutputFormat::Table;
        QString errorText;
        QString tableText;
        QString filePath;
        int rowCount = 0;
        int totalRows = 0;
        bool truncated = false;
    };

    using CancellationCheck = std::function<bool()>;

    explicit ServerStats(const QString &baseDatabasePath, QObject *parent = nullptr);
    ~ServerStats() override;

    void setEnabled(bool enabled);
    [[nodiscard]] bool isEnabled() const;

    [[nodiscard]] QString baseDatabasePath() const;
    [[nodiscard]] QString statsDirectory() const;
    [[nodiscard]] QString statsFilePathForMonth(const QDate &utcMonth) const;

    void captureInitialState(const UserModel *users,
                             const ResourceModel *resources,
                             const ResourceQueueModel *queue);

    void recordUserEvent(const User &user,
                         const MessengerUserKey &actor,
                         const QString &eventType,
                         bool isSnapshot = false);

    void recordResourceEvent(const Resource &resource,
                             const MessengerUserKey &actor,
                             const QString &eventType,
                             bool isSnapshot = false);

    void recordOccupationEvent(const QString &resourceName,
                               const QString &resourceUuid,
                               const MessengerUserKey &user,
                               const MessengerUserKey &initiator,
                               const QString &eventType,
                               bool scheduled,
                               int durationMinutes,
                               const QDateTime &eventAtUtc,
                               const QString &note = QString());

    void recordScheduleEvent(const QString &resourceName,
                             const QString &resourceUuid,
                             const MessengerUserKey &user,
                             const MessengerUserKey &initiator,
                             const QString &eventType,
                             const QDateTime &scheduleStartUtc,
                             const QDateTime &scheduleEndUtc,
                             const QDateTime &eventAtUtc,
                             const QString &note = QString(),
                             bool isSnapshot = false);

    static ReportResult generateReport(
            const QString &baseDatabasePath,
            const ReportRequest &request,
            const QString &outputDirectory = QString(),
            const CancellationCheck &cancellationCheck = CancellationCheck());
    static void configureActorScope(ReportRequest *request, const User &actor, bool unrestricted);
    static bool actorScopeStillValid(const ReportRequest &request,
                                     const User &actor,
                                     bool unrestricted,
                                     const ResourceModel *resources,
                                     const UserModel *users);
    static QString reportKindToken(ReportRequest::ReportKind kind);
    static bool reportKindFromToken(const QString &token, ReportRequest::ReportKind *kindOut);
    static QString reportKindUsageText();

    void startReportAsync(const ReportRequest &request,
                          QObject *receiver,
                          const std::function<void(const ReportResult &)> &callback) const;

signals:
    void errorOccurred(const QString &description);

private:
    ServerStatsPrivate *d;
};

#endif // SERVERSTATS_H
