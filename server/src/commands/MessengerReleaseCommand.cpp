#include "MessengerReleaseCommand.h"

#include "Resource.h"
#include "MessengerBot.h"
#include "ResourceModel.h"
#include "ServerDatabase.h"
#include "MessengerTextRender.h"
#include "MessengerCommandManager.h"
#include "ResourceQueueUserMessages.h"

#include <QStringList>

MessengerReleaseCommand::MessengerReleaseCommand(MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::UsersOnly, 170)
{
    initParser(tr("/release"), tr("Release a resource you occupy."));
}

void MessengerReleaseCommand::configureParser(MessengerCommandParser &parser) const
{
    parser.addArgument(QStringLiteral("name"));
}

void MessengerReleaseCommand::execute(const AbstractBot::IncomingMessage &message,
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

    const ServerDatabaseError releaseError =
            b->database()->releaseUserOccupationForResource(resourceName, actor, actor);
    if (releaseError) {
        b->sendReply(message.chat, releaseError.errorString());
        return;
    }

    b->sendReply(message.chat,
                 MessengerTextRender::formatReleasedOccupationReply(b->database(),
                                                                    resourceName,
                                                                    actor));

    b->notifyNextUserQueueStatus(resourceName, actor);
}

QStringList MessengerReleaseCommand::details() const
{
    return QStringList();
}

QStringList MessengerReleaseCommand::examples() const
{
    return QStringList();
}
