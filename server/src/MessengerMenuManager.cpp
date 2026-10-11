#include "MessengerMenuManager.h"

#include "MessengerBot.h"
#include "ServerDatabase.h"
#include "MessengerMenuIds.h"
#include "MessengerUserKey.h"
#include "MessengerTextRender.h"
#include "ResourceQueueModel.h"
#include "ServerSettingsModel.h"
#include "ServerDateTimeFormat.h"
#include "ServerRuntimeSettings.h"

#include "MessengerAbstractMenu.h"
#include "MessengerAdminUsersState.h"
#include "MessengerUserResourcesState.h"
#include "MessengerAdminResourcesState.h"

#include <QList>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QVector>
#include <QDateTime>
#include <QByteArray>
#include <QStringList>

namespace {
constexpr char kPayloadPrefix[] = "mm";
constexpr int kPayloadVersion = 3;
constexpr int kSessionCleanupBatchSize = 20;
constexpr int kSessionCleanupBatchDelayMs = 50;
constexpr int kMaxPendingMenuMessagesPerChat = 16;
constexpr int kMaxPendingMenuMessages = 1024;
constexpr qint64 kPendingMenuMessageTtlSeconds = 5 * 60;

QString encodePayload(MessengerMenu::ButtonActionType type, const QString &value)
{
    QString actionToken;
    switch (type) {
    case MessengerMenu::ButtonActionType::Command:
        actionToken = QStringLiteral("c");
        break;
    case MessengerMenu::ButtonActionType::Navigate:
        actionToken = QStringLiteral("n");
        break;
    case MessengerMenu::ButtonActionType::Event:
        actionToken = QStringLiteral("e");
        break;
    }

    // Keep payload compatible with legacy parser: "mm|<version>|<type>|<value>"
    QStringList parts;
    parts << QLatin1String(kPayloadPrefix) << QString::number(kPayloadVersion) << actionToken
          << value;
    return parts.join(QLatin1Char('|'));
}

BookingResourcesTranslator::ScopedLocaleOverride applyLocaleForMenuRender(
        MessengerBot *ownerBot, const MessengerMenu::RenderContext &context)
{
    if (!ownerBot)
        return BookingResourcesTranslator::ScopedLocaleOverride();

    const MessengerUserKey senderKey = AbstractBot::senderKey(context.chat, context.sender);
    const QString effectiveLocale = ownerBot->effectiveLocaleForUser(senderKey, QString());
    return ownerBot->applyScopedLocale(effectiveLocale);
}

bool accessAllowed(MessengerMenu::AccessLevel accessLevel,
                   const MessengerMenu::RenderContext &context)
{
    switch (accessLevel) {
    case MessengerMenu::AccessLevel::Everyone:
        return true;
    case MessengerMenu::AccessLevel::AdminsOnly:
        return context.isAdmin;
    case MessengerMenu::AccessLevel::ServerAdminsOnly:
        return context.canManageServerSettings;
    }
    return false;
}

bool menuAccessAllowed(const MessengerAbstractMenu *menu,
                       const MessengerMenu::RenderContext &context)
{
    if (!menu)
        return false;

    if (menu->adminsOnly() && !context.isAdmin)
        return false;

    return accessAllowed(menu->accessLevel(), context);
}
} // namespace

class MessengerMenuManagerPrivate
{
public:
    MessengerMenuManagerPrivate(MessengerBot *bot, MessengerMenuManager *manager);

    void registerMenu(MessengerAbstractMenu *menu);

    enum class CloseReason {
        ReplacedByNewMenu = 0,
        Expired,
        Shutdown,
        UnknownMenu
    };

    enum class ActionType {
        Command = 0,
        Navigate,
        Event
    };

    bool decodePayload(const QString &payload, ActionType *typeOut, QString *valueOut) const;
    static QString decodeToken(const QString &encoded);
    static QString sanitizedCommand(const QString &command);

    static const QString &rootMenuIdValue();

    struct Session
    {
        QString currentMenuId;
        QString lastMenuMessageId;
        AbstractBot::ChatAddress chat;
        AbstractBot::UserAddress sender;
        QDateTime lastActivityUtc;

        void resetFlows() { }
    };

    static QString sessionKey(const MessengerMenu::RenderContext &context);
    Session *ensureSession(const MessengerMenu::RenderContext &context);
    const Session *sessionFor(const MessengerMenu::RenderContext &context);
    void touchSession(Session *session, const MessengerMenu::RenderContext &context);

    struct PendingMenuMessage
    {
        MessengerMenu::RenderContext context;
        QString previousMenuMessageId;
        QDateTime createdAtUtc;
        bool closePreviousMenu = false;
    };

    static QString chatThreadKey(const AbstractBot::ChatAddress &chat);
    void expectMenuMessageId(const MessengerMenu::RenderContext &context, bool closePreviousMenu);
    void onMenuMessageSent(const AbstractBot::ChatAddress &chat, const QString &messageId);
    bool hasPendingMenuReplacement(const QString &sessionId, const QString &messageId) const;
    void purgeExpiredPendingMenuMessages(const QDateTime &nowUtc);
    void removePendingMenuMessagesForSessions(const QSet<QString> &sessionIds);
    void clearPendingMenuMessages();

    void setMenuUpdateMode(MessengerMenuManager::MenuUpdateMode mode);
    void setSessionTimeoutMinutes(uint minutes);
    void updateSessionCleanupTimerState();

    void connectDatabaseModels();
    void applyRuntimeSettingsFromDatabase();
    void scheduleAutoRefreshFromModelChange();
    void refreshOpenMenus();
    void cleanupExpiredSessions();
    void closeMenuMessage(const AbstractBot::ChatAddress &chat,
                          const AbstractBot::UserAddress &sender,
                          const QString &messageId,
                          CloseReason reason) const;
    void closeAllOpenMenus(CloseReason reason) const;

    MessengerBot *bot = nullptr;             // non-owning
    MessengerMenuManager *manager = nullptr; // non-owning

    MessengerUserResourcesState *userResourcesState = nullptr;
    MessengerAdminUsersState *adminUsersState = nullptr;
    MessengerAdminResourcesState *adminResourcesState = nullptr;

    MessengerMenuManager::MenuUpdateMode menuUpdateMode =
            MessengerMenuManager::MenuUpdateMode::Active;
    uint sessionTimeoutMinutes = 30;

    QTimer sessionCleanupTimer;
    QTimer autoRefreshTimer;
    bool shutdownRequested = false;
    bool sessionCleanupContinuationScheduled = false;
    bool refreshInProgress = false;
    bool refreshRequested = false;
    QVector<QString> refreshQueue;
    int refreshQueueIndex = 0;

    QVector<MessengerAbstractMenu *> menus;
    QHash<QString, MessengerAbstractMenu *> menusById; // non-owning (QObject parented to manager)
    QHash<QString, MessengerAbstractMenu *> handlersByEventKey; // eventKey -> handler menu
    QHash<QString, QString> activeTextHandlerBySessionId;       // sessionId -> eventKey

    QHash<QString, Session> sessions;
    QHash<QString, QList<PendingMenuMessage>> pendingMenuMessagesByChatThread;
    int pendingMenuMessageCount = 0;
    UserTimeZone currentRenderTimeZone;
};

MessengerMenuManager::MessengerMenuManager(MessengerBot *bot)
    : QObject(bot)
    , d(new MessengerMenuManagerPrivate(bot, this))
{
    registerMenus();
}

MessengerMenuManager::~MessengerMenuManager()
{
    qDeleteAll(d->menus);

    delete d->userResourcesState;
    delete d->adminUsersState;
    delete d->adminResourcesState;

    delete d;
}

void MessengerMenuManager::shutdown()
{
    if (!d)
        return;
    if (d->shutdownRequested)
        return;

    d->shutdownRequested = true;

    d->sessionCleanupTimer.stop();
    d->autoRefreshTimer.stop();
    d->closeAllOpenMenus(MessengerMenuManagerPrivate::CloseReason::Shutdown);
    d->clearPendingMenuMessages();
}

MessengerMenuManager::MenuUpdateMode MessengerMenuManager::menuUpdateMode() const
{
    return d->menuUpdateMode;
}

