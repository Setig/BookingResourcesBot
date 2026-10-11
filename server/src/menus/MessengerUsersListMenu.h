#ifndef MESSENGER_USERS_LIST_MENU_H
#define MESSENGER_USERS_LIST_MENU_H

#include "MessengerAbstractMenu.h"

class MessengerUsersListMenu final : public MessengerAbstractMenu
{
    Q_OBJECT

public:
    explicit MessengerUsersListMenu(MessengerMenuManager *manager);

    QString menuId() const override;
    MessengerMenu::MenuPage buildPage(const MessengerMenu::RenderContext &context) const override;
};

BOOKING_RESOURCES_REGISTER_MENU(MessengerUsersListMenu)

#endif // MESSENGER_USERS_LIST_MENU_H
