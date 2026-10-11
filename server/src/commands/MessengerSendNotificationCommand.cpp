#include "MessengerSendNotificationCommand.h"

#include "User.h"
#include "UserModel.h"
#include "MessengerBot.h"
#include "MessengerBotPoll.h"
#include "ServerDatabase.h"
#include "MessengerTextRender.h"
#include "MessengerCommandManager.h"
#include "MessengerUserFacingErrors.h"

namespace {
QStringList splitRecipientsToken(const QString &token)
{
    QStringList values = token.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (QString &value : values)
        value = value.trimmed();
    values.erase(std::remove_if(values.begin(),
                                values.end(),
                                [](const QString &value) { return value.trimmed().isEmpty(); }),
                 values.end());
    return values;
}

bool canSendDirectNotification(MessengerBot *bot, const MessengerUserKey &user)
{
    if (!bot || !user.isValid())
        return false;
    if (user.messenger() == bot->botType())
        return true;
    MessengerBotPoll *poll = bot->poll();
    return poll && poll->backendBot(user.messenger());
}

bool userMatchesNotificationScope(ServerDatabase *database,
                                  const MessengerUserKey &actor,
                                  const User &recipient)
{
    return database && database->canAdministerUser(actor, recipient);
}
} // namespace

MessengerSendNotificationCommand::MessengerSendNotificationCommand(MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::AdminsOnly, 190)
{
    initParser(tr("/send_notification"), tr("Send an alert to users."));
}

void MessengerSendNotificationCommand::configureParser(MessengerCommandParser &parser) const
{
    parser.addArgument(QStringLiteral("recipients"),
                       QString(),
                       MessengerCommandParser::ArgumentFlag::Optional);
    parser.addArgument(QStringLiteral("message"),
                       QString(),
                       MessengerCommandParser::ArgumentFlag::Optional
                               | MessengerCommandParser::ArgumentFlag::Greedy);
}

