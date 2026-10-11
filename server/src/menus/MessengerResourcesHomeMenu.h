#ifndef MESSENGER_RESOURCES_HOME_MENU_H
#define MESSENGER_RESOURCES_HOME_MENU_H

#include "MessengerAbstractMenu.h"

class MessengerResourcesHomeMenu final : public MessengerAbstractMenu
{
    Q_OBJECT

public:
    explicit MessengerResourcesHomeMenu(MessengerMenuManager *manager);

    QString menuId() const override;
    QStringList eventKeys() const override;
    MessengerMenu::MenuPage buildPage(const MessengerMenu::RenderContext &context) const override;

    bool handleEvent(const QString &event,
                     const QString &sessionId,
                     const MessengerMenu::RenderContext &context,
                     MessengerMenuManager::ActionResult *result) const override;
};

BOOKING_RESOURCES_REGISTER_MENU(MessengerResourcesHomeMenu)

#endif // MESSENGER_RESOURCES_HOME_MENU_H
