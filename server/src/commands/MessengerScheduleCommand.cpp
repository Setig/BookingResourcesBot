#include "MessengerScheduleCommand.h"

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

MessengerScheduleCommand::MessengerScheduleCommand(MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::UsersOnly, 160)
{
    initParser(
            tr("/schedule"),
            tr("Schedule resource for a time window (end is optional, use quotes for date/time)."));
}

void MessengerScheduleCommand::configureParser(MessengerCommandParser &parser) const
{
    parser.addArgument(QStringLiteral("name"));
    parser.addArgument(QStringLiteral("start"));
    parser.addArgument(QStringLiteral("end"),
                       QString(),
                       MessengerCommandParser::ArgumentFlag::Optional
                               | MessengerCommandParser::ArgumentFlag::Greedy
                               | MessengerCommandParser::ArgumentFlag::AllowEmpty);
}

void MessengerScheduleCommand::execute(const AbstractBot::IncomingMessage &message,
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
    const QString startToken = parts.value(1).trimmed();
    const QString endToken = parts.value(2).trimmed();
    const QString dateTimeFormat = ServerDateTimeFormat::effectiveFormat(b->database());
    const MessengerUserKey actor = AbstractBot::senderKey(message);
    const UserTimeZone actorTimeZone = b->effectiveTimeZoneForUser(actor);

    const ResourceModel *model = b->database()->resourceModel();
    const Resource resource = model ? model->resourceByName(resourceName) : Resource();
    if (!MessengerTextRender::isResourceVisibleToViewer(resource,
                                                        b->database(),
                                                        message.chat.messenger,
                                                        actor)) {
        b->sendReply(message.chat, ResourceQueueUserMessages::resourceNotFound(resourceName));
        return;
    }

    if (isAfterReleaseStartToken(startToken)) {
        const QList<int> durationOptions = b->database()->occupationDurationModel()->minutes();
        int durationMinutes = 0;
        if (!endToken.isEmpty()
            && !parseDurationChoice(endToken, durationOptions, &durationMinutes)) {
            const QString reply = QStringLiteral("%1\n%2")
                                          .arg(ResourceQueueUserMessages::unknownDuration(endToken),
                                               buildDurationPrompt(resourceName, durationOptions));
            b->sendReply(message.chat, reply);
            return;
        }

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
        const QString durationLabel = MessengerTextRender::formatDurationMinutes(durationMinutes);
        if (occupant.isValid() && occupant.user() == actor) {
            b->sendReply(message.chat,
                         tr("Scheduled '%1' to start when the resource is released (%2). It has "
                            "started now because the resource was free.")
                                 .arg(resourceName, durationLabel));
        } else {
            b->sendReply(message.chat,
                         tr("Queued '%1' to start when the resource is released (%2).")
                                 .arg(resourceName, durationLabel));
        }
        return;
    }

    QDateTime startUtc;
    QDateTime endUtc;
    const QString scheduleUsage = tr("Use: /schedule <resource> \"%1\" [\"%1\"]\n"
                                     "Use quotes when date/time contains spaces.")
                                          .arg(dateTimeFormat);

    if (!parseScheduleDateTimeToken(startToken, dateTimeFormat, &startUtc, actorTimeZone)
        || !startUtc.isValid()) {
        b->sendReply(message.chat,
                     QStringLiteral("%1\n%2").arg(ResourceQueueUserMessages::invalidScheduleStart(
                                                          dateTimeFormat),
                                                  scheduleUsage));
        return;
    }

    if (!endToken.isEmpty()) {
        if (!parseScheduleDateTimeToken(endToken, dateTimeFormat, &endUtc, actorTimeZone)
            || !endUtc.isValid()) {
            b->sendReply(message.chat,
                         QStringLiteral("%1\n%2").arg(ResourceQueueUserMessages::invalidScheduleEnd(
                                                              dateTimeFormat),
                                                      scheduleUsage));
            return;
        }
        if (startUtc >= endUtc) {
            b->sendReply(message.chat, ResourceQueueUserMessages::endMustBeAfterStart());
            return;
        }
    }

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    if (startUtc.toUTC() < nowUtc) {
        b->sendReply(message.chat, ResourceQueueUserMessages::scheduleStartInPast());
        return;
    }

    const ServerDatabaseError scheduleError =
            b->database()->scheduleResourceUser(resourceName, actor, startUtc, endUtc, actor);
    if (scheduleError) {
        b->sendReply(message.chat, scheduleError.errorString());
        return;
    }

    const QString startLocal = MessengerTextRender::formatLocalDateTime(startUtc,
                                                                        QString(),
                                                                        dateTimeFormat,
                                                                        actorTimeZone);
    const QString endLocal =
            MessengerTextRender::formatLocalDateTime(endUtc,
                                                     MessengerTextRender::formatDurationMinutes(0),
                                                     dateTimeFormat,
                                                     actorTimeZone);
    const QString reply =
            endUtc.isValid()
                    ? tr("Scheduled '%1' from %2 to %3.").arg(resourceName, startLocal, endLocal)
                    : tr("Scheduled '%1' starting at %2 (indefinite).")
                              .arg(resourceName, startLocal);
    b->sendReply(message.chat, reply);
}

QStringList MessengerScheduleCommand::details() const
{
    QStringList details;
    details << tr(
            "Use current server date/time format (see /server_settings get ui.date_time_format).");
    details << tr("ISO input is also accepted.");
    details << tr("Use quotes for date/time arguments when they contain spaces.");
    details << tr("Leave end empty to schedule indefinitely.");
    details << tr("Use 0 as start to wait for the current occupation to end; the optional end "
                  "argument is treated as a duration.");
    details << tr("\"when the resource is released\" is also accepted as a readable start value.");
    return details;
}

QStringList MessengerScheduleCommand::examples() const
{
    QStringList examples;
    examples << tr("/schedule Printer \"2026-02-03 10:00\" \"2026-02-03 12:00\"");
    examples << tr("/schedule Printer \"2026-02-03 10:00\"");
    examples << tr("/schedule Printer 0 5 min");
    examples << tr("/schedule Printer 0 5 minutes");
    examples << tr("/schedule Printer 0 2 hours");
    return examples;
}
