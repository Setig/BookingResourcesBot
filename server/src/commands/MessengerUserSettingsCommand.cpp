#include "MessengerUserSettingsCommand.h"

#include "UserModel.h"
#include "MessengerBot.h"
#include "ServerDatabase.h"
#include "ServerDateTimeFormat.h"
#include "MessengerUserFacingErrors.h"

#include <QStringList>

namespace {
constexpr char kKeyLocale[] = "locale";
constexpr char kKeyTimezone[] = "timezone";
constexpr char kKeyOccupationNotify[] = "occupation.notify";

QString formatLocaleValue(const QString &locale)
{
    const QString trimmed = locale.trimmed();
    if (trimmed.isEmpty())
        return MessengerUserSettingsCommand::tr("default (messenger)");
    return trimmed;
}

QString formatTimezoneValue(const UserTimeZone &timezone)
{
    if (!timezone.hasCustomOffset())
        return MessengerUserSettingsCommand::tr("default (server timezone)");
    const short offsetMinutes = timezone.utcOffsetMinutes();
    return MessengerUserSettingsCommand::tr("%1 (%2 min)")
            .arg(timezone.toDisplayString(), QString::number(offsetMinutes));
}

QString formatNotifyMode(User::OccupationNotifyMode mode)
{
    switch (mode) {
    case User::OccupationNotifyMode::AllResources:
        return MessengerUserSettingsCommand::tr("all");
    case User::OccupationNotifyMode::OwnResourcesOnly:
        return MessengerUserSettingsCommand::tr("mine");
    case User::OccupationNotifyMode::Disabled:
        return MessengerUserSettingsCommand::tr("off");
    }
    return MessengerUserSettingsCommand::tr("mine");
}

QString descriptionForKey(const QString &key)
{
    const QString trimmed = key.trimmed().toLower();
    if (trimmed == QLatin1String(kKeyLocale)) {
        return MessengerUserSettingsCommand::tr(
                "Preferred language for bot replies (default uses messenger language).");
    }
    if (trimmed == QLatin1String(kKeyTimezone)) {
        return MessengerUserSettingsCommand::tr(
                "Preferred time zone for date/time input and output "
                "(default uses server time zone).");
    }
    if (trimmed == QLatin1String(kKeyOccupationNotify)) {
        return MessengerUserSettingsCommand::tr(
                "Notifications about resource occupation: all visible resources, only events "
                "involving you, or off (all/mine/off).");
    }
    return QString();
}

bool parseTimezoneToken(const QString &token, UserTimeZone *timeZoneOut, QString *errorOut)
{
    if (!timeZoneOut)
        return false;

    UserTimeZone parsed;
    if (!ServerDateTimeFormat::parseTimeZoneValue(token, &parsed)) {
        if (errorOut) {
            *errorOut = MessengerUserFacingErrors::invalidTimeZoneValue();
        }
        return false;
    }

    *timeZoneOut = parsed;
    return true;
}

bool parseLocaleToken(const QString &token, QString *localeOut, QString *errorOut)
{
    if (!localeOut)
        return false;

    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty()) {
        if (errorOut)
            *errorOut = MessengerUserFacingErrors::valueCannotBeEmpty();
        return false;
    }

    const QString normalized = trimmed.toLower();
    if (normalized == QStringLiteral("default") || normalized == QStringLiteral("auto")
        || normalized == QStringLiteral("messenger")) {
        localeOut->clear();
        return true;
    }

    *localeOut = trimmed;
    return true;
}

