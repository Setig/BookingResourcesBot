#include "ServerSettingsModel.h"

#include "ServerRuntimeSettings.h"

#include <QByteArray>

using Base = AbstractServerDatabaseModel<ServerSettingEntry, QString>;

namespace {
using Setting = ServerRuntimeSettings::Setting;
} // namespace

ServerSettingsModel::ServerSettingsModel(QObject *parent)
    : Base(Base::InsertPolicy::Sorted, parent)
{
    // do nothing
}

ServerSettingsModel::~ServerSettingsModel() = default;

QVariant ServerSettingsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();
    const int row = index.row();
    if (row < 0 || row >= entryCount())
        return QVariant();

    const ServerSettingEntry &entry = entryAt(row);

    switch (role) {
    case KeyRole:
        return entry.key();
    case ValueRole:
        return entry.value();
    case DescriptionRole:
        return descriptionForKey(entry.key());
    default:
        break;
    }

    return QVariant();
}

QHash<int, QByteArray> ServerSettingsModel::roleNames() const
{
    QHash<int, QByteArray> names;
    names.insert(KeyRole, "key");
    names.insert(ValueRole, "value");
    names.insert(DescriptionRole, "description");
    return names;
}

QString ServerSettingsModel::descriptionForKey(const QString &key) const
{
    const QString trimmedKey = key.trimmed();
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::MaxResourcesPerUser)) {
        return tr("Max number of simultaneously occupied resources per user (0 = unlimited).");
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::MaxOccupationDurationMinutes)) {
        return tr("Max occupation duration in minutes (0 = unlimited).");
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::MenuUpdateMode)) {
        return tr("Menu update mode: 0 = active, 1 = passive.");
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::MenuSessionTimeoutMinutes)) {
        return tr("Menu session timeout in minutes (0 = disabled).");
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::StatsEnabled)) {
        return tr("Enable statistics collection (0 = disabled, 1 = enabled).");
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::StatsAdminsOnly)) {
        return tr("Allow statistics for admins only (0 = all users, 1 = admins only).");
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::DateTimeFormat)) {
        return tr("Date/time display format (empty = locale default; examples: "
                  "dd.MM.yyyy HH:mm:ss, yyyy-MM-dd HH:mm).");
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::PendingAccessUserReplyMessage)) {
        return tr("Reply message sent to users awaiting access activation (empty = localized "
                  "default).");
    }
    return QString();
}

QString ServerSettingsModel::keyFromEntry(const ServerSettingEntry &entry) const
{
    return entry.key().trimmed();
}

bool ServerSettingsModel::lessThan(const ServerSettingEntry &lhs,
                                   const ServerSettingEntry &rhs) const
{
    return lhs.key().trimmed() < rhs.key().trimmed();
}
