#include "MessengerAdminUserAddMenu.h"

#include "Group.h"
#include "User.h"
#include "MessengerBot.h"
#include "MessengerMenuIds.h"
#include "ServerDatabase.h"
#include "MessengerTextRender.h"
#include "MessengerAdminUsersState.h"
#include "MessengerUserFacingErrors.h"

namespace {
QString formatGroupRoles(const QMap<Group, User::Role> &roles)
{
    if (roles.isEmpty())
        return MessengerAdminUserAddMenu::tr("Root (all groups)");
    QStringList values;
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
        values.append(QStringLiteral("%1=%2")
                              .arg(MessengerTextRender::formatGroupDisplayName(it.key(), false),
                                   it.value() == User::Role::FixedAdminRole
                                           ? MessengerAdminUserAddMenu::tr("fixed admin",
                                                                           "User role label "
                                                                           "(fixed admin)")
                                   : it.value() == User::Role::AdminRole
                                           ? MessengerAdminUserAddMenu::tr("admin")
                                           : MessengerAdminUserAddMenu::tr("user")));
    }
    return values.join(QStringLiteral(", "));
}

QString roleLabelForGroupRoles(const QMap<Group, User::Role> &roles)
{
    User user;
    user.setGroupRoles(roles);
    const User::Role role = user.role();
    if (role == User::Role::FixedAdminRole)
        return MessengerAdminUserAddMenu::tr("fixed admin", "User role label (fixed admin)");
    if (role == User::Role::AdminRole)
        return MessengerAdminUserAddMenu::tr("admin", "User role label (admin)");
    return MessengerAdminUserAddMenu::tr("user", "User role label (non-admin)");
}
} // namespace

#include "MessengerUserKey.h"

MessengerAdminUserAddMenu::MessengerAdminUserAddMenu(MessengerMenuManager *manager,
                                                     MessengerAdminUsersState *state)
    : MessengerAbstractMenu(manager)
    , m_state(state)
{
    // do nothing
}

QString MessengerAdminUserAddMenu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuAdminUserAdd);
}

bool MessengerAdminUserAddMenu::adminsOnly() const
{
    return true;
}

