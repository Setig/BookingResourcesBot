#include "MessengerAddResourceCommand.h"

#include "Resource.h"
#include "MessengerBot.h"
#include "ServerDatabase.h"
#include "MessengerCommandManager.h"

#include <QStringList>

MessengerAddResourceCommand::MessengerAddResourceCommand(MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::AdminsOnly, 200)
{
    initParser(tr("/add_resource"), tr("Add a new resource."));
}

void MessengerAddResourceCommand::configureParser(MessengerCommandParser &parser) const
{
    parser.addArgument(QStringLiteral("name"));
    parser.addArgument(QStringLiteral("description"),
                       QString(),
                       MessengerCommandParser::ArgumentFlag::AllowEmpty);
    parser.addArgument(QStringLiteral("start-message"),
                       QString(),
                       MessengerCommandParser::ArgumentFlag::AllowEmpty);
    parser.addArgument(QStringLiteral("groups"),
                       QString(),
                       MessengerCommandParser::ArgumentFlag::Optional
                               | MessengerCommandParser::ArgumentFlag::AllowEmpty);
}

void MessengerAddResourceCommand::execute(const AbstractBot::IncomingMessage &message,
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
    const QString groupsToken = parts.value(3).trimmed();
    const bool groupsProvided = parseResult.provided.value(3, false);
    const MessengerUserKey actor = AbstractBot::senderKey(message);

    QSet<Group> groups;
    if (groupsProvided) {
        QString groupsError;
        if (!parseGroupListToken(groupsToken, &groups, &groupsError)) {
            b->sendReply(message.chat, groupsError);
            return;
        }
    } else {
        groups = b->database()->administeredGroupRoots(actor);
    }

    Resource resource(name, description, startMessage);
    resource.setGroups(groups);
    const ServerDatabaseError error = b->database()->addResource(resource, actor);
    if (error) {
        b->sendReply(message.chat, error.errorString());
        return;
    }

    b->sendReply(message.chat, tr("Resource added: %1").arg(formatResourceLine(resource)));
}

QStringList MessengerAddResourceCommand::details() const
{
    QStringList details;
    details << tr("Description and start-message can be empty.");
    details << tr("Use quotes for arguments with spaces.");
    details << tr("Use \"\" for empty description or start-message.");
    details << tr("Groups are optional: omit them to use your administered groups, or use \"\" "
                  "for Root (all groups).");
    details << tr("Group paths cannot contain '=' or empty segments.");
    return details;
}

QStringList MessengerAddResourceCommand::examples() const
{
    QStringList examples;
    examples << tr("/add_resource \"Printer\" \"Color laser printer\" \"Load paper before use\"");
    examples << tr("/add_resource \"Projector\" \"\" \"Turn off after use\"");
    examples << tr("/add_resource \"Test printer\" \"\" \"\" \"test_group_1\"");
    return examples;
}
