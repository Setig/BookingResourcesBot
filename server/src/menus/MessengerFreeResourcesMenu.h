#ifndef MESSENGER_FREE_RESOURCES_MENU_H
#define MESSENGER_FREE_RESOURCES_MENU_H

#include "MessengerAbstractMenu.h"

class MessengerFreeResourcesMenu final : public MessengerAbstractMenu
{
    Q_OBJECT

public:
    explicit MessengerFreeResourcesMenu(MessengerMenuManager *manager);

    QString menuId() const override;
    MessengerMenu::MenuPage buildPage(const MessengerMenu::RenderContext &context) const override;
};

BOOKING_RESOURCES_REGISTER_MENU(MessengerFreeResourcesMenu)

#endif // MESSENGER_FREE_RESOURCES_MENU_H
