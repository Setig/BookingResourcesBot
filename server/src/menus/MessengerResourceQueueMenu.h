#ifndef MESSENGER_RESOURCE_QUEUE_MENU_H
#define MESSENGER_RESOURCE_QUEUE_MENU_H

#include "MessengerAbstractMenu.h"

class MessengerUserResourcesState;

class MessengerResourceQueueMenu final : public MessengerAbstractMenu
{
    Q_OBJECT

public:
    MessengerResourceQueueMenu(MessengerMenuManager *manager, MessengerUserResourcesState *state);

    QString menuId() const override;
    MessengerMenu::MenuPage buildPage(const MessengerMenu::RenderContext &context) const override;

    bool handleEvent(const QString &event,
                     const QString &sessionId,
                     const MessengerMenu::RenderContext &context,
                     MessengerMenuManager::ActionResult *result) const override;

private:
    MessengerUserResourcesState *m_state; // non-owning
};

BOOKING_RESOURCES_REGISTER_MENU_USER_RESOURCES_STATE(MessengerResourceQueueMenu)

#endif // MESSENGER_RESOURCE_QUEUE_MENU_H
