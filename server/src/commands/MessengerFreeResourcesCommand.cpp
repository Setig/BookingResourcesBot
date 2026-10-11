#include "MessengerFreeResourcesCommand.h"

#include "MessengerBot.h"
#include "MessengerTextRender.h"
#include "MessengerCommandManager.h"

MessengerFreeResourcesCommand::MessengerFreeResourcesCommand(MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::AdminsAndUsers, 40)
{
    initParser(tr("/free_resources"), tr("List currently free resources."));
}

void MessengerFreeResourcesCommand::execute(const AbstractBot::IncomingMessage &message,
                                            const QString &args)
{
    Q_UNUSED(args);

    MessengerBot *b = bot();
    if (!b || !b->ensureDatabase(message.chat))
        return;

    const MessengerTextRender::RenderedText rendered = MessengerTextRender::renderFreeResources(
            b->database(),
            message.chat.messenger,
            AbstractBot::senderKey(message),
            b->backendBot(),
            MessengerTextRender::ShowResourceListTitle | MessengerTextRender::ShowResourceStatus
                    | MessengerTextRender::HideOffResources);
    b->sendReply(message.chat, rendered.text, rendered.format);
}

QStringList MessengerFreeResourcesCommand::details() const
{
    return QStringList();
}

QStringList MessengerFreeResourcesCommand::examples() const
{
    return QStringList();
}