bool parseNotifyModeToken(const QString &token,
                          User::OccupationNotifyMode *modeOut,
                          QString *errorOut)
{
    if (!modeOut)
        return false;

    const QString trimmed = token.trimmed().toLower();
    if (trimmed.isEmpty()) {
        if (errorOut)
            *errorOut = MessengerUserFacingErrors::valueCannotBeEmpty();
        return false;
    }

    if (trimmed == QStringLiteral("all") || trimmed == QStringLiteral("all_resources")) {
        *modeOut = User::OccupationNotifyMode::AllResources;
        return true;
    }
    if (trimmed == QStringLiteral("mine") || trimmed == QStringLiteral("own")
        || trimmed == QStringLiteral("own_resources")) {
        *modeOut = User::OccupationNotifyMode::OwnResourcesOnly;
        return true;
    }
    if (trimmed == QStringLiteral("off") || trimmed == QStringLiteral("disabled")) {
        *modeOut = User::OccupationNotifyMode::Disabled;
        return true;
    }

    if (errorOut) {
        *errorOut = MessengerUserFacingErrors::invalidNotifyMode();
    }
    return false;
}
} // namespace

MessengerUserSettingsCommand::MessengerUserSettingsCommand(MessengerCommandManager *manager)
    : MessengerAbstractCommand(manager, MessengerCommandAudience::AdminsAndUsers, 120)
{
    initParser(tr("/user_settings"), tr("List/get/set user settings."));
}

void MessengerUserSettingsCommand::configureParser(MessengerCommandParser &parser) const
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