void MessengerMenuManager::setMenuUpdateMode(MessengerMenuManager::MenuUpdateMode mode)
{
    d->setMenuUpdateMode(mode);
}

uint MessengerMenuManager::sessionTimeoutMinutes() const
{
    return d->sessionTimeoutMinutes;
}

void MessengerMenuManager::setSessionTimeoutMinutes(uint minutes)
{
    d->setSessionTimeoutMinutes(minutes);
}

MessengerBot *MessengerMenuManager::bot() const
{
    return d->bot;
}

QString MessengerMenuManager::sessionId(const MessengerMenu::RenderContext &context) const
{
    Q_UNUSED(d);
    return MessengerMenuManagerPrivate::sessionKey(context);
}

void MessengerMenuManager::setActiveTextHandler(const QString &sessionId, const QString &eventKey)
{
    const QString sid = sessionId.trimmed();
    const QString key = eventKey.trimmed();
    if (sid.isEmpty() || key.isEmpty())
        return;
    d->activeTextHandlerBySessionId.insert(sid, key);
}

void MessengerMenuManager::clearActiveTextHandler(const QString &sessionId)
{
    const QString sid = sessionId.trimmed();
    if (sid.isEmpty())
        return;
    d->activeTextHandlerBySessionId.remove(sid);
}

QString MessengerMenuManager::currentMenuId(const MessengerMenu::RenderContext &context) const
{
    const MessengerMenuManagerPrivate::Session *session = d->sessionFor(context);
    if (!session)
        return QString();
    return session->currentMenuId.trimmed();
}

void MessengerMenuManager::setCurrentMenuId(const MessengerMenu::RenderContext &context,
                                            const QString &menuId,
                                            bool resetFlowsOnChange)
{
    MessengerMenuManagerPrivate::Session *session = d->ensureSession(context);
    if (!session)
        return;

    const QString target = menuId.trimmed();
    if (target.isEmpty())
        return;

    const QString from = session->currentMenuId.trimmed();
    if (resetFlowsOnChange && from != target) {
        session->resetFlows();
        clearActiveTextHandler(sessionId(context));
    }
    session->currentMenuId = target;

    if (from != target) {
        const QString sid = sessionId(context);
        if (!sid.isEmpty()) {
            Q_EMIT menuChanged(sid, from, target, resetFlowsOnChange);
        }
    }
}

QString MessengerMenuManager::lastMenuMessageId(const MessengerMenu::RenderContext &context) const
{
    const MessengerMenuManagerPrivate::Session *session = d->sessionFor(context);
    if (!session)
        return QString();
    return session->lastMenuMessageId.trimmed();
}

MessengerMenu::MenuButton MessengerMenuManager::makeNavigateButton(const QString &text,
                                                                   const QString &targetMenuId,
                                                                   bool adminsOnly) const
{
    return makeNavigateButton(text,
                              targetMenuId,
                              adminsOnly ? MessengerMenu::AccessLevel::AdminsOnly
                                         : MessengerMenu::AccessLevel::Everyone);
}

MessengerMenu::MenuButton MessengerMenuManager::makeNavigateButton(
        const QString &text,
        const QString &targetMenuId,
        MessengerMenu::AccessLevel accessLevel) const
{
    MessengerMenu::MenuButton button;
    button.text = text;
    button.action.type = MessengerMenu::ButtonActionType::Navigate;
    button.action.value = targetMenuId;
    button.adminsOnly = accessLevel == MessengerMenu::AccessLevel::AdminsOnly;
    button.accessLevel = accessLevel;
    return button;
}

MessengerMenu::MenuButton MessengerMenuManager::makeEventButton(const QString &text,
                                                                const QString &event,
                                                                bool adminsOnly) const
{
    return makeEventButton(text,
                           event,
                           adminsOnly ? MessengerMenu::AccessLevel::AdminsOnly
                                      : MessengerMenu::AccessLevel::Everyone);
}

MessengerMenu::MenuButton MessengerMenuManager::makeEventButton(
        const QString &text, const QString &event, MessengerMenu::AccessLevel accessLevel) const
{
    MessengerMenu::MenuButton button;
    button.text = text;
    button.action.type = MessengerMenu::ButtonActionType::Event;
    button.action.value = event;
    button.adminsOnly = accessLevel == MessengerMenu::AccessLevel::AdminsOnly;
    button.accessLevel = accessLevel;
    return button;
}

MessengerMenu::MenuButton MessengerMenuManager::makeCommandButton(const QString &text,
                                                                  const QString &command,
                                                                  bool adminsOnly) const
{
    return makeCommandButton(text,
                             command,
                             adminsOnly ? MessengerMenu::AccessLevel::AdminsOnly
                                        : MessengerMenu::AccessLevel::Everyone);
}

MessengerMenu::MenuButton MessengerMenuManager::makeCommandButton(
        const QString &text, const QString &command, MessengerMenu::AccessLevel accessLevel) const
{
    MessengerMenu::MenuButton button;
    button.text = text;
    button.action.type = MessengerMenu::ButtonActionType::Command;
    button.action.value = command;
    button.adminsOnly = accessLevel == MessengerMenu::AccessLevel::AdminsOnly;
    button.accessLevel = accessLevel;
    return button;
}

MessengerMenuManager::Reply MessengerMenuManager::renderPage(
        const MessengerMenu::MenuPage &page, const MessengerMenu::RenderContext &context) const
{
    Reply reply;

    QStringList textParts;
    const QString title = page.title.trimmed();
    const QString body = page.body.trimmed();

    const UserTimeZone previousRenderTimeZone = d->currentRenderTimeZone;
    const MessengerBot *ownerBot = bot();
    if (ownerBot) {
        const MessengerUserKey senderKey(context.chat.messenger, context.sender.userId.trimmed());
        d->currentRenderTimeZone = ownerBot->effectiveTimeZoneForUser(senderKey);
    } else {
        d->currentRenderTimeZone.clear();
    }

    MessengerMenu::MenuPage::DynamicText dynamic;
    if (page.dynamicText) {
        dynamic = page.dynamicText(context);
        reply.options.format = dynamic.format;
    }
    d->currentRenderTimeZone = previousRenderTimeZone;

    const bool isHtml = (reply.options.format == AbstractBot::MessageFormat::Html);
    if (isHtml) {
        if (!title.isEmpty())
            textParts << QStringLiteral("<b>%1</b>").arg(escapeHtml(title));
        if (!body.isEmpty())
            textParts << escapeHtml(body);
        if (!dynamic.text.trimmed().isEmpty())
            textParts << dynamic.text.trimmed();
    } else {
        if (!title.isEmpty())
            textParts << title;
        if (!body.isEmpty())
            textParts << body;
        if (!dynamic.text.trimmed().isEmpty())
            textParts << dynamic.text.trimmed();
    }

    reply.text = textParts.join(QStringLiteral("\n\n")).trimmed();

    AbstractBot::MessageOptions::InlineKeyboard keyboard;
    for (const QVector<MessengerMenu::MenuButton> &row : page.rows) {
        QVector<AbstractBot::MessageOptions::InlineKeyboardButton> renderedRow;
        renderedRow.reserve(row.size());
        for (const MessengerMenu::MenuButton &button : row) {
            if (button.adminsOnly && !context.isAdmin)
                continue;
            if (!accessAllowed(button.accessLevel, context))
                continue;

            AbstractBot::MessageOptions::InlineKeyboardButton keyboardButton;
            keyboardButton.text = button.text;
            keyboardButton.callbackData = encodePayload(button.action.type, button.action.value);
            if (!keyboardButton.isValid())
                continue;
            renderedRow.append(keyboardButton);
        }
        if (!renderedRow.isEmpty())
            keyboard.rows.append(renderedRow);
    }

    reply.options.inlineKeyboard = keyboard;
    reply.valid = !reply.text.isEmpty() || !reply.options.inlineKeyboard.isEmpty();

    return reply;
}

QString MessengerMenuManager::escapeHtml(const QString &text)
{
    QString escaped = text;
    escaped.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    escaped.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    escaped.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    return escaped;
}

