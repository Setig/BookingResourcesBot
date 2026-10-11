#include "MessengerAdminResourceAddMenu.h"

#include "Group.h"
#include "MessengerMenuIds.h"
#include "MessengerTextRender.h"
#include "MessengerAdminResourcesState.h"

MessengerAdminResourceAddMenu::MessengerAdminResourceAddMenu(MessengerMenuManager *manager,
                                                             MessengerAdminResourcesState *state)
    : MessengerAbstractMenu(manager)
    , m_state(state)
{
    // do nothing
}

QString MessengerAdminResourceAddMenu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuAdminResourceAdd);
}

bool MessengerAdminResourceAddMenu::adminsOnly() const
{
    return true;
}

MessengerMenu::MenuPage MessengerAdminResourceAddMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuAdminResourceAdd);
    page.adminsOnly = true;
    page.title = tr("Add resource");

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

    if (state->wizard.mode != MessengerAdminResourcesState::ResourceWizardMode::Add) {
        state->resetFlows();
        state->wizard.mode = MessengerAdminResourcesState::ResourceWizardMode::Add;
        state->wizard.step = MessengerAdminResourcesState::ResourceWizardStep::Name;
    }

    switch (state->wizard.step) {
    case MessengerAdminResourcesState::ResourceWizardStep::Name:
        page.body = tr("Send resource name.");
        break;
    case MessengerAdminResourcesState::ResourceWizardStep::Description:
        page.body = tr("Resource: %1\nSend description, or press Skip to leave it empty.")
                            .arg(state->wizard.name.trimmed());
        page.rows.append({ mgr->makeEventButton(tr("Back"), QStringLiteral("rw:back"), true),
                           mgr->makeEventButton(tr("Skip"), QStringLiteral("rw:skip"), true) });
        break;
    case MessengerAdminResourcesState::ResourceWizardStep::StartMessage:
        page.body = tr("Resource: %1\nSend start message, or press Skip to leave it empty.")
                            .arg(state->wizard.name.trimmed());
        page.rows.append({ mgr->makeEventButton(tr("Back"), QStringLiteral("rw:back"), true),
                           mgr->makeEventButton(tr("Skip"), QStringLiteral("rw:skip"), true) });
        break;
    case MessengerAdminResourcesState::ResourceWizardStep::Groups:
        page.body = tr("Resource: %1\n"
                       "Send groups separated by comma, or press Skip to use your administered "
                       "groups.")
                            .arg(state->wizard.name.trimmed());
        page.rows.append({ mgr->makeEventButton(tr("Back"), QStringLiteral("rw:back"), true),
                           mgr->makeEventButton(tr("Skip"), QStringLiteral("rw:skip"), true) });
        break;
    case MessengerAdminResourcesState::ResourceWizardStep::Review: {
        const QString name = state->wizard.name.trimmed();
        const QString description = state->wizard.description.trimmed();
        const QString start = state->wizard.startMessage.trimmed();
        const QString groups = MessengerTextRender::formatGroupList(state->wizard.groups, false);
        page.body = tr("Confirm adding resource:\n"
                       "Name: %1\n"
                       "Description: %2\n"
                       "Start message: %3\n"
                       "Groups: %4")
                            .arg(name,
                                 description.isEmpty() ? tr("(empty)") : description,
                                 start.isEmpty() ? tr("(empty)") : start,
                                 groups);
        page.rows.append(
                { mgr->makeEventButton(tr("Edit name"), QStringLiteral("rw:edit:name"), true) });
        page.rows.append({ mgr->makeEventButton(tr("Edit description"),
                                                QStringLiteral("rw:edit:desc"),
                                                true),
                           mgr->makeEventButton(tr("Edit start message"),
                                                QStringLiteral("rw:edit:start"),
                                                true) });
        page.rows.append({ mgr->makeEventButton(tr("Edit groups"),
                                                QStringLiteral("rw:edit:groups"),
                                                true) });
        page.rows.append(
                { mgr->makeEventButton(tr("Accept"), QStringLiteral("rw:confirm"), true) });
        break;
    }
    default:
        break;
    }

    MessengerMenuManager::prependErrorBlock(&page, state->wizard.errorText);

    page.rows.append({ mgr->makeNavigateButton(tr("Back to resource management"),
                                               QLatin1String(MessengerMenuIds::kMenuAdminResources),
                                               true) });
    return page;
}

bool MessengerAdminResourceAddMenu::handleEvent(const QString &event,
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

bool MessengerAdminResourceAddMenu::handleTextInput(
        const QString &text,
        const QString &sessionId,
        const MessengerMenu::RenderContext &context,
        MessengerMenuManager::ActionResult *result) const
{
    if (!result || !m_state)
        return false;

    MessengerAdminResourcesState::State *state = m_state->ensure(sessionId);
    if (!state)
        return false;

    return m_state->handleTextInput(text, sessionId, context, state, result);
}
