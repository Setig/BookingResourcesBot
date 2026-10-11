#include "MessengerListPendingAccessUsersCommand.h"

#include "MessengerBot.h"
#include "MessengerTextRender.h"
#include "MessengerCommandManager.h"

MessengerListPendingAccessUsersCommand::MessengerListPendingAccessUsersCommand(
        MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::ServerAdminsOnly, 110)
{
    initParser(tr("/pending_access"), tr("List recent users awaiting access activation."));
}

void MessengerListPendingAccessUsersCommand::execute(const AbstractBot::IncomingMessage &message,
                                                     const QString &args)
{
    Q_UNUSED(args);

    MessengerBot *b = bot();
    if (!b || !b->ensureDatabase(message.chat))
        return;

    const MessengerTextRender::RenderedText rendered =
            MessengerTextRender::renderPendingAccessUsers(b->database(),
                                                          message.chat.messenger,
                                                          AbstractBot::senderKey(message),
                                                          b->backendBot());
    b->sendReply(message.chat, rendered.text, rendered.format);
}

QStringList MessengerListPendingAccessUsersCommand::details() const
{
    return QStringList();
}

QStringList MessengerListPendingAccessUsersCommand::examples() const
{
    return QStringList();
}
