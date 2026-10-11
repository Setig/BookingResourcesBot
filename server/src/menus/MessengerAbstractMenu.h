#ifndef MESSENGER_ABSTRACT_MENU_H
#define MESSENGER_ABSTRACT_MENU_H

#include <QObject>

#include <QStringList>

#include "MessengerMenuManager.h"
#include "MessengerMenuStaticRegistration.h"

/**
 * @brief Base abstraction for a single menu screen or a menu event/text handler.
 *
 * Implementations live under server/src/menus and are registered in MessengerMenuManager.
 *
 * Notes:
 * - Menu page implementations should override menuId() + buildPage().
 * - Event/text handler implementations should override eventKeys()/handleEvent()/handleTextInput().
 * - Prefer using tr(...) from the concrete menu/state class to keep translation contexts
 *   aligned with the code that owns the strings.
 */
class MessengerAbstractMenu : public QObject
{
    Q_OBJECT

public:
    explicit MessengerAbstractMenu(MessengerMenuManager *manager);
    ~MessengerAbstractMenu() override;

    MessengerMenuManager *menuManager() const;

    /**
     * @brief Menu id for navigation/routing.
     *
     * Return an empty string for handler-only implementations.
     */
    virtual QString menuId() const = 0;

    /**
     * @brief Whether this menu page requires admin access.
     */
    virtual bool adminsOnly() const;
    /**
     * @brief Access level required for this menu page and its event/text handlers.
     */
    virtual MessengerMenu::AccessLevel accessLevel() const;

    /**
     * @brief Event keys (prefix before ':' in callback event value) handled by this menu.
     *
     * Example: "ur" for all ur:* events.
     */
    virtual QStringList eventKeys() const;

    /**
     * @brief Builds a logical page representation for rendering.
     *
     * Handler-only menus may keep the default empty implementation.
     */
    virtual MessengerMenu::MenuPage buildPage(const MessengerMenu::RenderContext &context) const;

    /**
     * @brief Handles a callback event (ActionType::Event).
     *
     * @return true if the event was handled.
     */
    virtual bool handleEvent(const QString &event,
                             const QString &sessionId,
                             const MessengerMenu::RenderContext &context,
                             MessengerMenuManager::ActionResult *result) const;

    /**
     * @brief Handles free-form text input from user while a wizard/flow is active.
     *
     * @return true if input was handled.
     */
    virtual bool handleTextInput(const QString &text,
                                 const QString &sessionId,
                                 const MessengerMenu::RenderContext &context,
                                 MessengerMenuManager::ActionResult *result) const;

private:
    MessengerMenuManager *m_manager; // non-owning
};

#endif // MESSENGER_ABSTRACT_MENU_H
