#include "MessengerStatisticsMenu.h"

#include "User.h"
#include "Resource.h"
#include "UserModel.h"
#include "ServerStats.h"
#include "MessengerBot.h"
#include "ResourceModel.h"
#include "ServerDatabase.h"
#include "MessengerMenuIds.h"
#include "MessengerTextRender.h"
#include "MessengerMenuManager.h"
#include "ServerDateTimeFormat.h"
#include "ServerRuntimeSettings.h"
#include "ResourceQueueUserMessages.h"
#include "MessengerUserFacingErrors.h"

#include <QTime>
#include <QFile>
#include <QTimer>
#include <QPointer>
#include <QTimeZone>
#include <QCollator>
#include <QStringList>

#include <algorithm>
#include <memory>
#include <utility>

namespace {
constexpr int kDefaultPageSize = MessengerMenuIds::kSelectorPageSize;
constexpr int kReportCompletionMenuReopenDelayMs = 500;

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

bool statisticsEnabled(const ServerDatabase *database)
{
    bool enabled = ServerRuntimeSettings::defaultValue(ServerRuntimeSettings::Setting::StatsEnabled)
                           .toBool();
    if (database && database->isReady()) {
        enabled =
                database->serverSettingValue(ServerRuntimeSettings::Setting::StatsEnabled).toBool();
    }
    return enabled;
}

bool statisticsAdminsOnly(const ServerDatabase *database)
{
    bool adminsOnly =
            ServerRuntimeSettings::defaultValue(ServerRuntimeSettings::Setting::StatsAdminsOnly)
                    .toBool();
    if (database && database->isReady()) {
        adminsOnly = database->serverSettingValue(ServerRuntimeSettings::Setting::StatsAdminsOnly)
                             .toBool();
    }
    return adminsOnly;
}

bool canUseStatistics(const MessengerBot *bot,
                      const MessengerMenu::RenderContext &context,
                      QString *errorOut)
{
    const ServerDatabase *database = bot ? bot->database() : nullptr;
    if (!statisticsEnabled(database)) {
        if (errorOut) {
            *errorOut = MessengerUserFacingErrors::statisticsNotAvailable();
        }
        return false;
    }
    if (statisticsAdminsOnly(database) && !context.isAdmin) {
        if (errorOut)
            *errorOut = MessengerUserFacingErrors::accessDeniedAdminsOnly();
        return false;
    }
    if (errorOut)
        errorOut->clear();
    return true;
}

QTimeZone effectiveTimeZone(const UserTimeZone &timeZone)
{
    return ServerDateTimeFormat::effectiveTimeZone(timeZone);
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

QString formatTimeStamp(const QDateTime &utc,
                        const QString &dateTimeFormat,
                        const UserTimeZone &timeZone)
{
    if (!utc.isValid())
        return QString();
    return ServerDateTimeFormat::formatLocalDateTime(utc, dateTimeFormat, QString(), timeZone);
}

QDateTime buildUtcDateTime(const QDate &date, const QTime &time, const UserTimeZone &timeZone)
{
    if (!date.isValid() || !time.isValid())
        return QDateTime();
    return QDateTime(date, time, effectiveTimeZone(timeZone)).toUTC();
}

bool resolveQuickRangePreset(const QString &token,
                             const UserTimeZone &timeZone,
                             QDateTime *startUtc,
                             QDateTime *endUtc)
{
    if (!startUtc || !endUtc)
        return false;

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    if (!nowUtc.isValid())
        return false;
    const QDateTime nowLocal = ServerDateTimeFormat::toLocalDateTime(nowUtc, timeZone);
    if (!nowLocal.isValid())
        return false;

    const QString normalized = token.trimmed().toLower();
    if (normalized == QStringLiteral("last_1h")) {
        *startUtc = nowUtc.addSecs(-60 * 60);
        *endUtc = nowUtc;
        return true;
    }
    if (normalized == QStringLiteral("last_24h")) {
        *startUtc = nowUtc.addSecs(-24 * 60 * 60);
        *endUtc = nowUtc;
        return true;
    }
    if (normalized == QStringLiteral("last_7d")) {
        *startUtc = nowUtc.addDays(-7);
        *endUtc = nowUtc;
        return true;
    }
    if (normalized == QStringLiteral("today")) {
        *startUtc = buildUtcDateTime(nowLocal.date(), QTime(0, 0), timeZone);
        *endUtc = nowUtc;
        return startUtc->isValid();
    }
    if (normalized == QStringLiteral("month")) {
        const QDate monthStart(nowLocal.date().year(), nowLocal.date().month(), 1);
        *startUtc = buildUtcDateTime(monthStart, QTime(0, 0), timeZone);
        *endUtc = nowUtc;
        return startUtc->isValid();
    }

    return false;
}

bool resolveQuickEndPreset(const QString &token,
                           const UserTimeZone &timeZone,
                           const QDateTime &startUtc,
                           QDateTime *endUtc)
{
    if (!endUtc || !startUtc.isValid())
        return false;

    Q_UNUSED(timeZone);
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    if (!nowUtc.isValid())
        return false;

    const QString normalized = token.trimmed().toLower();
    if (normalized == QStringLiteral("now")) {
        *endUtc = nowUtc;
        return true;
    }
    if (normalized == QStringLiteral("plus_1h")) {
        *endUtc = startUtc.addSecs(60 * 60);
        return endUtc->isValid();
    }
    if (normalized == QStringLiteral("plus_24h")) {
        *endUtc = startUtc.addSecs(24 * 60 * 60);
        return endUtc->isValid();
    }
    if (normalized == QStringLiteral("plus_7d")) {
        *endUtc = startUtc.addDays(7);
        return endUtc->isValid();
    }

    return false;
}

QString resourceLabelForButton(const Resource &resource)
{
    return MessengerStatisticsMenu::tr("%1 (groups: %2)")
            .arg(MessengerTextRender::formatResourceNameWithStatus(resource, false),
                 MessengerTextRender::formatResourceGroups(resource, false));
}

QString userLabelForButton(const User &user)
{
    const QString display = MessengerTextRender::formatUserDisplayName(user, false).trimmed();
    const QString key = user.key().toString().trimmed();
    const QString label = display.isEmpty() ? key : display;
    return label.trimmed();
}

QString resourceLabelForSummary(const QString &name, const ResourceModel *resourceModel, bool html)
{
    const QString trimmedName = name.trimmed();
    if (resourceModel) {
        const Resource resource = resourceModel->resourceByName(trimmedName);
        if (resource.isValid())
            return MessengerTextRender::formatResourceLine(resource, html);
    }
    return html ? MessengerMenuManager::escapeHtml(trimmedName) : trimmedName;
}

QString userLabelForSummary(const MessengerUserKey &key, const UserModel *userModel, bool html)
{
    if (userModel) {
        const User user = userModel->userByKey(key);
        if (user.isValid()) {
            const QString display = MessengerTextRender::formatUserLine(user, html).trimmed();
            if (!display.isEmpty())
                return display;
        }
    }

    if (key.isValid()) {
        const User unresolvedUser(key, QString(), QString(), User::Role::UserRole);
        const QString fallback =
                MessengerTextRender::formatUserDisplayName(unresolvedUser, html).trimmed();
        if (!fallback.isEmpty())
            return fallback;
    }
    return html ? MessengerMenuManager::escapeHtml(key.toString().trimmed())
                : key.toString().trimmed();
}

QString reportKindTitle(ServerStats::ReportRequest::ReportKind kind)
{
    switch (kind) {
    case ServerStats::ReportRequest::ReportKind::UtilizationSummary:
        return MessengerStatisticsMenu::tr("Utilization summary");
    case ServerStats::ReportRequest::ReportKind::UserActivitySummary:
        return MessengerStatisticsMenu::tr("User activity summary");
    case ServerStats::ReportRequest::ReportKind::TopResources:
        return MessengerStatisticsMenu::tr("Top resources");
    case ServerStats::ReportRequest::ReportKind::PeakHours:
        return MessengerStatisticsMenu::tr("Peak hours");
    case ServerStats::ReportRequest::ReportKind::ScheduledVsActual:
        return MessengerStatisticsMenu::tr("Scheduled vs actual");
    case ServerStats::ReportRequest::ReportKind::WeekdayPattern:
        return MessengerStatisticsMenu::tr("Weekday pattern");
    case ServerStats::ReportRequest::ReportKind::SessionDurationDistribution:
        return MessengerStatisticsMenu::tr("Session duration distribution");
    case ServerStats::ReportRequest::ReportKind::InitiatorAnalysis:
        return MessengerStatisticsMenu::tr("Initiator analysis");
    case ServerStats::ReportRequest::ReportKind::EventsLog:
        return MessengerStatisticsMenu::tr("Events log");
    }

    return MessengerStatisticsMenu::tr("Utilization summary");
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
        return MessengerStatisticsMenu::tr("%1 ms").arg(QString::number(safeMs));
    return MessengerStatisticsMenu::tr("%1 s").arg(
            QString::number(static_cast<double>(safeMs) / 1000.0, 'f', 2));
}

QString reportGeneratedStatusText(qint64 elapsedMs)
{
    return MessengerStatisticsMenu::tr("Report generated in %1.")
            .arg(formatElapsedDurationText(elapsedMs));
}

QString reportTruncatedStatusText(int rowCount, int totalRows)
{
    return MessengerStatisticsMenu::tr("Report is truncated: %1 of %2 rows are shown.")
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

} // namespace

MessengerStatisticsMenu::MessengerStatisticsMenu(MessengerMenuManager *manager)
    : MessengerAbstractMenu(manager)
{
    if (manager) {
        QObject::connect(manager,
                         &MessengerMenuManager::sessionEnded,
                         this,
                         [this](const QString &sessionId) {
                             m_stateBySessionId.remove(sessionId);
                         });
    }
}

QString MessengerStatisticsMenu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuStatistics);
}

