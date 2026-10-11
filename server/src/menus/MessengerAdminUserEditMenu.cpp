#include "MessengerAdminUserEditMenu.h"

#include "Group.h"
#include "User.h"
#include "UserModel.h"
#include "MessengerBot.h"
#include "MessengerMenuIds.h"
#include "ServerDatabase.h"
#include "MessengerTextRender.h"
#include "MessengerAdminUsersState.h"

#include "MessengerUserKey.h"

namespace {
QString formatGroupRoles(const QMap<Group, User::Role> &roles)
{
    if (roles.isEmpty())
        return MessengerAdminUserEditMenu::tr("Root (all groups)");
    QStringList values;
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
        values.append(QStringLiteral("%1=%2").arg(
                MessengerTextRender::formatGroupDisplayName(it.key(), false),
                it.value() == User::Role::FixedAdminRole
                        ? MessengerAdminUserEditMenu::tr("fixed admin",
                                                         "User role label (fixed admin)")
                : it.value() == User::Role::AdminRole ? MessengerAdminUserEditMenu::tr("admin")
                                                      : MessengerAdminUserEditMenu::tr("user")));
    }
    return values.join(QStringLiteral(", "));
}

QString roleLabelForGroupRoles(const QMap<Group, User::Role> &roles)
{
    User user;
    user.setGroupRoles(roles);
    const User::Role role = user.role();
    if (role == User::Role::FixedAdminRole)
        return MessengerAdminUserEditMenu::tr("fixed admin", "User role label (fixed admin)");
    if (role == User::Role::AdminRole)
        return MessengerAdminUserEditMenu::tr("admin", "User role label (admin)");
    return MessengerAdminUserEditMenu::tr("user", "User role label (non-admin)");
}
} // namespace

MessengerAdminUserEditMenu::MessengerAdminUserEditMenu(MessengerMenuManager *manager,
                                                       MessengerAdminUsersState *state)
    : MessengerAbstractMenu(manager)
    , m_state(state)
{
    // do nothing
}

QString MessengerAdminUserEditMenu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuAdminUserEdit);
}

bool MessengerAdminUserEditMenu::adminsOnly() const
{
    return true;
}

