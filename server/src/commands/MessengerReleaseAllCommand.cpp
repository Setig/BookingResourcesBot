#include "MessengerReleaseAllCommand.h"

#include "MessengerBot.h"
#include "ServerDatabase.h"
#include "MessengerTextRender.h"
#include "MessengerCommandManager.h"

#include <QStringList>

MessengerReleaseAllCommand::MessengerReleaseAllCommand(MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::UsersOnly, 175)
{
    initParser(tr("/release_all"), tr("Release all resources you occupy."));
}

void MessengerReleaseAllCommand::execute(const AbstractBot::IncomingMessage &message,
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

    const MessengerUserKey actor = AbstractBot::senderKey(message);
    const QStringList resourceNames =
            MessengerTextRender::currentOccupationResourceNames(b->database(),
                                                                message.chat.messenger,
                                                                actor);

    if (resourceNames.isEmpty()) {
        b->sendReply(message.chat, tr("You are not occupying any resources."));
        return;
    }

    QStringList lines;
    if (resourceNames.size() >= 2)
        lines << tr("Released occupied resources:");

    for (const QString &resourceName : resourceNames) {
        const ServerDatabaseError releaseError =
                b->database()->releaseUserOccupationForResource(resourceName, actor, actor);
        if (releaseError) {
            b->sendReply(message.chat, releaseError.errorString());
            return;
        }

        lines << MessengerTextRender::formatReleasedOccupationReply(b->database(),
                                                                    resourceName,
                                                                    actor);
        b->notifyNextUserQueueStatus(resourceName, actor);
    }

    b->sendReply(message.chat, lines.join(QStringLiteral("\n")));
}

QStringList MessengerReleaseAllCommand::details() const
{
    return QStringList() << tr("Releases every resource you currently occupy. Waiting-queue and "
                               "scheduled reservations are not removed.");
}

QStringList MessengerReleaseAllCommand::examples() const
{
    return QStringList();
}
