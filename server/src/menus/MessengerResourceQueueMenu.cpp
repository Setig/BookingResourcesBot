#include "MessengerResourceQueueMenu.h"

#include "MessengerMenuIds.h"
#include "MessengerUserResourcesState.h"

#include "Resource.h"
#include "MessengerBot.h"
#include "ResourceModel.h"
#include "ServerDatabase.h"
#include "ResourceQueueModel.h"
#include "MessengerTextRender.h"
#include "ResourceQueueUserMessages.h"
#include "MessengerUserFacingErrors.h"

#include <QCollator>
#include <QRegularExpression>

namespace {
QString resourceLabelForButton(const Resource &resource)
{
    return MessengerTextRender::formatResourceNameWithStatus(resource, false);
}
} // namespace

MessengerResourceQueueMenu::MessengerResourceQueueMenu(MessengerMenuManager *manager,
                                                       MessengerUserResourcesState *state)
    : MessengerAbstractMenu(manager)
    , m_state(state)
{
    // do nothing
}

QString MessengerResourceQueueMenu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuResourceQueue);
}

MessengerMenu::MenuPage MessengerResourceQueueMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuResourceQueue);
    page.title = tr("Resource queue");
    page.body = tr("Queue details: position, user, expected time.");

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
        return page;
    }

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

        const MessengerTextRender::RenderedText rendered =
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
        page.rows.append(
                { mgr->makeNavigateButton(tr("Refresh queue"),
                                          QLatin1String(MessengerMenuIds::kMenuResourceQueue)) });
    }

    bool canLeaveQueue = false;
    bool canAdminClearResourceQueue = false;
    MessengerBot *bot = mgr->bot();
    if (bot && bot->database() && bot->database()->isReady()
        && bot->database()->resourceQueueModel()) {
        const ResourceQueueModel *queueModel = bot->database()->resourceQueueModel();
        const ResourceModel *resourceModel = bot->database()->resourceModel();
        QString resourceName;
        if (resourceModel) {
            const Resource resource = resourceModel->resourceByUuid(selectedResourceUuid);
            if (resource.isValid())
                resourceName = resource.name().trimmed();
        }
        if (!resourceName.isEmpty()) {
            const QList<ResourceQueueEntry> schedules =
                    queueModel->schedulesForResource(resourceName);
            canAdminClearResourceQueue = !schedules.isEmpty();
            for (const ResourceQueueEntry &entry : schedules) {
                if (entry.user() == AbstractBot::senderKey(context.chat, context.sender)) {
                    canLeaveQueue = true;
                    break;
                }
            }
        }
    }
    if (canLeaveQueue) {
        page.rows.append(
                { mgr->makeEventButton(tr("Leave queue"), QStringLiteral("ur:leavequeue")) });
    }
    if (context.isAdmin) {
        page.rows.append({ mgr->makeEventButton(tr("Admin clear current occupation"),
                                                QStringLiteral("ur:admin:release"),
                                                true) });
    }
    if (context.isAdmin && canAdminClearResourceQueue) {
        page.rows.append({ mgr->makeEventButton(tr("Admin release resource"),
                                                QStringLiteral("ur:admin:clearqueue"),
                                                true) });
    }
    page.rows.append(
            { mgr->makeEventButton(tr("Back to resource"), QStringLiteral("ur:nav:card")) });

    MessengerMenuManager::prependErrorBlock(&page, state->wizard.errorText);

    return page;
}

bool MessengerResourceQueueMenu::handleEvent(const QString &event,
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

    if (trimmed == QStringLiteral("ur:nav:card")) {
        state->wizard.clear();
        mgr->setCurrentMenuId(context,
                              QLatin1String(MessengerMenuIds::kMenuResourceCardUser),
                              false);
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (trimmed == QStringLiteral("ur:leavequeue")) {
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
        const ServerDatabaseError queueError =
                bot->database()->removeUserSchedulesForResource(resourceName, actor, actor);
        if (queueError) {
            state->wizard.errorText = queueError.errorString();
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        state->wizard.errorText.clear();
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (trimmed == QStringLiteral("ur:admin:release")) {
        state->wizard.errorText.clear();

        if (!context.isAdmin) {
            state->wizard.errorText = MessengerUserFacingErrors::accessDeniedAdminsOnly();
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

        const ResourceQueueModel *queueModel = bot->database()->resourceQueueModel();
        if (!queueModel) {
            state->wizard.errorText = tr("Queue model is not available right now.");
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

        const ResourceQueueEntry occupant = queueModel->currentOccupantForResource(resourceName);
        if (!occupant.isValid()) {
            state->wizard.errorText = ResourceQueueUserMessages::noCurrentOccupant(resourceName);
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        const MessengerUserKey target = occupant.user();
        const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
        const ServerDatabaseError releaseError =
                bot->database()->releaseUserOccupationForResource(resourceName, target, actor);
        if (releaseError) {
            state->wizard.errorText = releaseError.errorString();
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        state->wizard.errorText.clear();
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (trimmed == QStringLiteral("ur:admin:clearqueue")) {
        state->wizard.errorText.clear();

        if (!context.isAdmin) {
            state->wizard.errorText = MessengerUserFacingErrors::accessDeniedAdminsOnly();
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

        const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
        int removedCount = 0;
        const ServerDatabaseError queueError =
                bot->database()->removeResourceQueueEntries(resourceName, actor, &removedCount);
        if (queueError) {
            state->wizard.errorText = queueError.errorString();
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        if (removedCount <= 0) {
            state->wizard.errorText = ResourceQueueUserMessages::noQueueEntries(resourceName);
        } else {
            state->wizard.errorText.clear();
        }
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    return false;
}
