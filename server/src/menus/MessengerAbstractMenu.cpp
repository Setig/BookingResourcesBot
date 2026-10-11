#include "MessengerAbstractMenu.h"

MessengerAbstractMenu::MessengerAbstractMenu(MessengerMenuManager *manager)
    : QObject(manager)
    , m_manager(manager)
{
    // do nothing
}

MessengerAbstractMenu::~MessengerAbstractMenu()
{
    // do nothing
}

MessengerMenuManager *MessengerAbstractMenu::menuManager() const
{
    return m_manager;
}

bool MessengerAbstractMenu::adminsOnly() const
{
    return false;
}

MessengerMenu::AccessLevel MessengerAbstractMenu::accessLevel() const
{
    return MessengerMenu::AccessLevel::Everyone;
}

QStringList MessengerAbstractMenu::eventKeys() const
{
    return {};
}

MessengerMenu::MenuPage MessengerAbstractMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    Q_UNUSED(context);
    return MessengerMenu::MenuPage{};
}

bool MessengerAbstractMenu::handleEvent(const QString &event,
                                        const QString &sessionId,
                                        const MessengerMenu::RenderContext &context,
                                        MessengerMenuManager::ActionResult *result) const
{
    Q_UNUSED(event);
    Q_UNUSED(sessionId);
    Q_UNUSED(context);
    Q_UNUSED(result);
    return false;
}

bool MessengerAbstractMenu::handleTextInput(const QString &text,
                                            const QString &sessionId,
                                            const MessengerMenu::RenderContext &context,
                                            MessengerMenuManager::ActionResult *result) const
{
    Q_UNUSED(text);
    Q_UNUSED(sessionId);
    Q_UNUSED(context);
    Q_UNUSED(result);
    return false;
}
