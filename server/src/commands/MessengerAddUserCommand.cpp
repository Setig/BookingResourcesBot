#include "MessengerAddUserCommand.h"

#include "User.h"
#include "MessengerBot.h"
#include "ServerDatabase.h"
#include "MessengerTextRender.h"
#include "MessengerCommandManager.h"
#include "MessengerUserFacingErrors.h"

#include <QStringList>

MessengerAddUserCommand::MessengerAddUserCommand(MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::AdminsOnly, 230)
{
    initParser(tr("/add_user"), tr("Add a new user."));
}

void MessengerAddUserCommand::configureParser(MessengerCommandParser &parser) const
{
    parser.addArgument(QStringLiteral("messenger:id"));
    parser.addArgument(QStringLiteral("first"),
                       QString(),
                       MessengerCommandParser::ArgumentFlag::Optional
                               | MessengerCommandParser::ArgumentFlag::AllowEmpty);
    parser.addArgument(QStringLiteral("last"),
                       QString(),
                       MessengerCommandParser::ArgumentFlag::Optional
                               | MessengerCommandParser::ArgumentFlag::AllowEmpty);
    parser.addArgument(QStringLiteral("admin"),
                       QString(),
                       MessengerCommandParser::ArgumentFlag::Optional
                               | MessengerCommandParser::ArgumentFlag::AllowEmpty);
    parser.addArgument(QStringLiteral("groups"),
                       QString(),
                       MessengerCommandParser::ArgumentFlag::Optional
                               | MessengerCommandParser::ArgumentFlag::AllowEmpty);
}

void MessengerAddUserCommand::execute(const AbstractBot::IncomingMessage &message,
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

    bool userKeyOk = false;
    MessengerUserKey userKey = MessengerUserKey::fromString(parts.at(0), &userKeyOk);
    if (!userKeyOk) {
        b->sendReply(message.chat, MessengerUserFacingErrors::invalidUserValue());
        return;
    }

    AbstractBot::UserProfile remoteProfile;
    QString lookupError;
    if (!fetchMessengerUserProfile(userKey.messenger(),
                                   userKey.userId(),
                                   &remoteProfile,
                                   &lookupError)) {
        b->sendReply(message.chat,
                     MessengerUserFacingErrors::unableToVerifyUser(lookupError, userKey.userId()));
        return;
    }

    const auto sanitizeName = [](const QString &value) { return value.trimmed(); };

    QString first = (parts.size() > 1) ? sanitizeName(parts.at(1)) : QString();
    QString last = (parts.size() > 2) ? sanitizeName(parts.at(2)) : QString();

    if (first.isEmpty())
        first = sanitizeName(remoteProfile.firstName);
    if (last.isEmpty())
        last = sanitizeName(remoteProfile.lastName);

    if (first.isEmpty() && last.isEmpty())
        first = remoteProfile.userId;

    bool admin = false;
    if (parts.size() > 3) {
        const QString adminToken = sanitizeName(parts.at(3));
        if (!adminToken.isEmpty() && !parseBoolTokenValue(adminToken, &admin)) {
            b->sendReply(message.chat, MessengerUserFacingErrors::invalidAdminValue(adminToken));
            return;
        }
    }

    const MessengerUserKey actor = AbstractBot::senderKey(message);
    const User::Role selectedRole = admin ? User::Role::AdminRole : User::Role::UserRole;
    User user(userKey.messenger(), userKey.userId(), first, last, selectedRole);
    const QString groupsToken = parts.value(4).trimmed();
    const bool groupsProvided = parseResult.provided.value(4, false);
    if (groupsProvided) {
        QMap<Group, User::Role> groupRoles;
        if (groupsToken.isEmpty() || groupsToken == QStringLiteral("-")) {
            groupRoles.insert(Group::root(), selectedRole);
        } else {
            QString groupsError;
            if (!parseUserGroupRolesToken(groupsToken, &groupRoles, &groupsError)) {
                b->sendReply(message.chat, groupsError);
                return;
            }
        }
        user.setGroupRoles(groupRoles);
    } else {
        QMap<Group, User::Role> groupRoles;
        const QSet<Group> defaultGroups = b->database()->administeredGroupRoots(actor);
        for (const Group &group : defaultGroups)
            groupRoles.insert(group, selectedRole);
        user.setGroupRoles(groupRoles);
    }
    bool removedFromBlacklist = false;
    const ServerDatabaseError error = b->database()->addUser(user, actor, &removedFromBlacklist);
    if (error) {
        b->sendReply(message.chat, error.errorString());
        return;
    }
    b->notifyAccessActivated(user.key());

    const bool useHtml =
            MessengerTextRender::shouldUseHtmlLists(message.chat.messenger, b->backendBot());

    QStringList replyLines;
    replyLines << tr("User added: %1").arg(formatUserLine(user, useHtml));
    if (removedFromBlacklist) {
        const QString unblacklistedLine = tr("User was removed from the blacklist.");
        replyLines << (useHtml ? unblacklistedLine.toHtmlEscaped() : unblacklistedLine);
    }

    b->sendReply(message.chat,
                 replyLines.join(QStringLiteral("\n")),
                 useHtml ? AbstractBot::MessageFormat::Html
                         : AbstractBot::MessageFormat::PlainText);
}

QStringList MessengerAddUserCommand::details() const
{
    QStringList details;
    details << tr("Use <messenger:id> like telegram:1234.");
    details << tr("First/last name can be omitted to use messenger profile values.");
    details << tr("Admin flag is optional; use 1/true/yes/on/admin to enable.");
    details << tr("Groups are optional: omit them to use your administered groups, or use "
                  "group=user,group/subgroup=admin.");
    details << tr("Use \"\" to skip optional first/last name fields.");
    return details;
}

QStringList MessengerAddUserCommand::examples() const
{
    QStringList examples;
    examples << tr("/add_user telegram:1234 John Doe true");
    examples << tr("/add_user telegram:1234 \"\" \"\" admin");
    examples << tr("/add_user telegram:1234 John Doe \"\" \"test_group_1=user\"");
    return examples;
}
