#include "MessengerDeleteResourceCommand.h"

#include "Resource.h"
#include "MessengerBot.h"
#include "ResourceModel.h"
#include "ServerDatabase.h"
#include "MessengerTextRender.h"
#include "MessengerCommandManager.h"

#include <QStringList>

MessengerDeleteResourceCommand::MessengerDeleteResourceCommand(MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::AdminsOnly, 220)
{
    initParser(tr("/delete_resource"), tr("Delete a resource."));
}

void MessengerDeleteResourceCommand::configureParser(MessengerCommandParser &parser) const
{
    parser.addArgument(QStringLiteral("name"));
}

void MessengerDeleteResourceCommand::execute(const AbstractBot::IncomingMessage &message,
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
    const QString name = parseResult.values.value(0).trimmed();
    Resource resource;
    if (const ResourceModel *model = b->database()->resourceModel())
        resource = model->resourceByName(name);

    const MessengerUserKey actor = AbstractBot::senderKey(message);
    const ServerDatabaseError error = b->database()->deleteResource(name, actor);
    if (error) {
        b->sendReply(message.chat, error.errorString());
        return;
    }

    const QString resourceDisplay =
            resource.isValid() ? MessengerTextRender::formatResourceLine(resource, false) : name;
    b->sendReply(message.chat, tr("Resource deleted: %1").arg(resourceDisplay));
}

QStringList MessengerDeleteResourceCommand::details() const
{
    return QStringList();
}

QStringList MessengerDeleteResourceCommand::examples() const
{
    return QStringList();
}
