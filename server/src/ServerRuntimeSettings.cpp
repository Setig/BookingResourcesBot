#include "ServerRuntimeSettings.h"

#include "ServerDateTimeFormat.h"

#include <QCoreApplication>

namespace ServerRuntimeSettings {

class Messages
{
    Q_DECLARE_TR_FUNCTIONS(ServerRuntimeSettings)
};

namespace {
bool isStatsBooleanSetting(const QString &trimmedKey)
{
    return ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::StatsEnabled)
           || ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::StatsAdminsOnly);
}
} // namespace

const char *key(Setting setting)
{
    switch (setting) {
    case Setting::MaxResourcesPerUser:
        return "queue.max_resources_per_user";
    case Setting::MaxOccupationDurationMinutes:
        return "queue.max_occupation_duration_minutes";
    case Setting::MenuUpdateMode:
        return "menu.update_mode";
    case Setting::MenuSessionTimeoutMinutes:
        return "menu.session_timeout_minutes";
    case Setting::StatsEnabled:
        return "stats.enabled";
    case Setting::DateTimeFormat:
        return "ui.date_time_format";
    case Setting::StatsAdminsOnly:
        return "stats.admins_only";
    case Setting::PendingAccessUserReplyMessage:
        return "access.pending_access_user_reply";
    }
    return "";
}

QVariant defaultValue(Setting setting)
{
    switch (setting) {
    case Setting::MaxResourcesPerUser:
        return QVariant::fromValue(uint(0)); // 0 = unlimited
    case Setting::MaxOccupationDurationMinutes:
        return QVariant::fromValue(uint(0)); // 0 = unlimited
    case Setting::MenuUpdateMode:
        return QVariant::fromValue(uint(0)); // MessengerMenuManager::MenuUpdateMode::Active
    case Setting::MenuSessionTimeoutMinutes:
        return QVariant::fromValue(uint(30)); // MessengerMenuManager default
    case Setting::StatsEnabled:
        return QVariant::fromValue(false); // disabled
    case Setting::DateTimeFormat:
        return QVariant::fromValue(QString()); // string-backed setting
    case Setting::StatsAdminsOnly:
        return QVariant::fromValue(true); // admins only
    case Setting::PendingAccessUserReplyMessage:
        return QVariant::fromValue(QString()); // localized built-in message
    }
    return QVariant();
}

bool isSettingKey(const QString &trimmedKey, Setting setting)
{
    return trimmedKey == QLatin1String(ServerRuntimeSettings::key(setting));
}

bool settingFromKey(const QString &trimmedKey, Setting *settingOut)
{
    if (!settingOut)
        return false;

    const QList<Setting> settings = allSettings();
    for (const Setting setting : settings) {
        if (isSettingKey(trimmedKey, setting)) {
            *settingOut = setting;
            return true;
        }
    }

    return false;
}

QList<Setting> allSettings()
{
    return { Setting::MaxResourcesPerUser, Setting::MaxOccupationDurationMinutes,
             Setting::MenuUpdateMode,      Setting::MenuSessionTimeoutMinutes,
             Setting::StatsEnabled,        Setting::StatsAdminsOnly,
             Setting::DateTimeFormat,      Setting::PendingAccessUserReplyMessage };
}

QString formatSettingValue(const QString &trimmedKey, const QVariant &value, ValueFormatStyle style)
{
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::MenuUpdateMode)) {
        bool ok = false;
        const uint mode = value.toUInt(&ok);
        if (!ok)
            return QStringLiteral("<invalid>");
        return mode == 0 ? QStringLiteral("active") : QStringLiteral("passive");
    }
    if (isStatsBooleanSetting(trimmedKey)) {
        if (!value.isValid() || value.isNull())
            return QStringLiteral("<invalid>");
        if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::StatsAdminsOnly)) {
            if (style == ValueFormatStyle::HumanReadable)
                return value.toBool() ? QStringLiteral("admins only")
                                      : QStringLiteral("admins and users");
            return value.toBool() ? QStringLiteral("admins_only") : QStringLiteral("all_users");
        }
        return value.toBool() ? QStringLiteral("enabled") : QStringLiteral("disabled");
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::DateTimeFormat)) {
        const QString format = value.toString().trimmed();
        if (format.isEmpty()) {
            return QStringLiteral("auto (%1)").arg(ServerDateTimeFormat::defaultLocalizedFormat());
        }
        return format;
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::PendingAccessUserReplyMessage)) {
        const QString message = value.toString().trimmed();
        if (message.isEmpty())
            return QStringLiteral("default");
        return message;
    }
    if (!value.isValid() || value.isNull())
        return QStringLiteral("<null>");

    bool ok = false;
    const uint numericValue = value.toUInt(&ok);
    if (ok)
        return QString::number(numericValue);

    return value.toString();
}

