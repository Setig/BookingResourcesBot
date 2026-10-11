#include "MessengerCommandManager.h"

#include "MessengerBot.h"

#include "MessengerCommandParser.h"
#include "MessengerAbstractCommand.h"
#include "MessengerUserFacingErrors.h"

#include <QHash>
#include <QString>

class MessengerCommandManagerPrivate
{
public:
    MessengerCommandManagerPrivate(MessengerCommandManager *q, MessengerBot *bot);

    void clearCommands();
    void rebuildCommands();
    void registerCommand(MessengerAbstractCommand *command);

    MessengerAbstractCommand *lookup(const QString &command,
                                     const AbstractBot::IncomingMessage *message);

    bool requireAdmin(const AbstractBot::IncomingMessage &message) const;
    bool requireServerAdmin(const AbstractBot::IncomingMessage &message) const;
    bool canAccessCommand(const MessengerAbstractCommand *command,
                          const AbstractBot::IncomingMessage &message) const;

    bool handleCommandMessage(const AbstractBot::IncomingMessage &message);
    void handleSingleCommandMessage(const AbstractBot::IncomingMessage &message);

    QString normalizeCommand(const QString &command) const;

    MessengerCommandManager *q = nullptr; // non-owning
    MessengerBot *bot = nullptr;          // non-owning
    QVector<MessengerAbstractCommand *> commands;
    QHash<QString, MessengerAbstractCommand *> lookupTable;
};

MessengerCommandManagerPrivate::MessengerCommandManagerPrivate(MessengerCommandManager *q,
                                                               MessengerBot *bot)
    : q(q)
    , bot(bot)
{
    // do nothing
}

void MessengerCommandManagerPrivate::clearCommands()
{
    qDeleteAll(commands);
    commands.clear();
    lookupTable.clear();
}

void MessengerCommandManagerPrivate::rebuildCommands()
{
    clearCommands();

    const QVector<MessengerCommandStaticRegistry::Factory> factories =
            MessengerCommandStaticRegistry::factories();

    for (MessengerCommandStaticRegistry::Factory factory : factories) {
        if (!factory)
            continue;

        MessengerAbstractCommand *command = factory(q);
        if (!command)
            continue;

        registerCommand(command);
    }
}

void MessengerCommandManagerPrivate::registerCommand(MessengerAbstractCommand *command)
{
    if (!command)
        return;

    const QString normalized = normalizeCommand(command->command());
    if (normalized.isEmpty()) {
        delete command;
        return;
    }

    if (lookupTable.contains(normalized)) {
        delete command;
        return;
    }

    commands.append(command);
    lookupTable.insert(normalized, command);
}

MessengerAbstractCommand *MessengerCommandManagerPrivate::lookup(
        const QString &command, const AbstractBot::IncomingMessage *message)
{
    Q_UNUSED(message);
    const QString normalized = normalizeCommand(command);
    QHash<QString, MessengerAbstractCommand *>::iterator it = lookupTable.find(normalized);
    if (it == lookupTable.end())
        return nullptr;

    MessengerAbstractCommand *commandImpl = it.value();
    if (!commandImpl)
        return nullptr;
    return commandImpl;
}

bool MessengerCommandManagerPrivate::requireAdmin(const AbstractBot::IncomingMessage &message) const
{
    if (!bot)
        return false;

    if (bot->isAdminUser(AbstractBot::senderKey(message)))
        return true;

    bot->sendReply(message.chat, MessengerUserFacingErrors::accessDeniedAdminsOnly());
    return false;
}

bool MessengerCommandManagerPrivate::requireServerAdmin(
        const AbstractBot::IncomingMessage &message) const
{
    if (!bot)
        return false;

    if (bot->canManageServerSettings(AbstractBot::senderKey(message)))
        return true;

    bot->sendReply(message.chat, MessengerUserFacingErrors::accessDeniedAdminsOnly());
    return false;
}

