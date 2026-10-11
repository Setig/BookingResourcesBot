#ifndef MESSENGER_ADMIN_RESOURCE_DELETE_MENU_H
#define MESSENGER_ADMIN_RESOURCE_DELETE_MENU_H

#include "MessengerAbstractMenu.h"

class MessengerAdminResourcesState;

class MessengerAdminResourceDeleteMenu final : public MessengerAbstractMenu
{
    Q_OBJECT

public:
    MessengerAdminResourceDeleteMenu(MessengerMenuManager *manager,
                                     MessengerAdminResourcesState *state);

    QString menuId() const override;

    bool adminsOnly() const override;

    MessengerMenu::MenuPage buildPage(const MessengerMenu::RenderContext &context) const override;

    bool handleEvent(const QString &event,
                     const QString &sessionId,
                     const MessengerMenu::RenderContext &context,
                     MessengerMenuManager::ActionResult *result) const override;

private:
    MessengerAdminResourcesState *m_state; // non-owning
};

BOOKING_RESOURCES_REGISTER_MENU_ADMIN_RESOURCES_STATE(MessengerAdminResourceDeleteMenu)

#endif // MESSENGER_ADMIN_RESOURCE_DELETE_MENU_H
