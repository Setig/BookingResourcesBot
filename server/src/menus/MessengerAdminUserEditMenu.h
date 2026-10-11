#ifndef MESSENGER_ADMIN_USER_EDIT_MENU_H
#define MESSENGER_ADMIN_USER_EDIT_MENU_H

#include "MessengerAbstractMenu.h"

class MessengerAdminUsersState;

class MessengerAdminUserEditMenu final : public MessengerAbstractMenu
{
    Q_OBJECT

public:
    MessengerAdminUserEditMenu(MessengerMenuManager *manager, MessengerAdminUsersState *state);

    QString menuId() const override;

    bool adminsOnly() const override;

    MessengerMenu::MenuPage buildPage(const MessengerMenu::RenderContext &context) const override;

    bool handleEvent(const QString &event,
                     const QString &sessionId,
                     const MessengerMenu::RenderContext &context,
                     MessengerMenuManager::ActionResult *result) const override;

    bool handleTextInput(const QString &text,
                         const QString &sessionId,
                         const MessengerMenu::RenderContext &context,
                         MessengerMenuManager::ActionResult *result) const override;

private:
    MessengerAdminUsersState *m_state; // non-owning
};

BOOKING_RESOURCES_REGISTER_MENU_ADMIN_USERS_STATE(MessengerAdminUserEditMenu)

#endif // MESSENGER_ADMIN_USER_EDIT_MENU_H
