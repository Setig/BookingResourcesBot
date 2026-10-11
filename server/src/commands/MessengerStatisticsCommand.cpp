#include "MessengerStatisticsCommand.h"

#include "ServerStats.h"
#include "User.h"
#include "UserModel.h"
#include "MessengerBot.h"
#include "ResourceModel.h"
#include "ServerDatabase.h"
#include "MessengerTextRender.h"
#include "ServerDateTimeFormat.h"
#include "ServerRuntimeSettings.h"
#include "MessengerCommandManager.h"
#include "ResourceQueueUserMessages.h"
#include "MessengerUserFacingErrors.h"

#include <QSet>
#include <QFile>
#include <QVector>
#include <QPointer>
#include <QStringList>

#include <algorithm>
#include <memory>
#include <utility>

namespace {

QStringList splitListToken(const QString &token)
{
    QStringList parts = token.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (QString &part : parts)
        part = part.trimmed();
    parts.erase(std::remove_if(parts.begin(),
                               parts.end(),
                               [](const QString &value) { return value.trimmed().isEmpty(); }),
                parts.end());
    return parts;
}

bool isEmptyListToken(const QString &token)
{
    const QString normalized = token.trimmed().toLower();
    return normalized.isEmpty() || normalized == QStringLiteral("-")
           || normalized == QStringLiteral("none") || normalized == QStringLiteral("all")
           || normalized == QStringLiteral("*");
}

QStringList parseOptionalListToken(const QString &token)
{
    if (isEmptyListToken(token))
        return {};
    return splitListToken(token);
}

bool parseUserDateTimeToken(const QString &token,
                            const QString &dateTimeFormat,
                            const UserTimeZone &timeZone,
                            QDateTime *utcOut)
{
    return ServerDateTimeFormat::parseUserDateTime(token,
                                                   dateTimeFormat,
                                                   ServerDateTimeFormat::ParseMode::LocalToUtc,
                                                   utcOut,
                                                   timeZone);
}

bool parseUserEndToken(const QString &token,
                       const QString &dateTimeFormat,
                       const UserTimeZone &timeZone,
                       QDateTime *utcOut)
{
    if (!utcOut)
        return false;

    const QString normalized = token.trimmed().toLower();
    if (normalized == QStringLiteral("now")) {
        *utcOut = QDateTime::currentDateTimeUtc();
        return utcOut->isValid();
    }

    return parseUserDateTimeToken(token, dateTimeFormat, timeZone, utcOut);
}

QString formatUsageText()
{
    return MessengerUserFacingErrors::statisticsFormatExpected();
}

struct OutputFormatSelection
{
    bool csvSelected = true;
    bool tableSelected = false;
};

bool parseOutputFormatToken(const QString &token, OutputFormatSelection *selectionOut)
{
    if (!selectionOut)
        return false;

    const QString normalized = token.trimmed().toLower();
    if (normalized.isEmpty() || normalized == QStringLiteral("csv")) {
        selectionOut->csvSelected = true;
        selectionOut->tableSelected = false;
        return true;
    }
    if (normalized == QStringLiteral("table")) {
        selectionOut->csvSelected = false;
        selectionOut->tableSelected = true;
        return true;
    }
    if (normalized == QStringLiteral("all")) {
        selectionOut->csvSelected = true;
        selectionOut->tableSelected = true;
        return true;
    }
    return false;
}

QString formatTableAsHtmlCodeBlock(const QString &tableText)
{
    return QStringLiteral("<pre><code>%1</code></pre>").arg(tableText.toHtmlEscaped());
}

struct ConsolidatedReportResult
{
    QString csvFilePath;
    QString tableText;
    int rowCount = 0;
    int totalRows = 0;
    bool truncated = false;
};

QString formatElapsedDurationText(qint64 elapsedMs)
{
    const qint64 safeMs = qMax<qint64>(0, elapsedMs);
    if (safeMs < 1000)
        return MessengerStatisticsCommand::tr("%1 ms").arg(QString::number(safeMs));
    return MessengerStatisticsCommand::tr("%1 s").arg(
            QString::number(static_cast<double>(safeMs) / 1000.0, 'f', 2));
}

QString reportGeneratedStatusText(qint64 elapsedMs)
{
    return MessengerStatisticsCommand::tr("Report generated in %1.")
            .arg(formatElapsedDurationText(elapsedMs));
}

QString reportTruncatedStatusText(int rowCount, int totalRows)
{
    return MessengerStatisticsCommand::tr("Report is truncated: %1 of %2 rows are shown.")
            .arg(QString::number(rowCount), QString::number(totalRows));
}

QString buildPlainConsolidatedReportText(const ConsolidatedReportResult &combinedResult,
                                         qint64 elapsedMs)
{
    QStringList parts;
    const bool hasTable = !combinedResult.tableText.trimmed().isEmpty();
    if (hasTable)
        parts << combinedResult.tableText;
    if (combinedResult.truncated && combinedResult.totalRows > combinedResult.rowCount) {
        parts << reportTruncatedStatusText(combinedResult.rowCount, combinedResult.totalRows);
    }
    parts << reportGeneratedStatusText(elapsedMs);
    return parts.join(QStringLiteral("\n\n"));
}

QString buildHtmlConsolidatedReportText(const ConsolidatedReportResult &combinedResult,
                                        qint64 elapsedMs)
{
    QStringList parts;
    const bool hasTable = !combinedResult.tableText.trimmed().isEmpty();
    if (hasTable)
        parts << formatTableAsHtmlCodeBlock(combinedResult.tableText);
    if (combinedResult.truncated && combinedResult.totalRows > combinedResult.rowCount) {
        parts << reportTruncatedStatusText(combinedResult.rowCount, combinedResult.totalRows)
                         .toHtmlEscaped();
    }
    parts << reportGeneratedStatusText(elapsedMs).toHtmlEscaped();
    return parts.join(QStringLiteral("\n\n"));
}

void sendConsolidatedReport(MessengerBot *bot,
                            const AbstractBot::ChatAddress &chat,
                            const ConsolidatedReportResult &combinedResult,
                            qint64 elapsedMs)
{
    if (!bot)
        return;

    const QString plainText = buildPlainConsolidatedReportText(combinedResult, elapsedMs);
    const bool useHtml =
            !combinedResult.tableText.trimmed().isEmpty()
            && MessengerTextRender::shouldUseHtmlLists(chat.messenger, bot->backendBot());
    if (!combinedResult.csvFilePath.trimmed().isEmpty()) {
        const QString captionText =
                useHtml ? buildHtmlConsolidatedReportText(combinedResult, elapsedMs) : plainText;
        bot->sendFile(chat,
                      combinedResult.csvFilePath,
                      captionText,
                      useHtml ? AbstractBot::MessageFormat::Html
                              : AbstractBot::MessageFormat::PlainText);
        return;
    }

    if (useHtml) {
        bot->sendReply(chat,
                       buildHtmlConsolidatedReportText(combinedResult, elapsedMs),
                       AbstractBot::MessageFormat::Html);
        return;
    }
    bot->sendReply(chat, plainText);
}

bool collectSingleReportResult(MessengerBot *bot,
                               const AbstractBot::ChatAddress &chat,
                               const ServerStats::ReportResult &result,
                               ConsolidatedReportResult *combinedResultOut)
{
    if (!bot || !combinedResultOut)
        return false;

    if (!result.ok) {
        bot->sendReply(chat,
                       result.errorText.isEmpty() ? MessengerUserFacingErrors::failedToBuildReport()
                                                  : result.errorText);
        return false;
    }

    if (result.format == ServerStats::ReportRequest::OutputFormat::Csv) {
        if (result.filePath.trimmed().isEmpty()) {
            bot->sendReply(chat, MessengerUserFacingErrors::failedToBuildReport());
            return false;
        }
        combinedResultOut->csvFilePath = result.filePath;
        return true;
    }

    if (result.tableText.trimmed().isEmpty()) {
        bot->sendReply(chat, MessengerUserFacingErrors::failedToBuildReport());
        return false;
    }
    combinedResultOut->tableText = result.tableText;
    combinedResultOut->rowCount = result.rowCount;
    combinedResultOut->totalRows = result.totalRows;
    combinedResultOut->truncated = result.truncated;
    return true;
}

struct StatisticsPolicy
{
    bool enabled = false;
    bool adminsOnly = false;
};

StatisticsPolicy readStatisticsPolicy(const ServerDatabase *db)
{
    StatisticsPolicy policy;
    policy.enabled =
            ServerRuntimeSettings::defaultValue(ServerRuntimeSettings::Setting::StatsEnabled)
                    .toBool();
    policy.adminsOnly =
            ServerRuntimeSettings::defaultValue(ServerRuntimeSettings::Setting::StatsAdminsOnly)
                    .toBool();
    if (db && db->isReady()) {
        policy.enabled =
                db->serverSettingValue(ServerRuntimeSettings::Setting::StatsEnabled).toBool();
        policy.adminsOnly =
                db->serverSettingValue(ServerRuntimeSettings::Setting::StatsAdminsOnly).toBool();
    }
    return policy;
}

} // namespace

