#ifndef MESSENGER_USER_RESOURCE_CARD_MENU_H
#define MESSENGER_USER_RESOURCE_CARD_MENU_H

#include "MessengerAbstractMenu.h"

class MessengerUserResourcesState;

class MessengerUserResourceCardMenu final : public MessengerAbstractMenu
{
    Q_OBJECT

public:
    MessengerUserResourceCardMenu(MessengerMenuManager *manager,
                                  MessengerUserResourcesState *state);

    QString menuId() const override;
    MessengerMenu::MenuPage buildPage(const MessengerMenu::RenderContext &context) const override;

    bool handleEvent(const QString &event,
                     const QString &sessionId,
                     const MessengerMenu::RenderContext &context,
                     MessengerMenuManager::ActionResult *result) const override;

private:
    MessengerUserResourcesState *m_state; // non-owning
};

BOOKING_RESOURCES_REGISTER_MENU_USER_RESOURCES_STATE(MessengerUserResourceCardMenu)

#endif // MESSENGER_USER_RESOURCE_CARD_MENU_H
