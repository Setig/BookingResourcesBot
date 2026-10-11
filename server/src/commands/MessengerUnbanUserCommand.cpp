#include "MessengerUnbanUserCommand.h"

#include "User.h"
#include "UserModel.h"
#include "MessengerBot.h"
#include "ServerDatabase.h"
#include "MessengerTextRender.h"
#include "MessengerCommandManager.h"
#include "MessengerUserFacingErrors.h"
#include "BookingResourcesTranslator.h"

#include <QStringList>

MessengerUnbanUserCommand::MessengerUnbanUserCommand(MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::AdminsOnly, 270)
{
    initParser(tr("/unban_user"), tr("Remove user from blacklist."));
}

void MessengerUnbanUserCommand::configureParser(MessengerCommandParser &parser) const
{
    parser.addArgument(QStringLiteral("messenger:id"));
}

void MessengerUnbanUserCommand::execute(const AbstractBot::IncomingMessage &message,
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

    BlacklistEntry existingEntry;
    const MessengerUserKey actor = AbstractBot::senderKey(message);
    const ServerDatabaseError error =
            b->database()->unbanUser(targetUserKey, actor, &existingEntry);
    if (error) {
        b->sendReply(message.chat, error.errorString());
        return;
    }

    const bool useHtml =
            MessengerTextRender::shouldUseHtmlLists(message.chat.messenger, b->backendBot());
    const QString targetDisplay = MessengerTextRender::formatUserLine(targetUser, useHtml);
    const QString reply = tr("User %1 unblocked.").arg(targetDisplay);
    b->sendReply(message.chat,
                 reply,
                 useHtml ? AbstractBot::MessageFormat::Html
                         : AbstractBot::MessageFormat::PlainText);

    if (b->hasActiveAccess(targetUserKey)) {
        const QString targetLocale = b->effectiveLocaleForUser(targetUserKey);
        BookingResourcesTranslator::ScopedLocaleOverride targetGuard =
                b->applyScopedLocale(targetLocale);
        Q_UNUSED(targetGuard);

        b->sendDirectMessage(targetUserKey,
                             tr("Your blacklist entry has been removed by administrator."));
    }
}

QStringList MessengerUnbanUserCommand::details() const
{
    QStringList details;
    details << tr("Use <messenger:id> like telegram:1234.");
    return details;
}

QStringList MessengerUnbanUserCommand::examples() const
{
    return QStringList();
}
