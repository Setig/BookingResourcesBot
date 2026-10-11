#include "MessengerOccupationDurationsCommand.h"

#include "MessengerBot.h"
#include "ServerDatabase.h"
#include "MessengerTextRender.h"
#include "OccupationDurationModel.h"
#include "MessengerCommandManager.h"
#include "MessengerUserFacingErrors.h"

#include <QStringList>

namespace {

QStringList buildDurationLines(const QList<int> &durations)
{
    QStringList lines;
    if (durations.isEmpty()) {
        lines << MessengerOccupationDurationsCommand::tr(
                "No resource occupation durations are configured.");
        return lines;
    }

    lines << MessengerOccupationDurationsCommand::tr("Resource occupation durations:");
    int index = 1;
    for (int minutes : durations) {
        lines << MessengerOccupationDurationsCommand::tr("%1) %2 (%3 min)")
                         .arg(QString::number(index++),
                              MessengerTextRender::formatDurationMinutes(minutes),
                              QString::number(minutes));
    }
    lines << MessengerOccupationDurationsCommand::tr("0 minutes means indefinite.");
    return lines;
}

} // namespace

MessengerOccupationDurationsCommand::MessengerOccupationDurationsCommand(
        MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::ServerAdminsOnly, 140)
{
    initParser(tr("/occupation_durations"), tr("List/add/remove resource occupation durations."));
}

void MessengerOccupationDurationsCommand::configureParser(MessengerCommandParser &parser) const
{
    parser.addArgument(QStringLiteral("action"));
    parser.addArgument(QStringLiteral("duration"),
                       QString(),
                       MessengerCommandParser::ArgumentFlag::Optional
                               | MessengerCommandParser::ArgumentFlag::Greedy);
}

void MessengerOccupationDurationsCommand::execute(const AbstractBot::IncomingMessage &message,
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
    const QString action = parts.value(0).trimmed().toLower();
    const QString minutesToken = parts.value(1).trimmed();

    ServerDatabase *db = b->database();
    const OccupationDurationModel *model = db ? db->occupationDurationModel() : nullptr;
    if (!model) {
        b->sendReply(message.chat,
                     MessengerUserFacingErrors::occupationDurationModelNotAvailable());
        return;
    }

    if (action == QStringLiteral("list")) {
        if (!minutesToken.isEmpty()) {
            b->sendReply(message.chat, tr("Usage: %1").arg(parser().usageSignature()));
            return;
        }
        const QStringList lines = buildDurationLines(model->occupationDurations());
        b->sendReply(message.chat, lines.join(QStringLiteral("\n")));
        return;
    }

    if (action == QStringLiteral("add") || action == QStringLiteral("remove")) {
        if (minutesToken.isEmpty()) {
            b->sendReply(message.chat, tr("Usage: %1").arg(parser().usageSignature()));
            return;
        }

        int minutes = 0;
        if (!parseDurationMinutesToken(minutesToken, &minutes)) {
            b->sendReply(message.chat, MessengerUserFacingErrors::invalidNonNegativeDuration());
            return;
        }

        const MessengerUserKey actor = AbstractBot::senderKey(message);
        ServerDatabaseError error;
        if (action == QStringLiteral("add")) {
            error = db->addOccupationDuration(minutes, actor);
        } else {
            error = db->removeOccupationDuration(minutes, actor);
        }
        if (error) {
            b->sendReply(message.chat, error.errorString());
            return;
        }

        const QString label = MessengerTextRender::formatDurationMinutes(minutes);
        const QString minutesText = QString::number(minutes);
        if (action == QStringLiteral("add")) {
            b->sendReply(message.chat,
                         tr("Occupation duration added: %1 (%2 min)").arg(label, minutesText));
        } else {
            b->sendReply(message.chat,
                         tr("Occupation duration removed: %1 (%2 min)").arg(label, minutesText));
        }
        return;
    }

    b->sendReply(message.chat,
                 MessengerUserFacingErrors::unknownAction(action,
                                                          QStringLiteral("list, add, remove")));
}

QStringList MessengerOccupationDurationsCommand::details() const
{
    QStringList details;
    details << tr("Duration accepts minutes, hours, days, weeks, and months. 0 means indefinite.");
    return details;
}

QStringList MessengerOccupationDurationsCommand::examples() const
{
    QStringList examples;
    examples << tr("/occupation_durations list");
    examples << tr("/occupation_durations add 5 minutes");
    examples << tr("/occupation_durations add 2 hours");
    examples << tr("/occupation_durations remove 30");
    examples << tr("/occupation_durations remove 1 hour");
    return examples;
}
