#ifndef MESSENGER_SOONEST_RESOURCES_10_MENU_H
#define MESSENGER_SOONEST_RESOURCES_10_MENU_H

#include "MessengerAbstractMenu.h"

class MessengerSoonestResources10Menu final : public MessengerAbstractMenu
{
    Q_OBJECT

public:
    explicit MessengerSoonestResources10Menu(MessengerMenuManager *manager);

    QString menuId() const override;
    MessengerMenu::MenuPage buildPage(const MessengerMenu::RenderContext &context) const override;
};

BOOKING_RESOURCES_REGISTER_MENU(MessengerSoonestResources10Menu)

#endif // MESSENGER_SOONEST_RESOURCES_10_MENU_H
