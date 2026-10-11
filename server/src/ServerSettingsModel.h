#ifndef SERVERSETTINGSMODEL_H
#define SERVERSETTINGSMODEL_H

#include "AbstractServerDatabaseModel.h"

#include "ServerSettingEntry.h"

/**
 * @brief In-memory list model of runtime-configurable server settings.
 *
 * This model stores current runtime setting values and provides localized descriptions based on
 * setting keys. The actual validation, access control, and persistence are performed by
 * ServerDatabase.
 */
class ServerSettingsModel final : public AbstractServerDatabaseModel<ServerSettingEntry, QString>
{
    Q_OBJECT

public:
    enum Roles {
        KeyRole = Qt::UserRole + 1,
        ValueRole,
        DescriptionRole,
    };

    explicit ServerSettingsModel(QObject *parent = nullptr);
    ~ServerSettingsModel() override;

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] QString descriptionForKey(const QString &key) const;

protected:
    QString keyFromEntry(const ServerSettingEntry &entry) const override;
    bool lessThan(const ServerSettingEntry &lhs, const ServerSettingEntry &rhs) const override;
};

#endif // SERVERSETTINGSMODEL_H
