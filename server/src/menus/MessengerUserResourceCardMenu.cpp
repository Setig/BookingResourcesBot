#include "MessengerUserResourceCardMenu.h"

#include "MessengerMenuIds.h"
#include "MessengerUserResourcesState.h"

#include "MessengerBot.h"
#include "ResourceModel.h"
#include "ServerDatabase.h"
#include "ResourceQueueModel.h"
#include "MessengerTextRender.h"
#include "ServerDateTimeFormat.h"
#include "OccupationDurationModel.h"
#include "MessengerOccupyCommand.h"
#include "ResourceQueueUserMessages.h"

#include <QSet>
#include <QCollator>
#include <QDateTime>
#include <QRegularExpression>

namespace {
QString resourceLabelForButton(const Resource &resource)
{
    return MessengerTextRender::formatResourceNameWithStatus(resource, false);
}

bool actorHasActiveAccessForQueueAction(const ServerDatabase *database,
                                        const MessengerUserKey &actor)
{
    if (!database || !database->isReady() || !actor.isValid())
        return false;

    if (actor.messenger() == MessengerType::LocalCli)
        return true;

    const UserModel *userModel = database->userModel();
    return userModel && userModel->exists(actor);
}

bool isQueueActionBaseAllowed(const ServerDatabase *database,
                              const Resource &resource,
                              const MessengerUserKey &actor)
{
    if (!database || !database->isReady() || !resource.isValid())
        return false;
    if (resource.name().trimmed().isEmpty())
        return false;
    if (resource.status() != Resource::Status::On)
        return false;
    return actorHasActiveAccessForQueueAction(database, actor);
}

bool isUserWithinResourceLimit(const ResourceQueueModel *queueModel,
                               const MessengerUserKey &user,
                               const QString &resourceUuid)
{
    if (!queueModel || !user.isValid())
        return false;

    const uint limit = queueModel->maxResourcesPerUser();
    if (limit == 0)
        return true;

    const QString normalizedResourceUuid = Resource::normalizeUuidString(resourceUuid);
    if (normalizedResourceUuid.isEmpty())
        return false;

    QSet<QString> userResourceUuids;
    const QList<ResourceQueueEntry> entries = queueModel->items();
    for (const ResourceQueueEntry &entry : entries) {
        if (!entry.isValid() || entry.user() != user)
            continue;

        const QString entryResourceUuid = Resource::normalizeUuidString(entry.resourceUuid());
        if (!entryResourceUuid.isEmpty())
            userResourceUuids.insert(entryResourceUuid);
    }

    if (userResourceUuids.contains(normalizedResourceUuid))
        return true;

    return static_cast<uint>(userResourceUuids.size()) < limit;
}

bool occupationDurationAllowedByPolicy(int minutes, uint maxMinutes)
{
    const int clamped = minutes < 0 ? 0 : minutes;
    if (maxMinutes == 0)
        return true;
    return clamped > 0 && static_cast<uint>(clamped) <= maxMinutes;
}

bool occupationDurationFitsBeforeNextSchedule(int minutes,
                                              const QDateTime &nowUtc,
                                              const QDateTime &nextScheduleStartUtc)
{
    if (!nextScheduleStartUtc.isValid())
        return true;

    const int clamped = minutes < 0 ? 0 : minutes;
    if (clamped <= 0)
        return false;

    return nowUtc.addSecs(static_cast<qint64>(clamped) * 60) <= nextScheduleStartUtc;
}

bool hasUsableOccupationDuration(const ServerDatabase *database,
                                 const ResourceQueueModel *queueModel,
                                 const QDateTime &nowUtc,
                                 const QDateTime &nextScheduleStartUtc)
{
    if (!queueModel)
        return false;

    const uint maxMinutes = queueModel->maxOccupationDurationMinutes();
    const QList<int> durations = (database && database->occupationDurationModel())
                                         ? database->occupationDurationModel()->minutes()
                                         : QList<int>();
    QList<int> effectiveDurations = durations;
    if (effectiveDurations.isEmpty())
        effectiveDurations.append(0);

    for (int minutes : effectiveDurations) {
        if (!occupationDurationAllowedByPolicy(minutes, maxMinutes))
            continue;
        if (!occupationDurationFitsBeforeNextSchedule(minutes, nowUtc, nextScheduleStartUtc))
            continue;
        return true;
    }

    return false;
}

bool hasFutureAtTimeEntry(const QList<ResourceQueueEntry> &entries, const QDateTime &nowUtc)
{
    for (const ResourceQueueEntry &entry : entries) {
        if (!entry.isValid() || entry.waitsForRelease() || !entry.startAtUtc().isValid())
            continue;
        if (entry.startAtUtc() > nowUtc)
            return true;
    }

    return false;
}

bool hasAfterReleaseEntry(const QList<ResourceQueueEntry> &entries)
{
    for (const ResourceQueueEntry &entry : entries) {
        if (entry.isValid() && entry.waitsForRelease())
            return true;
    }

    return false;
}

bool hasAfterReleaseEntryForUser(const QList<ResourceQueueEntry> &entries,
                                 const MessengerUserKey &user)
{
    for (const ResourceQueueEntry &entry : entries) {
        if (entry.isValid() && entry.waitsForRelease() && entry.user() == user)
            return true;
    }

    return false;
}

bool canShowOccupyButton(const ServerDatabase *database,
                         const Resource &resource,
                         const MessengerUserKey &actor,
                         bool canRelease)
{
    if (canRelease || !isQueueActionBaseAllowed(database, resource, actor))
        return false;

    const ResourceQueueModel *queueModel = database->resourceQueueModel();
    if (!queueModel || !isUserWithinResourceLimit(queueModel, actor, resource.uuidString()))
        return false;

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    const ResourceQueueEntry occupant = queueModel->currentOccupantForResource(resource.name());
    const QList<ResourceQueueEntry> entries = queueModel->schedulesForResource(resource.name());

    if (occupant.isValid()) {
        if (occupant.user() == actor)
            return false;
        if (hasFutureAtTimeEntry(entries, nowUtc))
            return false;
        if (hasAfterReleaseEntryForUser(entries, actor))
            return false;
        return hasUsableOccupationDuration(database, queueModel, nowUtc, QDateTime());
    }

    const QDateTime nextScheduleStartUtc =
            queueModel->nextScheduleStartUtc(resource.name(), nowUtc);
    return hasUsableOccupationDuration(database, queueModel, nowUtc, nextScheduleStartUtc);
}

bool canShowScheduleButton(const ServerDatabase *database,
                           const Resource &resource,
                           const MessengerUserKey &actor)
{
    if (!isQueueActionBaseAllowed(database, resource, actor))
        return false;

    const ResourceQueueModel *queueModel = database->resourceQueueModel();
    if (!queueModel || !isUserWithinResourceLimit(queueModel, actor, resource.uuidString()))
        return false;

    const ResourceQueueEntry occupant = queueModel->currentOccupantForResource(resource.name());
    if (occupant.isValid() && !occupant.endAtUtc().isValid())
        return false;

    const QList<ResourceQueueEntry> entries = queueModel->schedulesForResource(resource.name());
    return !hasAfterReleaseEntry(entries);
}
} // namespace

