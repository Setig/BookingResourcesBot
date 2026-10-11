#include "MessengerBlacklistCommand.h"

#include "MessengerBot.h"
#include "MessengerTextRender.h"
#include "MessengerCommandManager.h"

MessengerBlacklistCommand::MessengerBlacklistCommand(MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::AdminsOnly, 100)
{
    initParser(tr("/blacklist"), tr("Show active blacklist entries."));
}

void MessengerBlacklistCommand::execute(const AbstractBot::IncomingMessage &message,
                                        const QString &args)
{
    Q_UNUSED(args);

    MessengerBot *b = bot();
    if (!b || !b->ensureDatabase(message.chat))
        return;

    const MessengerTextRender::RenderedText rendered =
            MessengerTextRender::renderBlacklist(b->database(),
                                                 message.chat.messenger,
                                                 AbstractBot::senderKey(message),
                                                 b->backendBot());
    b->sendReply(message.chat, rendered.text, rendered.format);
}

QStringList MessengerBlacklistCommand::details() const
{
    return QStringList();
}

QStringList MessengerBlacklistCommand::examples() const
{
    return QStringList();
}