QString MessengerMenuManager::buildAsOfLine(const QDateTime &utc,
                                            AbstractBot::MessageFormat format,
                                            const QString &dateTimeFormat,
                                            const UserTimeZone &timeZone)
{
    if (!utc.isValid())
        return QString();

    const QString localTime =
            MessengerTextRender::formatLocalDateTime(utc, QString(), dateTimeFormat, timeZone);
    const QString line = tr("As of: %1").arg(localTime);
    if (format == AbstractBot::MessageFormat::Html) {
        return QStringLiteral("<i>%1</i>").arg(escapeHtml(line));
    }
    return line;
}

QString MessengerMenuManager::buildTextWithAsOf(const QString &text,
                                                AbstractBot::MessageFormat format,
                                                const QString &dateTimeFormat,
                                                const UserTimeZone &timeZone)
{
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    const QString asOf = buildAsOfLine(nowUtc, format, dateTimeFormat, timeZone);
    const QString trimmedText = text.trimmed();
    if (asOf.trimmed().isEmpty())
        return trimmedText;
    if (trimmedText.isEmpty())
        return asOf.trimmed();
    return QStringLiteral("%1\n\n%2").arg(asOf.trimmed(), trimmedText).trimmed();
}

QString MessengerMenuManager::buildTextWithAsOfCurrentFormat(
        const QString &text, AbstractBot::MessageFormat format) const
{
    const MessengerBot *ownerBot = bot();
    const QString dateTimeFormat =
            ServerDateTimeFormat::effectiveFormat(ownerBot ? ownerBot->database() : nullptr);
    return buildTextWithAsOf(text, format, dateTimeFormat, d->currentRenderTimeZone);
}

void MessengerMenuManager::prependErrorBlock(MessengerMenu::MenuPage *page,
                                             const QString &errorText)
{
    if (!page)
        return;

    const QString trimmed = errorText.trimmed();
    if (trimmed.isEmpty())
        return;

    const QString errorBlock = tr("Error: %1").arg(trimmed);
    page->body = QStringLiteral("%1\n\n%2").arg(errorBlock, page->body).trimmed();
}

MessengerMenuManager::Reply MessengerMenuManager::buildInputStartedReply()
{
    Reply reply;
    reply.valid = true;
    reply.options.format = AbstractBot::MessageFormat::PlainText;
    reply.text = tr("Input mode started. Please send the requested data in chat.");
    reply.options.inlineKeyboard.clear();
    return reply;
}

MessengerMenuManager::Reply MessengerMenuManager::buildPlainTextReply(const QString &text)
{
    Reply reply;
    reply.valid = true;
    reply.options.format = AbstractBot::MessageFormat::PlainText;
    reply.options.inlineKeyboard.clear();
    reply.text = text;
    return reply;
}

void MessengerMenuManager::markHandledSendReply(ActionResult *result)
{
    if (!result)
        return;
    result->handled = true;
    result->sendReply = true;
}

void MessengerMenuManager::requestPreferEdit(ActionResult *result, const QString &messageId)
{
    if (!result)
        return;
    const QString trimmed = messageId.trimmed();
    if (trimmed.isEmpty())
        return;
    result->preferEdit = true;
    result->editMessageId = trimmed;
}

void MessengerMenuManager::requestEditOriginMessage(ActionResult *result,
                                                    const Reply &originReply,
                                                    const QString &originMessageId)
{
    if (!result)
        return;
    result->editOriginMessage = true;
    result->originMessageId = originMessageId.trimmed();
    result->originReply = originReply;
}

void MessengerMenuManager::requestSendAsNewMessageAndEditOrigin(ActionResult *result,
                                                                const Reply &originReply,
                                                                const QString &originMessageId)
{
    if (!result)
        return;
    result->sendAsNewMessage = true;
    requestEditOriginMessage(result, originReply, originMessageId);
}

MessengerMenuManager::Reply MessengerMenuManager::renderRoot(
        const MessengerMenu::RenderContext &context) const
{
    return renderMenu(MessengerMenuManagerPrivate::rootMenuIdValue(), context);
}

MessengerMenuManager::Reply MessengerMenuManager::renderMenu(
        const QString &menuId, const MessengerMenu::RenderContext &context) const
{
    const QString resolvedId =
            menuId.isEmpty() ? MessengerMenuManagerPrivate::rootMenuIdValue() : menuId;

    const QHash<QString, MessengerAbstractMenu *>::const_iterator newMenuIt =
            d->menusById.constFind(resolvedId);
    if (newMenuIt != d->menusById.cend()) {
        const MessengerAbstractMenu *menu = newMenuIt.value();
        if (!menu)
            return Reply{};
        if (!menuAccessAllowed(menu, context))
            return Reply{};

        MessengerMenu::MenuPage page = menu->buildPage(context);
        if (page.id.trimmed().isEmpty())
            page.id = resolvedId;
        page.adminsOnly = page.adminsOnly || menu->adminsOnly();
        if (page.adminsOnly && !context.isAdmin)
            return Reply{};
        if (!accessAllowed(page.accessLevel, context))
            return Reply{};

        return renderPage(page, context);
    }

    return Reply{};
}