QStringList MessengerStatisticsMenu::eventKeys() const
{
    return { QStringLiteral("st") };
}

MessengerStatisticsMenu::SessionState *MessengerStatisticsMenu::ensureState(
        const QString &sessionId) const
{
    const QString sid = sessionId.trimmed();
    if (sid.isEmpty())
        return nullptr;
    QHash<QString, SessionState>::iterator it = m_stateBySessionId.find(sid);
    if (it == m_stateBySessionId.end()) {
        SessionState created;
        it = m_stateBySessionId.insert(sid, created);
    }
    return &it.value();
}

const MessengerStatisticsMenu::SessionState *MessengerStatisticsMenu::findState(
        const QString &sessionId) const
{
    const QString sid = sessionId.trimmed();
    if (sid.isEmpty())
        return nullptr;
    const auto it = m_stateBySessionId.constFind(sid);
    return it == m_stateBySessionId.cend() ? nullptr : &it.value();
}

bool MessengerStatisticsMenu::ensureSelector(SessionState *state,
                                             MessengerBot *bot,
                                             bool forceRefresh,
                                             const MessengerUserKey &viewerKey) const
{
    if (!state || !bot)
        return false;
    if (state->filter == FilterMode::None)
        return false;

    const bool filterChanged = (state->selector.filter != state->filter);
    if (filterChanged) {
        state->selector.clear();
        state->selector.filter = state->filter;
    }

    if (!forceRefresh && !filterChanged && !state->selector.items.isEmpty()
        && state->selector.updatedAtUtc.isValid()) {
        return true;
    }

    ServerDatabase *database = bot->database();
    if (!database || !database->isReady())
        return false;
    const UserModel *viewerModel = database->userModel();
    const User viewer = viewerModel ? viewerModel->userByKey(viewerKey) : User();
    const bool unrestricted = viewerKey.messenger() == MessengerType::LocalCli
                              || viewer.isFixedAdmin()
                              || database->canManageServerSettings(viewerKey);

    QVector<SelectorItem> items;
    if (state->filter == FilterMode::Resources) {
        const ResourceModel *model = database->resourceModel();
        if (!model)
            return false;

        const QList<Resource> resources = model->items();
        items.reserve(resources.size());
        for (const Resource &resource : resources) {
            if (!resource.isValid())
                continue;
            const bool allowed =
                    unrestricted
                    || (viewer.isAdmin()
                                ? database->canAdministerResource(viewerKey, resource)
                                : MessengerTextRender::isResourceVisibleToViewer(
                                          resource, database, viewerKey.messenger(), viewerKey));
            if (!allowed)
                continue;

            SelectorItem item;
            item.id = resource.uuidString().trimmed();
            item.label = resourceLabelForButton(resource);
            item.value = resource.name().trimmed();
            if (!item.id.isEmpty() && !item.label.trimmed().isEmpty() && !item.value.isEmpty()) {
                items.append(item);
            }
        }
    } else if (state->filter == FilterMode::Users) {
        const UserModel *model = database->userModel();
        if (!model)
            return false;

        const QList<User> users = model->items();
        items.reserve(users.size());
        for (const User &user : users) {
            if (!user.isValid() || !user.key().isValid())
                continue;
            if (!unrestricted
                && (viewer.isAdmin() ? !database->canAdministerUser(viewerKey, user)
                                     : user.key() != viewerKey)) {
                continue;
            }

            SelectorItem item;
            item.id = user.key().toString().trimmed();
            item.label = userLabelForButton(user);
            item.value = item.id;
            if (!item.id.isEmpty() && !item.label.trimmed().isEmpty())
                items.append(item);
        }
    } else {
        return false;
    }

    QCollator collator;
    collator.setNumericMode(true);
    std::sort(items.begin(),
              items.end(),
              [&collator](const SelectorItem &lhs, const SelectorItem &rhs) {
                  return collator.compare(lhs.label, rhs.label) < 0;
              });

    QSet<QString> availableIds;
    availableIds.reserve(items.size());
    for (const SelectorItem &item : std::as_const(items))
        availableIds.insert(item.id);

    QSet<QString> selectedIds;
    for (const QString &selectedId : std::as_const(state->selector.selectedIds)) {
        if (availableIds.contains(selectedId))
            selectedIds.insert(selectedId);
    }

    if (selectedIds.isEmpty()) {
        if (state->filter == FilterMode::Resources) {
            if (state->resourcesAllSelected) {
                for (const SelectorItem &item : std::as_const(items))
                    selectedIds.insert(item.id);
            } else {
                QSet<QString> selectedNames;
                for (const QString &name : std::as_const(state->resources)) {
                    const QString trimmed = name.trimmed();
                    if (!trimmed.isEmpty())
                        selectedNames.insert(trimmed);
                }
                for (const SelectorItem &item : std::as_const(items)) {
                    if (selectedNames.contains(item.value.trimmed()))
                        selectedIds.insert(item.id);
                }
            }
        } else if (state->filter == FilterMode::Users) {
            if (state->usersAllSelected) {
                for (const SelectorItem &item : std::as_const(items))
                    selectedIds.insert(item.id);
            } else {
                QSet<QString> selectedKeys;
                for (const MessengerUserKey &userKey : std::as_const(state->users)) {
                    const QString key = userKey.toString().trimmed();
                    if (!key.isEmpty())
                        selectedKeys.insert(key);
                }
                for (const SelectorItem &item : std::as_const(items)) {
                    if (selectedKeys.contains(item.value.trimmed()))
                        selectedIds.insert(item.id);
                }
            }
        }
    }

    state->selector.items = items;
    state->selector.selectedIds = selectedIds;
    state->selector.pageSize = kDefaultPageSize;
    state->selector.updatedAtUtc = QDateTime::currentDateTimeUtc();
    if (state->selector.items.isEmpty()) {
        state->selector.page = 0;
    } else {
        const int pageSize = qMax(1, state->selector.pageSize);
        const int pageCount = (state->selector.items.size() + pageSize - 1) / pageSize;
        state->selector.page = qBound(0, state->selector.page, pageCount - 1);
    }
    return true;
}

