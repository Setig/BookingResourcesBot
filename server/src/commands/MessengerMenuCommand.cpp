#include "MessengerMenuCommand.h"

#include "MessengerBot.h"
#include "MessengerMenuManager.h"
#include "MessengerCommandManager.h"

MessengerMenuCommand::MessengerMenuCommand(MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::AdminsAndUsers, 20)
{
    initParser(tr("/menu"), tr("Open the interactive menu."));
}

void MessengerMenuCommand::execute(const AbstractBot::IncomingMessage &message, const QString &args)
{
    Q_UNUSED(args);

    MessengerBot *b = bot();
    if (!b)
        return;

    MessengerMenuManager *menuManager = b->menuManager();
    if (!menuManager) {
        b->sendReply(message.chat, tr("Menu is not available right now."));
        return;
    }

    MessengerMenu::RenderContext context;
    context.chat = message.chat;
    context.sender = message.sender;
    context.isAdmin = b->isAdminUser(AbstractBot::senderKey(message));
    context.canManageServerSettings = b->canManageServerSettings(AbstractBot::senderKey(message));

    MessengerMenuManager::Reply reply = menuManager->renderRoot(context);
    if (!reply.valid) {
        b->sendReply(message.chat, tr("Menu is not available right now."));
        return;
    }

    QString text =
            reply.text.trimmed().isEmpty() ? tr("Menu is not available right now.") : reply.text;
    if (!reply.options.inlineKeyboard.isEmpty()) {
        menuManager->expectMenuMessageId(context, true);
    }
    b->sendReply(message.chat, text, reply.options);
}

QStringList MessengerMenuCommand::details() const
{
    return QStringList();
}

QStringList MessengerMenuCommand::examples() const
{
    return QStringList();
}