MessengerUserResourceCardMenu::MessengerUserResourceCardMenu(MessengerMenuManager *manager,
                                                             MessengerUserResourcesState *state)
    : MessengerAbstractMenu(manager)
    , m_state(state)
{
    // do nothing
}

QString MessengerUserResourceCardMenu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuResourceCardUser);
}

MessengerMenu::MenuPage MessengerUserResourceCardMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuResourceCardUser);
    page.title = tr("Resource overview");

    MessengerMenuManager *mgr = menuManager();
    if (!mgr || !m_state) {
        page.body = tr("Menu session is not available right now.");
        return page;
    }

    const QString sid = mgr->sessionId(context);
    MessengerUserResourcesState::State *state = m_state->ensure(sid);
    if (!state) {
        page.body = tr("Menu session is not available right now.");
        page.rows.append(
                { mgr->makeNavigateButton(tr("Back to resources"),
                                          QLatin1String(MessengerMenuIds::kMenuResourcesHome)) });
        return page;
    }

    mgr->setCurrentMenuId(context, page.id, false);
    const MessengerUserKey viewerKey = AbstractBot::senderKey(context.chat, context.sender);

    QString selectedResourceUuid = state->selectedResourceUuid.trimmed();
    if (!selectedResourceUuid.isEmpty()) {
        ServerDatabase *database = mgr->bot() ? mgr->bot()->database() : nullptr;
        const ResourceModel *resourceModel =
                (database && database->isReady()) ? database->resourceModel() : nullptr;
        const Resource selectedResource =
                resourceModel ? resourceModel->resourceByUuid(selectedResourceUuid) : Resource();
        if (!MessengerTextRender::isResourceVisibleToViewer(selectedResource,
                                                            database,
                                                            context.chat.messenger,
                                                            viewerKey)) {
            state->selectedResourceUuid.clear();
            state->wizard.clear();
            selectedResourceUuid.clear();
        }
    }

    if (selectedResourceUuid.isEmpty()) {
        // Selector mode.
        page.title = tr("Select a resource");

        ServerDatabase *database = mgr->bot() ? mgr->bot()->database() : nullptr;
        const ResourceModel *resourceModel =
                (database && database->isReady()) ? database->resourceModel() : nullptr;

        const bool needRefresh =
                state->selector.items.isEmpty() || !state->selector.updatedAtUtc.isValid();
        if (needRefresh) {
            state->selector.clear();
            state->selector.pageSize = MessengerMenuIds::kSelectorPageSize;
            state->selector.updatedAtUtc = QDateTime::currentDateTimeUtc();

            if (resourceModel) {
                const QList<Resource> resources = resourceModel->items();
                QVector<MessengerUserResourcesState::SelectorItem> items;
                items.reserve(resources.size());
                for (const Resource &r : resources) {
                    if (!r.isValid())
                        continue;
                    if (!MessengerTextRender::isResourceVisibleToViewer(r,
                                                                        database,
                                                                        context.chat.messenger,
                                                                        viewerKey))
                        continue;
                    const QString id = r.uuidString();
                    const QString label = resourceLabelForButton(r);
                    if (id.isEmpty() || label.trimmed().isEmpty())
                        continue;
                    MessengerUserResourcesState::SelectorItem item;
                    item.id = id;
                    item.label = label;
                    items.append(item);
                }

                QCollator collator;
                collator.setNumericMode(true);
                std::sort(items.begin(),
                          items.end(),
                          [&collator](const MessengerUserResourcesState::SelectorItem &lhs,
                                      const MessengerUserResourcesState::SelectorItem &rhs) {
                              return collator.compare(lhs.label, rhs.label) < 0;
                          });
                state->selector.items = items;
            }
        }

        const int total = state->selector.items.size();
        const int pageSize = qMax(1, state->selector.pageSize);
        const int pageCount = (total + pageSize - 1) / pageSize;
        if (pageCount > 0) {
            state->selector.page = qBound(0, state->selector.page, pageCount - 1);
        } else {
            state->selector.page = 0;
        }

        QStringList bodyLines;
        QString asOf;
        if (mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive) {
            asOf = MessengerMenuManager::buildAsOfLine(state->selector.updatedAtUtc,
                                                       AbstractBot::MessageFormat::PlainText);
        }
        if (!asOf.trimmed().isEmpty())
            bodyLines << asOf;

        if (!resourceModel) {
            bodyLines << tr("Database is not available right now.");
        } else if (total <= 0) {
            bodyLines << tr("No resources in database.");
        } else {
            bodyLines << tr("Items: %1").arg(QString::number(total));
            if (pageCount > 1) {
                bodyLines << tr("Page: %1/%2")
                                     .arg(QString::number(state->selector.page + 1),
                                          QString::number(pageCount));
            }
        }

        page.body = bodyLines.join(QStringLiteral("\n")).trimmed();

        if (mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive) {
            page.rows.append(
                    { mgr->makeEventButton(tr("Refresh list"), QStringLiteral("refresh")) });
        }

        if (total > 0) {
            const int start = state->selector.page * pageSize;
            const int end = qMin(total, start + pageSize);
            for (int i = start; i < end; ++i) {
                const MessengerUserResourcesState::SelectorItem &item = state->selector.items.at(i);
                page.rows.append(
                        { mgr->makeEventButton(item.label, QStringLiteral("sel:%1").arg(i)) });
            }
        }

        QVector<MessengerMenu::MenuButton> navRow;
        if (pageCount > 1 && state->selector.page > 0) {
            navRow.append(mgr->makeEventButton(tr("Previous page"), QStringLiteral("page:prev")));
        }
        if (pageCount > 1 && state->selector.page + 1 < pageCount) {
            navRow.append(mgr->makeEventButton(tr("Next page"), QStringLiteral("page:next")));
        }
        if (!navRow.isEmpty())
            page.rows.append(navRow);

        page.rows.append(
                { mgr->makeNavigateButton(tr("Back to resources"),
                                          QLatin1String(MessengerMenuIds::kMenuResourcesHome)) });
        MessengerMenuManager::prependErrorBlock(&page, state->wizard.errorText);
        return page;
    }

    // Occupy wizard step (duration choice).
    if (state->wizard.mode == MessengerUserResourcesState::WizardMode::Occupy
        && state->wizard.step == MessengerUserResourcesState::WizardStep::OccupyDuration) {
        page.title = tr("Occupy");

        Resource resource;
        ServerDatabase *database = mgr->bot() ? mgr->bot()->database() : nullptr;
        if (database && database->isReady() && database->resourceModel()) {
            resource = database->resourceModel()->resourceByUuid(selectedResourceUuid);
        }
        const QString name =
                resource.isValid()
                        ? MessengerTextRender::formatResourceNameWithStatus(resource, false)
                        : tr("(unknown)");

        page.body = tr("Resource: %1\nChoose duration:").arg(name);

        const QList<int> durationOptions = (database && database->occupationDurationModel())
                                                   ? database->occupationDurationModel()->minutes()
                                                   : QList<int>();
        const ResourceQueueModel *queueModel = database ? database->resourceQueueModel() : nullptr;
        const uint maxMinutes = queueModel ? queueModel->maxOccupationDurationMinutes() : 0;
        const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
        QDateTime nextScheduleStartUtc;
        if (queueModel && resource.isValid()) {
            const ResourceQueueEntry occupant =
                    queueModel->currentOccupantForResource(resource.name());
            if (!occupant.isValid()) {
                nextScheduleStartUtc = queueModel->nextScheduleStartUtc(resource.name(), nowUtc);
            }
        }

        QVector<MessengerMenu::MenuButton> row;
        auto appendDurationButton = [&](int minutes) {
            const QString label = MessengerTextRender::formatDurationMinutes(minutes);
            const QString value = QStringLiteral("ur:dur:%1").arg(QString::number(minutes));
            row.append(mgr->makeEventButton(label, value));
            if (row.size() >= 2) {
                page.rows.append(row);
                row.clear();
            }
        };

        QList<int> effectiveDurations = durationOptions;
        if (effectiveDurations.isEmpty())
            effectiveDurations.append(0);
        for (int minutes : effectiveDurations) {
            if (!occupationDurationAllowedByPolicy(minutes, maxMinutes))
                continue;
            if (!occupationDurationFitsBeforeNextSchedule(minutes, nowUtc, nextScheduleStartUtc))
                continue;
            appendDurationButton(minutes);
        }
        if (!row.isEmpty())
            page.rows.append(row);

        page.rows.append({ mgr->makeEventButton(tr("Back"), QStringLiteral("ur:cancel")) });
        MessengerMenuManager::prependErrorBlock(&page, state->wizard.errorText);
        return page;
    }

    // Normal card view.
    Resource resource;
    ServerDatabase *database = mgr->bot() ? mgr->bot()->database() : nullptr;
    if (database && database->isReady() && database->resourceModel()) {
        resource = database->resourceModel()->resourceByUuid(selectedResourceUuid);
    }

    const QString name =
            resource.isValid() ? MessengerTextRender::formatResourceNameWithStatus(resource, false)
                               : tr("(unknown)");
    const QString description = resource.isValid() ? resource.description().trimmed() : QString();

    QStringList lines;
    lines << tr("Resource: %1").arg(name);
    if (!description.isEmpty())
        lines << tr("Description: %1").arg(description);
    page.body = lines.join(QStringLiteral("\n")).trimmed();

    page.dynamicText = [mgr, selectedResourceUuid](const MessengerMenu::RenderContext &ctx) {
        MessengerMenu::MenuPage::DynamicText out;
        MessengerBot *bot = mgr ? mgr->bot() : nullptr;
        const bool showAsOf =
                mgr && mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive;

        QString resourceName;
        ServerDatabase *database = bot ? bot->database() : nullptr;
        if (database && database->isReady() && database->resourceModel()) {
            const Resource resource =
                    database->resourceModel()->resourceByUuid(selectedResourceUuid);
            if (resource.isValid())
                resourceName = resource.name();
        }

        MessengerTextRender::RenderedText rendered =
                MessengerTextRender::renderResourceQueueStatus(bot ? bot->database() : nullptr,
                                                               ctx.chat.messenger,
                                                               resourceName,
                                                               AbstractBot::senderKey(ctx.chat,
                                                                                      ctx.sender),
                                                               bot ? bot->backendBot() : nullptr);
        const QString trimmedText = rendered.text.trimmed();
        out.text = showAsOf ? mgr->buildTextWithAsOfCurrentFormat(trimmedText, rendered.format)
                            : trimmedText;
        out.format = rendered.format;
        return out;
    };

    if (mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive) {
        page.rows.append({ mgr->makeNavigateButton(
                tr("Refresh"), QLatin1String(MessengerMenuIds::kMenuResourceCardUser)) });
    }

    bool canRelease = false;
    bool canUnschedule = false;
    if (database && database->isReady() && database->resourceQueueModel()) {
        const ResourceQueueModel *queueModel = database->resourceQueueModel();

        const ResourceQueueEntry occupant = queueModel->currentOccupantForResource(
                resource.isValid() ? resource.name() : QString());
        if (occupant.isValid() && occupant.user() == viewerKey) {
            canRelease = true;
        }

        const QList<ResourceQueueEntry> schedules =
                queueModel->schedulesForResource(resource.isValid() ? resource.name() : QString());
        for (const ResourceQueueEntry &entry : schedules) {
            if (entry.user() == viewerKey) {
                canUnschedule = true;
                break;
            }
        }
    }

    QVector<MessengerMenu::MenuButton> occupyRow;
    if (canShowOccupyButton(database, resource, viewerKey, canRelease)) {
        occupyRow.append(mgr->makeEventButton(tr("Occupy"), QStringLiteral("ur:occupy")));
    }
    if (canRelease) {
        occupyRow.append(mgr->makeEventButton(tr("Release"), QStringLiteral("ur:release")));
    }
    if (!occupyRow.isEmpty())
        page.rows.append(occupyRow);

    QVector<MessengerMenu::MenuButton> scheduleRow;
    if (canUnschedule) {
        scheduleRow.append(
                mgr->makeEventButton(tr("Cancel schedule"), QStringLiteral("ur:unschedule")));
    }
    if (canShowScheduleButton(database, resource, viewerKey)) {
        scheduleRow.append(
                mgr->makeEventButton(tr("Plan usage"), QStringLiteral("ur:nav:schedule")));
    }
    if (!scheduleRow.isEmpty())
        page.rows.append(scheduleRow);

    page.rows.append({ mgr->makeEventButton(tr("View queue"), QStringLiteral("ur:nav:queue")) });
    page.rows.append(
            { mgr->makeEventButton(tr("Select a resource"), QStringLiteral("ur:pick")),
              mgr->makeNavigateButton(tr("Back to resources"),
                                      QLatin1String(MessengerMenuIds::kMenuResourcesHome)) });

    MessengerMenuManager::prependErrorBlock(&page, state->wizard.errorText);
    return page;
}

