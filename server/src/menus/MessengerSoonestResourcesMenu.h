#ifndef MESSENGER_SOONEST_RESOURCES_MENU_H
#define MESSENGER_SOONEST_RESOURCES_MENU_H

#include "MessengerAbstractMenu.h"

class MessengerSoonestResourcesMenu final : public MessengerAbstractMenu
{
    Q_OBJECT

public:
    explicit MessengerSoonestResourcesMenu(MessengerMenuManager *manager);

    QString menuId() const override;
    MessengerMenu::MenuPage buildPage(const MessengerMenu::RenderContext &context) const override;
};

BOOKING_RESOURCES_REGISTER_MENU(MessengerSoonestResourcesMenu)

#endif // MESSENGER_SOONEST_RESOURCES_MENU_H