MessengerMenuManager::ActionResult MessengerMenuManager::handlePayload(
        const QString &payload,
        const MessengerMenu::RenderContext &context,
        const QString &originMessageId) const
{
    ActionResult result;

    MessengerMenuManagerPrivate::ActionType actionType =
            MessengerMenuManagerPrivate::ActionType::Command;
    QString actionValue;
    if (!d->decodePayload(payload, &actionType, &actionValue))
        return result;

    MessengerMenuManagerPrivate::Session *session = d->ensureSession(context);
    if (session && !originMessageId.trimmed().isEmpty()) {
        session->lastMenuMessageId = originMessageId.trimmed();
    }

    switch (actionType) {
    case MessengerMenuManagerPrivate::ActionType::Command: {
        const QString command = MessengerMenuManagerPrivate::sanitizedCommand(actionValue);
        if (command.isEmpty())
            return result;
        // Legacy payloads (and older messages) can still contain command-type actions. We no longer
        // execute slash commands from the menu. Instead, map known actions to internal events.
        if (!session) {
            return result;
        }

        auto dispatchMenuEvent = [&](const QString &targetMenuId, const QString &event) {
            const QString sid = sessionId(context);
            if (sid.isEmpty())
                return false;

            const QHash<QString, MessengerAbstractMenu *>::const_iterator it =
                    d->menusById.constFind(targetMenuId);
            if (it == d->menusById.cend() || !it.value())
                return false;
            if (!menuAccessAllowed(it.value(), context))
                return false;

            const_cast<MessengerMenuManager *>(this)->setCurrentMenuId(context,
                                                                       targetMenuId,
                                                                       false);

            const bool handledByMenu = it.value()->handleEvent(event, sid, context, &result);
            if (handledByMenu && !result.handled) {
                result.handled = true;
            }
            if (handledByMenu && !result.sendReply) {
                // For legacy command-buttons, always render a menu reply after handling.
                result.sendReply = true;
            }
            return handledByMenu;
        };

        auto sendOutdated = [&]() {
            result.handled = true;
            result.sendReply = true;
            result.reply.valid = true;
            result.reply.options.format = AbstractBot::MessageFormat::PlainText;
            result.reply.text = tr("This menu button is outdated. Send /menu to open it again.");
            result.reply.options.inlineKeyboard.clear();
        };

        if (command == QStringLiteral("/occupy")) {
            if (!dispatchMenuEvent(QLatin1String(MessengerMenuIds::kMenuResourceCardUser),
                                   QStringLiteral("ur:occupy"))) {
                sendOutdated();
            }
        } else if (command == QStringLiteral("/release")) {
            if (!dispatchMenuEvent(QLatin1String(MessengerMenuIds::kMenuResourceCardUser),
                                   QStringLiteral("ur:release"))) {
                sendOutdated();
            }
        } else if (command == QStringLiteral("/unschedule")) {
            if (!dispatchMenuEvent(QLatin1String(MessengerMenuIds::kMenuResourceCardUser),
                                   QStringLiteral("ur:unschedule"))) {
                sendOutdated();
            }
        } else if (command == QStringLiteral("/queue")) {
            const_cast<MessengerMenuManager *>(this)->setCurrentMenuId(
                    context, QLatin1String(MessengerMenuIds::kMenuResourceQueue), false);
            result.handled = true;
            result.sendReply = true;
        } else if (command == QStringLiteral("/schedule")) {
            if (!dispatchMenuEvent(QLatin1String(MessengerMenuIds::kMenuResourceSchedule),
                                   QStringLiteral("ur:schedule:start"))) {
                sendOutdated();
            }
        } else {
            sendOutdated();
        }
        break;
    }
    case MessengerMenuManagerPrivate::ActionType::Navigate: {
        const QString target = actionValue.isEmpty()
                                       ? MessengerMenuManagerPrivate::rootMenuIdValue()
                                       : actionValue;
        const QString previous = session ? session->currentMenuId.trimmed() : QString();
        // Use the public manager API to ensure consistent flow reset + signals.
        // This also allows per-menu state holders (server/src/menus/*State) to track resets.
        const_cast<MessengerMenuManager *>(this)->setCurrentMenuId(context, target, true);
        Reply reply = renderMenu(target, context);
        if (!reply.valid) {
            if (!previous.isEmpty()) {
                const_cast<MessengerMenuManager *>(this)->setCurrentMenuId(context,
                                                                           previous,
                                                                           false);
            }
            return result;
        }
        result.handled = true;
        result.sendReply = true;
        result.reply = reply;

        const QString trimmedOriginId = originMessageId.trimmed();
        const bool isInputMenu =
                (target == QLatin1String(MessengerMenuIds::kMenuAdminUserAdd))
                || (target == QLatin1String(MessengerMenuIds::kMenuAdminResourceAdd));
        if (isInputMenu && !trimmedOriginId.isEmpty()) {
            requestSendAsNewMessageAndEditOrigin(&result,
                                                 buildInputStartedReply(),
                                                 trimmedOriginId);
        }
        break;
    }
    case MessengerMenuManagerPrivate::ActionType::Event: {
        if (!session)
            return result;

        const QString sid = sessionId(context);
        const QString trimmedEvent = actionValue.trimmed();

        // New-style event dispatch (O(1) by current menu id).
        const QString currentMenuId = !session->currentMenuId.trimmed().isEmpty()
                                              ? session->currentMenuId.trimmed()
                                              : MessengerMenuManagerPrivate::rootMenuIdValue();
        const QHash<QString, MessengerAbstractMenu *>::const_iterator currentMenuIt =
                d->menusById.constFind(currentMenuId);
        if (currentMenuIt != d->menusById.cend() && currentMenuIt.value() && !sid.isEmpty()
            && menuAccessAllowed(currentMenuIt.value(), context)) {
            const bool handledByMenu =
                    currentMenuIt.value()->handleEvent(trimmedEvent, sid, context, &result);
            if (handledByMenu && !result.handled) {
                result.handled = true;
            }
            if (handledByMenu) {
                break;
            }
        }

        // New-style event dispatch (O(1) by event key).
        const int colonIndex = trimmedEvent.indexOf(QLatin1Char(':'));
        const QString eventKey = (colonIndex > 0) ? trimmedEvent.left(colonIndex) : trimmedEvent;
        if (!eventKey.isEmpty()) {
            const QHash<QString, MessengerAbstractMenu *>::const_iterator it =
                    d->handlersByEventKey.constFind(eventKey);
            if (it != d->handlersByEventKey.cend() && it.value()
                && menuAccessAllowed(it.value(), context)) {
                if (!sid.isEmpty()) {
                    const bool handledByNew =
                            it.value()->handleEvent(trimmedEvent, sid, context, &result);
                    if (handledByNew && !result.handled) {
                        result.handled = true;
                    }
                    if (handledByNew) {
                        break;
                    }
                }
            }
        }

        if (!result.handled) {
            // The callback likely came from an outdated/stale menu message.
            result.handled = true;
            result.sendReply = true;
            result.reply = buildPlainTextReply(
                    tr("This menu button is outdated. Send /menu to open it again."));
        }
        break;
    }
    }

    if (result.handled && result.sendReply && !result.reply.valid) {
        const QString target = (session && !session->currentMenuId.trimmed().isEmpty())
                                       ? session->currentMenuId
                                       : MessengerMenuManagerPrivate::rootMenuIdValue();
        BookingResourcesTranslator::ScopedLocaleOverride localeGuard =
                applyLocaleForMenuRender(bot(), context);
        Q_UNUSED(localeGuard);
        Reply reply = renderMenu(target, context);
        if (reply.valid) {
            result.reply = reply;
        } else {
            result.sendReply = false;
        }
    }

    return result;
}

MessengerMenuManager::ActionResult MessengerMenuManager::handleTextInput(
        const QString &text, const MessengerMenu::RenderContext &context) const
{
    ActionResult result;
    MessengerMenuManagerPrivate::Session *session = d->ensureSession(context);
    if (!session)
        return result;

    // New-style text input dispatch (O(1) by active handler key).
    const QString sid = sessionId(context);
    if (!sid.isEmpty()) {
        const QHash<QString, QString>::const_iterator activeIt =
                d->activeTextHandlerBySessionId.constFind(sid);
        if (activeIt != d->activeTextHandlerBySessionId.cend()) {
            const QString handlerKey = activeIt.value().trimmed();
            const QHash<QString, MessengerAbstractMenu *>::const_iterator handlerIt =
                    d->handlersByEventKey.constFind(handlerKey);
            if (handlerIt != d->handlersByEventKey.cend() && handlerIt.value()
                && menuAccessAllowed(handlerIt.value(), context)) {
                const bool handledByNew =
                        handlerIt.value()->handleTextInput(text, sid, context, &result);
                if (handledByNew && !result.handled) {
                    result.handled = true;
                }
            }
        }
    }

    // New-style text input dispatch (O(1) by current menu id).
    if (!result.handled) {
        const QString currentMenuId = !session->currentMenuId.trimmed().isEmpty()
                                              ? session->currentMenuId.trimmed()
                                              : MessengerMenuManagerPrivate::rootMenuIdValue();
        const QHash<QString, MessengerAbstractMenu *>::const_iterator currentMenuIt =
                d->menusById.constFind(currentMenuId);
        if (currentMenuIt != d->menusById.cend() && currentMenuIt.value() && !sid.isEmpty()
            && menuAccessAllowed(currentMenuIt.value(), context)) {
            const bool handledByMenu =
                    currentMenuIt.value()->handleTextInput(text, sid, context, &result);
            if (handledByMenu && !result.handled) {
                result.handled = true;
            }
        }
    }

    if (result.handled && result.sendReply && !result.reply.valid) {
        const QString target = !session->currentMenuId.trimmed().isEmpty()
                                       ? session->currentMenuId
                                       : MessengerMenuManagerPrivate::rootMenuIdValue();
        BookingResourcesTranslator::ScopedLocaleOverride localeGuard =
                applyLocaleForMenuRender(bot(), context);
        Q_UNUSED(localeGuard);
        Reply reply = renderMenu(target, context);
        if (reply.valid) {
            result.reply = reply;
        } else {
            result.sendReply = false;
        }
    }
    return result;
}