MessengerStatisticsCommand::MessengerStatisticsCommand(MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::AdminsAndUsers, 90)
{
    initParser(tr("/statistics"), tr("Build a statistics report."));
}

bool MessengerStatisticsCommand::isEnabled(const AbstractBot::IncomingMessage *message) const
{
    const MessengerBot *b = bot();
    const StatisticsPolicy policy = readStatisticsPolicy(b ? b->database() : nullptr);

    if (!policy.enabled)
        return false;
    if (!policy.adminsOnly)
        return true;
    if (!b || !message)
        return false;
    return b->isAdminUser(AbstractBot::senderKey(*message));
}

QString MessengerStatisticsCommand::disabledUserFacingReason(
        const AbstractBot::IncomingMessage &message) const
{
    const MessengerBot *b = bot();
    const StatisticsPolicy policy = readStatisticsPolicy(b ? b->database() : nullptr);

    if (!policy.enabled)
        return MessengerUserFacingErrors::statisticsNotAvailable();
    if (policy.adminsOnly && (!b || !b->isAdminUser(AbstractBot::senderKey(message))))
        return MessengerUserFacingErrors::accessDeniedAdminsOnly();
    return QString();
}

void MessengerStatisticsCommand::configureParser(MessengerCommandParser &parser) const
{
    parser.addArgument(QStringLiteral("resources_list_or_all_or_dash"));
    parser.addArgument(QStringLiteral("users_list_or_all_or_dash"));
    parser.addArgument(QStringLiteral("start_time"));
    parser.addArgument(QStringLiteral("end_time_or_now"));
    parser.addArgument(QStringLiteral("format_csv_or_table_or_all"),
                       QString(),
                       MessengerCommandParser::ArgumentFlag::Optional);
    parser.addArgument(QStringLiteral("report_kind"),
                       QString(),
                       MessengerCommandParser::ArgumentFlag::Optional);
}

