#include "MessengerUnscheduleCommand.h"

#include "Resource.h"
#include "MessengerBot.h"
#include "ResourceModel.h"
#include "ServerDatabase.h"
#include "ResourceQueueModel.h"
#include "MessengerTextRender.h"
#include "MessengerCommandManager.h"
#include "ResourceQueueUserMessages.h"

#include <QStringList>

MessengerUnscheduleCommand::MessengerUnscheduleCommand(MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::UsersOnly, 180)
{
    initParser(tr("/unschedule"), tr("Remove your schedule for a resource."));
}

void MessengerUnscheduleCommand::configureParser(MessengerCommandParser &parser) const
{
    parser.addArgument(QStringLiteral("name"));
}

void MessengerUnscheduleCommand::execute(const AbstractBot::IncomingMessage &message,
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
    const MessengerUserKey actor = AbstractBot::senderKey(message);
    const ResourceModel *model = b->database()->resourceModel();
    const Resource resource = model ? model->resourceByName(resourceName) : Resource();
    if (!MessengerTextRender::isResourceVisibleToViewer(resource,
                                                        b->database(),
                                                        message.chat.messenger,
                                                        actor)) {
        b->sendReply(message.chat, ResourceQueueUserMessages::resourceNotFound(resourceName));
        return;
    }

    const ServerDatabaseError scheduleError =
            b->database()->removeUserSchedulesForResource(resourceName, actor, actor);
    if (scheduleError) {
        b->sendReply(message.chat, scheduleError.errorString());
        return;
    }

    b->sendReply(message.chat, tr("Schedule removed for '%1'.").arg(resourceName));
}

QStringList MessengerUnscheduleCommand::details() const
{
    return QStringList();
}

QStringList MessengerUnscheduleCommand::examples() const
{
    return QStringList();
}
