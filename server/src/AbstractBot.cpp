#include "AbstractBot.h"

AbstractBot::AbstractBot(QObject *parent)
    : QObject(parent)
    , m_commandManager(nullptr)
{
    // do nothing
}

AbstractBot::~AbstractBot()
{
    // do nothing
}

void AbstractBot::startShutdown()
{
    stop();
}

bool AbstractBot::waitShutdown(int msecs)
{
    Q_UNUSED(msecs);
    // Default implementation: no-op.
    return true;
}

bool AbstractBot::fetchUserProfile(const QString &userId,
                                   AbstractBot::UserProfile *profileOut,
                                   QString *errorDescription)
{
    Q_UNUSED(userId);
    Q_UNUSED(profileOut);
    Q_UNUSED(errorDescription);
    return false;
}

void AbstractBot::editMessage(const AbstractBot::ChatAddress &chat,
                              const QString &messageId,
                              const QString &text,
                              const AbstractBot::MessageOptions &options)
{
    Q_UNUSED(messageId);
    sendMessage(chat, text, options);
}

void AbstractBot::setCommandManager(const MessengerCommandManager *commandManager)
{
    m_commandManager = commandManager;
    syncCommandsWithPlatform();
}

const MessengerCommandManager *AbstractBot::commandManager() const
{
    return m_commandManager;
}

void AbstractBot::syncCommandsWithPlatform()
{
    // Default implementation: no-op.
}

MessengerUserKey AbstractBot::senderKey(const AbstractBot::IncomingMessage &message)
{
    return senderKey(message.chat, message.sender);
}

MessengerUserKey AbstractBot::senderKey(const AbstractBot::CallbackQuery &query)
{
    return senderKey(query.chat, query.sender);
}

MessengerUserKey AbstractBot::senderKey(const AbstractBot::ChatAddress &chat,
                                        const AbstractBot::UserAddress &sender)
{
    return MessengerUserKey(chat.messenger, sender.userId);
}
