#include "MessengerUpdateResourceCommand.h"

#include "Resource.h"
#include "MessengerBot.h"
#include "ResourceModel.h"
#include "ServerDatabase.h"
#include "MessengerCommandManager.h"
#include "MessengerUserFacingErrors.h"

#include <QStringList>

MessengerUpdateResourceCommand::MessengerUpdateResourceCommand(MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::AdminsOnly, 210)
{
    initParser(tr("/update_resource"), tr("Update an existing resource."));
}

void MessengerUpdateResourceCommand::configureParser(MessengerCommandParser &parser) const
{
    parser.addArgument(QStringLiteral("name"));
    parser.addArgument(QStringLiteral("description"),
                       QString(),
                       MessengerCommandParser::ArgumentFlag::AllowEmpty);
    parser.addArgument(QStringLiteral("start-message"),
                       QString(),
                       MessengerCommandParser::ArgumentFlag::AllowEmpty);
    parser.addArgument(QStringLiteral("status"),
                       QString(),
                       MessengerCommandParser::ArgumentFlag::Optional
                               | MessengerCommandParser::ArgumentFlag::AllowEmpty);
    parser.addArgument(QStringLiteral("groups"),
                       QString(),
                       MessengerCommandParser::ArgumentFlag::Optional
                               | MessengerCommandParser::ArgumentFlag::AllowEmpty);
}

void MessengerUpdateResourceCommand::execute(const AbstractBot::IncomingMessage &message,
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
    const QStringList parts = parseResult.values;

    const QString name = parts.at(0).trimmed();
    const QString description = parts.at(1).trimmed();
    const QString startMessage = parts.at(2).trimmed();
    const QString statusToken = parts.value(3).trimmed();
    const QString groupsToken = parts.value(4).trimmed();

    Resource::Status parsedStatus = Resource::Status::On;
    const bool hasStatusToken = parseResult.provided.value(3, false) && !statusToken.isEmpty();
    if (hasStatusToken && !Resource::tryParseStatusToken(statusToken, &parsedStatus)) {
        b->sendReply(message.chat, MessengerUserFacingErrors::invalidStatus(statusToken));
        return;
    }

    const Resource existing = b->database()->resourceModel()
                                      ? b->database()->resourceModel()->resourceByName(name)
                                      : Resource();
    Resource resource(existing.uuid(),
                      name,
                      description,
                      startMessage,
                      hasStatusToken
                              ? parsedStatus
                              : (existing.isValid() ? existing.status() : Resource::Status::On));
    if (parseResult.provided.value(4, false)) {
        QSet<Group> groups;
        QString groupsError;
        if (!parseGroupListToken(groupsToken, &groups, &groupsError)) {
            b->sendReply(message.chat, groupsError);
            return;
        }
        resource.setGroups(groups);
    } else if (existing.isValid()) {
        resource.setGroups(existing.groups());
    }
    const MessengerUserKey actor = AbstractBot::senderKey(message);
    const ServerDatabaseError error = b->database()->replaceResource(name, resource, actor);
    if (error) {
        b->sendReply(message.chat, error.errorString());
        return;
    }

    const Resource updatedResource = b->database()->resourceModel()
                                             ? b->database()->resourceModel()->resourceByName(name)
                                             : Resource();
    const Resource replyResource = updatedResource.isValid() ? updatedResource : resource;
    b->sendReply(message.chat, tr("Resource updated: %1").arg(formatResourceLine(replyResource)));
}

QStringList MessengerUpdateResourceCommand::details() const
{
    QStringList details;
    details << tr("Description and start-message can be empty.");
    details << tr("Status is optional: On, TemporarilyUnavailable, Off.");
    details << tr("Groups are optional: use group/subgroup,other_group or \"\" for Root (all "
                  "groups).");
    details << tr("Group paths cannot contain '=' or empty segments.");
    details << tr("Use quotes for arguments with spaces.");
    details << tr("Use \"\" for empty description or start-message.");
    return details;
}

QStringList MessengerUpdateResourceCommand::examples() const
{
    QStringList examples;
    examples << tr("/update_resource \"Printer\" \"Updated description\" \"\"");
    examples << tr("/update_resource \"Projector\" \"\" \"New start message\"");
    examples << tr("/update_resource \"Projector\" \"\" \"\" \"TemporarilyUnavailable\"");
    examples << tr("/update_resource \"Projector\" \"\" \"\" \"On\" \"test_group_1\"");
    return examples;
}