MessengerMenu::MenuPage MessengerStatisticsMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuStatistics);
    page.title = tr("Statistics");

    MessengerMenuManager *mgr = menuManager();
    MessengerBot *bot = mgr ? mgr->bot() : nullptr;
    const QString dateTimeFormat =
            ServerDateTimeFormat::effectiveFormat(bot ? bot->database() : nullptr);
    const MessengerUserKey viewerKey = AbstractBot::senderKey(context.chat, context.sender);
    const UserTimeZone viewerTimeZone =
            bot ? bot->effectiveTimeZoneForUser(viewerKey) : UserTimeZone();
    const AbstractBot *backendBot = bot ? bot->backendBot() : nullptr;
    const bool useHtmlSummary =
            MessengerTextRender::shouldUseHtmlLists(context.chat.messenger, backendBot);
    const UserModel *userModel = (bot && bot->database() && bot->database()->isReady())
                                         ? bot->database()->userModel()
                                         : nullptr;
    const ResourceModel *resourceModel = (bot && bot->database() && bot->database()->isReady())
                                                 ? bot->database()->resourceModel()
                                                 : nullptr;
    if (!mgr || !bot) {
        page.body = tr("Menu session is not available right now.");
        return page;
    }

    QString accessError;
    if (!canUseStatistics(bot, context, &accessError)) {
        page.body = accessError;
        page.rows.append({ mgr->makeNavigateButton(tr("Back to main"),
                                                   QLatin1String(MessengerMenuIds::kMenuMain),
                                                   false) });
        return page;
    }

    const QString sid = mgr->sessionId(context);
    SessionState *state = ensureState(sid);
    if (!state) {
        page.body = tr("Menu session is not available right now.");
        return page;
    }

    const QString currentMenuId = mgr->currentMenuId(context);
    if (currentMenuId.trimmed() != page.id) {
        state->reset();
    }

    mgr->setCurrentMenuId(context, page.id, false);

    ServerDatabase *database = bot->database();
    const User viewer = userModel ? userModel->userByKey(viewerKey) : User();
    const bool unrestricted = viewerKey.messenger() == MessengerType::LocalCli
                              || viewer.isFixedAdmin()
                              || (database && database->canManageServerSettings(viewerKey));
    if (!state->resourcesAllSelected && resourceModel && database) {
        state->resources.erase(
                std::remove_if(state->resources.begin(),
                               state->resources.end(),
                               [database, resourceModel, viewer, viewerKey, unrestricted](
                                       const QString &name) {
                                   const Resource resource = resourceModel->resourceByName(name);
                                   if (!resource.isValid())
                                       return true;
                                   if (unrestricted)
                                       return false;
                                   if (viewer.isAdmin())
                                       return !database->canAdministerResource(viewerKey, resource);
                                   return !MessengerTextRender::isResourceVisibleToViewer(
                                           resource, database, viewerKey.messenger(), viewerKey);
                               }),
                state->resources.end());
    }
    if (!state->usersAllSelected && userModel && database) {
        state->users.erase(std::remove_if(state->users.begin(),
                                          state->users.end(),
                                          [database, userModel, viewer, viewerKey, unrestricted](
                                                  const MessengerUserKey &key) {
                                              const User target = userModel->userByKey(key);
                                              if (!target.isValid())
                                                  return true;
                                              if (unrestricted)
                                                  return false;
                                              if (viewer.isAdmin())
                                                  return !database->canAdministerUser(viewerKey,
                                                                                      target);
                                              return key != viewerKey;
                                          }),
                           state->users.end());
    }

    switch (state->step) {
    case FlowStep::ChooseFilter: {
        QStringList bodyLines;
        if (!state->statusText.trimmed().isEmpty())
            bodyLines << state->statusText.trimmed();
        const QString resourcesSelectionText =
                state->resourcesAllSelected
                        ? tr("all")
                        : (state->resources.isEmpty() ? tr("none")
                                                      : QString::number(state->resources.size()));
        const QString usersSelectionText =
                state->usersAllSelected
                        ? tr("all")
                        : (state->users.isEmpty() ? tr("none")
                                                  : QString::number(state->users.size()));
        bodyLines << tr("Select filters.");
        bodyLines << tr("Report: %1").arg(reportKindTitle(state->reportKind));
        bodyLines << tr("Resources selected: %1").arg(resourcesSelectionText);
        bodyLines << tr("Users selected: %1").arg(usersSelectionText);
        page.body = bodyLines.join(QStringLiteral("\n")).trimmed();

        page.rows.append({ mgr->makeEventButton(tr("Select resources"),
                                                QStringLiteral("st:resources"),
                                                false) });
        page.rows.append(
                { mgr->makeEventButton(tr("Select users"), QStringLiteral("st:users"), false) });
        page.rows.append(
                { mgr->makeEventButton(tr("Continue"), QStringLiteral("st:continue"), false) });
        page.rows.append({ mgr->makeEventButton(tr("Reset"), QStringLiteral("st:reset"), false) });
        page.rows.append({ mgr->makeNavigateButton(tr("Back to main"),
                                                   QLatin1String(MessengerMenuIds::kMenuMain),
                                                   false) });
        break;
    }
    case FlowStep::EnterTargets: {
        const bool forceRefresh =
                mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Active;
        const bool selectorReady =
                ensureSelector(state,
                               bot,
                               forceRefresh,
                               AbstractBot::senderKey(context.chat, context.sender));
        const bool showAsOf =
                mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive;

        QStringList bodyLines;
        if (showAsOf) {
            const QString asOf =
                    MessengerMenuManager::buildAsOfLine(state->selector.updatedAtUtc,
                                                        AbstractBot::MessageFormat::PlainText,
                                                        dateTimeFormat,
                                                        viewerTimeZone);
            if (!asOf.trimmed().isEmpty())
                bodyLines << asOf;
        }

        if (state->filter == FilterMode::Resources) {
            bodyLines << tr("Select a resource");
        } else if (state->filter == FilterMode::Users) {
            bodyLines << tr("Select a user");
        } else {
            bodyLines << tr("Select filters.");
        }
        bodyLines << tr("Selected: %1").arg(QString::number(state->selector.selectedIds.size()));

        if (!selectorReady) {
            bodyLines << tr("Database is not available right now.");
            page.body = bodyLines.join(QStringLiteral("\n")).trimmed();
            page.rows.append(
                    { mgr->makeEventButton(tr("Back"), QStringLiteral("st:back"), false) });
            page.rows.append(
                    { mgr->makeEventButton(tr("Reset"), QStringLiteral("st:reset"), false) });
            page.rows.append({ mgr->makeNavigateButton(tr("Back to main"),
                                                       QLatin1String(MessengerMenuIds::kMenuMain),
                                                       false) });
            break;
        }

        const int total = state->selector.items.size();
        const int pageSize = qMax(1, state->selector.pageSize);
        const int pageCount = (total + pageSize - 1) / pageSize;
        if (pageCount > 0) {
            state->selector.page = qBound(0, state->selector.page, pageCount - 1);
        } else {
            state->selector.page = 0;
        }

        if (total <= 0) {
            if (state->filter == FilterMode::Resources) {
                bodyLines << tr("No resources in database.");
            } else if (state->filter == FilterMode::Users) {
                bodyLines << tr("No users in database.");
            }
        } else {
            bodyLines << tr("Items: %1").arg(QString::number(total));
            if (pageCount > 1) {
                bodyLines << tr("Page: %1/%2")
                                     .arg(QString::number(state->selector.page + 1),
                                          QString::number(pageCount));
            }
        }
        page.body = bodyLines.join(QStringLiteral("\n")).trimmed();

        if (showAsOf) {
            page.rows.append({ mgr->makeEventButton(tr("Refresh list"),
                                                    QStringLiteral("st:refresh"),
                                                    false) });
        }

        if (total > 0) {
            const int start = state->selector.page * pageSize;
            const int end = qMin(total, start + pageSize);
            for (int i = start; i < end; ++i) {
                const SelectorItem &item = state->selector.items.at(i);
                const bool selected = state->selector.selectedIds.contains(item.id);
                const QString prefix = selected ? QStringLiteral("[x] ") : QStringLiteral("[ ] ");
                page.rows.append({ mgr->makeEventButton(prefix + item.label,
                                                        QStringLiteral("st:sel:%1").arg(i),
                                                        false) });
            }

            QVector<MessengerMenu::MenuButton> navRow;
            if (pageCount > 1 && state->selector.page > 0) {
                navRow.append(mgr->makeEventButton(tr("Previous page"),
                                                   QStringLiteral("st:page:prev"),
                                                   false));
            }
            if (pageCount > 1 && state->selector.page + 1 < pageCount) {
                navRow.append(mgr->makeEventButton(tr("Next page"),
                                                   QStringLiteral("st:page:next"),
                                                   false));
            }
            if (!navRow.isEmpty())
                page.rows.append(navRow);

            QVector<MessengerMenu::MenuButton> actionRow;
            if (!state->selector.selectedIds.isEmpty()) {
                actionRow.append(mgr->makeEventButton(tr("Accept"),
                                                      QStringLiteral("st:targets:accept"),
                                                      false));
                actionRow.append(mgr->makeEventButton(tr("Clear selection"),
                                                      QStringLiteral("st:targets:clear"),
                                                      false));
            }
            if (!actionRow.isEmpty())
                page.rows.append(actionRow);
        }

        page.rows.append({ mgr->makeEventButton(state->filter == FilterMode::Resources
                                                        ? tr("Use all resources")
                                                        : tr("Use all users"),
                                                QStringLiteral("st:targets:all"),
                                                false) });

        page.rows.append({ mgr->makeEventButton(tr("Back"), QStringLiteral("st:back"), false) });
        page.rows.append({ mgr->makeEventButton(tr("Reset"), QStringLiteral("st:reset"), false) });
        page.rows.append({ mgr->makeNavigateButton(tr("Back to main"),
                                                   QLatin1String(MessengerMenuIds::kMenuMain),
                                                   false) });
        break;
    }
    case FlowStep::EnterStartUtc: {
        QStringList lines;
        lines << tr("Send start time (%1).").arg(dateTimeFormat);
        lines << tr("Or use quick range buttons.");
        page.body = lines.join(QStringLiteral("\n")).trimmed();
        page.rows.append(
                { mgr->makeEventButton(tr("Last hour"), QStringLiteral("st:range:last_1h"), false),
                  mgr->makeEventButton(tr("Last 24 hours"),
                                       QStringLiteral("st:range:last_24h"),
                                       false) });
        page.rows.append(
                { mgr->makeEventButton(tr("Last 7 days"),
                                       QStringLiteral("st:range:last_7d"),
                                       false),
                  mgr->makeEventButton(tr("Today"), QStringLiteral("st:range:today"), false) });
        page.rows.append({ mgr->makeEventButton(tr("Current month"),
                                                QStringLiteral("st:range:month"),
                                                false),
                           mgr->makeEventButton(tr("Set start = now"),
                                                QStringLiteral("st:start:now"),
                                                false) });
        page.rows.append({ mgr->makeEventButton(tr("Back"), QStringLiteral("st:back"), false) });
        page.rows.append({ mgr->makeEventButton(tr("Reset"), QStringLiteral("st:reset"), false) });
        page.rows.append({ mgr->makeNavigateButton(tr("Back to main"),
                                                   QLatin1String(MessengerMenuIds::kMenuMain),
                                                   false) });
        break;
    }
    case FlowStep::EnterEndUtc: {
        QStringList lines;
        lines << tr("Send end time (%1).").arg(dateTimeFormat);
        lines << tr("Start: %1")
                         .arg(formatTimeStamp(state->startUtc, dateTimeFormat, viewerTimeZone));
        lines << tr("Or use quick end buttons.");
        page.body = lines.join(QStringLiteral("\n")).trimmed();
        page.rows.append(
                { mgr->makeEventButton(tr("End = now"), QStringLiteral("st:end:now"), false),
                  mgr->makeEventButton(tr("Start + 1 hour"),
                                       QStringLiteral("st:end:plus_1h"),
                                       false) });
        page.rows.append({ mgr->makeEventButton(tr("Start + 24 hours"),
                                                QStringLiteral("st:end:plus_24h"),
                                                false),
                           mgr->makeEventButton(tr("Start + 7 days"),
                                                QStringLiteral("st:end:plus_7d"),
                                                false) });
        page.rows.append({ mgr->makeEventButton(tr("Back"), QStringLiteral("st:back"), false) });
        page.rows.append({ mgr->makeEventButton(tr("Reset"), QStringLiteral("st:reset"), false) });
        page.rows.append({ mgr->makeNavigateButton(tr("Back to main"),
                                                   QLatin1String(MessengerMenuIds::kMenuMain),
                                                   false) });
        break;
    }
    case FlowStep::ChooseReport: {
        QStringList summary;
        const auto appendSummaryLine = [&summary, useHtmlSummary](const QString &plainTextLine) {
            if (useHtmlSummary) {
                summary << MessengerMenuManager::escapeHtml(plainTextLine);
            } else {
                summary << plainTextLine;
            }
        };
        if (state->resourcesAllSelected) {
            appendSummaryLine(tr("Resources: all"));
        } else if (!state->resources.isEmpty()) {
            if (useHtmlSummary) {
                QStringList escapedResources;
                escapedResources.reserve(state->resources.size());
                for (const QString &resourceName : std::as_const(state->resources)) {
                    const QString label =
                            resourceLabelForSummary(resourceName, resourceModel, useHtmlSummary);
                    if (!label.trimmed().isEmpty())
                        escapedResources << label;
                }
                summary << QStringLiteral("%1 %2").arg(MessengerMenuManager::escapeHtml(
                                                               tr("Resources:")),
                                                       escapedResources.join(QStringLiteral(", ")));
            } else {
                QStringList labels;
                labels.reserve(state->resources.size());
                for (const QString &resourceName : std::as_const(state->resources)) {
                    const QString label =
                            resourceLabelForSummary(resourceName, resourceModel, useHtmlSummary);
                    if (!label.trimmed().isEmpty())
                        labels << label;
                }
                summary << tr("Resources: %1").arg(labels.join(QStringLiteral(", ")));
            }
        } else {
            appendSummaryLine(tr("Resources: none"));
        }
        if (state->usersAllSelected) {
            appendSummaryLine(tr("Users: all"));
        } else if (!state->users.isEmpty()) {
            QStringList keys;
            keys.reserve(state->users.size());
            for (const MessengerUserKey &key : std::as_const(state->users))
                keys << userLabelForSummary(key, userModel, useHtmlSummary);
            if (useHtmlSummary) {
                summary << QStringLiteral("%1 %2").arg(MessengerMenuManager::escapeHtml(
                                                               tr("Users:")),
                                                       keys.join(QStringLiteral(", ")));
            } else {
                summary << tr("Users: %1").arg(keys.join(QStringLiteral(", ")));
            }
        } else {
            appendSummaryLine(tr("Users: none"));
        }
        appendSummaryLine(
                tr("Range: %1 -> %2")
                        .arg(formatTimeStamp(state->startUtc, dateTimeFormat, viewerTimeZone),
                             formatTimeStamp(state->endUtc, dateTimeFormat, viewerTimeZone)));
        appendSummaryLine(tr("Current report: %1").arg(reportKindTitle(state->reportKind)));

        page.body = tr("Select report kind.");
        if (!summary.isEmpty()) {
            const QString summaryText = summary.join(QStringLiteral("\n")).trimmed();
            const AbstractBot::MessageFormat summaryFormat =
                    useHtmlSummary ? AbstractBot::MessageFormat::Html
                                   : AbstractBot::MessageFormat::PlainText;
            page.dynamicText = [summaryText, summaryFormat](const MessengerMenu::RenderContext &) {
                MessengerMenu::MenuPage::DynamicText out;
                out.text = summaryText;
                out.format = summaryFormat;
                return out;
            };
        }

        page.rows.append({ mgr->makeEventButton(tr("Utilization summary"),
                                                QStringLiteral("st:report:%1")
                                                        .arg(ServerStats::reportKindToken(
                                                                ReportKind::UtilizationSummary)),
                                                false) });
        page.rows.append({ mgr->makeEventButton(tr("User activity summary"),
                                                QStringLiteral("st:report:%1")
                                                        .arg(ServerStats::reportKindToken(
                                                                ReportKind::UserActivitySummary)),
                                                false) });
        page.rows.append({ mgr->makeEventButton(tr("Top resources"),
                                                QStringLiteral("st:report:%1")
                                                        .arg(ServerStats::reportKindToken(
                                                                ReportKind::TopResources)),
                                                false) });
        page.rows.append({ mgr->makeEventButton(tr("Peak hours"),
                                                QStringLiteral("st:report:%1")
                                                        .arg(ServerStats::reportKindToken(
                                                                ReportKind::PeakHours)),
                                                false) });
        page.rows.append({ mgr->makeEventButton(tr("Scheduled vs actual"),
                                                QStringLiteral("st:report:%1")
                                                        .arg(ServerStats::reportKindToken(
                                                                ReportKind::ScheduledVsActual)),
                                                false) });
        page.rows.append({ mgr->makeEventButton(tr("Weekday pattern"),
                                                QStringLiteral("st:report:%1")
                                                        .arg(ServerStats::reportKindToken(
                                                                ReportKind::WeekdayPattern)),
                                                false) });
        page.rows.append(
                { mgr->makeEventButton(tr("Session duration distribution"),
                                       QStringLiteral("st:report:%1")
                                               .arg(ServerStats::reportKindToken(
                                                       ReportKind::SessionDurationDistribution)),
                                       false) });
        page.rows.append({ mgr->makeEventButton(tr("Initiator analysis"),
                                                QStringLiteral("st:report:%1")
                                                        .arg(ServerStats::reportKindToken(
                                                                ReportKind::InitiatorAnalysis)),
                                                false) });
        page.rows.append({ mgr->makeEventButton(tr("Events log"),
                                                QStringLiteral("st:report:%1")
                                                        .arg(ServerStats::reportKindToken(
                                                                ReportKind::EventsLog)),
                                                false) });
        page.rows.append({ mgr->makeEventButton(tr("Back"), QStringLiteral("st:back"), false) });
        page.rows.append({ mgr->makeEventButton(tr("Reset"), QStringLiteral("st:reset"), false) });
        page.rows.append({ mgr->makeNavigateButton(tr("Back to main"),
                                                   QLatin1String(MessengerMenuIds::kMenuMain),
                                                   false) });
        break;
    }
    case FlowStep::ChooseFormat: {
        QStringList summary;
        const auto appendSummaryLine = [&summary, useHtmlSummary](const QString &plainTextLine) {
            if (useHtmlSummary) {
                summary << MessengerMenuManager::escapeHtml(plainTextLine);
            } else {
                summary << plainTextLine;
            }
        };
        appendSummaryLine(tr("Report: %1").arg(reportKindTitle(state->reportKind)));
        if (state->resourcesAllSelected) {
            appendSummaryLine(tr("Resources: all"));
        } else if (!state->resources.isEmpty()) {
            if (useHtmlSummary) {
                QStringList escapedResources;
                escapedResources.reserve(state->resources.size());
                for (const QString &resourceName : std::as_const(state->resources)) {
                    const QString label =
                            resourceLabelForSummary(resourceName, resourceModel, useHtmlSummary);
                    if (!label.trimmed().isEmpty())
                        escapedResources << label;
                }
                summary << QStringLiteral("%1 %2").arg(MessengerMenuManager::escapeHtml(
                                                               tr("Resources:")),
                                                       escapedResources.join(QStringLiteral(", ")));
            } else {
                QStringList labels;
                labels.reserve(state->resources.size());
                for (const QString &resourceName : std::as_const(state->resources)) {
                    const QString label =
                            resourceLabelForSummary(resourceName, resourceModel, useHtmlSummary);
                    if (!label.trimmed().isEmpty())
                        labels << label;
                }
                summary << tr("Resources: %1").arg(labels.join(QStringLiteral(", ")));
            }
        } else {
            appendSummaryLine(tr("Resources: none"));
        }
        if (state->usersAllSelected) {
            appendSummaryLine(tr("Users: all"));
        } else if (!state->users.isEmpty()) {
            QStringList keys;
            keys.reserve(state->users.size());
            for (const MessengerUserKey &key : std::as_const(state->users))
                keys << userLabelForSummary(key, userModel, useHtmlSummary);
            if (useHtmlSummary) {
                summary << QStringLiteral("%1 %2").arg(MessengerMenuManager::escapeHtml(
                                                               tr("Users:")),
                                                       keys.join(QStringLiteral(", ")));
            } else {
                summary << tr("Users: %1").arg(keys.join(QStringLiteral(", ")));
            }
        } else {
            appendSummaryLine(tr("Users: none"));
        }
        appendSummaryLine(
                tr("Range: %1 -> %2")
                        .arg(formatTimeStamp(state->startUtc, dateTimeFormat, viewerTimeZone),
                             formatTimeStamp(state->endUtc, dateTimeFormat, viewerTimeZone)));
        QStringList selectedFormatTokens;
        if (state->formatCsvSelected)
            selectedFormatTokens << tr("CSV");
        if (state->formatTableSelected)
            selectedFormatTokens << tr("Table");
        appendSummaryLine(selectedFormatTokens.isEmpty()
                                  ? tr("none")
                                  : selectedFormatTokens.join(QStringLiteral(", ")));
        page.body = tr("Select output format.");
        if (!summary.isEmpty()) {
            const QString summaryText = summary.join(QStringLiteral("\n")).trimmed();
            const AbstractBot::MessageFormat summaryFormat =
                    useHtmlSummary ? AbstractBot::MessageFormat::Html
                                   : AbstractBot::MessageFormat::PlainText;
            page.dynamicText = [summaryText, summaryFormat](const MessengerMenu::RenderContext &) {
                MessengerMenu::MenuPage::DynamicText out;
                out.text = summaryText;
                out.format = summaryFormat;
                return out;
            };
        }
        const QString csvPrefix =
                state->formatCsvSelected ? QStringLiteral("[x] ") : QStringLiteral("[ ] ");
        const QString tablePrefix =
                state->formatTableSelected ? QStringLiteral("[x] ") : QStringLiteral("[ ] ");
        page.rows.append({ mgr->makeEventButton(csvPrefix + tr("CSV"),
                                                QStringLiteral("st:format:csv"),
                                                false),
                           mgr->makeEventButton(tablePrefix + tr("Table"),
                                                QStringLiteral("st:format:table"),
                                                false) });
        page.rows.append({ mgr->makeEventButton(tr("all"), QStringLiteral("st:format:all"), false),
                           mgr->makeEventButton(tr("Clear selection"),
                                                QStringLiteral("st:format:clear"),
                                                false) });
        page.rows.append({ mgr->makeEventButton(tr("Continue"), QStringLiteral("st:run"), false) });
        page.rows.append({ mgr->makeEventButton(tr("Back"), QStringLiteral("st:back"), false) });
        page.rows.append({ mgr->makeEventButton(tr("Reset"), QStringLiteral("st:reset"), false) });
        page.rows.append({ mgr->makeNavigateButton(tr("Back to main"),
                                                   QLatin1String(MessengerMenuIds::kMenuMain),
                                                   false) });
        break;
    }
    case FlowStep::Running:
        page.body = state->statusText.trimmed().isEmpty() ? tr("Preparing statistics report...")
                                                          : state->statusText.trimmed();
        page.rows.append(
                { mgr->makeEventButton(tr("New report"), QStringLiteral("st:reset"), false) });
        page.rows.append({ mgr->makeNavigateButton(tr("Back to main"),
                                                   QLatin1String(MessengerMenuIds::kMenuMain),
                                                   false) });
        break;
    }

    if (!state->errorText.trimmed().isEmpty()) {
        MessengerMenuManager::prependErrorBlock(&page, state->errorText);
    }

    return page;
}