void MessengerMenuManager::processCallbackQuery(const AbstractBot::CallbackQuery &query) const
{
    if (!query.isValid())
        return;

    MessengerBot *ownerBot = bot();
    if (!ownerBot)
        return;

    const QString fallback = tr("Menu is not available right now.");

    MessengerMenu::RenderContext context;
    context.chat = query.chat;
    context.sender = query.sender;
    context.isAdmin = ownerBot->isAdminUser(AbstractBot::senderKey(query));
    context.canManageServerSettings =
            ownerBot->canManageServerSettings(AbstractBot::senderKey(query));

    const QString originMessageId = query.originalMessageId.trimmed();

    const MessengerMenuManagerPrivate::Session *existingSession = d->sessionFor(context);
    if (!existingSession) {
        if (!originMessageId.isEmpty()) {
            d->closeMenuMessage(query.chat,
                                query.sender,
                                originMessageId,
                                MessengerMenuManagerPrivate::CloseReason::UnknownMenu);
        } else {
            ownerBot->sendReply(query.chat, fallback);
        }
        return;
    }

    if (d->sessionTimeoutMinutes > 0 && existingSession->lastActivityUtc.isValid()) {
        const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
        const qint64 limitSeconds = static_cast<qint64>(d->sessionTimeoutMinutes) * 60;
        if (existingSession->lastActivityUtc.secsTo(nowUtc) >= limitSeconds) {
            const QString idToClose = !existingSession->lastMenuMessageId.trimmed().isEmpty()
                                              ? existingSession->lastMenuMessageId.trimmed()
                                              : originMessageId;
            if (!idToClose.isEmpty()) {
                d->closeMenuMessage(query.chat,
                                    query.sender,
                                    idToClose,
                                    MessengerMenuManagerPrivate::CloseReason::Expired);
            }
            const_cast<MessengerMenuManager *>(this)->clearSession(context);
            return;
        }
    }

    const QString expectedMessageId = existingSession->lastMenuMessageId.trimmed();
    if (!expectedMessageId.isEmpty() && !originMessageId.isEmpty()
        && expectedMessageId != originMessageId) {
        d->closeMenuMessage(query.chat,
                            query.sender,
                            originMessageId,
                            MessengerMenuManagerPrivate::CloseReason::ReplacedByNewMenu);
        return;
    }

    MessengerMenuManagerPrivate::Session *session = d->ensureSession(context);
    if (session) {
        if (session->lastMenuMessageId.trimmed().isEmpty() && !originMessageId.isEmpty()) {
            session->lastMenuMessageId = originMessageId;
        }
        d->touchSession(session, context);
    }

    const ActionResult result = handlePayload(query.data, context, originMessageId);
    if (!result.handled)
        return;

    if (result.editOriginMessage) {
        const QString originId = !result.originMessageId.trimmed().isEmpty()
                                         ? result.originMessageId.trimmed()
                                         : query.originalMessageId.trimmed();
        if (!originId.isEmpty()) {
            const AbstractBot::MessageOptions originOptions = result.originReply.options;
            const QString originText = result.originReply.text.trimmed().isEmpty()
                                               ? fallback
                                               : result.originReply.text;
            ownerBot->editReply(query.chat, originId, originText, originOptions);
        }
    }

    if (result.sendReply) {
        if (!result.reply.valid) {
            ownerBot->sendReply(query.chat, fallback);
            return;
        }

        const QString text = result.reply.text.trimmed().isEmpty() ? fallback : result.reply.text;
        if (result.preferEdit && !result.editMessageId.trimmed().isEmpty()) {
            ownerBot->editReply(query.chat,
                                result.editMessageId.trimmed(),
                                text,
                                result.reply.options);
        } else if (!result.sendAsNewMessage && !query.originalMessageId.trimmed().isEmpty()) {
            ownerBot->editReply(query.chat,
                                query.originalMessageId.trimmed(),
                                text,
                                result.reply.options);
        } else {
            AbstractBot::MessageOptions options = result.reply.options;
            if (!options.inlineKeyboard.isEmpty()) {
                const bool closePreviousMenu = !result.editOriginMessage;
                expectMenuMessageId(context, closePreviousMenu);
            }
            ownerBot->sendReply(query.chat, text, options);
        }
    }
}

bool MessengerMenuManager::processTextMessage(const AbstractBot::IncomingMessage &message) const
{
    MessengerBot *ownerBot = bot();
    if (!ownerBot)
        return false;

    MessengerMenu::RenderContext context;
    context.chat = message.chat;
    context.sender = message.sender;
    context.isAdmin = ownerBot->isAdminUser(AbstractBot::senderKey(message));
    context.canManageServerSettings =
            ownerBot->canManageServerSettings(AbstractBot::senderKey(message));

    const MessengerMenuManagerPrivate::Session *existingSession = d->sessionFor(context);
    if (existingSession && d->sessionTimeoutMinutes > 0
        && existingSession->lastActivityUtc.isValid()) {
        const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
        const qint64 limitSeconds = static_cast<qint64>(d->sessionTimeoutMinutes) * 60;
        if (existingSession->lastActivityUtc.secsTo(nowUtc) >= limitSeconds) {
            const QString messageId = existingSession->lastMenuMessageId.trimmed();
            if (!messageId.isEmpty()) {
                d->closeMenuMessage(message.chat,
                                    message.sender,
                                    messageId,
                                    MessengerMenuManagerPrivate::CloseReason::Expired);
            }
            const_cast<MessengerMenuManager *>(this)->clearSession(context);
            return false;
        }
    }

    const ActionResult menuResult = handleTextInput(message.text, context);
    if (!menuResult.handled)
        return false;

    if (MessengerMenuManagerPrivate::Session *session = d->ensureSession(context)) {
        d->touchSession(session, context);
    }

    const QString fallback = tr("Menu is not available right now.");

    if (menuResult.editOriginMessage) {
        const QString originId = menuResult.originMessageId.trimmed();
        if (!originId.isEmpty()) {
            const AbstractBot::MessageOptions originOptions = menuResult.originReply.options;
            const QString originText = menuResult.originReply.text.trimmed().isEmpty()
                                               ? fallback
                                               : menuResult.originReply.text;
            ownerBot->editReply(message.chat, originId, originText, originOptions);
        }
    }

    if (menuResult.sendReply) {
        const QString text =
                menuResult.reply.text.trimmed().isEmpty() ? fallback : menuResult.reply.text;
        if (menuResult.preferEdit && !menuResult.editMessageId.trimmed().isEmpty()) {
            ownerBot->editReply(message.chat,
                                menuResult.editMessageId.trimmed(),
                                text,
                                menuResult.reply.options);
        } else {
            AbstractBot::MessageOptions options = menuResult.reply.options;
            if (!options.inlineKeyboard.isEmpty()) {
                const bool closePreviousMenu = !menuResult.editOriginMessage;
                expectMenuMessageId(context, closePreviousMenu);
            }
            ownerBot->sendReply(message.chat, text, options);
        }
    }

    return true;
}

void MessengerMenuManager::rememberMenuMessageId(const MessengerMenu::RenderContext &context,
                                                 const QString &messageId) const
{
    MessengerMenuManagerPrivate::Session *session = d->ensureSession(context);
    if (!session)
        return;

    const QString trimmed = messageId.trimmed();
    if (trimmed.isEmpty())
        return;

    session->lastMenuMessageId = trimmed;
    d->touchSession(session, context);
}

void MessengerMenuManager::expectMenuMessageId(const MessengerMenu::RenderContext &context) const
{
    d->expectMenuMessageId(context, false);
}

void MessengerMenuManager::expectMenuMessageId(const MessengerMenu::RenderContext &context,
                                               bool closePreviousMenu) const
{
    d->expectMenuMessageId(context, closePreviousMenu);
}

void MessengerMenuManager::onMenuMessageSent(const AbstractBot::ChatAddress &chat,
                                             const QString &messageId) const
{
    d->onMenuMessageSent(chat, messageId);
}

void MessengerMenuManager::clearSession(const MessengerMenu::RenderContext &context)
{
    const QString key = MessengerMenuManagerPrivate::sessionKey(context);
    if (key.isEmpty())
        return;

    QHash<QString, MessengerMenuManagerPrivate::Session>::iterator it = d->sessions.find(key);
    if (it == d->sessions.end())
        return;

    d->sessions.erase(it);
    d->activeTextHandlerBySessionId.remove(key);
    d->removePendingMenuMessagesForSessions(QSet<QString>{ key });
    Q_EMIT sessionEnded(key);
}

QString MessengerMenuManager::rootMenuId()
{
    return MessengerMenuManagerPrivate::rootMenuIdValue();
}

