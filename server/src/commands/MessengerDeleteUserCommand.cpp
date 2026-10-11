#include "MessengerDeleteUserCommand.h"

#include "User.h"
#include "UserModel.h"
#include "MessengerBot.h"
#include "ServerDatabase.h"
#include "MessengerTextRender.h"
#include "MessengerCommandManager.h"
#include "MessengerUserFacingErrors.h"

#include <QStringList>

MessengerDeleteUserCommand::MessengerDeleteUserCommand(MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::AdminsOnly, 250)
{
    initParser(tr("/delete_user"), tr("Delete a user."));
}

void MessengerDeleteUserCommand::configureParser(MessengerCommandParser &parser) const
{
    parser.addArgument(QStringLiteral("messenger:id"));
}

void MessengerDeleteUserCommand::execute(const AbstractBot::IncomingMessage &message,
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

    bool targetUserKeyOk = false;
    MessengerUserKey targetUserKey = MessengerUserKey::fromString(parts.at(0), &targetUserKeyOk);
    if (!targetUserKeyOk) {
        b->sendReply(message.chat, MessengerUserFacingErrors::invalidUserValue());
        return;
    }

    User targetUser(targetUserKey, QString(), QString());
    const UserModel *userModel = b->database() ? b->database()->userModel() : nullptr;
    if (userModel) {
        const User existingTarget = userModel->userByKey(targetUserKey);
        if (existingTarget.isValid())
            targetUser = existingTarget;
    }

    const MessengerUserKey actor = AbstractBot::senderKey(message);
    const ServerDatabaseError error = b->database()->deleteUser(targetUserKey, actor);
    if (error) {
        b->sendReply(message.chat, error.errorString());
        return;
    }

    const bool useHtml =
            MessengerTextRender::shouldUseHtmlLists(message.chat.messenger, b->backendBot());
    const QString targetDisplay = MessengerTextRender::formatUserLine(targetUser, useHtml);
    const QString reply = tr("User deleted: %1").arg(targetDisplay);
    b->sendReply(message.chat,
                 reply,
                 useHtml ? AbstractBot::MessageFormat::Html
                         : AbstractBot::MessageFormat::PlainText);
}

QStringList MessengerDeleteUserCommand::details() const
{
    QStringList details;
    details << tr("Use <messenger:id> like telegram:1234.");
    return details;
}

QStringList MessengerDeleteUserCommand::examples() const
{
    return QStringList();
}
