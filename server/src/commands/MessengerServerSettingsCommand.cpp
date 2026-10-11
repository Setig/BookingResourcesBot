#include "MessengerServerSettingsCommand.h"

#include "MessengerBot.h"
#include "ServerDatabase.h"
#include "ServerSettingsModel.h"
#include "ServerRuntimeSettings.h"
#include "MessengerUserFacingErrors.h"

#include <QStringList>

namespace {

using Setting = ServerRuntimeSettings::Setting;

} // namespace

MessengerServerSettingsCommand::MessengerServerSettingsCommand(MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::ServerAdminsOnly, 130)
{
    initParser(tr("/server_settings"), tr("List/get/set runtime server settings."));
}

void MessengerServerSettingsCommand::configureParser(MessengerCommandParser &parser) const
{
    parser.addArgument(QStringLiteral("action"));
    parser.addArgument(QStringLiteral("key"),
                       QString(),
                       MessengerCommandParser::ArgumentFlag::Optional);
    parser.addArgument(QStringLiteral("value"),
                       QString(),
                       MessengerCommandParser::ArgumentFlags(
                               MessengerCommandParser::ArgumentFlag::Optional)
                               | MessengerCommandParser::ArgumentFlag::Greedy);
}

void MessengerServerSettingsCommand::execute(const AbstractBot::IncomingMessage &message,
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
    const QString key = parts.value(1).trimmed();
    const QString valueToken = parts.value(2).trimmed();

    const ServerDatabase *db = b->database();
    const ServerSettingsModel *model = db ? db->serverSettingsModel() : nullptr;
    if (!model) {
        b->sendReply(message.chat, tr("Settings model is not available."));
        return;
    }

    if (action == QStringLiteral("list")) {
        if (!key.isEmpty() || !valueToken.isEmpty()) {
            b->sendReply(message.chat, tr("Usage: %1").arg(parser().usageSignature()));
            return;
        }

        QStringList lines;
        const QList<ServerSettingEntry> entries = model->entriesCopy();
        for (const ServerSettingEntry &entry : entries) {
            const QString k = entry.key();
            const QString v = ServerRuntimeSettings::formatSettingValue(k, entry.value());
            const QString d = model->descriptionForKey(k);
            lines << QStringLiteral("%1 = %2 - %3").arg(k, v, d);
        }

        if (lines.isEmpty())
            lines << tr("No runtime settings are available.");

        b->sendReply(message.chat, lines.join(QStringLiteral("\n")));
        return;
    }

    if (action == QStringLiteral("get")) {
        if (key.isEmpty() || !valueToken.isEmpty()) {
            b->sendReply(message.chat, tr("Usage: %1").arg(parser().usageSignature()));
            return;
        }

        Setting setting = Setting::MaxResourcesPerUser;
        if (!ServerRuntimeSettings::settingFromKey(key.trimmed(), &setting)) {
            b->sendReply(message.chat, tr("Unknown setting key: %1").arg(key));
            return;
        }

        const QString settingKey = QLatin1String(ServerRuntimeSettings::key(setting));
        const QVariant currentValue = b->database()->serverSettingValue(setting);
        const QString v = ServerRuntimeSettings::formatSettingValue(settingKey, currentValue);
        const QString d = model->descriptionForKey(settingKey);
        b->sendReply(message.chat, QStringLiteral("%1 = %2 - %3").arg(settingKey, v, d));
        return;
    }

    if (action == QStringLiteral("set")) {
        if (key.isEmpty() || valueToken.isEmpty()) {
            b->sendReply(message.chat, tr("Usage: %1").arg(parser().usageSignature()));
            return;
        }

        Setting setting = Setting::MaxResourcesPerUser;
        if (!ServerRuntimeSettings::settingFromKey(key.trimmed(), &setting)) {
            b->sendReply(message.chat, tr("Unknown setting key: %1").arg(key));
            return;
        }
        const QString settingKey = QLatin1String(ServerRuntimeSettings::key(setting));

        QVariant parsed;
        QString parseError;
        if (!ServerRuntimeSettings::parseSettingValueToken(settingKey,
                                                           valueToken,
                                                           &parsed,
                                                           &parseError)) {
            b->sendReply(message.chat, parseError);
            return;
        }

        const MessengerUserKey actor = AbstractBot::senderKey(message);
        const ServerDatabaseError error = b->database()->setServerSetting(setting, parsed, actor);
        if (error) {
            b->sendReply(message.chat, error.errorString());
            return;
        }

        const QVariant updatedValue = b->database()->serverSettingValue(setting);
        const QString v = ServerRuntimeSettings::formatSettingValue(settingKey, updatedValue);
        const QString d = model->descriptionForKey(settingKey);
        b->sendReply(message.chat, tr("Updated: %1 = %2 - %3").arg(settingKey, v, d));
        return;
    }

    b->sendReply(message.chat,
                 MessengerUserFacingErrors::unknownAction(action,
                                                          QStringLiteral("list, get, set")));
}

QStringList MessengerServerSettingsCommand::details() const
{
    QStringList details;
    details << tr("Actions: list, get, set.");
    details << tr("list - show all keys with current values.");
    details << tr("get <key> - show value and description.");
    details << tr("set <key> <value> - update a value.");
    details << tr("Use /server_settings list to see available keys.");
    details << tr("Numeric settings accept non-negative integers; 0 means unlimited "
                  "(also 'unlimited'/'disabled').");
    details << tr("menu.update_mode accepts 0/1 or active/passive.");
    details << tr("stats.admins_only accepts 0/1 or admins_only/all_users.");
    details << tr("ui.date_time_format accepts date/time format values "
                  "(for example dd.MM.yyyy HH:mm:ss).");
    details << tr("access.pending_access_user_reply accepts reply text for users awaiting access "
                  "activation.");
    details << tr("Use value 'default' or 'auto' to restore built-in defaults.");
    return details;
}

QStringList MessengerServerSettingsCommand::examples() const
{
    QStringList examples;
    examples << tr("/server_settings list");
    examples << tr("/server_settings get menu.update_mode");
    examples << tr("/server_settings set menu.update_mode passive");
    examples << tr("/server_settings set queue.max_resources_per_user 2");
    examples << tr("/server_settings set ui.date_time_format dd.MM.yyyy HH:mm:ss");
    examples << tr("/server_settings set ui.date_time_format auto");
    examples << tr(
            "/server_settings set access.pending_access_user_reply Contact an administrator.");
    return examples;
}

BOOKING_RESOURCES_REGISTER_COMMAND(MessengerServerSettingsCommand)