MessengerMenu::MenuPage MessengerAdminUserAddMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuAdminUserAdd);
    page.adminsOnly = true;
    page.title = tr("Add user");

    MessengerMenuManager *mgr = menuManager();
    if (!mgr || !m_state) {
        page.body = tr("Menu session is not available right now.");
        return page;
    }

    mgr->setCurrentMenuId(context, page.id, false);

    const QString sid = mgr->sessionId(context);
    MessengerAdminUsersState::State *state = m_state->ensure(sid);
    if (!state) {
        page.body = tr("Menu session is not available right now.");
        page.rows.append({ mgr->makeNavigateButton(tr("Back to user management"),
                                                   QLatin1String(MessengerMenuIds::kMenuAdminUsers),
                                                   true) });
        return page;
    }

    if (state->wizard.mode != MessengerAdminUsersState::UserWizardMode::Add) {
        state->resetFlows();
        state->wizard.mode = MessengerAdminUsersState::UserWizardMode::Add;
        state->wizard.step = MessengerAdminUsersState::UserWizardStep::UserKey;
        state->wizard.user = MessengerUserKey(context.chat.messenger, QString());
    }

    if (state->selector.purpose
        == MessengerAdminUsersState::SelectorPurpose::AdminPendingAccessUserAdd) {
        MessengerBot *bot = mgr->bot();
        ServerDatabase *database = bot ? bot->database() : nullptr;
        const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
        if (!database || !database->canManageServerSettings(actor)) {
            state->selector.clear();
            state->wizard.errorText = MessengerUserFacingErrors::accessDeniedAdminsOnly();
            state->wizard.mode = MessengerAdminUsersState::UserWizardMode::Add;
            state->wizard.step = MessengerAdminUsersState::UserWizardStep::UserKey;
        } else {
            return m_state->buildSelectorPage(
                    page.id,
                    tr("Select a user awaiting access activation"),
                    MessengerTextRender::tr("No users awaiting access activation recorded "
                                            "recently."),
                    page.id,
                    true,
                    state);
        }
    }

    switch (state->wizard.step) {
    case MessengerAdminUsersState::UserWizardStep::UserKey:
        page.body = tr("Send user identifier in format messenger:id.\n"
                       "Example: telegram:1234");
        if (MessengerBot *bot = mgr->bot()) {
            ServerDatabase *database = bot->database();
            const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
            if (database && database->canManageServerSettings(actor)) {
                page.rows.append(
                        { mgr->makeEventButton(tr("Choose from users awaiting access activation"),
                                               QStringLiteral("uw:choose:pending_access"),
                                               MessengerMenu::AccessLevel::ServerAdminsOnly) });
            }
        }
        break;
    case MessengerAdminUsersState::UserWizardStep::FirstName: {
        const QString key = state->wizard.user.toString();
        page.body = tr("User: %1\nSend first name, or press Skip to leave it empty.").arg(key);
        page.rows.append({ mgr->makeEventButton(tr("Back"), QStringLiteral("uw:back"), true),
                           mgr->makeEventButton(tr("Skip"), QStringLiteral("uw:skip"), true) });
        break;
    }
    case MessengerAdminUsersState::UserWizardStep::LastName: {
        const QString key = state->wizard.user.toString();
        page.body = tr("User: %1\nSend last name, or press Skip to leave it empty.").arg(key);
        page.rows.append({ mgr->makeEventButton(tr("Back"), QStringLiteral("uw:back"), true),
                           mgr->makeEventButton(tr("Skip"), QStringLiteral("uw:skip"), true) });
        break;
    }
    case MessengerAdminUsersState::UserWizardStep::Role:
        page.body = tr("Choose a role for the new user:");
        page.rows.append(
                { mgr->makeEventButton(tr("User"), QStringLiteral("uw:role:user"), true),
                  mgr->makeEventButton(tr("Admin"), QStringLiteral("uw:role:admin"), true) });
        page.rows.append({ mgr->makeEventButton(tr("Back"), QStringLiteral("uw:back"), true) });
        break;
    case MessengerAdminUsersState::UserWizardStep::Groups:
        page.body = tr("Send group roles as group=user,group/subgroup=admin, or - for Root (all "
                       "groups).");
        {
            QVector<MessengerMenu::MenuButton> row;
            row.append(mgr->makeEventButton(tr("Back"), QStringLiteral("uw:back"), true));
            row.append(mgr->makeEventButton(tr("Use administered groups"),
                                            QStringLiteral("uw:groups:default"),
                                            true));
            page.rows.append(row);

            MessengerBot *bot = mgr->bot();
            ServerDatabase *database = bot ? bot->database() : nullptr;
            const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
            if (database && database->canManageServerSettings(actor)) {
                page.rows.append({ mgr->makeEventButton(
                        MessengerTextRender::formatGroupDisplayName(Group::root(), false),
                        QStringLiteral("uw:groups:none"),
                        MessengerMenu::AccessLevel::ServerAdminsOnly) });
            }
        }
        break;
    case MessengerAdminUsersState::UserWizardStep::Review: {
        const QString key = state->wizard.user.toString();
        const QString displayName =
                QStringLiteral("%1 %2")
                        .arg(state->wizard.firstName.trimmed(), state->wizard.lastName.trimmed())
                        .trimmed();
        const QString role = roleLabelForGroupRoles(state->wizard.groupRoles);

        page.body = tr("Confirm adding user:\n"
                       "User: %1\n"
                       "Name: %2\n"
                       "Role: %3\n"
                       "Groups: %4")
                            .arg(key,
                                 displayName.isEmpty() ? tr("(empty)") : displayName,
                                 role,
                                 formatGroupRoles(state->wizard.groupRoles));

        page.rows.append(
                { mgr->makeEventButton(tr("Edit user id"), QStringLiteral("uw:edit:id"), true) });
        page.rows.append({ mgr->makeEventButton(tr("Edit first name"),
                                                QStringLiteral("uw:edit:first"),
                                                true),
                           mgr->makeEventButton(tr("Edit last name"),
                                                QStringLiteral("uw:edit:last"),
                                                true) });
        page.rows.append(
                { mgr->makeEventButton(tr("Edit role"), QStringLiteral("uw:edit:role"), true) });
        page.rows.append({ mgr->makeEventButton(tr("Edit groups"),
                                                QStringLiteral("uw:edit:groups"),
                                                true) });
        page.rows.append(
                { mgr->makeEventButton(tr("Accept"), QStringLiteral("uw:confirm"), true) });
        break;
    }
    default:
        break;
    }

    MessengerMenuManager::prependErrorBlock(&page, state->wizard.errorText);

    page.rows.append({ mgr->makeNavigateButton(tr("Back to user management"),
                                               QLatin1String(MessengerMenuIds::kMenuAdminUsers),
                                               true) });
    return page;
}

bool MessengerAdminUserAddMenu::handleEvent(const QString &event,
                                            const QString &sessionId,
                                            const MessengerMenu::RenderContext &context,
                                            MessengerMenuManager::ActionResult *result) const
{
    if (!result || !m_state)
        return false;

    MessengerAdminUsersState::State *state = m_state->ensure(sessionId);
    if (!state)
        return false;

    return m_state->handleEvent(event, sessionId, context, state, result);
}

bool MessengerAdminUserAddMenu::handleTextInput(const QString &text,
                                                const QString &sessionId,
                                                const MessengerMenu::RenderContext &context,
                                                MessengerMenuManager::ActionResult *result) const
{
    if (!result || !m_state)
        return false;

    MessengerAdminUsersState::State *state = m_state->ensure(sessionId);
    if (!state)
        return false;

    return m_state->handleTextInput(text, sessionId, context, state, result);
}
