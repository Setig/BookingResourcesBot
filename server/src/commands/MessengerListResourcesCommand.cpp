#include "MessengerListResourcesCommand.h"

#include "MessengerBot.h"
#include "MessengerTextRender.h"
#include "MessengerCommandManager.h"

MessengerListResourcesCommand::MessengerListResourcesCommand(MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::AdminsAndUsers, 50)
{
    initParser(tr("/list_resources"), tr("List all resources."));
}

void MessengerListResourcesCommand::execute(const AbstractBot::IncomingMessage &message,
                                            const QString &args)
{
    Q_UNUSED(args);

    MessengerBot *b = bot();
    if (!b || !b->ensureDatabase(message.chat))
        return;

    const MessengerTextRender::RenderedText rendered = MessengerTextRender::renderResourcesList(
            b->database(),
            message.chat.messenger,
            AbstractBot::senderKey(message),
            b->backendBot(),
            MessengerTextRender::ShowResourceAll | MessengerTextRender::HideOffResources);
    b->sendReply(message.chat, rendered.text, rendered.format);
}

QStringList MessengerListResourcesCommand::details() const
{
    return QStringList();
}

QStringList MessengerListResourcesCommand::examples() const
{
    return QStringList();
}
