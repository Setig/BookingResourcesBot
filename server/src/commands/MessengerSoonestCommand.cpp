#include "MessengerSoonestCommand.h"

#include "MessengerBot.h"
#include "MessengerTextRender.h"
#include "MessengerCommandManager.h"
#include "MessengerUserFacingErrors.h"

MessengerSoonestCommand::MessengerSoonestCommand(MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::AdminsAndUsers, 60)
{
    initParser(tr("/soonest"), tr("Show resources that free up soon."));
}

void MessengerSoonestCommand::configureParser(MessengerCommandParser &parser) const
{
    parser.addArgument(QStringLiteral("count"),
                       QString(),
                       MessengerCommandParser::ArgumentFlag::Optional);
}

void MessengerSoonestCommand::execute(const AbstractBot::IncomingMessage &message,
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

    int limit = 5;
    const QString trimmed = parseResult.values.value(0).trimmed();
    if (!trimmed.isEmpty()) {
        bool ok = false;
        const int requested = trimmed.toInt(&ok);
        if (!ok || requested <= 0) {
            b->sendReply(message.chat, MessengerUserFacingErrors::invalidSoonestCount(trimmed));
            return;
        }
        limit = qMin(requested, 20);
    }

    const MessengerTextRender::RenderedText rendered = MessengerTextRender::renderSoonestResources(
            b->database(),
            message.chat.messenger,
            limit,
            AbstractBot::senderKey(message),
            b->backendBot(),
            MessengerTextRender::ShowResourceListTitle | MessengerTextRender::ShowResourceStatus
                    | MessengerTextRender::HideOffResources);
    b->sendReply(message.chat, rendered.text, rendered.format);
}

QStringList MessengerSoonestCommand::details() const
{
    QStringList details;
    details << tr("Count defaults to 5 and is capped at 20. A non-numeric or non-positive count "
                  "is rejected.");
    return details;
}

QStringList MessengerSoonestCommand::examples() const
{
    return QStringList();
}