bool MessengerStatisticsMenu::handleEvent(const QString &event,
                                          const QString &sessionId,
                                          const MessengerMenu::RenderContext &context,
                                          MessengerMenuManager::ActionResult *result) const
{
    if (!result)
        return false;

    const QString trimmed = event.trimmed();
    if (!trimmed.startsWith(QStringLiteral("st:")))
        return false;

    SessionState *state = ensureState(sessionId);
    if (!state)
        return false;

    const QStringList parts = trimmed.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    if (parts.size() < 2)
        return false;

    const QString action = parts.value(1);
    auto markHandled = [result]() {
        result->handled = true;
        result->sendReply = true;
    };

    MessengerMenuManager *mgr = menuManager();
    MessengerBot *bot = mgr ? mgr->bot() : nullptr;
    const QString dateTimeFormat =
            ServerDateTimeFormat::effectiveFormat(bot ? bot->database() : nullptr);
    const MessengerUserKey viewerKey = AbstractBot::senderKey(context.chat, context.sender);
    const UserTimeZone viewerTimeZone =
            bot ? bot->effectiveTimeZoneForUser(viewerKey) : UserTimeZone();
    QString accessError;
    if (!canUseStatistics(bot, context, &accessError)) {
        state->errorText = accessError;
        markHandled();
        return true;
    }

    const auto ensureTargetSelectionIsValid = [state](QString *errorOut) {
        const bool resourcesValid = state->resourcesAllSelected || !state->resources.isEmpty();
        const bool usersValid = state->usersAllSelected || !state->users.isEmpty();
        if (resourcesValid && usersValid) {
            if (errorOut)
                errorOut->clear();
            return true;
        }

        QString errorText;
        if (!resourcesValid && !usersValid) {
            errorText = MessengerStatisticsMenu::tr("Select at least one resource and one user.");
        } else if (!resourcesValid) {
            errorText = MessengerStatisticsMenu::tr("Select at least one resource.");
        } else {
            errorText = MessengerStatisticsMenu::tr("Select at least one user.");
        }
        if (errorOut)
            *errorOut = errorText;
        return false;
    };

    if (action == QStringLiteral("reset")) {
        state->reset();
        markHandled();
        return true;
    }

    if (action == QStringLiteral("back")) {
        state->errorText.clear();
        switch (state->step) {
        case FlowStep::EnterTargets:
            state->step = FlowStep::ChooseFilter;
            break;
        case FlowStep::EnterStartUtc:
            state->step = FlowStep::ChooseFilter;
            break;
        case FlowStep::EnterEndUtc:
            state->step = FlowStep::EnterStartUtc;
            break;
        case FlowStep::ChooseReport:
            state->step = FlowStep::EnterEndUtc;
            break;
        case FlowStep::ChooseFormat:
            state->step = FlowStep::ChooseReport;
            break;
        case FlowStep::Running:
        case FlowStep::ChooseFilter:
            break;
        }
        markHandled();
        return true;
    }

    if (action == QStringLiteral("resources")) {
        state->filter = FilterMode::Resources;
        state->selector.clear();
        state->startUtc = QDateTime();
        state->endUtc = QDateTime();
        state->step = FlowStep::EnterTargets;
        state->errorText.clear();
        markHandled();
        return true;
    }

    if (action == QStringLiteral("users")) {
        state->filter = FilterMode::Users;
        state->selector.clear();
        state->startUtc = QDateTime();
        state->endUtc = QDateTime();
        state->step = FlowStep::EnterTargets;
        state->errorText.clear();
        markHandled();
        return true;
    }

    if (action == QStringLiteral("continue")) {
        if (state->step != FlowStep::ChooseFilter) {
            markHandled();
            return true;
        }
        if (!ensureTargetSelectionIsValid(&state->errorText)) {
            markHandled();
            return true;
        }
        state->startUtc = QDateTime();
        state->endUtc = QDateTime();
        state->step = FlowStep::EnterStartUtc;
        state->errorText.clear();
        markHandled();
        return true;
    }

    if (action == QStringLiteral("range")) {
        if (state->step != FlowStep::EnterStartUtc) {
            markHandled();
            return true;
        }

        QDateTime startUtc;
        QDateTime endUtc;
        if (!resolveQuickRangePreset(parts.value(2), viewerTimeZone, &startUtc, &endUtc)
            || !startUtc.isValid() || !endUtc.isValid() || startUtc > endUtc) {
            state->errorText = tr("Unknown quick range.");
            markHandled();
            return true;
        }

        state->startUtc = startUtc;
        state->endUtc = endUtc;
        state->step = FlowStep::ChooseReport;
        state->errorText.clear();
        markHandled();
        return true;
    }

    if (action == QStringLiteral("start")) {
        if (state->step != FlowStep::EnterStartUtc) {
            markHandled();
            return true;
        }

        const QString token = parts.value(2).trimmed().toLower();
        if (token != QStringLiteral("now")) {
            state->errorText = tr("Unknown start preset.");
            markHandled();
            return true;
        }

        const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
        if (!nowUtc.isValid()) {
            state->errorText = MessengerUserFacingErrors::invalidStartTime(dateTimeFormat);
            markHandled();
            return true;
        }

        state->startUtc = nowUtc;
        state->endUtc = QDateTime();
        state->step = FlowStep::EnterEndUtc;
        state->errorText.clear();
        markHandled();
        return true;
    }

    if (action == QStringLiteral("end")) {
        if (state->step != FlowStep::EnterEndUtc) {
            markHandled();
            return true;
        }

        if (!state->startUtc.isValid()) {
            state->errorText = MessengerUserFacingErrors::invalidStartTime(dateTimeFormat);
            markHandled();
            return true;
        }

        QDateTime endUtc;
        if (!resolveQuickEndPreset(parts.value(2), viewerTimeZone, state->startUtc, &endUtc)
            || !endUtc.isValid()) {
            state->errorText = tr("Unknown end preset.");
            markHandled();
            return true;
        }
        if (state->startUtc > endUtc) {
            state->errorText = MessengerUserFacingErrors::endTimeMustBeAfterStartTime();
            markHandled();
            return true;
        }

        state->endUtc = endUtc;
        state->step = FlowStep::ChooseReport;
        state->errorText.clear();
        markHandled();
        return true;
    }

    if (action == QStringLiteral("report")) {
        if (state->step != FlowStep::ChooseReport) {
            markHandled();
            return true;
        }

        const QString reportToken = parts.value(2).trimmed();
        ReportKind parsedKind = ReportKind::UtilizationSummary;
        if (reportToken.isEmpty() || !ServerStats::reportKindFromToken(reportToken, &parsedKind)) {
            state->errorText = MessengerUserFacingErrors::invalidReportKind(
                    reportToken, ServerStats::reportKindUsageText());
            markHandled();
            return true;
        }

        state->reportKind = parsedKind;
        state->step = FlowStep::ChooseFormat;
        state->errorText.clear();
        markHandled();
        return true;
    }

    if (state->step == FlowStep::EnterTargets) {
        MessengerMenuManager *mgr = menuManager();
        MessengerBot *bot = mgr ? mgr->bot() : nullptr;

        if (action == QStringLiteral("refresh")) {
            if (!ensureSelector(state,
                                bot,
                                true,
                                AbstractBot::senderKey(context.chat, context.sender))) {
                state->errorText = tr("Database is not available right now.");
            } else {
                state->errorText.clear();
            }
            markHandled();
            return true;
        }

        if ((action == QStringLiteral("page") || action == QStringLiteral("sel")
             || action == QStringLiteral("targets"))
            && !ensureSelector(state,
                               bot,
                               false,
                               AbstractBot::senderKey(context.chat, context.sender))) {
            state->errorText = tr("Database is not available right now.");
            markHandled();
            return true;
        }

        if (action == QStringLiteral("page")) {
            const QString direction = parts.value(2).trimmed();
            if (direction == QStringLiteral("prev")) {
                if (state->selector.page > 0)
                    state->selector.page -= 1;
            } else if (direction == QStringLiteral("next")) {
                const int total = state->selector.items.size();
                const int pageSize = qMax(1, state->selector.pageSize);
                const int pageCount = (total + pageSize - 1) / pageSize;
                if (state->selector.page + 1 < pageCount)
                    state->selector.page += 1;
            }
            state->errorText.clear();
            markHandled();
            return true;
        }

        if (action == QStringLiteral("sel")) {
            bool ok = false;
            const int index = parts.value(2).toInt(&ok);
            if (!ok || index < 0 || index >= state->selector.items.size()) {
                markHandled();
                return true;
            }

            const SelectorItem &item = state->selector.items.at(index);
            if (state->selector.selectedIds.contains(item.id)) {
                state->selector.selectedIds.remove(item.id);
            } else if (!item.id.trimmed().isEmpty()) {
                state->selector.selectedIds.insert(item.id.trimmed());
            }

            state->errorText.clear();
            markHandled();
            return true;
        }

        if (action == QStringLiteral("targets")) {
            const QString targetAction = parts.value(2).trimmed();
            if (targetAction == QStringLiteral("all")) {
                state->selector.selectedIds.clear();
                if (state->filter == FilterMode::Resources) {
                    state->resourcesAllSelected = true;
                    state->resources.clear();
                } else if (state->filter == FilterMode::Users) {
                    state->usersAllSelected = true;
                    state->users.clear();
                } else {
                    state->errorText = tr("Select filters.");
                    markHandled();
                    return true;
                }
                state->startUtc = QDateTime();
                state->endUtc = QDateTime();
                state->step = FlowStep::ChooseFilter;
                state->errorText.clear();
                markHandled();
                return true;
            }
            if (targetAction == QStringLiteral("clear")) {
                state->selector.selectedIds.clear();
                if (state->filter == FilterMode::Resources) {
                    state->resourcesAllSelected = false;
                    state->resources.clear();
                } else if (state->filter == FilterMode::Users) {
                    state->usersAllSelected = false;
                    state->users.clear();
                } else {
                    state->resourcesAllSelected = false;
                    state->usersAllSelected = false;
                    state->resources.clear();
                    state->users.clear();
                }
                state->errorText.clear();
                markHandled();
                return true;
            }

            if (targetAction == QStringLiteral("accept")) {
                if (state->selector.selectedIds.isEmpty()) {
                    if (state->filter == FilterMode::Users) {
                        state->usersAllSelected = false;
                        state->users.clear();
                        state->errorText = tr("User list is empty.");
                    } else {
                        state->resourcesAllSelected = false;
                        state->resources.clear();
                        state->errorText = tr("Resource list is empty.");
                    }
                    markHandled();
                    return true;
                }

                const auto areAllCurrentTargetsSelected = [state]() {
                    if (state->selector.items.isEmpty())
                        return false;
                    for (const SelectorItem &item : std::as_const(state->selector.items)) {
                        if (!state->selector.selectedIds.contains(item.id))
                            return false;
                    }
                    return true;
                };
                const bool allCurrentTargetsSelected = areAllCurrentTargetsSelected();

                if (state->filter == FilterMode::Resources) {
                    if (allCurrentTargetsSelected) {
                        state->resourcesAllSelected = true;
                        state->resources.clear();
                        state->startUtc = QDateTime();
                        state->endUtc = QDateTime();
                        state->step = FlowStep::ChooseFilter;
                        state->errorText.clear();
                        markHandled();
                        return true;
                    }

                    QStringList selectedResources;
                    QSet<QString> seenResources;
                    for (const SelectorItem &item : std::as_const(state->selector.items)) {
                        if (!state->selector.selectedIds.contains(item.id))
                            continue;
                        const QString resourceName = item.value.trimmed();
                        if (resourceName.isEmpty() || seenResources.contains(resourceName))
                            continue;
                        seenResources.insert(resourceName);
                        selectedResources.append(resourceName);
                    }

                    if (selectedResources.isEmpty()) {
                        state->errorText = tr("Resource list is empty.");
                        markHandled();
                        return true;
                    }

                    state->resources = selectedResources;
                    state->resourcesAllSelected = false;
                    state->startUtc = QDateTime();
                    state->endUtc = QDateTime();
                    state->step = FlowStep::ChooseFilter;
                    state->errorText.clear();
                    markHandled();
                    return true;
                }

                if (state->filter == FilterMode::Users) {
                    if (allCurrentTargetsSelected) {
                        state->usersAllSelected = true;
                        state->users.clear();
                        state->startUtc = QDateTime();
                        state->endUtc = QDateTime();
                        state->step = FlowStep::ChooseFilter;
                        state->errorText.clear();
                        markHandled();
                        return true;
                    }

                    QList<MessengerUserKey> selectedUsers;
                    QSet<QString> seenUsers;
                    for (const SelectorItem &item : std::as_const(state->selector.items)) {
                        if (!state->selector.selectedIds.contains(item.id))
                            continue;
                        const QString token = item.value.trimmed();
                        if (token.isEmpty() || seenUsers.contains(token))
                            continue;

                        bool ok = false;
                        const MessengerUserKey key = MessengerUserKey::fromString(token, &ok);
                        if (!ok || !key.isValid())
                            continue;

                        seenUsers.insert(token);
                        selectedUsers.append(key);
                    }

                    if (selectedUsers.isEmpty()) {
                        state->errorText = tr("User list is empty.");
                        markHandled();
                        return true;
                    }

                    state->users = selectedUsers;
                    state->usersAllSelected = false;
                    state->startUtc = QDateTime();
                    state->endUtc = QDateTime();
                    state->step = FlowStep::ChooseFilter;
                    state->errorText.clear();
                    markHandled();
                    return true;
                }

                state->errorText = tr("Select filters.");
                markHandled();
                return true;
            }
        }
    }

    if (action == QStringLiteral("format")) {
        if (state->step != FlowStep::ChooseFormat) {
            markHandled();
            return true;
        }

        const QString formatToken = parts.value(2).trimmed().toLower();
        if (formatToken == QStringLiteral("csv")) {
            state->formatCsvSelected = !state->formatCsvSelected;
        } else if (formatToken == QStringLiteral("table")) {
            state->formatTableSelected = !state->formatTableSelected;
        } else if (formatToken == QStringLiteral("all")) {
            state->formatCsvSelected = true;
            state->formatTableSelected = true;
        } else if (formatToken == QStringLiteral("clear")
                   || formatToken == QStringLiteral("none")) {
            state->formatCsvSelected = false;
            state->formatTableSelected = false;
        } else {
            state->errorText = MessengerUserFacingErrors::invalidFormat(
                    formatToken, MessengerUserFacingErrors::statisticsFormatExpected());
            markHandled();
            return true;
        }

        state->errorText.clear();
        markHandled();
        return true;
    }

    if (action == QStringLiteral("run")) {
        if (state->step != FlowStep::ChooseFormat) {
            markHandled();
            return true;
        }

        if (!state->formatCsvSelected && !state->formatTableSelected) {
            state->errorText = tr("Select output format.");
            markHandled();
            return true;
        }

        if (!ensureTargetSelectionIsValid(&state->errorText)) {
            markHandled();
            return true;
        }

        if (!state->startUtc.isValid() || !state->endUtc.isValid()) {
            state->errorText = tr("Invalid time range.");
            markHandled();
            return true;
        }
        MessengerMenuManager *mgr = menuManager();
        MessengerBot *bot = mgr ? mgr->bot() : nullptr;
        if (!bot || !bot->database() || !bot->database()->isReady()) {
            state->errorText = tr("Database is not available right now.");
            markHandled();
            return true;
        }

        ServerStats *stats = bot->database()->stats();
        if (!stats) {
            state->errorText = MessengerUserFacingErrors::statisticsNotAvailable();
            markHandled();
            return true;
        }

        ServerStats::ReportRequest requestBase;
        requestBase.startUtc = state->startUtc;
        requestBase.endUtc = state->endUtc;
        requestBase.timeZone = viewerTimeZone;
        requestBase.reportKind = state->reportKind;
        ServerDatabase *database = bot->database();
        const MessengerUserKey actorKey = AbstractBot::senderKey(context.chat, context.sender);
        const UserModel *scopeUserModel = database->userModel();
        const ResourceModel *scopeResourceModel = database->resourceModel();
        const User actorUser = scopeUserModel ? scopeUserModel->userByKey(actorKey) : User();
        const bool unrestricted = actorKey.messenger() == MessengerType::LocalCli
                                  || actorUser.isFixedAdmin()
                                  || database->canManageServerSettings(actorKey);
        ServerStats::configureActorScope(&requestBase, actorUser, unrestricted);

        if (scopeResourceModel) {
            const QList<Resource> resources = scopeResourceModel->items();
            for (const Resource &resource : resources) {
                if (!resource.isValid())
                    continue;
                const bool allowed =
                        unrestricted
                        || (actorUser.isAdmin()
                                    ? database->canAdministerResource(actorKey, resource)
                                    : MessengerTextRender::isResourceVisibleToViewer(
                                              resource, database, actorKey.messenger(), actorKey));
                if (allowed)
                    requestBase.allowedResourceUuids.append(resource.uuidString());
            }
        }
        if (scopeUserModel) {
            const QList<User> users = scopeUserModel->items();
            for (const User &candidate : users) {
                if (!candidate.isValid())
                    continue;
                if (unrestricted
                    || (actorUser.isAdmin() && database->canAdministerUser(actorKey, candidate))
                    || (!actorUser.isAdmin() && candidate.key() == actorKey)) {
                    requestBase.allowedUsers.append(candidate.key());
                }
            }
        }

        if (!state->resourcesAllSelected)
            requestBase.resourceNames = state->resources;
        if (!state->usersAllSelected)
            requestBase.users = state->users;
        requestBase.filter = requestBase.resourceNames.isEmpty()
                                     ? ServerStats::ReportRequest::FilterType::Users
                                     : ServerStats::ReportRequest::FilterType::Resources;
        if (scopeResourceModel) {
            QSet<QString> uuids;
            for (const QString &name : std::as_const(requestBase.resourceNames)) {
                const Resource resource = scopeResourceModel->resourceByName(name);
                const bool allowed =
                        resource.isValid()
                        && (unrestricted
                            || (actorUser.isAdmin()
                                        ? database->canAdministerResource(actorKey, resource)
                                        : MessengerTextRender::isResourceVisibleToViewer(
                                                  resource,
                                                  database,
                                                  actorKey.messenger(),
                                                  actorKey)));
                if (!allowed) {
                    state->errorText = ResourceQueueUserMessages::resourceNotFound(name);
                    markHandled();
                    return true;
                }
                uuids.insert(resource.uuidString());
            }
            requestBase.resourceUuids = QStringList(uuids.cbegin(), uuids.cend());
        }
        for (const MessengerUserKey &key : std::as_const(requestBase.users)) {
            const User target = scopeUserModel ? scopeUserModel->userByKey(key) : User();
            const bool allowed =
                    target.isValid()
                    && (unrestricted
                        || (actorUser.isAdmin() && database->canAdministerUser(actorKey, target))
                        || (!actorUser.isAdmin() && key == actorKey));
            if (!allowed) {
                state->errorText = MessengerUserFacingErrors::userNotFound(key.toString());
                markHandled();
                return true;
            }
        }

        QVector<ServerStats::ReportRequest::OutputFormat> formatsToGenerate;
        if (state->formatTableSelected) {
            formatsToGenerate.append(ServerStats::ReportRequest::OutputFormat::Table);
        }
        if (state->formatCsvSelected) {
            formatsToGenerate.append(ServerStats::ReportRequest::OutputFormat::Csv);
        }
        if (formatsToGenerate.isEmpty()) {
            state->errorText = tr("Select output format.");
            markHandled();
            return true;
        }

        const QString effectiveLocale =
                bot->effectiveLocaleForUser(AbstractBot::senderKey(context.chat, context.sender));
        const AbstractBot::ChatAddress chat = context.chat;
        const MessengerMenu::RenderContext menuContext = context;
        const QString sid = sessionId.trimmed();
        const UserModel *userModel = bot->database()->userModel();
        const ResourceModel *resourceModel = bot->database()->resourceModel();
        const bool useHtmlStatus =
                MessengerTextRender::shouldUseHtmlLists(chat.messenger, bot->backendBot());
        QPointer<MessengerBot> botPtr = bot;
        QPointer<ServerStats> statsPtr = stats;
        QPointer<MessengerStatisticsMenu> self(const_cast<MessengerStatisticsMenu *>(this));
        const qint64 reportStartedMs = QDateTime::currentMSecsSinceEpoch();

        const auto summarizeTargets = [](const QStringList &values) {
            static constexpr int kMaxShownTargets = 6;
            QStringList normalized;
            normalized.reserve(values.size());
            for (const QString &value : values) {
                const QString trimmed = value.trimmed();
                if (!trimmed.isEmpty())
                    normalized.append(trimmed);
            }
            if (normalized.isEmpty())
                return MessengerStatisticsMenu::tr("none");
            if (normalized.size() <= kMaxShownTargets)
                return normalized.join(QStringLiteral(", "));

            const int hiddenCount = normalized.size() - kMaxShownTargets;
            const QStringList shown = normalized.mid(0, kMaxShownTargets);
            return MessengerStatisticsMenu::tr("%1 (+%2 more)")
                    .arg(shown.join(QStringLiteral(", ")), QString::number(hiddenCount));
        };

        QStringList usersSummary;
        usersSummary.reserve(state->users.size());
        for (const MessengerUserKey &key : std::as_const(state->users)) {
            const QString token = userLabelForSummary(key, userModel, useHtmlStatus);
            if (!token.isEmpty())
                usersSummary.append(token);
        }

        QStringList resourcesSummary;
        resourcesSummary.reserve(state->resources.size());
        for (const QString &resourceName : std::as_const(state->resources)) {
            const QString token =
                    resourceLabelForSummary(resourceName, resourceModel, useHtmlStatus);
            if (!token.trimmed().isEmpty())
                resourcesSummary.append(token);
        }
        QStringList formatLabels;
        if (state->formatTableSelected)
            formatLabels << tr("Table");
        if (state->formatCsvSelected)
            formatLabels << tr("CSV");

        QStringList statusLines;
        if (useHtmlStatus) {
            statusLines << MessengerMenuManager::escapeHtml(tr("Preparing statistics report..."));
            statusLines << MessengerMenuManager::escapeHtml(
                    tr("Report: %1").arg(reportKindTitle(state->reportKind)));
            statusLines << MessengerMenuManager::escapeHtml(
                    tr("Range: %1 -> %2")
                            .arg(formatTimeStamp(state->startUtc, dateTimeFormat, viewerTimeZone),
                                 formatTimeStamp(state->endUtc, dateTimeFormat, viewerTimeZone)));
            statusLines << QStringLiteral("%1 %2")
                                   .arg(MessengerMenuManager::escapeHtml(tr("Resources:")),
                                        state->resourcesAllSelected
                                                ? MessengerMenuManager::escapeHtml(tr("all"))
                                                : summarizeTargets(resourcesSummary));
            statusLines << QStringLiteral("%1 %2")
                                   .arg(MessengerMenuManager::escapeHtml(tr("Users:")),
                                        state->usersAllSelected
                                                ? MessengerMenuManager::escapeHtml(tr("all"))
                                                : summarizeTargets(usersSummary));
            if (!formatLabels.isEmpty()) {
                statusLines << MessengerMenuManager::escapeHtml(
                        formatLabels.join(QStringLiteral(", ")));
            }
        } else {
            statusLines << tr("Preparing statistics report...");
            statusLines << tr("Report: %1").arg(reportKindTitle(state->reportKind));
            statusLines << tr("Range: %1 -> %2")
                                   .arg(formatTimeStamp(state->startUtc,
                                                        dateTimeFormat,
                                                        viewerTimeZone),
                                        formatTimeStamp(state->endUtc,
                                                        dateTimeFormat,
                                                        viewerTimeZone));
            statusLines << tr("Resources: %1")
                                   .arg(state->resourcesAllSelected
                                                ? tr("all")
                                                : summarizeTargets(resourcesSummary));
            statusLines << tr("Users: %1")
                                   .arg(state->usersAllSelected ? tr("all")
                                                                : summarizeTargets(usersSummary));
            if (!formatLabels.isEmpty())
                statusLines << formatLabels.join(QStringLiteral(", "));
        }
        const quint64 reportGeneration = ++state->generation;
        state->statusText = statusLines.join(QStringLiteral("\n")).trimmed();
        state->step = FlowStep::Running;
        state->errorText.clear();
        result->handled = true;
        result->sendReply = false;
        MessengerMenuManager::Reply statusReply;
        statusReply.valid = true;
        statusReply.text = state->statusText;
        statusReply.options.format = useHtmlStatus ? AbstractBot::MessageFormat::Html
                                                   : AbstractBot::MessageFormat::PlainText;
        statusReply.options.inlineKeyboard.clear();
        MessengerMenuManager::requestEditOriginMessage(result,
                                                       statusReply,
                                                       mgr->lastMenuMessageId(context));

        const auto reopenMenuInNewMessage = [botPtr, menuContext, sid, self, reportGeneration]() {
            if (!botPtr)
                return;

            if (self) {
                const SessionState *current = self->findState(sid);
                if (!current || current->generation != reportGeneration)
                    return;
                SessionState *state = self->ensureState(sid);
                state->step = FlowStep::ChooseFilter;
                state->statusText.clear();
                state->errorText.clear();
            }

            MessengerMenuManager *mgr = botPtr->menuManager();
            if (!mgr)
                return;

            MessengerMenu::RenderContext nextContext = menuContext;
            nextContext.isAdmin = botPtr->isAdminUser(
                    AbstractBot::senderKey(nextContext.chat, nextContext.sender));
            nextContext.canManageServerSettings = botPtr->canManageServerSettings(
                    AbstractBot::senderKey(nextContext.chat, nextContext.sender));
            mgr->setCurrentMenuId(nextContext,
                                  QLatin1String(MessengerMenuIds::kMenuStatistics),
                                  false);

            const MessengerMenuManager::Reply menuReply =
                    mgr->renderMenu(QLatin1String(MessengerMenuIds::kMenuStatistics), nextContext);
            if (!menuReply.valid || menuReply.text.trimmed().isEmpty())
                return;

            if (!menuReply.options.inlineKeyboard.isEmpty()) {
                mgr->expectMenuMessageId(nextContext, false);
            }
            botPtr->sendReply(nextContext.chat, menuReply.text, menuReply.options);
        };
        const auto reopenMenuInNewMessageDelayed = [reopenMenuInNewMessage]() {
            QTimer::singleShot(kReportCompletionMenuReopenDelayMs,
                               [reopenMenuInNewMessage]() { reopenMenuInNewMessage(); });
        };

        std::shared_ptr<int> nextFormatIndex = std::make_shared<int>(0);
        std::shared_ptr<bool> hasSuccessfulReports = std::make_shared<bool>(false);
        std::shared_ptr<bool> completionHandled = std::make_shared<bool>(false);
        std::shared_ptr<ConsolidatedReportResult> consolidatedResult =
                std::make_shared<ConsolidatedReportResult>();
        const std::function<bool()> requestStillCurrent = [self, sid, reportGeneration]() {
            if (!self)
                return false;
            const SessionState *current = self->findState(sid);
            return current && current->generation == reportGeneration
                   && current->step == FlowStep::Running;
        };
        const std::function<bool()> scopeStillValid = [botPtr, actorKey, requestBase]() {
            if (!botPtr)
                return false;
            ServerDatabase *database = botPtr->database();
            if (!database || !database->isReady())
                return false;
            const UserModel *users = database->userModel();
            const ResourceModel *resources = database->resourceModel();
            const User actor = users ? users->userByKey(actorKey) : User();
            const bool unrestricted = actorKey.messenger() == MessengerType::LocalCli
                                      || actor.isFixedAdmin()
                                      || database->canManageServerSettings(actorKey);
            if (!statisticsEnabled(database)
                || (statisticsAdminsOnly(database) && !unrestricted && !actor.isAdmin())) {
                return false;
            }
            if (actorKey.messenger() != MessengerType::LocalCli && !actor.isFixedAdmin()
                && database->isBlacklisted(actorKey)) {
                return false;
            }
            return ServerStats::actorScopeStillValid(requestBase,
                                                     actor,
                                                     unrestricted,
                                                     resources,
                                                     users);
        };
        std::shared_ptr<std::function<void()>> runNextFormat =
                std::make_shared<std::function<void()>>();
        const std::weak_ptr<std::function<void()>> weakRunNextFormat = runNextFormat;
        *runNextFormat = [botPtr,
                          statsPtr,
                          requestBase,
                          formatsToGenerate,
                          effectiveLocale,
                          reportStartedMs,
                          reopenMenuInNewMessage,
                          reopenMenuInNewMessageDelayed,
                          nextFormatIndex,
                          hasSuccessfulReports,
                          completionHandled,
                          consolidatedResult,
                          requestStillCurrent,
                          scopeStillValid,
                          weakRunNextFormat,
                          chat]() {
            const std::shared_ptr<std::function<void()>> continuation = weakRunNextFormat.lock();
            if (!continuation)
                return;
            if (!botPtr || !statsPtr)
                return;
            if (*completionHandled || *nextFormatIndex >= formatsToGenerate.size())
                return;
            if (!requestStillCurrent()) {
                *completionHandled = true;
                if (!consolidatedResult->csvFilePath.isEmpty())
                    QFile::remove(consolidatedResult->csvFilePath);
                return;
            }
            if (!scopeStillValid()) {
                *completionHandled = true;
                if (!consolidatedResult->csvFilePath.isEmpty())
                    QFile::remove(consolidatedResult->csvFilePath);
                botPtr->sendReply(chat, MessengerUserFacingErrors::accessDeniedAdminsOnly());
                reopenMenuInNewMessage();
                return;
            }

            ServerStats::ReportRequest nextRequest = requestBase;
            nextRequest.format = formatsToGenerate.at(*nextFormatIndex);
            statsPtr->startReportAsync(
                    nextRequest,
                    botPtr.data(),
                    [botPtr,
                     effectiveLocale,
                     reopenMenuInNewMessage,
                     reopenMenuInNewMessageDelayed,
                     reportStartedMs,
                     nextFormatIndex,
                     hasSuccessfulReports,
                     completionHandled,
                     consolidatedResult,
                     requestStillCurrent,
                     scopeStillValid,
                     runNextFormat = continuation,
                     formatsToGenerate,
                     chat](const ServerStats::ReportResult &r) {
                        if (!botPtr || *completionHandled)
                            return;
                        if (!requestStillCurrent()) {
                            *completionHandled = true;
                            if (!r.filePath.isEmpty())
                                QFile::remove(r.filePath);
                            if (!consolidatedResult->csvFilePath.isEmpty())
                                QFile::remove(consolidatedResult->csvFilePath);
                            return;
                        }
                        if (!scopeStillValid()) {
                            *completionHandled = true;
                            if (!r.filePath.isEmpty())
                                QFile::remove(r.filePath);
                            if (!consolidatedResult->csvFilePath.isEmpty())
                                QFile::remove(consolidatedResult->csvFilePath);
                            botPtr->sendReply(chat,
                                              MessengerUserFacingErrors::accessDeniedAdminsOnly());
                            reopenMenuInNewMessage();
                            return;
                        }
                        BookingResourcesTranslator::ScopedLocaleOverride guard =
                                botPtr->applyScopedLocale(effectiveLocale);
                        if (!collectSingleReportResult(botPtr, chat, r, consolidatedResult.get())) {
                            *completionHandled = true;
                            reopenMenuInNewMessage();
                            return;
                        }
                        *hasSuccessfulReports = true;
                        *nextFormatIndex += 1;
                        if (*nextFormatIndex >= formatsToGenerate.size()) {
                            *completionHandled = true;
                            if (*hasSuccessfulReports) {
                                const qint64 elapsedMs =
                                        QDateTime::currentMSecsSinceEpoch() - reportStartedMs;
                                sendConsolidatedReport(botPtr,
                                                       chat,
                                                       *consolidatedResult,
                                                       elapsedMs);
                                reopenMenuInNewMessageDelayed();
                            } else {
                                reopenMenuInNewMessage();
                            }
                            return;
                        }
                        (*runNextFormat)();
                    });
        };
        (*runNextFormat)();
        return true;
    }

    markHandled();
    return true;
}