bool MessengerCommandManagerPrivate::canAccessCommand(
        const MessengerAbstractCommand *command, const AbstractBot::IncomingMessage &message) const
{
    if (!bot || !command)
        return false;

    const bool isLocalCli = message.chat.messenger == MessengerType::LocalCli;
    const MessengerUserKey senderKey = AbstractBot::senderKey(message);
    const MessengerCommandAudience audience = command->audience();
    if (isLocalCli && audience == MessengerCommandAudience::UsersOnly)
        return false;
    if (audience == MessengerCommandAudience::AdminsOnly && !bot->isAdminUser(senderKey))
        return false;
    if (audience == MessengerCommandAudience::ServerAdminsOnly
        && !bot->canManageServerSettings(senderKey)) {
        return false;
    }
    return true;
}

bool MessengerCommandManagerPrivate::handleCommandMessage(
        const AbstractBot::IncomingMessage &message)
{
    const QString trimmed = message.text.trimmed();
    if (!trimmed.startsWith(QLatin1Char('/')))
        return false;

    rebuildCommands();

    if (!bot)
        return true;

    if (!bot->ensureDatabase(message.chat))
        return true;

    const MessengerUserKey senderKey = AbstractBot::senderKey(message);
    if (!bot->hasActiveAccess(senderKey)) {
        bot->sendReply(message.chat,
                       MessengerUserFacingErrors::accessDeniedAccessNotActivated(senderKey));
        return true;
    }

    QStringList commandTexts;
    QString splitError;
    if (!MessengerCommandParser::splitMessageCommands(message.text, &commandTexts, &splitError)) {
        bot->sendReply(message.chat, splitError);
        return true;
    }

    for (const QString &commandText : commandTexts) {
        AbstractBot::IncomingMessage commandMessage = message;
        commandMessage.text = commandText;
        handleSingleCommandMessage(commandMessage);
    }

    return true;
}

void MessengerCommandManagerPrivate::handleSingleCommandMessage(
        const AbstractBot::IncomingMessage &message)
{
    const QString trimmed = message.text.trimmed();
    if (trimmed.isEmpty() || !bot)
        return;

    const int spaceIndex = trimmed.indexOf(QLatin1Char(' '));
    const QString commandToken = (spaceIndex < 0) ? trimmed : trimmed.left(spaceIndex);
    const QString args = (spaceIndex < 0) ? QString() : trimmed.mid(spaceIndex + 1).trimmed();

    const bool isLocalCli = message.chat.messenger == MessengerType::LocalCli;

    MessengerAbstractCommand *commandImpl = lookup(commandToken, &message);
    if (commandImpl) {
        const MessengerCommandAudience audience = commandImpl->audience();
        if (isLocalCli && audience == MessengerCommandAudience::UsersOnly) {
            bot->sendReply(message.chat,
                           MessengerBot::tr(
                                   "Local CLI exposes admin commands only.\n"
                                   "Use /help for admin commands or \\help for CLI help."));
            return;
        }

        if (audience == MessengerCommandAudience::AdminsOnly && !requireAdmin(message)) {
            return;
        }
        if (audience == MessengerCommandAudience::ServerAdminsOnly
            && !requireServerAdmin(message)) {
            return;
        }

        commandImpl->execute(message, args);
        return;
    }

    const QString reply = isLocalCli
                                  ? MessengerBot::tr("Unknown admin command: %1\n"
                                                     "Send /help to see available admin commands.")
                                            .arg(commandToken)
                                  : MessengerBot::tr("Unknown command: %1\n"
                                                     "Send /help to see available commands.")
                                            .arg(commandToken);
    bot->sendReply(message.chat, reply);
}

QString MessengerCommandManagerPrivate::normalizeCommand(const QString &command) const
{
    return command.trimmed();
}

MessengerCommandManager::MessengerCommandManager(MessengerBot *bot)
    : QObject(bot)
    , d(new MessengerCommandManagerPrivate(this, bot))
{
    d->rebuildCommands();
}

MessengerCommandManager::~MessengerCommandManager()
{
    d->clearCommands();

    delete d;
}

MessengerBot *MessengerCommandManager::bot() const
{
    return d->bot;
}

const QVector<MessengerAbstractCommand *> &MessengerCommandManager::commands() const
{
    return d->commands;
}

void MessengerCommandManager::refreshCommandTranslations() const
{
    d->rebuildCommands();
}

bool MessengerCommandManager::handleCommandMessage(const AbstractBot::IncomingMessage &message)
{
    return d->handleCommandMessage(message);
}
