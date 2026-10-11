#ifndef SERVERRUNTIMESETTINGS_H
#define SERVERRUNTIMESETTINGS_H

#include <QList>
#include <QString>
#include <QVariant>

namespace ServerRuntimeSettings {

// Runtime-configurable settings (persisted in SQLite).
enum class Setting {
    MaxResourcesPerUser = 0,
    MaxOccupationDurationMinutes,
    MenuUpdateMode,
    MenuSessionTimeoutMinutes,
    StatsEnabled,
    DateTimeFormat,
    StatsAdminsOnly,
    PendingAccessUserReplyMessage,
};

enum class ValueFormatStyle {
    Tokens = 0,
    HumanReadable
};

// Returns the persistent key for a setting.
const char *key(Setting setting);

// Default values (must match component defaults).
QVariant defaultValue(Setting setting);

// Returns true if the key matches the provided setting.
bool isSettingKey(const QString &trimmedKey, Setting setting);

// Resolves runtime setting enum by key. Returns false for unknown keys.
bool settingFromKey(const QString &trimmedKey, Setting *settingOut);

// Returns all supported runtime settings.
QList<Setting> allSettings();

QString formatSettingValue(const QString &trimmedKey,
                           const QVariant &value,
                           ValueFormatStyle style = ValueFormatStyle::Tokens);
QString formatSettingValue(Setting setting,
                           const QVariant &value,
                           ValueFormatStyle style = ValueFormatStyle::Tokens);

bool parseSettingValueToken(const QString &trimmedKey,
                            const QString &token,
                            QVariant *valueOut,
                            QString *errorOut = nullptr);
bool parseSettingValueToken(Setting setting,
                            const QString &token,
                            QVariant *valueOut,
                            QString *errorOut = nullptr);

} // namespace ServerRuntimeSettings

#endif // SERVERRUNTIMESETTINGS_H