void MessengerStatisticsCommand::execute(const AbstractBot::IncomingMessage &message,
                                         const QString &args)
{
    MessengerBot *b = bot();
    if (!b || !b->ensureDatabase(message.chat))
        return;

    const StatisticsPolicy policy = readStatisticsPolicy(b->database());
    if (!policy.enabled) {
        b->sendReply(message.chat, MessengerUserFacingErrors::statisticsNotAvailable());
        return;
    }
    if (policy.adminsOnly && !b->isAdminUser(AbstractBot::senderKey(message))) {
        b->sendReply(message.chat, MessengerUserFacingErrors::accessDeniedAdminsOnly());
        return;
    }

    const MessengerCommandParser::ParseResult parseResult = parseArguments(args);
    if (!parseResult.ok) {
        b->sendReply(message.chat, parseResult.errorText);
        return;
    }

    const QStringList parts = parseResult.values;
    const QString resourcesToken = parts.value(0).trimmed();
    const QString usersToken = parts.value(1).trimmed();
    const QString startToken = parts.value(2).trimmed();
    const QString endToken = parts.value(3).trimmed();
    QString formatToken = parts.value(4).trimmed().toLower();
    QString reportKindToken = parts.value(5).trimmed().toLower();
    const QString dateTimeFormat = ServerDateTimeFormat::effectiveFormat(b->database());
    const MessengerUserKey senderKey = AbstractBot::senderKey(message);
    const UserTimeZone senderTimeZone = b->effectiveTimeZoneForUser(senderKey);

    if (startToken.isEmpty() || endToken.isEmpty()) {
        b->sendReply(message.chat, tr("Usage: %1").arg(parser().usageSignature()));
        return;
    }

    ServerDatabase *db = b->database();
    if (!db || !db->isReady()) {
        b->sendReply(message.chat, tr("Database is not available right now."));
        return;
    }

    ServerStats *stats = db->stats();
    if (!stats) {
        b->sendReply(message.chat, tr("Statistics are not available right now."));
        return;
    }

    const QStringList resources = parseOptionalListToken(resourcesToken);
    const QStringList users = parseOptionalListToken(usersToken);

    ServerStats::ReportRequest request;
    const UserModel *userModel = db->userModel();
    const ResourceModel *resourceModel = db->resourceModel();
    const User sender = userModel ? userModel->userByKey(senderKey) : User();
    const bool unrestricted = senderKey.messenger() == MessengerType::LocalCli
                              || sender.isFixedAdmin() || db->canManageServerSettings(senderKey);
    ServerStats::configureActorScope(&request, sender, unrestricted);

    if (resourceModel) {
        const QList<Resource> allResources = resourceModel->items();
        for (const Resource &resource : allResources) {
            if (!resource.isValid())
                continue;
            const bool allowed =
                    unrestricted
                    || (sender.isAdmin()
                                ? db->canAdministerResource(senderKey, resource)
                                : MessengerTextRender::isResourceVisibleToViewer(
                                          resource, db, message.chat.messenger, senderKey));
            if (allowed)
                request.allowedResourceUuids.append(resource.uuidString());
        }
    }
    if (userModel) {
        const QList<User> allUsers = userModel->items();
        for (const User &candidate : allUsers) {
            if (!candidate.isValid())
                continue;
            if (unrestricted || (sender.isAdmin() && db->canAdministerUser(senderKey, candidate))
                || (!sender.isAdmin() && candidate.key() == senderKey)) {
                request.allowedUsers.append(candidate.key());
            }
        }
    }

    if (!resources.isEmpty()) {
        request.resourceNames = resources;
        if (resourceModel) {
            QSet<QString> uuids;
            for (const QString &name : resources) {
                const Resource resource = resourceModel->resourceByName(name);
                const bool allowed =
                        resource.isValid()
                        && (unrestricted
                            || (sender.isAdmin() ? db->canAdministerResource(senderKey, resource)
                                                 : MessengerTextRender::isResourceVisibleToViewer(
                                                           resource,
                                                           db,
                                                           message.chat.messenger,
                                                           senderKey)));
                if (!allowed) {
                    b->sendReply(message.chat, ResourceQueueUserMessages::resourceNotFound(name));
                    return;
                }
                uuids.insert(resource.uuidString());
            }
            request.resourceUuids = QStringList(uuids.cbegin(), uuids.cend());
        }
    }

    if (!users.isEmpty()) {
        if (resources.isEmpty())
            request.filter = ServerStats::ReportRequest::FilterType::Users;
        for (const QString &token : users) {
            bool ok = false;
            const MessengerUserKey key = MessengerUserKey::fromString(token, &ok);
            if (!ok || !key.isValid()) {
                b->sendReply(message.chat, MessengerUserFacingErrors::invalidUserValue(token));
                return;
            }
            const User target = userModel ? userModel->userByKey(key) : User();
            const bool allowed =
                    target.isValid()
                    && (unrestricted
                        || (sender.isAdmin() && db->canAdministerUser(senderKey, target))
                        || (!sender.isAdmin() && key == senderKey));
            if (!allowed) {
                b->sendReply(message.chat, MessengerUserFacingErrors::userNotFound(token));
                return;
            }
            request.users.append(key);
        }
    }

    if (!resources.isEmpty() && users.isEmpty())
        request.filter = ServerStats::ReportRequest::FilterType::Resources;

    QDateTime startUtc;
    QDateTime endUtc;
    if (!parseUserDateTimeToken(startToken, dateTimeFormat, senderTimeZone, &startUtc)
        || !startUtc.isValid()) {
        b->sendReply(message.chat, MessengerUserFacingErrors::invalidStartTime(dateTimeFormat));
        return;
    }
    if (!parseUserEndToken(endToken, dateTimeFormat, senderTimeZone, &endUtc)
        || !endUtc.isValid()) {
        b->sendReply(message.chat, MessengerUserFacingErrors::invalidEndTime(dateTimeFormat));
        return;
    }
    if (startUtc > endUtc) {
        b->sendReply(message.chat, MessengerUserFacingErrors::endTimeMustBeAfterStartTime());
        return;
    }

    request.startUtc = startUtc;
    request.endUtc = endUtc;
    request.timeZone = senderTimeZone;

    ServerStats::ReportRequest::ReportKind reportKind =
            ServerStats::ReportRequest::ReportKind::UtilizationSummary;
    OutputFormatSelection formatSelection;
    if (reportKindToken.isEmpty() && !formatToken.isEmpty()
        && !parseOutputFormatToken(formatToken, &formatSelection)
        && ServerStats::reportKindFromToken(formatToken, &reportKind)) {
        reportKindToken = formatToken;
        formatToken.clear();
    }

    if (!parseOutputFormatToken(formatToken, &formatSelection)) {
        b->sendReply(message.chat,
                     MessengerUserFacingErrors::invalidFormat(formatToken, formatUsageText()));
        return;
    }

    if (!ServerStats::reportKindFromToken(reportKindToken, &reportKind)) {
        b->sendReply(message.chat,
                     MessengerUserFacingErrors::invalidReportKind(
                             reportKindToken, ServerStats::reportKindUsageText()));
        return;
    }
    request.reportKind = reportKind;

    QVector<ServerStats::ReportRequest::OutputFormat> formatsToGenerate;
    if (formatSelection.tableSelected) {
        formatsToGenerate.append(ServerStats::ReportRequest::OutputFormat::Table);
    }
    if (formatSelection.csvSelected) {
        formatsToGenerate.append(ServerStats::ReportRequest::OutputFormat::Csv);
    }
    if (formatsToGenerate.isEmpty()) {
        b->sendReply(message.chat,
                     MessengerUserFacingErrors::invalidFormat(formatToken, formatUsageText()));
        return;
    }

    const QString effectiveLocale = b->effectiveLocaleForUser(senderKey, message.languageCode);
    QPointer<MessengerBot> bot = b;
    QPointer<ServerStats> statsPtr = stats;
    b->sendReply(message.chat, tr("Preparing statistics report..."));

    const AbstractBot::ChatAddress chat = message.chat;
    const qint64 reportStartedMs = QDateTime::currentMSecsSinceEpoch();
    std::shared_ptr<int> nextFormatIndex = std::make_shared<int>(0);
    std::shared_ptr<bool> hasSuccessfulReports = std::make_shared<bool>(false);
    std::shared_ptr<ConsolidatedReportResult> consolidatedResult =
            std::make_shared<ConsolidatedReportResult>();
    const std::function<bool()> scopeStillValid = [bot, senderKey, request]() {
        if (!bot)
            return false;
        ServerDatabase *database = bot->database();
        if (!database || !database->isReady())
            return false;
        const UserModel *users = database->userModel();
        const ResourceModel *resources = database->resourceModel();
        const User actor = users ? users->userByKey(senderKey) : User();
        const bool unrestricted = senderKey.messenger() == MessengerType::LocalCli
                                  || actor.isFixedAdmin()
                                  || database->canManageServerSettings(senderKey);
        const StatisticsPolicy currentPolicy = readStatisticsPolicy(database);
        if (!currentPolicy.enabled
            || (currentPolicy.adminsOnly && !unrestricted && !actor.isAdmin())) {
            return false;
        }
        if (senderKey.messenger() != MessengerType::LocalCli && !actor.isFixedAdmin()
            && database->isBlacklisted(senderKey)) {
            return false;
        }
        return ServerStats::actorScopeStillValid(request, actor, unrestricted, resources, users);
    };
    std::shared_ptr<std::function<void()>> runNextFormat =
            std::make_shared<std::function<void()>>();
    const std::weak_ptr<std::function<void()>> weakRunNextFormat = runNextFormat;
    *runNextFormat = [bot,
                      statsPtr,
                      request,
                      formatsToGenerate,
                      effectiveLocale,
                      reportStartedMs,
                      nextFormatIndex,
                      hasSuccessfulReports,
                      consolidatedResult,
                      scopeStillValid,
                      weakRunNextFormat,
                      chat]() {
        const std::shared_ptr<std::function<void()>> continuation = weakRunNextFormat.lock();
        if (!continuation)
            return;
        if (!bot || !statsPtr)
            return;
        if (!scopeStillValid()) {
            if (!consolidatedResult->csvFilePath.isEmpty())
                QFile::remove(consolidatedResult->csvFilePath);
            bot->sendReply(chat, MessengerUserFacingErrors::accessDeniedAdminsOnly());
            return;
        }
        if (*nextFormatIndex >= formatsToGenerate.size())
            return;

        ServerStats::ReportRequest nextRequest = request;
        nextRequest.format = formatsToGenerate.at(*nextFormatIndex);
        statsPtr->startReportAsync(
                nextRequest,
                bot.data(),
                [bot,
                 effectiveLocale,
                 reportStartedMs,
                 nextFormatIndex,
                 hasSuccessfulReports,
                 consolidatedResult,
                 scopeStillValid,
                 runNextFormat = continuation,
                 formatsToGenerate,
                 chat](const ServerStats::ReportResult &result) {
                    if (!bot)
                        return;
                    if (!scopeStillValid()) {
                        if (!result.filePath.isEmpty())
                            QFile::remove(result.filePath);
                        if (!consolidatedResult->csvFilePath.isEmpty())
                            QFile::remove(consolidatedResult->csvFilePath);
                        bot->sendReply(chat, MessengerUserFacingErrors::accessDeniedAdminsOnly());
                        return;
                    }
                    BookingResourcesTranslator::ScopedLocaleOverride guard =
                            bot->applyScopedLocale(effectiveLocale);
                    if (!collectSingleReportResult(bot, chat, result, consolidatedResult.get()))
                        return;
                    *hasSuccessfulReports = true;
                    *nextFormatIndex += 1;
                    if (*nextFormatIndex >= formatsToGenerate.size()) {
                        if (*hasSuccessfulReports) {
                            const qint64 elapsedMs =
                                    QDateTime::currentMSecsSinceEpoch() - reportStartedMs;
                            sendConsolidatedReport(bot, chat, *consolidatedResult, elapsedMs);
                        }
                        return;
                    }
                    (*runNextFormat)();
                });
    };
    (*runNextFormat)();
}

