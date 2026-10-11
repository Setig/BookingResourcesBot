#include "MessengerListUsersCommand.h"

#include "MessengerBot.h"
#include "MessengerTextRender.h"
#include "MessengerCommandManager.h"

MessengerListUsersCommand::MessengerListUsersCommand(MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::AdminsAndUsers, 80)
{
    initParser(tr("/list_users"), tr("List users visible through your group scope."));
}

void MessengerListUsersCommand::execute(const AbstractBot::IncomingMessage &message,
                                        const QString &args)
{
    Q_UNUSED(args);

    MessengerBot *b = bot();
    if (!b || !b->ensureDatabase(message.chat))
        return;

    const bool viewerIsAdmin = b->isAdminUser(AbstractBot::senderKey(message));
    const MessengerTextRender::RenderedText rendered =
            MessengerTextRender::renderUsersList(b->database(),
                                                 message.chat.messenger,
                                                 viewerIsAdmin,
                                                 AbstractBot::senderKey(message),
                                                 b->backendBot());
    b->sendReply(message.chat, rendered.text, rendered.format);
}

QStringList MessengerListUsersCommand::details() const
{
    return QStringList();
}

QStringList MessengerListUsersCommand::examples() const
{
    return QStringList();
}
