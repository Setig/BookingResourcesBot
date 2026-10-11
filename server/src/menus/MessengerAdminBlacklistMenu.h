#ifndef MESSENGER_ADMIN_BLACKLIST_MENU_H
#define MESSENGER_ADMIN_BLACKLIST_MENU_H

#include "MessengerAbstractMenu.h"

class MessengerAdminBlacklistMenu final : public MessengerAbstractMenu
{
    Q_OBJECT

public:
    explicit MessengerAdminBlacklistMenu(MessengerMenuManager *manager);

    QString menuId() const override;

    bool adminsOnly() const override;

    MessengerMenu::MenuPage buildPage(const MessengerMenu::RenderContext &context) const override;
};

BOOKING_RESOURCES_REGISTER_MENU(MessengerAdminBlacklistMenu)

#endif // MESSENGER_ADMIN_BLACKLIST_MENU_H