void MessengerSendNotificationCommand::execute(const AbstractBot::IncomingMessage &message,
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

    const QString recipientToken = parseResult.values.value(0).trimmed();
    QString rawMessageText = parseResult.values.value(0);
    if (!parseResult.values.value(1).trimmed().isEmpty()) {
        if (!rawMessageText.isEmpty())
            rawMessageText.append(QLatin1Char(' '));
        rawMessageText.append(parseResult.values.value(1));
    }
    bool explicitRecipients = false;

    if (!recipientToken.isEmpty()) {
        const QStringList candidates = splitRecipientsToken(recipientToken);
        bool allCandidatePrefixesAreSupported = !candidates.isEmpty();
        for (const QString &candidate : candidates) {
            const int colonIndex = candidate.indexOf(QLatin1Char(':'));
            if (colonIndex <= 0) {
                allCandidatePrefixesAreSupported = false;
                break;
            }
            const QString messengerToken = candidate.left(colonIndex).trimmed();
            MessengerType parsedMessenger = MessengerType::LocalCli;
            if (!parseMessengerToken(messengerToken, &parsedMessenger)) {
                allCandidatePrefixesAreSupported = false;
                break;
            }
        }
        if (!allCandidatePrefixesAreSupported && recipientToken.contains(QLatin1Char(':'))
            && !parseResult.values.value(1).trimmed().isEmpty()) {
            b->sendReply(message.chat, MessengerUserFacingErrors::invalidUserValue(recipientToken));
            return;
        }
        if (allCandidatePrefixesAreSupported) {
            explicitRecipients = true;
            rawMessageText = parseResult.values.value(1);
        }
    }

    if (MessengerTextRender::formatUserAlertMessage(rawMessageText).isEmpty()) {
        b->sendReply(message.chat,
                     tr("%1\nUsage: %2")
                             .arg(MessengerUserFacingErrors::alertTextCannotBeEmpty(),
                                  parser().usageSignature()));
        return;
    }

    ServerDatabase *database = b->database();
    const UserModel *model = database ? database->userModel() : nullptr;
    if (!model) {
        b->sendReply(message.chat, MessengerUserFacingErrors::userModelNotAvailable());
        return;
    }

    const MessengerUserKey actor = AbstractBot::senderKey(message);
    const QList<User> users = model->items();

    QList<MessengerUserKey> requestedRecipients;
    if (explicitRecipients) {
        const QStringList recipientValues = splitRecipientsToken(recipientToken);
        for (const QString &value : recipientValues) {
            bool recipientOk = false;
            const MessengerUserKey key = MessengerUserKey::fromString(value, &recipientOk);
            if (!recipientOk || !key.isValid()) {
                b->sendReply(message.chat, MessengerUserFacingErrors::invalidUserValue(value));
                return;
            }
            bool duplicate = false;
            for (const MessengerUserKey &existing : std::as_const(requestedRecipients)) {
                if (existing == key) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate)
                requestedRecipients.append(key);
        }
    }

    QHash<QString, MessengerUserKey> knownUsersByKey;
    knownUsersByKey.reserve(users.size());
    for (const User &user : users) {
        if (!user.isValid())
            continue;
        const MessengerUserKey key = user.key();
        if (!key.isValid())
            continue;
        if (!canSendDirectNotification(b, key))
            continue;
        if (!userMatchesNotificationScope(database, actor, user))
            continue;
        knownUsersByKey.insert(key.toString(), key);
    }

    int sentCount = 0;
    if (explicitRecipients) {
        QList<MessengerUserKey> deliverableRecipients;
        for (const MessengerUserKey &recipient : std::as_const(requestedRecipients)) {
            if (!recipient.isValid())
                continue;
            if (actor.isValid() && recipient == actor)
                continue;
            const QString keyText = recipient.toString();
            if (!knownUsersByKey.contains(keyText)) {
                b->sendReply(message.chat, MessengerUserFacingErrors::userNotFound(keyText));
                return;
            }
            deliverableRecipients.append(recipient);
        }
        for (const MessengerUserKey &recipient : std::as_const(deliverableRecipients)) {
            const bool useHtml =
                    b->supportsDirectMessageFormat(recipient, AbstractBot::MessageFormat::Html);
            b->sendDirectMessage(recipient,
                                 MessengerTextRender::formatUserAlertMessage(rawMessageText,
                                                                             useHtml),
                                 useHtml ? AbstractBot::MessageFormat::Html
                                         : AbstractBot::MessageFormat::PlainText);
            ++sentCount;
        }
    } else {
        for (const User &user : users) {
            if (!user.isValid())
                continue;

            const MessengerUserKey key = user.key();
            if (!key.isValid())
                continue;
            if (!canSendDirectNotification(b, key))
                continue;
            if (actor.isValid() && key == actor)
                continue;
            if (!userMatchesNotificationScope(database, actor, user))
                continue;

            const bool useHtml =
                    b->supportsDirectMessageFormat(key, AbstractBot::MessageFormat::Html);
            b->sendDirectMessage(key,
                                 MessengerTextRender::formatUserAlertMessage(rawMessageText,
                                                                             useHtml),
                                 useHtml ? AbstractBot::MessageFormat::Html
                                         : AbstractBot::MessageFormat::PlainText);
            ++sentCount;
        }
    }

    if (sentCount <= 0) {
        b->sendReply(message.chat, tr("No matching users found."));
        return;
    }

    b->sendReply(message.chat, tr("Alert sent to %1 users.").arg(QString::number(sentCount)));
}

QStringList MessengerSendNotificationCommand::details() const
{
    QStringList details;
    details << tr("By default, sends to users with active access in the current messenger that are "
                  "visible to the sender by group.");
    details << tr("To target specific visible users, pass comma-separated <messenger:id> list "
                  "first.");
    details << tr("Unparsed or unknown recipient keys are reported instead of being skipped.");
    details << tr("The sender is excluded from recipients.");
    details << tr("Use quotes when alert text contains spaces.");
    return details;
}

QStringList MessengerSendNotificationCommand::examples() const
{
    QStringList examples;
    examples << tr("/send_notification Office closes at 20:00 today.");
    examples << tr("/send_notification telegram:1001,telegram:1002 "
                   "\"Reminder: clean up your desk before leaving.\"");
    return examples;
}