QStringList MessengerStatisticsCommand::details() const
{
    QStringList details;
    details << tr("Dates are interpreted in your configured time zone.");
    details << tr(
            "Use current server date/time format (see /server_settings get ui.date_time_format).");
    details << tr("ISO input is also accepted.");
    details << tr("Use quotes for date/time arguments when they contain spaces.");
    details << tr("Use 'now' as end time to include data up to current time.");
    details << tr("Provide resource and user lists as comma-separated values.");
    details << tr("Use '-' (or 'none' or 'all') to include all authorized items for either list.");
    details << tr("User keys must be in the form <messenger>:<user_id>.");
    details << tr("Supported formats: %1 (defaults to csv).").arg(formatUsageText());
    details << tr("Optional report kind: %1 (defaults to utilization).")
                       .arg(ServerStats::reportKindUsageText());
    return details;
}

QStringList MessengerStatisticsCommand::examples() const
{
    QStringList examples;
    examples << tr("/statistics all all \"2026-02-01 00:00\" now all utilization");
    examples << tr("/statistics Printer telegram:12345,telegram:67890 \"2026-02-01 00:00\" "
                   "\"2026-02-07 00:00\" csv plan_vs_actual");
    examples << tr("/statistics all all \"2026-02-01 00:00\" \"2026-02-02 00:00\" csv events");
    return examples;
}

BOOKING_RESOURCES_REGISTER_COMMAND(MessengerStatisticsCommand)
