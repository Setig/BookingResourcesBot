#ifndef MESSENGER_ADMIN_USERS_MENU_H
#define MESSENGER_ADMIN_USERS_MENU_H

#include "MessengerAbstractMenu.h"

class MessengerAdminUsersMenu final : public MessengerAbstractMenu
{
    Q_OBJECT

public:
    explicit MessengerAdminUsersMenu(MessengerMenuManager *manager);

    QString menuId() const override;

    bool adminsOnly() const override;

    MessengerMenu::MenuPage buildPage(const MessengerMenu::RenderContext &context) const override;
};

BOOKING_RESOURCES_REGISTER_MENU(MessengerAdminUsersMenu)

#endif // MESSENGER_ADMIN_USERS_MENU_H
