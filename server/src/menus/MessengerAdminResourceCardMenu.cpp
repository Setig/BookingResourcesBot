#include "MessengerAdminResourceCardMenu.h"

#include "MessengerMenuIds.h"
#include "MessengerAdminResourcesState.h"

#include "Group.h"
#include "Resource.h"
#include "MessengerBot.h"
#include "ResourceModel.h"
#include "ServerDatabase.h"
#include "MessengerTextRender.h"

MessengerAdminResourceCardMenu::MessengerAdminResourceCardMenu(MessengerMenuManager *manager,
                                                               MessengerAdminResourcesState *state)
    : MessengerAbstractMenu(manager)
    , m_state(state)
{
    // do nothing
}

QString MessengerAdminResourceCardMenu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuAdminResourceCard);
}

bool MessengerAdminResourceCardMenu::adminsOnly() const
{
    return true;
}

MessengerMenu::MenuPage MessengerAdminResourceCardMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuAdminResourceCard);
    page.adminsOnly = true;
    page.title = tr("Resource card");

    MessengerMenuManager *mgr = menuManager();
    if (!mgr || !m_state) {
        page.body = tr("Menu session is not available right now.");
        return page;
    }

    mgr->setCurrentMenuId(context, page.id, false);

    const QString sid = mgr->sessionId(context);
    MessengerAdminResourcesState::State *state = m_state->ensure(sid);
    if (!state) {
        page.body = tr("Menu session is not available right now.");
        page.rows.append(
                { mgr->makeNavigateButton(tr("Back to resource management"),
                                          QLatin1String(MessengerMenuIds::kMenuAdminResources),
                                          true) });
        return page;
    }

    if (state->selectedResourceUuid.trimmed().isEmpty()) {
        if (!m_state->ensureSelector(
                    state,
                    MessengerAdminResourcesState::SelectorPurpose::AdminResourceCard,
                    AbstractBot::senderKey(context.chat, context.sender),
                    false)) {
            page.body = tr("Database is not available right now.");
            page.rows.append(
                    { mgr->makeNavigateButton(tr("Back to resource management"),
                                              QLatin1String(MessengerMenuIds::kMenuAdminResources),
                                              true) });
            return page;
        }

        return m_state->buildSelectorPage(page.id,
                                          tr("Select a resource"),
                                          tr("No resources in database."),
                                          QLatin1String(MessengerMenuIds::kMenuAdminResources),
                                          true,
                                          state);
    }

    Resource resource;
    MessengerBot *bot = mgr->bot();
    ServerDatabase *database = bot ? bot->database() : nullptr;
    const ResourceModel *resourceModel =
            (database && database->isReady()) ? database->resourceModel() : nullptr;
    if (resourceModel) {
        resource = resourceModel->resourceByUuid(state->selectedResourceUuid);
    }
    const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
    if (!resource.isValid() || !database->canAdministerResource(actor, resource)) {
        state->selectedResourceUuid.clear();
        page.body = tr("Resource not found.");
        page.rows.append(
                { mgr->makeNavigateButton(tr("Back to resource management"),
                                          QLatin1String(MessengerMenuIds::kMenuAdminResources),
                                          true) });
        return page;
    }

    const QString name = MessengerTextRender::formatResourceNameWithStatus(resource, false);
    const QString description = resource.description().trimmed();
    const QString startMessage = resource.startMessage().trimmed();

    QStringList lines;
    lines << tr("Resource: %1").arg(name);
    if (!description.isEmpty()) {
        lines << tr("Description: %1").arg(description);
    }
    if (!startMessage.isEmpty()) {
        lines << tr("Start message: %1").arg(startMessage);
    }
    lines << tr("Groups: %1").arg(MessengerTextRender::formatGroupList(resource.groups(), false));
    page.body = lines.join(QStringLiteral("\n")).trimmed();

    page.rows.append(
            { mgr->makeEventButton(tr("Edit resource"), QStringLiteral("rc:edit"), true),
              mgr->makeEventButton(tr("Delete resource"), QStringLiteral("rc:delete"), true) });
    page.rows.append({ mgr->makeNavigateButton(tr("Back to resource management"),
                                               QLatin1String(MessengerMenuIds::kMenuAdminResources),
                                               true) });
    return page;
}

bool MessengerAdminResourceCardMenu::handleEvent(const QString &event,
                                                 const QString &sessionId,
                                                 const MessengerMenu::RenderContext &context,
                                                 MessengerMenuManager::ActionResult *result) const
{
    if (!result || !m_state)
        return false;

    MessengerAdminResourcesState::State *state = m_state->ensure(sessionId);
    if (!state)
        return false;

    return m_state->handleEvent(event, sessionId, context, state, result);
}