QString formatSettingValue(Setting setting, const QVariant &value, ValueFormatStyle style)
{
    return formatSettingValue(QLatin1String(ServerRuntimeSettings::key(setting)), value, style);
}

bool parseSettingValueToken(const QString &trimmedKey,
                            const QString &token,
                            QVariant *valueOut,
                            QString *errorOut)
{
    if (!valueOut)
        return false;

    const QString trimmedToken = token.trimmed();
    if (trimmedToken.isEmpty()) {
        if (errorOut)
            *errorOut = Messages::tr("Value cannot be empty.");
        return false;
    }

    const QString normalizedToken = trimmedToken.toLower();
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::MenuUpdateMode)) {
        if (normalizedToken == QStringLiteral("0") || normalizedToken == QStringLiteral("active")) {
            *valueOut = QVariant::fromValue(uint(0));
            return true;
        }
        if (normalizedToken == QStringLiteral("1")
            || normalizedToken == QStringLiteral("passive")) {
            *valueOut = QVariant::fromValue(uint(1));
            return true;
        }
        if (errorOut) {
            *errorOut = Messages::tr("Invalid value. Expected 0/1 or active/passive.");
        }
        return false;
    }
    if (isStatsBooleanSetting(trimmedKey)) {
        if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::StatsAdminsOnly)) {
            if (normalizedToken == QStringLiteral("admins_only")
                || normalizedToken == QStringLiteral("admin_only")
                || normalizedToken == QStringLiteral("admins")) {
                *valueOut = QVariant::fromValue(true);
                return true;
            }
            if (normalizedToken == QStringLiteral("all_users")
                || normalizedToken == QStringLiteral("admins_and_users")
                || normalizedToken == QStringLiteral("users")) {
                *valueOut = QVariant::fromValue(false);
                return true;
            }
        }
        if (normalizedToken == QStringLiteral("1") || normalizedToken == QStringLiteral("enabled")
            || normalizedToken == QStringLiteral("on")
            || normalizedToken == QStringLiteral("true")) {
            *valueOut = QVariant::fromValue(true);
            return true;
        }
        if (normalizedToken == QStringLiteral("0") || normalizedToken == QStringLiteral("disabled")
            || normalizedToken == QStringLiteral("off")
            || normalizedToken == QStringLiteral("false")) {
            *valueOut = QVariant::fromValue(false);
            return true;
        }
        if (errorOut) {
            *errorOut = ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::StatsAdminsOnly)
                                ? Messages::tr("Invalid value. Expected 0/1 or "
                                               "admins_only/all_users.")
                                : Messages::tr("Invalid value. Expected 0/1 or enabled/disabled.");
        }
        return false;
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::DateTimeFormat)) {
        QString normalizedFormat;
        if (!ServerDateTimeFormat::parseSettingValue(trimmedToken, &normalizedFormat)) {
            if (errorOut) {
                *errorOut = Messages::tr("Invalid value. Expected date/time format like "
                                         "dd.MM.yyyy HH:mm:ss or yyyy-MM-dd HH:mm, or "
                                         "default/auto.");
            }
            return false;
        }
        *valueOut = QVariant::fromValue(normalizedFormat);
        return true;
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::PendingAccessUserReplyMessage)) {
        if (normalizedToken == QStringLiteral("default")
            || normalizedToken == QStringLiteral("auto")) {
            *valueOut = QVariant::fromValue(QString());
            return true;
        }
        *valueOut = QVariant::fromValue(trimmedToken);
        return true;
    }

    if (normalizedToken == QStringLiteral("unlimited")
        || normalizedToken == QStringLiteral("disabled")) {
        *valueOut = QVariant::fromValue(uint(0));
        return true;
    }

    bool ok = false;
    const uint numericValue = trimmedToken.toUInt(&ok);
    if (!ok) {
        if (errorOut) {
            *errorOut = Messages::tr("Invalid value. Expected a non-negative integer.");
        }
        return false;
    }
    *valueOut = QVariant::fromValue(numericValue);
    return true;
}

bool parseSettingValueToken(Setting setting,
                            const QString &token,
                            QVariant *valueOut,
                            QString *errorOut)
{
    return parseSettingValueToken(QLatin1String(ServerRuntimeSettings::key(setting)),
                                  token,
                                  valueOut,
                                  errorOut);
}

} // namespace ServerRuntimeSettings
