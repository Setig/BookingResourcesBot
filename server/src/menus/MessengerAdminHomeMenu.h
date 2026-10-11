#ifndef MESSENGER_ADMIN_HOME_MENU_H
#define MESSENGER_ADMIN_HOME_MENU_H

#include "MessengerAbstractMenu.h"

class MessengerAdminHomeMenu final : public MessengerAbstractMenu
{
    Q_OBJECT

public:
    explicit MessengerAdminHomeMenu(MessengerMenuManager *manager);

    QString menuId() const override;

    bool adminsOnly() const override;

    MessengerMenu::MenuPage buildPage(const MessengerMenu::RenderContext &context) const override;
};

BOOKING_RESOURCES_REGISTER_MENU(MessengerAdminHomeMenu)

#endif // MESSENGER_ADMIN_HOME_MENU_H
