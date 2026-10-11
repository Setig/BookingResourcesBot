#ifndef MESSENGER_MENU_MANAGER_TYPES_H
#define MESSENGER_MENU_MANAGER_TYPES_H

#include <QString>
#include <QVector>

#include "AbstractBot.h"

namespace MessengerMenu {

/**
 * @brief Access level required to render or handle a menu element.
 */
enum class AccessLevel {
    Everyone = 0,
    AdminsOnly,
    ServerAdminsOnly
};

/**
 * @brief Context used for menu rendering and access checks.
 */
struct RenderContext
{
    bool isAdmin = false;
    bool canManageServerSettings = false;
    AbstractBot::ChatAddress chat;
    AbstractBot::UserAddress sender;
};

/**
 * @brief Button action type encoded into callback payloads.
 */
enum class ButtonActionType {
    Command = 0,
    Navigate,
    Event
};

/**
 * @brief Button action payload (type + value).
 */
struct ButtonAction
{
    ButtonActionType type = ButtonActionType::Command;
    QString value;
};

/**
 * @brief Logical menu button definition used for rendering.
 */
struct MenuButton
{
    QString text;
    ButtonAction action;
    bool adminsOnly = false;
    AccessLevel accessLevel = AccessLevel::Everyone;
};

/**
 * @brief Logical menu page definition (title/body + rows of buttons).
 */
struct MenuPage
{
    QString id;
    QString title;
    QString body;

    struct DynamicText
    {
        QString text;
        AbstractBot::MessageFormat format = AbstractBot::MessageFormat::PlainText;
    };

    std::function<DynamicText(const RenderContext &context)> dynamicText;
    QVector<QVector<MenuButton>> rows;
    bool adminsOnly = false;
    AccessLevel accessLevel = AccessLevel::Everyone;
};

/**
 * @brief Minimal per-user menu session owned by the core manager.
 *
 * Menu-specific state (wizards/selectors/etc.) should live in menu implementations.
 */
struct Session
{
    QString currentMenuId;
    QString lastMenuMessageId;
};

} // namespace MessengerMenu

#endif // MESSENGER_MENU_MANAGER_TYPES_H
