#include "MessengerHelpCommand.h"

#include "MessengerBot.h"
#include "MessengerCommandManager.h"
#include "MessengerUserFacingErrors.h"

#include <QStringList>

namespace {

bool commandVisibleForUsers(const MessengerAbstractCommand *command)
{
    if (!command)
        return false;

    const MessengerCommandAudience audience = command->audience();
    if (audience == MessengerCommandAudience::AdminsOnly
        || audience == MessengerCommandAudience::ServerAdminsOnly)
        return false;

    return command->isEnabled(nullptr);
}

} // namespace

MessengerHelpCommand::MessengerHelpCommand(MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::AdminsAndUsers, 10)
{
    initParser(tr("/help"), tr("Show available bot commands and detailed command usage."));
}

void MessengerHelpCommand::configureParser(MessengerCommandParser &parser) const
{
    parser.addArgument(QStringLiteral("command"),
                       QString(),
                       MessengerCommandParser::ArgumentFlag::Optional);
}

void MessengerHelpCommand::execute(const AbstractBot::IncomingMessage &message, const QString &args)
{
    MessengerBot *b = bot();
    if (!b || !manager())
        return;

    const MessengerCommandParser::ParseResult parseResult = parseArguments(args);
    if (!parseResult.ok) {
        b->sendReply(message.chat, parseResult.errorText);
        return;
    }

    const bool isLocalCli = message.chat.messenger == MessengerType::LocalCli;
    const bool isAdmin = b->isAdminUser(AbstractBot::senderKey(message));
    const bool canManageServerSettings =
            b->canManageServerSettings(AbstractBot::senderKey(message));
    const QString commandToken = parseResult.values.value(0).trimmed();

    if (!commandToken.isEmpty()) {
        QString normalized = commandToken;
        if (!normalized.startsWith(QLatin1Char('/')))
            normalized.prepend(QLatin1Char('/'));

        const MessengerAbstractCommand *target = nullptr;
        const MessengerAbstractCommand *disabledTarget = nullptr;
        for (const MessengerAbstractCommand *command : manager()->commands()) {
            if (!command || command->command() != normalized)
                continue;
            if (command->isEnabled(&message)) {
                target = command;
                break;
            }
            if (!disabledTarget)
                disabledTarget = command;
        }

        if (!target && disabledTarget) {
            const QString disabledReason = disabledTarget->disabledUserFacingReason(message);
            if (!disabledReason.isEmpty()) {
                b->sendReply(message.chat, disabledReason);
                return;
            }
        }

        if (!target) {
            const QString reply =
                    isLocalCli ? MessengerBot::tr("Unknown admin command: %1\n"
                                                  "Send /help to see available admin commands.")
                                         .arg(normalized)
                               : MessengerBot::tr("Unknown command: %1\n"
                                                  "Send /help to see available commands.")
                                         .arg(normalized);
            b->sendReply(message.chat, reply);
            return;
        }

        const MessengerCommandAudience targetAudience = target->audience();
        if (isLocalCli && targetAudience == MessengerCommandAudience::UsersOnly) {
            b->sendReply(message.chat,
                         MessengerBot::tr("Local CLI exposes admin commands only.\n"
                                          "Use /help for admin commands or \\help for CLI help."));
            return;
        }
        if (targetAudience == MessengerCommandAudience::AdminsOnly && !isAdmin) {
            b->sendReply(message.chat, MessengerUserFacingErrors::accessDeniedAdminsOnly());
            return;
        }
        if (targetAudience == MessengerCommandAudience::ServerAdminsOnly
            && !canManageServerSettings) {
            b->sendReply(message.chat, MessengerUserFacingErrors::accessDeniedAdminsOnly());
            return;
        }

        QStringList lines;
        const QString description = target->parser().description().trimmed();
        if (!description.isEmpty())
            lines << description;

        const QString usage = target->parser().usageSignature().trimmed();
        if (!usage.isEmpty())
            lines << MessengerAbstractCommand::tr("Usage: %1").arg(usage);

        const QStringList details = target->details();
        for (const QString &detail : details) {
            if (!detail.trimmed().isEmpty())
                lines << detail;
        }

        QStringList cleanExamples;
        if (target->parser().argumentCount() > 1) {
            const QStringList examples = target->examples();
            for (const QString &example : examples) {
                if (!example.trimmed().isEmpty())
                    cleanExamples << example;
            }
        }

        if (!cleanExamples.isEmpty()) {
            lines << MessengerAbstractCommand::tr("Examples:");
            lines.append(cleanExamples);
        }

        if (lines.isEmpty())
            lines << target->parser().helpLine();

        b->sendReply(message.chat, lines.join(QStringLiteral("\n")).trimmed());
        return;
    }

    QVector<const MessengerAbstractCommand *> sortedCommands;
    sortedCommands.reserve(manager()->commands().size());
    for (const MessengerAbstractCommand *command : manager()->commands()) {
        if (!command || !command->isEnabled(&message))
            continue;

        sortedCommands.append(command);
    }

    std::sort(sortedCommands.begin(),
              sortedCommands.end(),
              [](const MessengerAbstractCommand *left, const MessengerAbstractCommand *right) {
                  if (left->priority() != right->priority())
                      return left->priority() < right->priority();

                  return left->command() < right->command();
              });

    QStringList userCommands;
    QStringList adminCommands;
    for (const MessengerAbstractCommand *command : sortedCommands) {
        const QString line = command->parser().helpLine();
        const MessengerCommandAudience commandAudience = command->audience();
        const bool visibleForUsers = commandVisibleForUsers(command);

        if (visibleForUsers)
            userCommands << line;

        if (isAdmin) {
            const bool serverAdminCommand =
                    commandAudience == MessengerCommandAudience::ServerAdminsOnly;
            const bool showInAdminSection =
                    (isLocalCli ? commandAudience != MessengerCommandAudience::UsersOnly
                                : !visibleForUsers)
                    && (!serverAdminCommand || canManageServerSettings);
            if (showInAdminSection)
                adminCommands << line;
        }
    }

    QStringList replyLines;
    if (!isLocalCli && !userCommands.isEmpty()) {
        replyLines << tr("User commands:");
        replyLines.append(userCommands);
    }

    if (isAdmin && !adminCommands.isEmpty()) {
        if (!isLocalCli && !userCommands.isEmpty())
            replyLines << QString();
        replyLines << (isLocalCli ? tr("Local CLI admin commands:") : tr("Admin commands:"));
        replyLines.append(adminCommands);
    }

    if (!replyLines.isEmpty())
        replyLines << QString();
    replyLines << tr("Tip: use /help <command> to see arguments and examples.");

    b->sendReply(message.chat, replyLines.join(QStringLiteral("\n")).trimmed());
}

QStringList MessengerHelpCommand::details() const
{
    QStringList details;
    details << tr("Use without arguments to list available commands.");
    details << tr("Use with a command name (with or without '/') to see detailed help.");
    return details;
}

QStringList MessengerHelpCommand::examples() const
{
    return QStringList();
}
