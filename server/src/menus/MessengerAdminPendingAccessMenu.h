#ifndef MESSENGER_ADMIN_PENDING_ACCESS_MENU_H
#define MESSENGER_ADMIN_PENDING_ACCESS_MENU_H

#include "MessengerAbstractMenu.h"

class MessengerAdminPendingAccessMenu final : public MessengerAbstractMenu
{
    Q_OBJECT

public:
    explicit MessengerAdminPendingAccessMenu(MessengerMenuManager *manager);

    QString menuId() const override;

    bool adminsOnly() const override;
    MessengerMenu::AccessLevel accessLevel() const override;

    MessengerMenu::MenuPage buildPage(const MessengerMenu::RenderContext &context) const override;
};

BOOKING_RESOURCES_REGISTER_MENU(MessengerAdminPendingAccessMenu)

#endif // MESSENGER_ADMIN_PENDING_ACCESS_MENU_H
