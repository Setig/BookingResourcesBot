#ifndef MESSENGER_MAIN_MENU_H
#define MESSENGER_MAIN_MENU_H

#include "MessengerAbstractMenu.h"

class MessengerMainMenu final : public MessengerAbstractMenu
{
    Q_OBJECT

public:
    explicit MessengerMainMenu(MessengerMenuManager *manager);

    QString menuId() const override;
    MessengerMenu::MenuPage buildPage(const MessengerMenu::RenderContext &context) const override;
};

BOOKING_RESOURCES_REGISTER_MENU(MessengerMainMenu)

#endif // MESSENGER_MAIN_MENU_H
