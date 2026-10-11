#include "MessengerBanUserCommand.h"

#include "User.h"
#include "UserModel.h"
#include "MessengerBot.h"
#include "ServerDatabase.h"
#include "MessengerTextRender.h"
#include "ServerDateTimeFormat.h"
#include "MessengerCommandManager.h"
#include "MessengerUserFacingErrors.h"
#include "BookingResourcesTranslator.h"

#include <QDateTime>
#include <QStringList>

namespace {

void splitFirstWord(const QString &text, QString *wordOut, QString *restOut)
{
    const QString trimmed = text.trimmed();
    int splitAt = -1;
    for (int i = 0; i < trimmed.size(); ++i) {
        if (trimmed.at(i).isSpace()) {
            splitAt = i;
            break;
        }
    }

    if (splitAt < 0) {
        if (wordOut)
            *wordOut = trimmed;
        if (restOut)
            restOut->clear();
        return;
    }

    if (wordOut)
        *wordOut = trimmed.left(splitAt).trimmed();
    if (restOut)
        *restOut = trimmed.mid(splitAt + 1).trimmed();
}

} // namespace

MessengerBanUserCommand::MessengerBanUserCommand(MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::AdminsOnly, 260)
{
    initParser(tr("/ban_user"), tr("Block a user for a period of time."));
}

void MessengerBanUserCommand::configureParser(MessengerCommandParser &parser) const
{
    parser.addArgument(QStringLiteral("messenger:id"));
    parser.addArgument(QStringLiteral("duration"));
    parser.addArgument(QStringLiteral("reason"),
                       QString(),
                       MessengerCommandParser::ArgumentFlag::Optional
                               | MessengerCommandParser::ArgumentFlag::Greedy);
}

void MessengerBanUserCommand::execute(const AbstractBot::IncomingMessage &message,
                                      const QString &args)
{
    MessengerBot *b = bot();
    if (!b || !b->ensureDatabase(message.chat))
        return;

    const MessengerCommandParser::ParseResult parseResult = parseArguments(args);
    if (!parseResult.ok) {
        QString reply = parseResult.errorText;
        reply += tr("\nDuration accepts minutes, hours, days, weeks, months, or 'permanent'.");
        b->sendReply(message.chat, reply.trimmed());
        return;
    }
    const QStringList parts = parseResult.values;

    bool targetUserKeyOk = false;
    MessengerUserKey targetUserKey = MessengerUserKey::fromString(parts.at(0), &targetUserKeyOk);
    if (!targetUserKeyOk) {
        b->sendReply(message.chat, MessengerUserFacingErrors::invalidUserValue());
        return;
    }

    QString durationToken = parts.at(1).trimmed();
    QString reason = parts.value(2).trimmed();
    QDateTime blockedUntilUtc;

    bool durationOk = false;
    if (!reason.isEmpty()) {
        QString unitToken;
        QString remainingReason;
        splitFirstWord(reason, &unitToken, &remainingReason);
        if (!unitToken.isEmpty()) {
            QDateTime candidateUntilUtc;
            const QString combinedDuration =
                    QStringLiteral("%1 %2").arg(durationToken, unitToken).trimmed();
            if (parseBlockDurationToken(combinedDuration, &candidateUntilUtc)) {
                durationToken = combinedDuration;
                reason = remainingReason;
                blockedUntilUtc = candidateUntilUtc;
                durationOk = true;
            }
        }
    }

    if (!durationOk)
        durationOk = parseBlockDurationToken(durationToken, &blockedUntilUtc);

    if (!durationOk) {
        b->sendReply(message.chat,
                     tr("Invalid duration. Use minutes, hours, days, weeks, months, or "
                        "'permanent'."));
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
    const BlacklistEntry entry(targetUserKey,
                               reason.isEmpty() ? tr("Blocked by administrator.") : reason,
                               actor,
                               QDateTime::currentDateTimeUtc(),
                               blockedUntilUtc);
    const ServerDatabaseError error = b->database()->banUser(entry, actor);
    if (error) {
        b->sendReply(message.chat, error.errorString());
        return;
    }

    const QString dateTimeFormat = ServerDateTimeFormat::effectiveFormat(b->database());
    const UserTimeZone actorTimeZone = b->effectiveTimeZoneForUser(actor);
    const QString durationText =
            MessengerTextRender::describeBlacklistWindow(entry, dateTimeFormat, actorTimeZone);

    const bool useHtml =
            MessengerTextRender::shouldUseHtmlLists(message.chat.messenger, b->backendBot());
    const QString targetDisplay = MessengerTextRender::formatUserLine(targetUser, useHtml);

    QString reply;
    if (useHtml) {
        reply = tr("User %1 blocked %2.").arg(targetDisplay, durationText.toHtmlEscaped());
        if (!entry.reason().isEmpty()) {
            reply += tr("\nReason: %1").arg(entry.reason().toHtmlEscaped());
        }
        b->sendReply(message.chat, reply, AbstractBot::MessageFormat::Html);
    } else {
        reply = tr("User %1 blocked %2.").arg(targetDisplay, durationText);
        if (!entry.reason().isEmpty()) {
            reply += tr("\nReason: %1").arg(entry.reason());
        }
        b->sendReply(message.chat, reply);
    }

    if (b->hasActiveAccess(targetUserKey)) {
        const QString targetLocale = b->effectiveLocaleForUser(targetUserKey);
        BookingResourcesTranslator::ScopedLocaleOverride targetGuard =
                b->applyScopedLocale(targetLocale);
        Q_UNUSED(targetGuard);

        const QString targetDurationText =
                MessengerTextRender::describeBlacklistWindow(entry,
                                                             dateTimeFormat,
                                                             b->effectiveTimeZoneForUser(
                                                                     targetUserKey));
        QString userNotice = tr("You have been blocked %1.").arg(targetDurationText);
        if (!entry.reason().isEmpty()) {
            userNotice += tr("\nReason: %1").arg(entry.reason());
        }
        b->sendDirectMessage(targetUserKey, userNotice);
    }
}

QStringList MessengerBanUserCommand::details() const
{
    QStringList details;
    details << tr("Use <messenger:id> like telegram:1234.");
    details << tr("Duration accepts minutes, hours, days, weeks, months, or 'permanent'.");
    return details;
}

QStringList MessengerBanUserCommand::examples() const
{
    QStringList examples;
    examples << tr("/ban_user telegram:1234 90 spam");
    examples << tr("/ban_user telegram:1234 \"5 minutes\" spam");
    examples << tr("/ban_user telegram:1234 2 hours spam");
    examples << tr("/ban_user telegram:1234 permanent");
    return examples;
}