bool MessengerStatisticsMenu::handleTextInput(const QString &text,
                                              const QString &sessionId,
                                              const MessengerMenu::RenderContext &context,
                                              MessengerMenuManager::ActionResult *result) const
{
    if (!result)
        return false;

    SessionState *state = ensureState(sessionId);
    if (!state)
        return false;

    const MessengerMenuManager *mgr = menuManager();
    const MessengerBot *bot = mgr ? mgr->bot() : nullptr;
    const MessengerUserKey viewerKey = AbstractBot::senderKey(context.chat, context.sender);
    const UserTimeZone viewerTimeZone =
            bot ? bot->effectiveTimeZoneForUser(viewerKey) : UserTimeZone();
    QString accessError;
    if (!canUseStatistics(bot, context, &accessError)) {
        state->errorText = accessError;
        result->handled = true;
        result->sendReply = true;
        return true;
    }
    const QString dateTimeFormat =
            ServerDateTimeFormat::effectiveFormat(bot ? bot->database() : nullptr);

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        state->errorText = MessengerUserFacingErrors::valueCannotBeEmpty();
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    switch (state->step) {
    case FlowStep::EnterTargets: {
        if (state->filter == FilterMode::Resources) {
            if (isEmptyListToken(trimmed)) {
                state->selector.selectedIds.clear();
                state->selector.updatedAtUtc = QDateTime();
                state->resourcesAllSelected = true;
                state->resources.clear();
                state->startUtc = QDateTime();
                state->endUtc = QDateTime();
                state->step = FlowStep::ChooseFilter;
                state->errorText.clear();
                break;
            }
            const QStringList resources = splitListToken(trimmed);
            if (resources.isEmpty()) {
                state->errorText = tr("Resource list is empty.");
                break;
            }
            state->selector.selectedIds.clear();
            state->selector.updatedAtUtc = QDateTime();
            state->resourcesAllSelected = false;
            state->resources = resources;
            state->startUtc = QDateTime();
            state->endUtc = QDateTime();
            state->step = FlowStep::ChooseFilter;
            state->errorText.clear();
        } else if (state->filter == FilterMode::Users) {
            if (isEmptyListToken(trimmed)) {
                state->selector.selectedIds.clear();
                state->selector.updatedAtUtc = QDateTime();
                state->usersAllSelected = true;
                state->users.clear();
                state->startUtc = QDateTime();
                state->endUtc = QDateTime();
                state->step = FlowStep::ChooseFilter;
                state->errorText.clear();
                break;
            }
            const QStringList tokens = splitListToken(trimmed);
            if (tokens.isEmpty()) {
                state->errorText = tr("User list is empty.");
                break;
            }
            QList<MessengerUserKey> parsed;
            for (const QString &token : tokens) {
                bool ok = false;
                const MessengerUserKey key = MessengerUserKey::fromString(token, &ok);
                if (!ok || !key.isValid()) {
                    state->errorText = MessengerUserFacingErrors::invalidUserValue(token);
                    parsed.clear();
                    break;
                }
                parsed.append(key);
            }
            if (!parsed.isEmpty()) {
                state->selector.selectedIds.clear();
                state->selector.updatedAtUtc = QDateTime();
                state->usersAllSelected = false;
                state->users = parsed;
                state->startUtc = QDateTime();
                state->endUtc = QDateTime();
                state->step = FlowStep::ChooseFilter;
                state->errorText.clear();
            }
        } else {
            state->errorText = tr("Select filters.");
        }
        break;
    }
    case FlowStep::EnterStartUtc: {
        QDateTime startUtc;
        if (!parseUserDateTimeToken(trimmed, dateTimeFormat, viewerTimeZone, &startUtc)) {
            state->errorText = MessengerUserFacingErrors::invalidStartTime(dateTimeFormat);
            break;
        }
        state->startUtc = startUtc;
        state->endUtc = QDateTime();
        state->step = FlowStep::EnterEndUtc;
        state->errorText.clear();
        break;
    }
    case FlowStep::EnterEndUtc: {
        QDateTime endUtc;
        if (!parseUserDateTimeToken(trimmed, dateTimeFormat, viewerTimeZone, &endUtc)) {
            state->errorText = MessengerUserFacingErrors::invalidEndTime(dateTimeFormat);
            break;
        }
        if (state->startUtc.isValid() && state->startUtc > endUtc) {
            state->errorText = MessengerUserFacingErrors::endTimeMustBeAfterStartTime();
            break;
        }
        state->endUtc = endUtc;
        state->step = FlowStep::ChooseReport;
        state->errorText.clear();
        break;
    }
    case FlowStep::ChooseFilter:
    case FlowStep::ChooseReport:
    case FlowStep::ChooseFormat:
    case FlowStep::Running:
        state->errorText = tr("Use menu buttons to continue.");
        break;
    }

    result->handled = true;
    result->sendReply = true;
    return true;
}

BOOKING_RESOURCES_REGISTER_MENU(MessengerStatisticsMenu)
