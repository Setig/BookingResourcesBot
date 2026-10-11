#include "MessengerAdminUserCardMenu.h"

#include "Group.h"
#include "MessengerMenuIds.h"
#include "MessengerAdminUsersState.h"

#include "User.h"
#include "UserModel.h"
#include "MessengerBot.h"
#include "ServerDatabase.h"
#include "MessengerTextRender.h"

MessengerAdminUserCardMenu::MessengerAdminUserCardMenu(MessengerMenuManager *manager,
                                                       MessengerAdminUsersState *state)
    : MessengerAbstractMenu(manager)
    , m_state(state)
{
    // do nothing
}

QString MessengerAdminUserCardMenu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuAdminUserCard);
}

bool MessengerAdminUserCardMenu::adminsOnly() const
{
    return true;
}

MessengerMenu::MenuPage MessengerAdminUserCardMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuAdminUserCard);
    page.adminsOnly = true;
    page.title = tr("User card");

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

    if (state->selectedUserKey.trimmed().isEmpty()) {
        if (!m_state->ensureSelector(state,
                                     MessengerAdminUsersState::SelectorPurpose::AdminUserCard,
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
                                          tr("Select a user"),
                                          tr("No users in database."),
                                          QLatin1String(MessengerMenuIds::kMenuAdminUsers),
                                          true,
                                          state);
    }

    MessengerUserKey userKey = MessengerUserKey::fromString(state->selectedUserKey);

    User user;
    MessengerBot *bot = mgr->bot();
    ServerDatabase *database = bot ? bot->database() : nullptr;
    const UserModel *userModel =
            (database && database->isReady()) ? database->userModel() : nullptr;
    if (userModel) {
        user = userModel->userByKey(userKey);
    }
    const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
    if (!user.isValid() || !database->canAdministerUser(actor, user)) {
        state->selectedUserKey.clear();
        page.body = tr("User not found.");
        page.rows.append({ mgr->makeNavigateButton(tr("Back to user management"),
                                                   QLatin1String(MessengerMenuIds::kMenuAdminUsers),
                                                   true) });
        return page;
    }

    const QString display = MessengerAdminUsersState::userDisplayNameForButton(user);
    const QString role = user.isFixedAdmin() ? tr("fixed admin", "User role label (fixed admin)")
                         : user.isAdmin()    ? tr("admin", "User role label (admin)")
                                             : tr("user", "User role label (non-admin)");

    QStringList groupEntries;
    const QMap<Group, User::Role> roles = user.groupRoles();
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
        groupEntries.append(
                QStringLiteral("%1=%2").arg(MessengerTextRender::formatGroupDisplayName(it.key(),
                                                                                        false),
                                            it.value() == User::Role::FixedAdminRole
                                                    ? tr("fixed admin")
                                            : it.value() == User::Role::AdminRole ? tr("admin")
                                                                                  : tr("user")));
    }
    page.body = tr("User: %1\nRole: %2\nGroups: %3")
                        .arg(display,
                             role,
                             groupEntries.isEmpty() ? tr("Root (all groups)")
                                                    : groupEntries.join(tr(", ")))
                        .trimmed();
    QVector<MessengerMenu::MenuButton> actions;
    actions.append(mgr->makeEventButton(tr("Edit user"), QStringLiteral("uc:edit"), true));
    if (!user.isFixedAdmin()) {
        actions.append(mgr->makeEventButton(tr("Delete user"), QStringLiteral("uc:delete"), true));
    }
    page.rows.append(actions);
    page.rows.append({ mgr->makeNavigateButton(tr("Back to user management"),
                                               QLatin1String(MessengerMenuIds::kMenuAdminUsers),
                                               true) });
    return page;
}

bool MessengerAdminUserCardMenu::handleEvent(const QString &event,
                                             const QString &sessionId,
                                             const MessengerMenu::RenderContext &context,
                                             MessengerMenuManager::ActionResult *result) const
{
    if (!result || !m_state)
        return false;

    MessengerMenuManager *mgr = menuManager();
    if (!mgr)
        return false;

    MessengerAdminUsersState::State *state = m_state->ensure(sessionId);
    if (!state)
        return false;

    return m_state->handleEvent(event, sessionId, context, state, result);
}
