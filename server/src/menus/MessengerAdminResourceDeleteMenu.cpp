#include "MessengerAdminResourceDeleteMenu.h"

#include "MessengerMenuIds.h"
#include "MessengerAdminResourcesState.h"

#include "Resource.h"
#include "MessengerBot.h"
#include "ResourceModel.h"
#include "ServerDatabase.h"
#include "MessengerTextRender.h"

MessengerAdminResourceDeleteMenu::MessengerAdminResourceDeleteMenu(
        MessengerMenuManager *manager, MessengerAdminResourcesState *state)
    : MessengerAbstractMenu(manager)
    , m_state(state)
{
    // do nothing
}

QString MessengerAdminResourceDeleteMenu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuAdminResourceDelete);
}

bool MessengerAdminResourceDeleteMenu::adminsOnly() const
{
    return true;
}

MessengerMenu::MenuPage MessengerAdminResourceDeleteMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuAdminResourceDelete);
    page.adminsOnly = true;
    page.title = tr("Delete resource");

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

    if (state->pendingDeleteResourceUuid.trimmed().isEmpty()) {
        if (!m_state->ensureSelector(
                    state,
                    MessengerAdminResourcesState::SelectorPurpose::AdminResourceDelete,
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
                                          tr("Select a resource to delete"),
                                          tr("No resources in database."),
                                          QLatin1String(MessengerMenuIds::kMenuAdminResources),
                                          true,
                                          state);
    }

    MessengerBot *bot = mgr->bot();
    ServerDatabase *database = bot ? bot->database() : nullptr;
    const ResourceModel *resourceModel =
            (database && database->isReady()) ? database->resourceModel() : nullptr;
    const Resource resource =
            resourceModel
                    ? resourceModel->resourceByUuid(state->pendingDeleteResourceUuid.trimmed())
                    : Resource();
    const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
    if (!resource.isValid() || !database->canAdministerResource(actor, resource)) {
        state->pendingDeleteResourceUuid.clear();
        page.body = tr("Resource not found.");
        page.rows.append(
                { mgr->makeNavigateButton(tr("Back to resource management"),
                                          QLatin1String(MessengerMenuIds::kMenuAdminResources),
                                          true) });
        return page;
    }

    const QString resourceName = MessengerTextRender::formatResourceLine(resource, false);
    page.body = tr("Confirm deleting resource:\n%1").arg(resourceName);
    page.rows.append({ mgr->makeEventButton(tr("Yes"), QStringLiteral("delr:yes"), true) });
    page.rows.append({ mgr->makeNavigateButton(tr("Back to resource management"),
                                               QLatin1String(MessengerMenuIds::kMenuAdminResources),
                                               true) });
    return page;
}

bool MessengerAdminResourceDeleteMenu::handleEvent(const QString &event,
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