MessengerMenu::MenuPage MessengerAdminUserEditMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuAdminUserEdit);
    page.adminsOnly = true;
    page.title = tr("Edit user");

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

    if (state->wizard.mode != MessengerAdminUsersState::UserWizardMode::Edit) {
        if (!m_state->ensureSelector(state,
                                     MessengerAdminUsersState::SelectorPurpose::AdminUserEdit,
                                     AbstractBot::senderKey(context.chat, context.sender),
                                     false)) {
            page.body = tr("Database is not available right now.");
            page.rows.append(
                    { mgr->makeNavigateButton(tr("Back to user management"),
                                              QLatin1String(MessengerMenuIds::kMenuAdminUsers),
                                              true) });
            return page;
        }

        return m_state->buildSelectorPage(page.id,
                                          tr("Select a user to edit"),
                                          tr("No users in database."),
                                          QLatin1String(MessengerMenuIds::kMenuAdminUsers),
                                          true,
                                          state);
    }

    MessengerBot *bot = mgr->bot();
    ServerDatabase *database = bot ? bot->database() : nullptr;
    const UserModel *model = (database && database->isReady()) ? database->userModel() : nullptr;
    const User current = model ? model->userByKey(state->wizard.user) : User();
    const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
    if (!current.isValid() || !database->canAdministerUser(actor, current)) {
        state->resetFlows();
        page.body = tr("User not found.");
        page.rows.append({ mgr->makeNavigateButton(tr("Back to user management"),
                                                   QLatin1String(MessengerMenuIds::kMenuAdminUsers),
                                                   true) });
        return page;
    }

    const QString key = state->wizard.user.toString();

    switch (state->wizard.step) {
    case MessengerAdminUsersState::UserWizardStep::UserKey:
        // For safety, treat unexpected user-id step as "edit first name".
        [[fallthrough]];
    case MessengerAdminUsersState::UserWizardStep::FirstName:
        page.body = tr("User: %1\nCurrent first name: %2\n"
                       "Send new first name, or press Keep to keep current.")
                            .arg(key,
                                 state->wizard.existingFirstName.trimmed().isEmpty()
                                         ? tr("(empty)")
                                         : state->wizard.existingFirstName.trimmed());
        page.rows.append({ mgr->makeEventButton(tr("Back"), QStringLiteral("uw:back"), true),
                           mgr->makeEventButton(tr("Keep"), QStringLiteral("uw:skip"), true) });
        break;
    case MessengerAdminUsersState::UserWizardStep::LastName:
        page.body = tr("User: %1\nCurrent last name: %2\n"
                       "Send new last name, or press Keep to keep current.")
                            .arg(key,
                                 state->wizard.existingLastName.trimmed().isEmpty()
                                         ? tr("(empty)")
                                         : state->wizard.existingLastName.trimmed());
        page.rows.append({ mgr->makeEventButton(tr("Back"), QStringLiteral("uw:back"), true),
                           mgr->makeEventButton(tr("Keep"), QStringLiteral("uw:skip"), true) });
        break;
    case MessengerAdminUsersState::UserWizardStep::Role:
        if (state->wizard.existingFixedAdmin) {
            const QString roleLabel = tr("fixed admin", "User role label (fixed admin)");
            page.body = tr("User: %1\nRole: %2").arg(key, roleLabel).trimmed();
            page.rows.append({ mgr->makeEventButton(tr("Back"), QStringLiteral("uw:back"), true) });
        } else {
            page.body = tr("User: %1\nChoose a role:").arg(key);
            page.rows.append(
                    { mgr->makeEventButton(tr("User"), QStringLiteral("uw:role:user"), true),
                      mgr->makeEventButton(tr("Admin"), QStringLiteral("uw:role:admin"), true) });
            page.rows.append({ mgr->makeEventButton(tr("Back"), QStringLiteral("uw:back"), true) });
        }
        break;
    case MessengerAdminUsersState::UserWizardStep::Groups:
        page.body = tr("User: %1\nCurrent groups: %2\n"
                       "Send group roles as group=user,group/subgroup=admin, or press Keep.")
                            .arg(key, formatGroupRoles(state->wizard.existingGroupRoles));
        page.rows.append({ mgr->makeEventButton(tr("Back"), QStringLiteral("uw:back"), true),
                           mgr->makeEventButton(tr("Keep"), QStringLiteral("uw:skip"), true) });
        break;
    case MessengerAdminUsersState::UserWizardStep::Review: {
        const QString role = roleLabelForGroupRoles(state->wizard.groupRoles);
        const QString name =
                QStringLiteral("%1 %2")
                        .arg(state->wizard.firstName.trimmed(), state->wizard.lastName.trimmed())
                        .trimmed();
        page.body = tr("User details:\n"
                       "User: %1\n"
                       "Name: %2\n"
                       "Role: %3\n"
                       "Groups: %4\n"
                       "\n"
                       "Select what to change, then press Save changes.")
                            .arg(key,
                                 name.isEmpty() ? tr("(empty)") : name,
                                 role,
                                 formatGroupRoles(state->wizard.groupRoles));
        page.rows.append({ mgr->makeEventButton(tr("Edit first name"),
                                                QStringLiteral("uw:edit:first"),
                                                true),
                           mgr->makeEventButton(tr("Edit last name"),
                                                QStringLiteral("uw:edit:last"),
                                                true) });
        if (!state->wizard.existingFixedAdmin) {
            page.rows.append({ mgr->makeEventButton(tr("Edit role"),
                                                    QStringLiteral("uw:edit:role"),
                                                    true) });
        }
        page.rows.append({ mgr->makeEventButton(tr("Edit groups"),
                                                QStringLiteral("uw:edit:groups"),
                                                true) });
        page.rows.append(
                { mgr->makeEventButton(tr("Save changes"), QStringLiteral("uw:confirm"), true) });
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

bool MessengerAdminUserEditMenu::handleEvent(const QString &event,
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

bool MessengerAdminUserEditMenu::handleTextInput(const QString &text,
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
