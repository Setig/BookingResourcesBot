#include "MessengerAdminResourceEditMenu.h"

#include "Group.h"
#include "Resource.h"
#include "MessengerBot.h"
#include "ResourceModel.h"
#include "MessengerMenuIds.h"
#include "ServerDatabase.h"
#include "MessengerTextRender.h"
#include "MessengerAdminResourcesState.h"

namespace {
QString resourceStatusLabel(Resource::Status status)
{
    switch (status) {
    case Resource::Status::On:
        return MessengerAdminResourceEditMenu::tr("On");
    case Resource::Status::TemporarilyUnavailable:
        return MessengerAdminResourceEditMenu::tr("TemporarilyUnavailable");
    case Resource::Status::Off:
        return MessengerAdminResourceEditMenu::tr("Off");
    }
    return MessengerAdminResourceEditMenu::tr("On");
}
} // namespace

MessengerAdminResourceEditMenu::MessengerAdminResourceEditMenu(MessengerMenuManager *manager,
                                                               MessengerAdminResourcesState *state)
    : MessengerAbstractMenu(manager)
    , m_state(state)
{
    // do nothing
}

QString MessengerAdminResourceEditMenu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuAdminResourceEdit);
}

bool MessengerAdminResourceEditMenu::adminsOnly() const
{
    return true;
}

