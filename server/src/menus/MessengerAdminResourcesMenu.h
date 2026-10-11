#ifndef MESSENGER_ADMIN_RESOURCES_MENU_H
#define MESSENGER_ADMIN_RESOURCES_MENU_H

#include "MessengerAbstractMenu.h"

class MessengerAdminResourcesMenu final : public MessengerAbstractMenu
{
    Q_OBJECT

public:
    explicit MessengerAdminResourcesMenu(MessengerMenuManager *manager);

    QString menuId() const override;

    bool adminsOnly() const override;

    MessengerMenu::MenuPage buildPage(const MessengerMenu::RenderContext &context) const override;
};

BOOKING_RESOURCES_REGISTER_MENU(MessengerAdminResourcesMenu)

#endif // MESSENGER_ADMIN_RESOURCES_MENU_H