bool MessengerUserResourceCardMenu::handleEvent(const QString &event,
                                                const QString &sessionId,
                                                const MessengerMenu::RenderContext &context,
                                                MessengerMenuManager::ActionResult *result) const
{
    if (!result || !m_state)
        return false;

    MessengerMenuManager *mgr = menuManager();
    if (!mgr)
        return false;

    MessengerUserResourcesState::State *state = m_state->ensure(sessionId);
    if (!state)
        return false;

    const QString trimmed = event.trimmed();
    if (trimmed.isEmpty())
        return false;

    // Selector events when resource is not selected yet.
    if (state->selectedResourceUuid.trimmed().isEmpty()) {
        if (trimmed == QStringLiteral("page:prev")) {
            if (state->selector.page > 0) {
                state->selector.page -= 1;
            }
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        if (trimmed == QStringLiteral("page:next")) {
            const int total = state->selector.items.size();
            const int pageSize = qMax(1, state->selector.pageSize);
            const int pageCount = (total + pageSize - 1) / pageSize;
            if (state->selector.page + 1 < pageCount) {
                state->selector.page += 1;
            }
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        if (trimmed == QStringLiteral("refresh")) {
            state->selector.clear();
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        static const QRegularExpression selRegex(QStringLiteral("^sel:(\\d+)$"));
        const QRegularExpressionMatch selMatch = selRegex.match(trimmed);
        if (selMatch.hasMatch()) {
            bool ok = false;
            const int index = selMatch.captured(1).toInt(&ok);
            if (!ok || index < 0 || index >= state->selector.items.size()) {
                result->handled = true;
                result->sendReply = true;
                return true;
            }

            const MessengerUserResourcesState::SelectorItem item = state->selector.items.at(index);
            state->selector.clear();
            state->wizard.clear();
            state->selectedResourceUuid = item.id;

            result->handled = true;
            result->sendReply = true;
            return true;
        }
    }

    // User-facing resource actions.
    if (trimmed == QStringLiteral("ur:pick")) {
        state->wizard.clear();
        state->selectedResourceUuid.clear();
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (trimmed == QStringLiteral("ur:cancel")) {
        state->wizard.clear();
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (trimmed == QStringLiteral("ur:nav:card")) {
        state->wizard.clear();
        mgr->setCurrentMenuId(context,
                              QLatin1String(MessengerMenuIds::kMenuResourceCardUser),
                              false);
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (trimmed == QStringLiteral("ur:nav:queue")) {
        state->wizard.clear();
        mgr->setCurrentMenuId(context, QLatin1String(MessengerMenuIds::kMenuResourceQueue), false);
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (trimmed == QStringLiteral("ur:nav:schedule")) {
        state->wizard.clear();
        mgr->setCurrentMenuId(context,
                              QLatin1String(MessengerMenuIds::kMenuResourceSchedule),
                              false);
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (trimmed == QStringLiteral("ur:occupy")) {
        state->wizard.clear();
        state->wizard.mode = MessengerUserResourcesState::WizardMode::Occupy;
        state->wizard.step = MessengerUserResourcesState::WizardStep::OccupyDuration;
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (trimmed.startsWith(QStringLiteral("ur:dur:"))) {
        const QString minutesToken = trimmed.mid(QStringLiteral("ur:dur:").size()).trimmed();
        bool ok = false;
        const int minutes = minutesToken.toInt(&ok);
        if (!ok || minutes < 0) {
            state->wizard.errorText = ResourceQueueUserMessages::unknownDuration(minutesToken);
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        const QString resourceUuid = state->selectedResourceUuid.trimmed();
        if (resourceUuid.isEmpty()) {
            state->wizard.errorText = tr("Select a resource.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        MessengerBot *bot = mgr->bot();
        if (!bot || !bot->database() || !bot->database()->isReady()) {
            state->wizard.errorText = tr("Database is not available right now.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        const ResourceModel *resourceModel = bot->database()->resourceModel();
        const Resource resource =
                resourceModel ? resourceModel->resourceByUuid(resourceUuid) : Resource();
        const QString resourceName = resource.isValid() ? resource.name() : QString();
        if (resourceName.trimmed().isEmpty()) {
            state->wizard.errorText = tr("Select a resource.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        const QList<int> allowedDurations =
                bot->database()->occupationDurationModel()
                        ? bot->database()->occupationDurationModel()->minutes()
                        : QList<int>();

        const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
        const ServerDatabaseError queueError = bot->database()->occupyResourceUser(resourceName,
                                                                                   actor,
                                                                                   minutes,
                                                                                   allowedDurations,
                                                                                   actor);
        if (queueError) {
            state->wizard.errorText = queueError.errorString();
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        const ResourceQueueModel *queueModel = bot->database()->resourceQueueModel();
        const ResourceQueueEntry occupant =
                queueModel ? queueModel->currentOccupantForResource(resourceName)
                           : ResourceQueueEntry();
        const QString occupyReply =
                MessengerOccupyCommand::buildOccupyReplyText(resource,
                                                             resourceName,
                                                             occupant,
                                                             actor,
                                                             minutes,
                                                             ServerDateTimeFormat::effectiveFormat(
                                                                     bot->database()),
                                                             bot->effectiveTimeZoneForUser(actor));

        state->wizard.clear();
        result->handled = true;
        result->sendReply = true;
        MessengerMenuManager::requestSendAsNewMessageAndEditOrigin(
                result,
                MessengerMenuManager::buildPlainTextReply(occupyReply),
                mgr->lastMenuMessageId(context));
        return true;
    }

    if (trimmed == QStringLiteral("ur:release")) {
        state->wizard.errorText.clear();

        const QString resourceUuid = state->selectedResourceUuid.trimmed();
        if (resourceUuid.isEmpty()) {
            state->wizard.errorText = tr("Select a resource.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        MessengerBot *bot = mgr->bot();
        if (!bot || !bot->database() || !bot->database()->isReady()) {
            state->wizard.errorText = tr("Database is not available right now.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        const ResourceModel *resourceModel = bot->database()->resourceModel();
        const Resource resource =
                resourceModel ? resourceModel->resourceByUuid(resourceUuid) : Resource();
        const QString resourceName = resource.isValid() ? resource.name() : QString();
        if (resourceName.trimmed().isEmpty()) {
            state->wizard.errorText = tr("Select a resource.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
        const ServerDatabaseError releaseError =
                bot->database()->releaseUserOccupationForResource(resourceName, actor, actor);
        if (releaseError) {
            state->wizard.errorText = releaseError.errorString();
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        bot->notifyNextUserQueueStatus(resourceName, actor);

        state->wizard.errorText.clear();
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (trimmed == QStringLiteral("ur:unschedule")) {
        state->wizard.errorText.clear();

        const QString resourceUuid = state->selectedResourceUuid.trimmed();
        if (resourceUuid.isEmpty()) {
            state->wizard.errorText = tr("Select a resource.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        MessengerBot *bot = mgr->bot();
        if (!bot || !bot->database() || !bot->database()->isReady()) {
            state->wizard.errorText = tr("Database is not available right now.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        const ResourceModel *resourceModel = bot->database()->resourceModel();
        const Resource resource =
                resourceModel ? resourceModel->resourceByUuid(resourceUuid) : Resource();
        const QString resourceName = resource.isValid() ? resource.name() : QString();
        if (resourceName.trimmed().isEmpty()) {
            state->wizard.errorText = tr("Select a resource.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
        const ServerDatabaseError scheduleError =
                bot->database()->removeUserSchedulesForResource(resourceName, actor, actor);
        if (scheduleError) {
            state->wizard.errorText = scheduleError.errorString();
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        state->wizard.errorText.clear();
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    return false;
}
