#include "MessengerOccupyCommand.h"

#include "Resource.h"
#include "MessengerBot.h"
#include "ResourceModel.h"
#include "ServerDatabase.h"
#include "ResourceQueueModel.h"
#include "MessengerTextRender.h"
#include "ServerDateTimeFormat.h"
#include "OccupationDurationModel.h"
#include "MessengerCommandManager.h"
#include "ResourceQueueUserMessages.h"

#include <QDateTime>
#include <QStringList>

MessengerOccupyCommand::MessengerOccupyCommand(MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::UsersOnly, 150)
{
    initParser(tr("/occupy"), tr("Occupy resource for a duration."));
}

void MessengerOccupyCommand::configureParser(MessengerCommandParser &parser) const
{
    parser.addArgument(QStringLiteral("name"));
    parser.addArgument(QStringLiteral("duration"),
                       QString(),
                       MessengerCommandParser::ArgumentFlag::Optional
                               | MessengerCommandParser::ArgumentFlag::Greedy
                               | MessengerCommandParser::ArgumentFlag::AllowEmpty);
}

void MessengerOccupyCommand::execute(const AbstractBot::IncomingMessage &message,
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
    const QString resourceName = parts.value(0).trimmed();
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

    const QList<int> durationOptions = b->database()->occupationDurationModel()->minutes();
    const QString durationToken = parts.value(1).trimmed();
    const QString dateTimeFormat = ServerDateTimeFormat::effectiveFormat(b->database());
    if (durationToken.isEmpty()) {
        b->sendReply(message.chat, buildDurationPrompt(resourceName, durationOptions));
        return;
    }

    int durationMinutes = 0;
    if (!parseDurationChoice(durationToken, durationOptions, &durationMinutes)) {
        const QString reply =
                QStringLiteral("%1\n%2").arg(ResourceQueueUserMessages::unknownDuration(
                                                     durationToken),
                                             buildDurationPrompt(resourceName, durationOptions));
        b->sendReply(message.chat, reply);
        return;
    }

    const UserTimeZone actorTimeZone = b->effectiveTimeZoneForUser(actor);
    const ServerDatabaseError queueError = b->database()->occupyResourceUser(resourceName,
                                                                             actor,
                                                                             durationMinutes,
                                                                             durationOptions,
                                                                             actor);
    if (queueError) {
        QString reply = queueError.errorString();
        if (reply == ResourceQueueUserMessages::hasScheduledReservations(resourceName)) {
            reply += QLatin1Char('\n')
                     + tr("Use: /schedule <resource> \"%1\" [\"%1\"]").arg(dateTimeFormat);
        }
        b->sendReply(message.chat, reply);
        return;
    }

    const ResourceQueueModel *queueModel = b->database()->resourceQueueModel();
    const ResourceQueueEntry occupant =
            queueModel ? queueModel->currentOccupantForResource(resourceName)
                       : ResourceQueueEntry();

    b->sendReply(message.chat,
                 buildOccupyReplyText(resource,
                                      resourceName,
                                      occupant,
                                      actor,
                                      durationMinutes,
                                      dateTimeFormat,
                                      actorTimeZone));
}

QString MessengerOccupyCommand::buildOccupyReplyText(const Resource &resource,
                                                     const QString &resourceName,
                                                     const ResourceQueueEntry &occupant,
                                                     const MessengerUserKey &actor,
                                                     int durationMinutes,
                                                     const QString &dateTimeFormat,
                                                     const UserTimeZone &timeZone)
{
    QStringList replyLines;
    if (occupant.isValid() && occupant.user() == actor) {
        const QString startLocal = MessengerTextRender::formatLocalDateTime(occupant.startAtUtc(),
                                                                            QString(),
                                                                            dateTimeFormat,
                                                                            timeZone);
        const QString endLocal =
                MessengerTextRender::formatLocalDateTime(occupant.endAtUtc(),
                                                         MessengerTextRender::formatDurationMinutes(
                                                                 0),
                                                         dateTimeFormat,
                                                         timeZone);
        replyLines << tr("You are now occupying '%1'.").arg(resourceName);
        replyLines << tr("Window: %1 → %2").arg(startLocal, endLocal);

        const QString normalizedStart =
                MessengerTextRender::normalizeNewlines(resource.startMessage()).trimmed();
        if (!normalizedStart.isEmpty()) {
            replyLines << MessengerBot::tr("Start instructions:");
            replyLines << normalizedStart;
        }
    } else {
        const QString durationLabel = MessengerTextRender::formatDurationMinutes(durationMinutes);
        replyLines << tr("Queued '%1' to start when the resource is released (%2).")
                              .arg(resourceName, durationLabel);
        replyLines << tr("Start: when the resource is released.");
    }

    return replyLines.join(QStringLiteral("\n"));
}

QStringList MessengerOccupyCommand::details() const
{
    QStringList details;
    details << tr("If duration is omitted, you will receive a list of allowed durations.");
    details << tr("Duration accepts minutes, hours, days, weeks, and months, or an option number "
                  "from the prompt.");
    return details;
}

QStringList MessengerOccupyCommand::examples() const
{
    QStringList examples;
    examples << tr("/occupy Printer");
    examples << tr("/occupy Printer 5 min");
    examples << tr("/occupy Printer 5 minutes");
    examples << tr("/occupy Printer 2 hours");
    return examples;
}
