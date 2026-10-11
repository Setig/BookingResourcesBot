#include "MessengerUpdateUserCommand.h"

#include "User.h"
#include "UserModel.h"
#include "MessengerBot.h"
#include "ServerDatabase.h"
#include "MessengerTextRender.h"
#include "MessengerCommandManager.h"
#include "MessengerUserFacingErrors.h"

#include <QStringList>

MessengerUpdateUserCommand::MessengerUpdateUserCommand(MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::AdminsOnly, 240)
{
    initParser(tr("/update_user"), tr("Update an existing user."));
}

void MessengerUpdateUserCommand::configureParser(MessengerCommandParser &parser) const
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

void MessengerUpdateUserCommand::execute(const AbstractBot::IncomingMessage &message,
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

    User existing;
    if (const UserModel *model = b->database()->userModel())
        existing = model->userByKey(userKey);

    QString first = parts.value(1).trimmed();
    QString last = parts.value(2).trimmed();
    QString adminToken = parts.value(3).trimmed();
    bool admin = existing.isValid() && existing.isAdmin();
    bool adminProvided = parseResult.provided.value(3, false) && !adminToken.isEmpty();

    bool roleOnlyAdmin = admin;
    if (adminToken.isEmpty() && last.isEmpty() && !first.isEmpty()
        && parseBoolTokenValue(first, &roleOnlyAdmin)) {
        adminToken = first;
        first.clear();
        admin = roleOnlyAdmin;
        adminProvided = true;
    }

    if (!adminToken.isEmpty() && !parseBoolTokenValue(adminToken, &admin)) {
        b->sendReply(message.chat, MessengerUserFacingErrors::invalidAdminValue(adminToken));
        return;
    }

    if (first.isEmpty() && existing.isValid())
        first = existing.firstName();
    if (last.isEmpty() && existing.isValid())
        last = existing.lastName();

    User user = existing.isValid()
                        ? existing
                        : User(userKey.messenger(), userKey.userId(), first, last, admin);
    user.setFirstName(first);
    user.setLastName(last);

    const User::Role selectedRole = admin ? User::Role::AdminRole : User::Role::UserRole;
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
    } else if (adminProvided && existing.isValid()) {
        QMap<Group, User::Role> groupRoles;
        const QMap<Group, User::Role> existingRoles = existing.groupRoles();
        for (auto it = existingRoles.constBegin(); it != existingRoles.constEnd(); ++it)
            groupRoles.insert(it.key(), selectedRole);
        user.setGroupRoles(groupRoles);
    } else if (!existing.isValid()) {
        user.setIsAdmin(admin);
    }
    const MessengerUserKey actor = AbstractBot::senderKey(message);
    const ServerDatabaseError error = b->database()->updateUser(user, actor);
    if (error) {
        b->sendReply(message.chat, error.errorString());
        return;
    }

    User effective = user;
    if (const UserModel *model = b->database()->userModel()) {
        const User refreshed = model->userByKey(userKey);
        if (refreshed.isValid())
            effective = refreshed;
    }

    const bool useHtml =
            MessengerTextRender::shouldUseHtmlLists(message.chat.messenger, b->backendBot());
    b->sendReply(message.chat,
                 tr("User updated: %1").arg(formatUserLine(effective, useHtml)),
                 useHtml ? AbstractBot::MessageFormat::Html
                         : AbstractBot::MessageFormat::PlainText);
}

QStringList MessengerUpdateUserCommand::details() const
{
    QStringList details;
    details << tr("Use <messenger:id> like telegram:1234.");
    details << tr("First/last name can be omitted to keep existing values.");
    details << tr("Admin flag is optional; use 1/true/yes/on/admin to enable.");
    details << tr("Groups are optional: use group=user,group/subgroup=admin.");
    return details;
}

QStringList MessengerUpdateUserCommand::examples() const
{
    QStringList examples;
    examples << tr("/update_user telegram:1234 John Doe");
    examples << tr("/update_user telegram:1234 John Doe true");
    examples << tr("/update_user telegram:1234 admin");
    examples << tr("/update_user telegram:1234 John Doe \"\" \"test_group_1=admin\"");
    return examples;
}