MessengerMenuManagerPrivate::MessengerMenuManagerPrivate(MessengerBot *bot,
                                                         MessengerMenuManager *manager)
    : bot(bot)
    , manager(manager)
{
    sessionCleanupTimer.setInterval(60 * 1000);
    sessionCleanupTimer.setSingleShot(false);
    QObject::connect(&sessionCleanupTimer, &QTimer::timeout, manager, [this]() {
        cleanupExpiredSessions();
    });
    updateSessionCleanupTimerState();

    autoRefreshTimer.setInterval(250);
    autoRefreshTimer.setSingleShot(true);
    QObject::connect(&autoRefreshTimer, &QTimer::timeout, manager, [this]() {
        if (menuUpdateMode != MessengerMenuManager::MenuUpdateMode::Active && !refreshInProgress)
            return;
        refreshOpenMenus();
    });

    connectDatabaseModels();
}

void MessengerMenuManagerPrivate::registerMenu(MessengerAbstractMenu *menu)
{
    if (!menu)
        return;

    const QString id = menu->menuId().trimmed();
    if (!id.isEmpty()) {
        menusById.insert(id, menu);
    }

    for (const QString &key : menu->eventKeys()) {
        const QString trimmedKey = key.trimmed();
        if (trimmedKey.isEmpty())
            continue;
        handlersByEventKey.insert(trimmedKey, menu);
    }

    menus.push_back(menu);
}

void MessengerMenuManager::registerMenus()
{
    MessengerMenuStaticContext context;

    if (!d->userResourcesState)
        d->userResourcesState = new MessengerUserResourcesState(this);
    if (!d->adminUsersState)
        d->adminUsersState = new MessengerAdminUsersState(this);
    if (!d->adminResourcesState)
        d->adminResourcesState = new MessengerAdminResourcesState(this);

    context.userResourcesState = d->userResourcesState;
    context.adminUsersState = d->adminUsersState;
    context.adminResourcesState = d->adminResourcesState;

    const QVector<MessengerMenuStaticRegistry::Factory> factories =
            MessengerMenuStaticRegistry::factories();

    for (MessengerMenuStaticRegistry::Factory factory : factories) {
        if (!factory)
            continue;

        MessengerAbstractMenu *menu = factory(this, context);
        if (!menu)
            continue;

        d->registerMenu(menu);
    }
}

void MessengerMenuManagerPrivate::setMenuUpdateMode(MessengerMenuManager::MenuUpdateMode mode)
{
    if (menuUpdateMode == mode)
        return;

    menuUpdateMode = mode;

    // Immediately re-render open menus so buttons reflect the new mode.
    refreshOpenMenus();
}

void MessengerMenuManagerPrivate::setSessionTimeoutMinutes(uint minutes)
{
    if (sessionTimeoutMinutes == minutes)
        return;

    sessionTimeoutMinutes = minutes;
    updateSessionCleanupTimerState();
    cleanupExpiredSessions();
}

void MessengerMenuManagerPrivate::updateSessionCleanupTimerState()
{
    if (sessionTimeoutMinutes == 0) {
        sessionCleanupTimer.stop();
        return;
    }

    if (!sessionCleanupTimer.isActive()) {
        sessionCleanupTimer.start();
    }
}

void MessengerMenuManagerPrivate::connectDatabaseModels()
{
    if (!bot || !manager)
        return;

    ServerDatabase *database = bot->database();
    if (!database)
        return;

    auto connectModelSignals = [this](const QAbstractItemModel *model,
                                      const std::function<void()> &onChanged) {
        if (!model || !manager)
            return;

        QObject::connect(model, &QAbstractItemModel::modelReset, manager, [onChanged]() {
            onChanged();
        });

        QObject::connect(model,
                         &QAbstractItemModel::layoutChanged,
                         manager,
                         [onChanged](const QList<QPersistentModelIndex> &,
                                     QAbstractItemModel::LayoutChangeHint) { onChanged(); });

        QObject::connect(model,
                         &QAbstractItemModel::rowsInserted,
                         manager,
                         [onChanged](const QModelIndex &, int, int) { onChanged(); });
        QObject::connect(model,
                         &QAbstractItemModel::rowsRemoved,
                         manager,
                         [onChanged](const QModelIndex &, int, int) { onChanged(); });

        QObject::connect(model,
                         &QAbstractItemModel::dataChanged,
                         manager,
                         [onChanged](const QModelIndex &,
                                     const QModelIndex &,
                                     const QVector<int> &) { onChanged(); });
    };

    // Keep runtime settings in sync with SQLite-backed model.
    applyRuntimeSettingsFromDatabase();
    connectModelSignals(database->serverSettingsModel(),
                        [this]() { applyRuntimeSettingsFromDatabase(); });

    auto onResourcesChanged = [this]() {
        if (menuUpdateMode != MessengerMenuManager::MenuUpdateMode::Active)
            return;
        if (userResourcesState)
            userResourcesState->invalidateSelectors();
        if (adminResourcesState)
            adminResourcesState->invalidateSelectors();
        scheduleAutoRefreshFromModelChange();
    };

    auto onUsersChanged = [this]() {
        if (menuUpdateMode != MessengerMenuManager::MenuUpdateMode::Active)
            return;
        if (adminUsersState)
            adminUsersState->invalidateSelectors();
        scheduleAutoRefreshFromModelChange();
    };

    auto onAnyModelChanged = [this]() {
        if (menuUpdateMode != MessengerMenuManager::MenuUpdateMode::Active)
            return;
        scheduleAutoRefreshFromModelChange();
    };

    connectModelSignals(database->resourceModel(), onResourcesChanged);
    connectModelSignals(database->userModel(), onUsersChanged);
    connectModelSignals(database->resourceQueueModel(), onAnyModelChanged);
    if (const ResourceQueueModel *queueModel = database->resourceQueueModel()) {
        QObject::connect(queueModel,
                         &ResourceQueueModel::occupationStarted,
                         manager,
                         [onAnyModelChanged](const QString &,
                                             const MessengerUserKey &,
                                             bool,
                                             const QDateTime &) { onAnyModelChanged(); });
        QObject::connect(queueModel,
                         &ResourceQueueModel::occupationEnded,
                         manager,
                         [onAnyModelChanged](const QString &,
                                             const MessengerUserKey &,
                                             bool,
                                             const QString &,
                                             const QDateTime &) { onAnyModelChanged(); });
    }
    connectModelSignals(database->occupationDurationModel(), onAnyModelChanged);
    connectModelSignals(database->blacklistModel(), onAnyModelChanged);
    connectModelSignals(database->pendingAccessUserModel(), onAnyModelChanged);
}

void MessengerMenuManagerPrivate::applyRuntimeSettingsFromDatabase()
{
    if (!bot)
        return;
    ServerDatabase *database = bot->database();
    if (!database)
        return;

    using Setting = ServerRuntimeSettings::Setting;

    const uint modeValue = database->serverSettingValue(Setting::MenuUpdateMode).toUInt();
    const uint timeoutMinutes =
            database->serverSettingValue(Setting::MenuSessionTimeoutMinutes).toUInt();

    const MessengerMenuManager::MenuUpdateMode mode =
            (modeValue == 0) ? MessengerMenuManager::MenuUpdateMode::Active
                             : MessengerMenuManager::MenuUpdateMode::Passive;
    setMenuUpdateMode(mode);
    setSessionTimeoutMinutes(timeoutMinutes);
}

void MessengerMenuManagerPrivate::scheduleAutoRefreshFromModelChange()
{
    if (menuUpdateMode != MessengerMenuManager::MenuUpdateMode::Active)
        return;
    refreshRequested = true;
    if (refreshInProgress)
        return;
    if (autoRefreshTimer.isActive())
        return;
    autoRefreshTimer.start();
}

