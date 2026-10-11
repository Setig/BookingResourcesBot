#ifndef MESSENGERMENUMANAGER_H
#define MESSENGERMENUMANAGER_H

#include <QObject>

#include <QString>
#include <QDateTime>

#include "MessengerMenuManagerTypes.h"

#include "AbstractBot.h"
#include "UserTimeZone.h"

class MessengerBot;
class MessengerAbstractMenu;

class MessengerMenuManagerPrivate;

/**
 * @brief Builds and handles interactive menus for a MessengerBot.
 *
 * MessengerMenuManager renders menu screens as text plus inline keyboards and converts callback
 * payloads into actions and applies them by sending/editing replies via the owning MessengerBot.
 */
class MessengerMenuManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Controls how menu screens are refreshed for active sessions.
     *
     * - Active: menus are refreshed automatically when database models change.
     * - Passive: menus are refreshed only by pressing explicit "Refresh" buttons.
     */
    enum class MenuUpdateMode {
        Active = 0,
        Passive
    };

    /**
     * @brief Rendered menu reply (text + message options).
     */
    struct Reply
    {
        bool valid = false;
        QString text;
        AbstractBot::MessageOptions options;
    };

    /**
     * @brief Result of handling a menu callback payload.
     *
     * When handled is true, the caller may send the reply (sendReply == true).
     */
    struct ActionResult
    {
        bool handled = false;
        bool sendReply = false;
        bool sendAsNewMessage = false;
        bool preferEdit = false;
        QString editMessageId;
        Reply reply;

        bool editOriginMessage = false;
        QString originMessageId;
        Reply originReply;
    };

    explicit MessengerMenuManager(MessengerBot *bot);
    ~MessengerMenuManager() override;

    /**
     * @brief Performs a best-effort graceful shutdown of the menu subsystem.
     *
     * The shutdown sequence stops internal timers and closes all open menu sessions by sending
     * a final menu message update. This method is idempotent and is intended to be called by the
     * application shutdown coordinator (ServerApplication) before messenger backends are stopped.
     */
    void shutdown();

    [[nodiscard]] MenuUpdateMode menuUpdateMode() const;
    void setMenuUpdateMode(MenuUpdateMode mode);

    /**
     * @brief Returns session inactivity timeout in minutes.
     *
     * When the value is 0, session expiration is disabled.
     */
    [[nodiscard]] uint sessionTimeoutMinutes() const;
    void setSessionTimeoutMinutes(uint minutes);

    /**
     * @brief Returns the owning MessengerBot (non-owning pointer).
     *
     * This is part of the internal menu API used by menu implementations.
     */
    MessengerBot *bot() const;

    /**
     * @brief Returns stable session identifier for a given context.
     *
     * Menu implementations can use it as a key for per-session state storage.
     */
    QString sessionId(const MessengerMenu::RenderContext &context) const;

    // --- Internal menu API (used by server/src/menus implementations) ---

    void setActiveTextHandler(const QString &sessionId, const QString &eventKey);
    void clearActiveTextHandler(const QString &sessionId);

    [[nodiscard]] QString currentMenuId(const MessengerMenu::RenderContext &context) const;
    void setCurrentMenuId(const MessengerMenu::RenderContext &context,
                          const QString &menuId,
                          bool resetFlowsOnChange = true);
    [[nodiscard]] QString lastMenuMessageId(const MessengerMenu::RenderContext &context) const;

    [[nodiscard]] MessengerMenu::MenuButton makeNavigateButton(const QString &text,
                                                               const QString &targetMenuId,
                                                               bool adminsOnly = false) const;
    [[nodiscard]] MessengerMenu::MenuButton makeNavigateButton(
            const QString &text,
            const QString &targetMenuId,
            MessengerMenu::AccessLevel accessLevel) const;
    [[nodiscard]] MessengerMenu::MenuButton makeEventButton(const QString &text,
                                                            const QString &event,
                                                            bool adminsOnly = false) const;
    [[nodiscard]] MessengerMenu::MenuButton makeEventButton(
            const QString &text,
            const QString &event,
            MessengerMenu::AccessLevel accessLevel) const;
    [[nodiscard]] MessengerMenu::MenuButton makeCommandButton(const QString &text,
                                                              const QString &command,
                                                              bool adminsOnly = false) const;
    [[nodiscard]] MessengerMenu::MenuButton makeCommandButton(
            const QString &text,
            const QString &command,
            MessengerMenu::AccessLevel accessLevel) const;

    [[nodiscard]] Reply renderPage(const MessengerMenu::MenuPage &page,
                                   const MessengerMenu::RenderContext &context) const;

    [[nodiscard]] static QString escapeHtml(const QString &text);
    [[nodiscard]] static QString buildAsOfLine(const QDateTime &utc,
                                               AbstractBot::MessageFormat format,
                                               const QString &dateTimeFormat = QString(),
                                               const UserTimeZone &timeZone = UserTimeZone());
    [[nodiscard]] static QString buildTextWithAsOf(const QString &text,
                                                   AbstractBot::MessageFormat format,
                                                   const QString &dateTimeFormat = QString(),
                                                   const UserTimeZone &timeZone = UserTimeZone());
    [[nodiscard]] QString buildTextWithAsOfCurrentFormat(const QString &text,
                                                         AbstractBot::MessageFormat format) const;
    static void prependErrorBlock(MessengerMenu::MenuPage *page, const QString &errorText);

    [[nodiscard]] static Reply buildInputStartedReply();
    [[nodiscard]] static Reply buildPlainTextReply(const QString &text);

    static void markHandledSendReply(ActionResult *result);
    static void requestPreferEdit(ActionResult *result, const QString &messageId);
    static void requestEditOriginMessage(ActionResult *result,
                                         const Reply &originReply,
                                         const QString &originMessageId = QString());
    static void requestSendAsNewMessageAndEditOrigin(ActionResult *result,
                                                     const Reply &originReply,
                                                     const QString &originMessageId = QString());

    Reply renderRoot(const MessengerMenu::RenderContext &context) const;
    Reply renderMenu(const QString &menuId, const MessengerMenu::RenderContext &context) const;
    ActionResult handlePayload(const QString &payload,
                               const MessengerMenu::RenderContext &context,
                               const QString &originMessageId = QString()) const;
    ActionResult handleTextInput(const QString &text,
                                 const MessengerMenu::RenderContext &context) const;

    /**
     * @brief High-level callback handler that fully processes a menu callback query.
     *
     * This method builds a RenderContext, routes the payload to the active menu, and applies the
     * resulting ActionResult by calling MessengerBot::sendReply / editReply.
     */
    void processCallbackQuery(const AbstractBot::CallbackQuery &query) const;

    /**
     * @brief Tries to handle a non-command incoming message as menu text input.
     *
     * Returns true if the message was handled by the menu subsystem (and any replies were sent).
     */
    [[nodiscard]] bool processTextMessage(const AbstractBot::IncomingMessage &message) const;

    /**
     * @brief Remembers the last menu message id for the session identified by @p context.
     *
     * This is used by higher-level code when a menu screen is sent as a new message and the
     * platform message id becomes known asynchronously (e.g. Telegram sendMessage result).
     */
    void rememberMenuMessageId(const MessengerMenu::RenderContext &context,
                               const QString &messageId) const;

    /**
     * @brief Registers that a menu screen will be sent as a new message for @p context.
     *
     * Some messengers expose the message id only asynchronously after sendMessage completes.
     * Menu subsystem should call this right before sending a new inline-keyboard menu message,
     * and then forward AbstractBot::messageSent(...) to onMenuMessageSent(...).
     */
    void expectMenuMessageId(const MessengerMenu::RenderContext &context) const;
    void expectMenuMessageId(const MessengerMenu::RenderContext &context,
                             bool closePreviousMenu) const;

    /**
     * @brief Called when the underlying messenger reports the message id of a sent message.
     *
     * This method matches the sent message to the oldest expected menu message for the chat/thread
     * and stores it as lastMenuMessageId in the corresponding menu session.
     */
    void onMenuMessageSent(const AbstractBot::ChatAddress &chat, const QString &messageId) const;

    /**
     * @brief Clears the stored menu session for the given context.
     *
     * This will emit sessionEnded(sessionId) if a session existed.
     */
    void clearSession(const MessengerMenu::RenderContext &context);

    static QString rootMenuId();

signals:
    void sessionCreated(const QString &sessionId);
    void sessionEnded(const QString &sessionId);
    void menuChanged(const QString &sessionId,
                     const QString &fromMenuId,
                     const QString &toMenuId,
                     bool resetFlows);

private:
    MessengerMenuManagerPrivate *d;

    void registerMenus();
};

#endif // MESSENGERMENUMANAGER_H