MessengerMenu::MenuPage MessengerAdminResourceEditMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuAdminResourceEdit);
    page.adminsOnly = true;
    page.title = tr("Edit resource");

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

    if (state->wizard.mode != MessengerAdminResourcesState::ResourceWizardMode::Edit) {
        if (!m_state->ensureSelector(
                    state,
                    MessengerAdminResourcesState::SelectorPurpose::AdminResourceEdit,
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
                                          tr("Select a resource to edit"),
                                          tr("No resources in database."),
                                          QLatin1String(MessengerMenuIds::kMenuAdminResources),
                                          true,
                                          state);
    }

    MessengerBot *bot = mgr->bot();
    ServerDatabase *database = bot ? bot->database() : nullptr;
    const ResourceModel *model =
            (database && database->isReady()) ? database->resourceModel() : nullptr;
    const Resource current =
            model ? model->resourceByUuid(state->selectedResourceUuid) : Resource();
    const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
    if (!current.isValid() || !database->canAdministerResource(actor, current)) {
        state->resetFlows();
        page.body = tr("Resource not found.");
        page.rows.append(
                { mgr->makeNavigateButton(tr("Back to resource management"),
                                          QLatin1String(MessengerMenuIds::kMenuAdminResources),
                                          true) });
        return page;
    }

    const QString name = state->wizard.name.trimmed();
    const QString statusToken = resourceStatusLabel(state->wizard.status);
    const QString existingStatusToken = resourceStatusLabel(state->wizard.existingStatus);
    switch (state->wizard.step) {
    case MessengerAdminResourcesState::ResourceWizardStep::Name:
        page.body = tr("Resource: %1\nCurrent name: %2\n"
                       "Send new name, or press Keep to keep current.")
                            .arg(state->wizard.existingName.trimmed().isEmpty()
                                         ? name
                                         : state->wizard.existingName.trimmed(),
                                 state->wizard.existingName.trimmed().isEmpty()
                                         ? name
                                         : state->wizard.existingName.trimmed());
        page.rows.append({ mgr->makeEventButton(tr("Back"), QStringLiteral("rw:back"), true),
                           mgr->makeEventButton(tr("Keep"), QStringLiteral("rw:skip"), true) });
        break;
    case MessengerAdminResourcesState::ResourceWizardStep::Description:
        page.body = tr("Resource: %1\nCurrent description: %2\n"
                       "Send new description, or press Keep to keep current.")
                            .arg(name,
                                 state->wizard.existingDescription.trimmed().isEmpty()
                                         ? tr("(empty)")
                                         : state->wizard.existingDescription.trimmed());
        page.rows.append({ mgr->makeEventButton(tr("Back"), QStringLiteral("rw:back"), true),
                           mgr->makeEventButton(tr("Keep"), QStringLiteral("rw:skip"), true) });
        break;
    case MessengerAdminResourcesState::ResourceWizardStep::StartMessage:
        page.body = tr("Resource: %1\nCurrent start message: %2\n"
                       "Send new start message, or press Keep to keep current.")
                            .arg(name,
                                 state->wizard.existingStartMessage.trimmed().isEmpty()
                                         ? tr("(empty)")
                                         : state->wizard.existingStartMessage.trimmed());
        page.rows.append({ mgr->makeEventButton(tr("Back"), QStringLiteral("rw:back"), true),
                           mgr->makeEventButton(tr("Keep"), QStringLiteral("rw:skip"), true) });
        break;
    case MessengerAdminResourcesState::ResourceWizardStep::Groups:
        page.body = tr("Resource: %1\nCurrent groups: %2\n"
                       "Send comma-separated groups, or press Keep to keep current.")
                            .arg(name,
                                 MessengerTextRender::formatGroupList(state->wizard.existingGroups,
                                                                      false));
        page.rows.append({ mgr->makeEventButton(tr("Back"), QStringLiteral("rw:back"), true),
                           mgr->makeEventButton(tr("Keep"), QStringLiteral("rw:skip"), true) });
        break;
    case MessengerAdminResourcesState::ResourceWizardStep::Status:
        page.body = tr("Resource: %1\nCurrent status: %2\n"
                       "Select new status, send it as text, or press Keep to keep current.")
                            .arg(name, existingStatusToken);
        page.rows.append({ mgr->makeEventButton(tr("On"), QStringLiteral("rw:setstatus:on"), true),
                           mgr->makeEventButton(tr("TemporarilyUnavailable"),
                                                QStringLiteral("rw:setstatus:temp"),
                                                true) });
        page.rows.append(
                { mgr->makeEventButton(tr("Off"), QStringLiteral("rw:setstatus:off"), true) });
        page.rows.append({ mgr->makeEventButton(tr("Back"), QStringLiteral("rw:back"), true),
                           mgr->makeEventButton(tr("Keep"), QStringLiteral("rw:skip"), true) });
        break;
    case MessengerAdminResourcesState::ResourceWizardStep::Review: {
        const QString description = state->wizard.description.trimmed();
        const QString start = state->wizard.startMessage.trimmed();
        const QString groups = MessengerTextRender::formatGroupList(state->wizard.groups, false);
        page.body = tr("Resource details:\n"
                       "Name: %1\n"
                       "Description: %2\n"
                       "Start message: %3\n"
                       "Status: %4\n"
                       "Groups: %5\n"
                       "\n"
                       "Select what to change, then press Save changes.")
                            .arg(name,
                                 description.isEmpty() ? tr("(empty)") : description,
                                 start.isEmpty() ? tr("(empty)") : start,
                                 statusToken,
                                 groups);
        page.rows.append(
                { mgr->makeEventButton(tr("Edit name"), QStringLiteral("rw:edit:name"), true) });
        page.rows.append({ mgr->makeEventButton(tr("Edit description"),
                                                QStringLiteral("rw:edit:desc"),
                                                true),
                           mgr->makeEventButton(tr("Edit start message"),
                                                QStringLiteral("rw:edit:start"),
                                                true) });
        page.rows.append({ mgr->makeEventButton(tr("Edit status"),
                                                QStringLiteral("rw:edit:status"),
                                                true) });
        page.rows.append({ mgr->makeEventButton(tr("Edit groups"),
                                                QStringLiteral("rw:edit:groups"),
                                                true) });
        page.rows.append(
                { mgr->makeEventButton(tr("Save changes"), QStringLiteral("rw:confirm"), true) });
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

bool MessengerAdminResourceEditMenu::handleEvent(const QString &event,
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

bool MessengerAdminResourceEditMenu::handleTextInput(
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