void MessengerMenuManagerPrivate::refreshOpenMenus()
{
    cleanupExpiredSessions();

    MessengerBot *ownerBot = bot;
    MessengerMenuManager *mgr = manager;
    if (!ownerBot || !mgr)
        return;

    const QString fallback = MessengerMenuManager::tr("Menu is not available right now.");

    if (!refreshInProgress) {
        refreshInProgress = true;
        refreshRequested = false;
        refreshQueue.clear();
        refreshQueueIndex = 0;
        refreshQueue.reserve(sessions.size());
        for (QHash<QString, Session>::const_iterator it = sessions.cbegin(); it != sessions.cend();
             ++it) {
            refreshQueue.append(it.key());
        }
    }

    static constexpr int kBatchSize = 20;
    static constexpr int kBatchDelayMs = 40;
    int processed = 0;
    while (refreshQueueIndex < refreshQueue.size() && processed < kBatchSize) {
        const QString sessionId = refreshQueue.at(refreshQueueIndex++);
        QHash<QString, Session>::iterator it = sessions.find(sessionId);
        if (it == sessions.end())
            continue;

        Session &session = it.value();
        const QString messageId = session.lastMenuMessageId.trimmed();
        if (messageId.isEmpty())
            continue;
        if (hasPendingMenuReplacement(sessionId, messageId))
            continue;

        if (!session.chat.isValid() || !session.sender.isValid()) {
            session.lastMenuMessageId.clear();
            continue;
        }

        MessengerMenu::RenderContext context;
        context.chat = session.chat;
        context.sender = session.sender;
        context.isAdmin =
                ownerBot->isAdminUser(AbstractBot::senderKey(session.chat, session.sender));
        context.canManageServerSettings = ownerBot->canManageServerSettings(
                AbstractBot::senderKey(session.chat, session.sender));

        const QString menuId = session.currentMenuId.trimmed().isEmpty()
                                       ? rootMenuIdValue()
                                       : session.currentMenuId.trimmed();

        BookingResourcesTranslator::ScopedLocaleOverride localeGuard =
                applyLocaleForMenuRender(ownerBot, context);
        Q_UNUSED(localeGuard);
        const MessengerMenuManager::Reply reply = mgr->renderMenu(menuId, context);
        if (!reply.valid) {
            closeMenuMessage(session.chat, session.sender, messageId, CloseReason::UnknownMenu);
            session.lastMenuMessageId.clear();
            continue;
        }

        const QString text = reply.text.trimmed().isEmpty() ? fallback : reply.text;
        ownerBot->editReply(session.chat, messageId, text, reply.options);
        ++processed;
    }

    if (refreshQueueIndex < refreshQueue.size()) {
        autoRefreshTimer.start(kBatchDelayMs);
        return;
    }

    refreshInProgress = false;
    refreshQueue.clear();
    refreshQueueIndex = 0;
    if (refreshRequested) {
        refreshRequested = false;
        autoRefreshTimer.start();
    }
}

void MessengerMenuManagerPrivate::cleanupExpiredSessions()
{
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    purgeExpiredPendingMenuMessages(nowUtc);

    if (sessionTimeoutMinutes == 0)
        return;

    const qint64 limitSeconds = static_cast<qint64>(sessionTimeoutMinutes) * 60;
    QSet<QString> endedSessionIds;
    int expiredCount = 0;

    QHash<QString, Session>::iterator it = sessions.begin();
    while (it != sessions.end() && expiredCount < kSessionCleanupBatchSize) {
        const QString sid = it.key();
        Session &session = it.value();

        if (!session.lastActivityUtc.isValid()) {
            session.lastActivityUtc = nowUtc;
            ++it;
            continue;
        }

        const qint64 inactiveSeconds = session.lastActivityUtc.secsTo(nowUtc);
        if (inactiveSeconds < limitSeconds) {
            ++it;
            continue;
        }

        const QString messageId = session.lastMenuMessageId.trimmed();
        if (!messageId.isEmpty()) {
            closeMenuMessage(session.chat, session.sender, messageId, CloseReason::Expired);
        }

        activeTextHandlerBySessionId.remove(sid);
        it = sessions.erase(it);
        endedSessionIds.insert(sid);
        ++expiredCount;
    }

    removePendingMenuMessagesForSessions(endedSessionIds);
    if (manager) {
        for (const QString &sessionId : std::as_const(endedSessionIds))
            Q_EMIT manager->sessionEnded(sessionId);
    }

    if (it != sessions.end() && !shutdownRequested && manager
        && !sessionCleanupContinuationScheduled) {
        sessionCleanupContinuationScheduled = true;
        QTimer::singleShot(kSessionCleanupBatchDelayMs, manager, [this]() {
            sessionCleanupContinuationScheduled = false;
            cleanupExpiredSessions();
        });
    }
}

void MessengerMenuManagerPrivate::closeMenuMessage(const AbstractBot::ChatAddress &chat,
                                                   const AbstractBot::UserAddress &sender,
                                                   const QString &messageId,
                                                   CloseReason reason) const
{
    MessengerBot *ownerBot = bot;
    if (!ownerBot || !chat.isValid())
        return;

    const QString trimmedId = messageId.trimmed();
    if (trimmedId.isEmpty())
        return;

    MessengerMenu::RenderContext localeContext;
    localeContext.chat = chat;
    localeContext.sender = sender;
    BookingResourcesTranslator::ScopedLocaleOverride localeGuard =
            applyLocaleForMenuRender(ownerBot, localeContext);
    Q_UNUSED(localeGuard);

    QString text;
    switch (reason) {
    case CloseReason::ReplacedByNewMenu:
        text = MessengerMenuManager::tr("Menu session ended.\nSend /menu to open it again.");
        break;
    case CloseReason::Expired:
        text = MessengerMenuManager::tr(
                "Menu session expired due to inactivity.\nSend /menu to open it again.");
        break;
    case CloseReason::Shutdown:
        text = MessengerMenuManager::tr("Menu session ended.\nSend /menu to open it again.");
        break;
    case CloseReason::UnknownMenu:
        text = MessengerMenuManager::tr(
                "This menu is no longer active.\nSend /menu to open it again.");
        break;
    }

    AbstractBot::MessageOptions options;
    options.format = AbstractBot::MessageFormat::PlainText;
    options.inlineKeyboard.clear();
    ownerBot->editReply(chat, trimmedId, text, options);
}

void MessengerMenuManagerPrivate::closeAllOpenMenus(CloseReason reason) const
{
    for (const Session &session : sessions) {
        const QString messageId = session.lastMenuMessageId.trimmed();
        if (messageId.isEmpty())
            continue;
        closeMenuMessage(session.chat, session.sender, messageId, reason);
    }
}

bool MessengerMenuManagerPrivate::decodePayload(const QString &payload,
                                                ActionType *typeOut,
                                                QString *valueOut) const
{
    const QStringList parts = payload.split(QLatin1Char('|'));
    if (parts.size() < 4)
        return false;
    if (parts.at(0) != QLatin1String(kPayloadPrefix))
        return false;

    bool ok = false;
    const int version = parts.at(1).toInt(&ok);
    if (!ok)
        return false;

    ActionType resolvedType = ActionType::Command;
    QString actionValue;
    const QString actionToken = parts.at(2);

    if (version == 1) {
        if (parts.size() < 5)
            return false;

        if (actionToken == QStringLiteral("cmd")) {
            resolvedType = ActionType::Command;
        } else if (actionToken == QStringLiteral("nav")) {
            resolvedType = ActionType::Navigate;
        } else {
            return false;
        }

        // In v1 payloads, the menu id was included, but it's not needed for processing.
        Q_UNUSED(decodeToken(parts.at(3)));
        actionValue = decodeToken(parts.at(4));
    } else if (version == 2 || version == 3) {
        if (actionToken == QStringLiteral("c") || actionToken == QStringLiteral("cmd")) {
            resolvedType = ActionType::Command;
        } else if (actionToken == QStringLiteral("n") || actionToken == QStringLiteral("nav")) {
            resolvedType = ActionType::Navigate;
        } else if (version >= 3
                   && (actionToken == QStringLiteral("e")
                       || actionToken == QStringLiteral("evt"))) {
            resolvedType = ActionType::Event;
        } else {
            return false;
        }

        // In v2+ payloads, only action value is included to minimize callback_data size.
        actionValue = parts.at(3);
    } else {
        return false;
    }

    if (typeOut)
        *typeOut = resolvedType;
    if (valueOut)
        *valueOut = actionValue;

    return true;
}

QString MessengerMenuManagerPrivate::decodeToken(const QString &encoded)
{
    QByteArray buffer = encoded.toUtf8();
    const int remainder = buffer.size() % 4;
    if (remainder > 0)
        buffer.append(QByteArray(4 - remainder, '='));
    const QByteArray decoded = QByteArray::fromBase64(buffer, QByteArray::Base64UrlEncoding);
    return QString::fromUtf8(decoded);
}

