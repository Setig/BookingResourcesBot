#include "MessengerStartCommand.h"

#include "MessengerBot.h"
#include "MessengerCommandManager.h"

#include <QStringList>

MessengerStartCommand::MessengerStartCommand(MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::AdminsAndUsers, 30)
{
    initParser(tr("/start"), tr("Show welcome message."));
}

void MessengerStartCommand::execute(const AbstractBot::IncomingMessage &message,
                                    const QString &args)
{
    Q_UNUSED(args);

    MessengerBot *b = bot();
    if (!b)
        return;

    QStringList lines;
    if (b->menuManager()) {
        lines << tr("Use /menu to open the interactive menu with buttons.");
    }
    lines << tr("Hi! I help you book shared resources.");
    lines << tr("/list_resources - browse available resources and descriptions.");
    lines << tr("/queue <resource> - see who is holding it and for how long.");
    lines << tr("/occupy <resource> [minutes] - choose duration and join the queue.");
    lines << tr("/schedule <resource> \"<start>\" [\"<end>\"] - reserve a resource for a time "
                "window.");
    lines << tr("/unschedule <resource> - remove your reservation for a resource.");
    lines << tr("/release <resource> - free what you occupy so the next user is notified.");
    lines << tr("/release_all - free all resources you occupy so the next users are notified.");
    lines << tr("/free_resources - quick list of idle resources.");
    lines << tr("/soonest [count] - top slots that will free up soon.");
    if (b->isAdminUser(AbstractBot::senderKey(message))) {
        lines << tr("Admins can manage users and resources via /add_user, /add_resource, etc.");
    }
    lines << tr("Need more? Send /help for the complete list.");

    b->sendReply(message.chat, lines.join(QStringLiteral("\n")));
}

QStringList MessengerStartCommand::details() const
{
    return QStringList();
}

QStringList MessengerStartCommand::examples() const
{
    return QStringList();
}
