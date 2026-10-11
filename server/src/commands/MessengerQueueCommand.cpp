#include "MessengerQueueCommand.h"

#include "MessengerBot.h"
#include "MessengerTextRender.h"
#include "MessengerCommandManager.h"

#include <QStringList>

MessengerQueueCommand::MessengerQueueCommand(MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::AdminsAndUsers, 70)
{
    initParser(tr("/queue"), tr("Show queue for a resource."));
}

void MessengerQueueCommand::configureParser(MessengerCommandParser &parser) const
{
    parser.addArgument(QStringLiteral("name"));
}

void MessengerQueueCommand::execute(const AbstractBot::IncomingMessage &message,
                                    const QString &args)
{
    MessengerBot *b = bot();
    if (!b || !b->ensureDatabase(message.chat))
        return;

    const MessengerCommandParser::ParseResult parseResult = parseArguments(args);
    if (!parseResult.ok) {
        b->sendReply(message.chat, parseResult.errorText);
        return;
    }

    const QString resourceName = parseResult.values.value(0).trimmed();
    const MessengerTextRender::RenderedText rendered =
            MessengerTextRender::renderResourceQueueStatus(b->database(),
                                                           message.chat.messenger,
                                                           resourceName,
                                                           AbstractBot::senderKey(message),
                                                           b->backendBot());
    b->sendReply(message.chat, rendered.text, rendered.format);
}

QStringList MessengerQueueCommand::details() const
{
    return QStringList();
}

QStringList MessengerQueueCommand::examples() const
{
    return QStringList();
}