QString MessengerMenuManagerPrivate::sanitizedCommand(const QString &command)
{
    QString trimmed = command.trimmed();
    if (trimmed.isEmpty())
        return trimmed;
    if (!trimmed.startsWith(QLatin1Char('/')))
        trimmed.prepend(QLatin1Char('/'));
    return trimmed;
}

const QString &MessengerMenuManagerPrivate::rootMenuIdValue()
{
    static const QString kRootId = QLatin1String(MessengerMenuIds::kMenuMain);
    return kRootId;
}

QString MessengerMenuManagerPrivate::sessionKey(const MessengerMenu::RenderContext &context)
{
    if (!context.chat.isValid() || !context.sender.isValid())
        return QString();

    const QString chatId = context.chat.chatId.trimmed();
    const QString threadId = context.chat.threadId.trimmed();
    const QString userId = context.sender.userId.trimmed();
    if (chatId.isEmpty() || userId.isEmpty())
        return QString();

    return QStringLiteral("%1|%2|%3|%4")
            .arg(messengerTypeToString(context.chat.messenger), chatId, threadId, userId);
}

QString MessengerMenuManagerPrivate::chatThreadKey(const AbstractBot::ChatAddress &chat)
{
    if (!chat.isValid())
        return QString();

    const QString chatId = chat.chatId.trimmed();
    const QString threadId = chat.threadId.trimmed();
    if (chatId.isEmpty())
        return QString();

    return QStringLiteral("%1|%2|%3").arg(messengerTypeToString(chat.messenger), chatId, threadId);
}

void MessengerMenuManagerPrivate::expectMenuMessageId(const MessengerMenu::RenderContext &context,
                                                      bool closePreviousMenu)
{
    const QString key = chatThreadKey(context.chat);
    if (key.isEmpty())
        return;

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    purgeExpiredPendingMenuMessages(nowUtc);
    if (pendingMenuMessageCount >= kMaxPendingMenuMessages)
        return;

    Session *session = ensureSession(context);

    PendingMenuMessage pending;
    pending.context = context;
    pending.closePreviousMenu = closePreviousMenu;
    pending.createdAtUtc = nowUtc;
    if (session) {
        pending.previousMenuMessageId = session->lastMenuMessageId.trimmed();
    }

    QList<PendingMenuMessage> &pendingMessages = pendingMenuMessagesByChatThread[key];
    if (pendingMessages.size() >= kMaxPendingMenuMessagesPerChat)
        return;
    pendingMessages.append(pending);
    ++pendingMenuMessageCount;
}

void MessengerMenuManagerPrivate::onMenuMessageSent(const AbstractBot::ChatAddress &chat,
                                                    const QString &messageId)
{
    const QString key = chatThreadKey(chat);
    if (key.isEmpty())
        return;

    const QString trimmedMessageId = messageId.trimmed();
    if (trimmedMessageId.isEmpty())
        return;

    QHash<QString, QList<PendingMenuMessage>>::iterator it =
            pendingMenuMessagesByChatThread.find(key);
    if (it == pendingMenuMessagesByChatThread.end() || it->isEmpty())
        return;

    const PendingMenuMessage pending = it->takeFirst();
    pendingMenuMessageCount = qMax(0, pendingMenuMessageCount - 1);
    if (it->isEmpty())
        pendingMenuMessagesByChatThread.erase(it);

    Session *session = ensureSession(pending.context);
    if (!session)
        return;

    session->lastMenuMessageId = trimmedMessageId;
    touchSession(session, pending.context);

    if (pending.closePreviousMenu) {
        const QString previousId = pending.previousMenuMessageId.trimmed();
        if (!previousId.isEmpty() && previousId != trimmedMessageId) {
            closeMenuMessage(pending.context.chat,
                             pending.context.sender,
                             previousId,
                             CloseReason::ReplacedByNewMenu);
        }
    }
}

bool MessengerMenuManagerPrivate::hasPendingMenuReplacement(const QString &sessionId,
                                                            const QString &messageId) const
{
    const QString sid = sessionId.trimmed();
    const QString previousId = messageId.trimmed();
    if (sid.isEmpty() || previousId.isEmpty())
        return false;

    for (QHash<QString, QList<PendingMenuMessage>>::const_iterator it =
                 pendingMenuMessagesByChatThread.cbegin();
         it != pendingMenuMessagesByChatThread.cend();
         ++it) {
        const QList<PendingMenuMessage> &pendingMessages = it.value();
        for (const PendingMenuMessage &pending : pendingMessages) {
            if (pending.previousMenuMessageId.trimmed() != previousId)
                continue;
            if (sessionKey(pending.context) == sid)
                return true;
        }
    }

    return false;
}

void MessengerMenuManagerPrivate::purgeExpiredPendingMenuMessages(const QDateTime &nowUtc)
{
    QHash<QString, QList<PendingMenuMessage>>::iterator it =
            pendingMenuMessagesByChatThread.begin();
    while (it != pendingMenuMessagesByChatThread.end()) {
        QList<PendingMenuMessage> &pendingMessages = it.value();
        for (int i = pendingMessages.size() - 1; i >= 0; --i) {
            const QDateTime createdAtUtc = pendingMessages.at(i).createdAtUtc;
            if (createdAtUtc.isValid()
                && createdAtUtc.secsTo(nowUtc) < kPendingMenuMessageTtlSeconds) {
                continue;
            }
            pendingMessages.removeAt(i);
            pendingMenuMessageCount = qMax(0, pendingMenuMessageCount - 1);
        }
        if (pendingMessages.isEmpty()) {
            it = pendingMenuMessagesByChatThread.erase(it);
        } else {
            ++it;
        }
    }
}

void MessengerMenuManagerPrivate::removePendingMenuMessagesForSessions(
        const QSet<QString> &sessionIds)
{
    if (sessionIds.isEmpty())
        return;

    QHash<QString, QList<PendingMenuMessage>>::iterator it =
            pendingMenuMessagesByChatThread.begin();
    while (it != pendingMenuMessagesByChatThread.end()) {
        QList<PendingMenuMessage> &pendingMessages = it.value();
        for (int i = pendingMessages.size() - 1; i >= 0; --i) {
            if (!sessionIds.contains(sessionKey(pendingMessages.at(i).context)))
                continue;
            pendingMessages.removeAt(i);
            pendingMenuMessageCount = qMax(0, pendingMenuMessageCount - 1);
        }
        if (pendingMessages.isEmpty()) {
            it = pendingMenuMessagesByChatThread.erase(it);
        } else {
            ++it;
        }
    }
}

void MessengerMenuManagerPrivate::clearPendingMenuMessages()
{
    pendingMenuMessagesByChatThread.clear();
    pendingMenuMessageCount = 0;
}

MessengerMenuManagerPrivate::Session *MessengerMenuManagerPrivate::ensureSession(
        const MessengerMenu::RenderContext &context)
{
    const QString key = sessionKey(context);
    if (key.isEmpty())
        return nullptr;

    QHash<QString, Session>::iterator it = sessions.find(key);
    if (it == sessions.end()) {
        Session created;
        created.currentMenuId = rootMenuIdValue();
        created.chat = context.chat;
        created.sender = context.sender;
        created.lastActivityUtc = QDateTime::currentDateTimeUtc();
        it = sessions.insert(key, created);
        if (manager)
            Q_EMIT manager->sessionCreated(key);
    }
    return &it.value();
}

void MessengerMenuManagerPrivate::touchSession(MessengerMenuManagerPrivate::Session *session,
                                               const MessengerMenu::RenderContext &context)
{
    if (!session)
        return;
    session->chat = context.chat;
    session->sender = context.sender;
    session->lastActivityUtc = QDateTime::currentDateTimeUtc();
}

const MessengerMenuManagerPrivate::Session *MessengerMenuManagerPrivate::sessionFor(
        const MessengerMenu::RenderContext &context)
{
    const QString key = sessionKey(context);
    if (key.isEmpty())
        return nullptr;

    const QHash<QString, Session>::const_iterator it = sessions.constFind(key);
    if (it == sessions.cend())
        return nullptr;
    return &it.value();
}