void MessengerUserSettingsCommand::execute(const AbstractBot::IncomingMessage &message,
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

    const MessengerUserKey actor = AbstractBot::senderKey(message);
    const UserModel *model = b->database() ? b->database()->userModel() : nullptr;
    if (!model) {
        b->sendReply(message.chat, MessengerUserFacingErrors::userModelNotAvailable());
        return;
    }

    const User current = model->userByKey(actor);
    if (!current.isValid()) {
        b->sendReply(message.chat,
                     MessengerUserFacingErrors::accessDeniedAccessNotActivated(actor));
        return;
    }

    if (action == QStringLiteral("list")) {
        if (!key.isEmpty() || !valueToken.isEmpty()) {
            b->sendReply(message.chat, tr("Usage: %1").arg(parser().usageSignature()));
            return;
        }

        QStringList lines;
        const QString localeValue = formatLocaleValue(current.locale());
        lines << QStringLiteral("%1 = %2 - %3")
                         .arg(QLatin1String(kKeyLocale),
                              localeValue,
                              descriptionForKey(kKeyLocale));
        const QString timezoneValue = formatTimezoneValue(current.timezone());
        lines << QStringLiteral("%1 = %2 - %3")
                         .arg(QLatin1String(kKeyTimezone),
                              timezoneValue,
                              descriptionForKey(kKeyTimezone));
        const QString notifyValue = formatNotifyMode(current.occupationNotifyMode());
        lines << QStringLiteral("%1 = %2 - %3")
                         .arg(QLatin1String(kKeyOccupationNotify),
                              notifyValue,
                              descriptionForKey(kKeyOccupationNotify));

        b->sendReply(message.chat, lines.join(QStringLiteral("\n")));
        return;
    }

    if (action == QStringLiteral("get")) {
        if (key.isEmpty() || !valueToken.isEmpty()) {
            b->sendReply(message.chat, tr("Usage: %1").arg(parser().usageSignature()));
            return;
        }

        const QString trimmedKey = key.trimmed().toLower();
        if (trimmedKey == QLatin1String(kKeyLocale)) {
            const QString reply = QStringLiteral("%1 = %2 - %3")
                                          .arg(QLatin1String(kKeyLocale),
                                               formatLocaleValue(current.locale()),
                                               descriptionForKey(kKeyLocale));
            b->sendReply(message.chat, reply);
            return;
        }
        if (trimmedKey == QLatin1String(kKeyTimezone)) {
            const QString reply = QStringLiteral("%1 = %2 - %3")
                                          .arg(QLatin1String(kKeyTimezone),
                                               formatTimezoneValue(current.timezone()),
                                               descriptionForKey(kKeyTimezone));
            b->sendReply(message.chat, reply);
            return;
        }
        if (trimmedKey == QLatin1String(kKeyOccupationNotify)) {
            const QString reply = QStringLiteral("%1 = %2 - %3")
                                          .arg(QLatin1String(kKeyOccupationNotify),
                                               formatNotifyMode(current.occupationNotifyMode()),
                                               descriptionForKey(kKeyOccupationNotify));
            b->sendReply(message.chat, reply);
            return;
        }

        b->sendReply(message.chat, tr("Unknown setting key: %1").arg(key));
        return;
    }

    if (action == QStringLiteral("set")) {
        if (key.isEmpty() || valueToken.isEmpty()) {
            b->sendReply(message.chat, tr("Usage: %1").arg(parser().usageSignature()));
            return;
        }

        User updated = current;
        const QString trimmedKey = key.trimmed().toLower();
        QString parseError;
        if (trimmedKey == QLatin1String(kKeyLocale)) {
            QString parsedLocale;
            if (!parseLocaleToken(valueToken, &parsedLocale, &parseError)) {
                b->sendReply(message.chat, parseError);
                return;
            }
            updated.setLocale(parsedLocale);
        } else if (trimmedKey == QLatin1String(kKeyTimezone)) {
            UserTimeZone parsedTimeZone;
            if (!parseTimezoneToken(valueToken, &parsedTimeZone, &parseError)) {
                b->sendReply(message.chat, parseError);
                return;
            }
            updated.setTimezone(parsedTimeZone);
        } else if (trimmedKey == QLatin1String(kKeyOccupationNotify)) {
            User::OccupationNotifyMode notifyMode = User::OccupationNotifyMode::OwnResourcesOnly;
            if (!parseNotifyModeToken(valueToken, &notifyMode, &parseError)) {
                b->sendReply(message.chat, parseError);
                return;
            }
            updated.setOccupationNotifyMode(notifyMode);
        } else {
            b->sendReply(message.chat, tr("Unknown setting key: %1").arg(key));
            return;
        }

        const ServerDatabaseError error = b->database()->updateUser(updated, actor);
        if (error) {
            b->sendReply(message.chat, error.errorString());
            return;
        }

        QString reply;
        if (trimmedKey == QLatin1String(kKeyLocale)) {
            reply = tr("Updated: %1 = %2")
                            .arg(QLatin1String(kKeyLocale), formatLocaleValue(updated.locale()));
        } else if (trimmedKey == QLatin1String(kKeyTimezone)) {
            reply = tr("Updated: %1 = %2")
                            .arg(QLatin1String(kKeyTimezone),
                                 formatTimezoneValue(updated.timezone()));
        } else {
            reply = tr("Updated: %1 = %2")
                            .arg(QLatin1String(kKeyOccupationNotify),
                                 formatNotifyMode(updated.occupationNotifyMode()));
        }
        b->sendReply(message.chat, reply);
        return;
    }

    b->sendReply(message.chat,
                 MessengerUserFacingErrors::unknownAction(action,
                                                          QStringLiteral("list, get, set")));
}

QStringList MessengerUserSettingsCommand::details() const
{
    QStringList details;
    details << tr("Actions: list, get, set.");
    details << tr("locale accepts default/auto/messenger to follow messenger language.");
    details << tr("timezone accepts time zone values "
                  "(e.g. UTC+3:00, UTC+03:00, +03:00, 03:00, -03:00 or 180) and "
                  "default/auto/system.");
    details << tr("occupation.notify accepts all/mine/off; 'mine' means only events involving "
                  "you.");
    return details;
}

QStringList MessengerUserSettingsCommand::examples() const
{
    QStringList examples;
    examples << tr("/user_settings list");
    examples << tr("/user_settings get locale");
    examples << tr("/user_settings get timezone");
    examples << tr("/user_settings set locale ru");
    examples << tr("/user_settings set timezone UTC+03:00");
    examples << tr("/user_settings set occupation.notify all");
    return examples;
}

BOOKING_RESOURCES_REGISTER_COMMAND(MessengerUserSettingsCommand)
