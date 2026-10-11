#include "MessengerAdminUserDeleteMenu.h"

#include "MessengerMenuIds.h"
#include "MessengerAdminUsersState.h"

#include "User.h"
#include "UserModel.h"
#include "MessengerBot.h"
#include "ServerDatabase.h"
#include "MessengerUserKey.h"

MessengerAdminUserDeleteMenu::MessengerAdminUserDeleteMenu(MessengerMenuManager *manager,
                                                           MessengerAdminUsersState *state)
    : MessengerAbstractMenu(manager)
    , m_state(state)
{
    // do nothing
}

QString MessengerAdminUserDeleteMenu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuAdminUserDelete);
}

bool MessengerAdminUserDeleteMenu::adminsOnly() const
{
    return true;
}

MessengerMenu::MenuPage MessengerAdminUserDeleteMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuAdminUserDelete);
    page.adminsOnly = true;
    page.title = tr("Delete user");

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

    if (state->pendingDeleteUserKey.trimmed().isEmpty()) {
        if (!m_state->ensureSelector(state,
                                     MessengerAdminUsersState::SelectorPurpose::AdminUserDelete,
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
                                          tr("Select a user to delete"),
                                          tr("No users in database."),
                                          QLatin1String(MessengerMenuIds::kMenuAdminUsers),
                                          true,
                                          state);
    }

    QString pendingUserLabel = state->pendingDeleteUserKey.trimmed();
    bool keyOk = false;
    const MessengerUserKey pendingKey = MessengerUserKey::fromString(pendingUserLabel, &keyOk);
    if (keyOk) {
        const QString normalizedKey = pendingKey.toString().trimmed();

        MessengerBot *bot = mgr->bot();
        ServerDatabase *database = bot ? bot->database() : nullptr;
        const UserModel *userModel =
                (database && database->isReady()) ? database->userModel() : nullptr;
        const User user = userModel ? userModel->userByKey(pendingKey) : User();
        const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
        if (!user.isValid() || !database->canAdministerUser(actor, user)) {
            state->pendingDeleteUserKey.clear();
            page.body = tr("User not found.");
            page.rows.append(
                    { mgr->makeNavigateButton(tr("Back to user management"),
                                              QLatin1String(MessengerMenuIds::kMenuAdminUsers),
                                              true) });
            return page;
        }

        QString display = normalizedKey;
        const QString label = MessengerAdminUsersState::userLabelForButton(user).trimmed();
        if (!label.isEmpty())
            display = label;
        if (!display.isEmpty() && !normalizedKey.isEmpty() && display != normalizedKey) {
            pendingUserLabel = QStringLiteral("%1 (%2)").arg(display, normalizedKey);
        } else if (!display.isEmpty()) {
            pendingUserLabel = display;
        } else if (!normalizedKey.isEmpty()) {
            pendingUserLabel = normalizedKey;
        }
    }

    page.body = tr("Confirm deleting user:\n%1").arg(pendingUserLabel);
    page.rows.append({ mgr->makeEventButton(tr("Yes"), QStringLiteral("delu:yes"), true) });
    page.rows.append({ mgr->makeNavigateButton(tr("Back to user management"),
                                               QLatin1String(MessengerMenuIds::kMenuAdminUsers),
                                               true) });
    return page;
}

bool MessengerAdminUserDeleteMenu::handleEvent(const QString &event,
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
