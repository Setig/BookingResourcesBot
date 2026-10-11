#ifndef RESOURCEMODEL_H
#define RESOURCEMODEL_H

#include "AbstractServerDatabaseModel.h"

#include "Resource.h"

class ResourceModelPrivate;

/**
 * @brief List model that stores available resources.
 *
 * Stores Resource values (uuid/name/description/start message/status) and can optionally persist
 * changes
 * via callbacks provided through StorageAdapter. The UUID is used as the internal key; resource
 * names remain unique and user-facing.
 */
class ResourceModel : public AbstractServerDatabaseModel<Resource, QString>
{
    Q_OBJECT

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        DescriptionRole,
        StartMessageRole,
        StatusRole,
        GroupsRole
    };

    explicit ResourceModel(QObject *parent = nullptr);
    ~ResourceModel() override;

    /**
     * @brief Storage callbacks used to persist resource changes.
     */
    struct StorageAdapter
    {
        std::function<bool(const Resource &resource)> upsert;
        std::function<bool(const QString &name)> remove;
    };

    void setStorageAdapter(StorageAdapter adapter);

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void replaceAll(const QList<Resource> &resources);
    bool upsert(const Resource &resource);

    /**
     * @brief Removes a resource by UUID.
     *
     * The @p uuid parameter will be normalized.
     */
    bool removeByUuid(const QString &uuid);

    /**
     * @brief Returns true if a resource exists for the given UUID.
     *
     * The @p uuid parameter will be normalized.
     */
    bool existsByUuid(const QString &uuid) const;

    /**
     * @brief Returns true if a resource exists for the given name.
     *
     * The @p name parameter will be trimmed.
     */
    bool existsByName(const QString &name) const;

    /**
     * @brief Returns the resource by UUID (or an invalid Resource if not found).
     *
     * The @p uuid parameter will be normalized.
     */
    Resource resourceByUuid(const QString &uuid) const;

    /**
     * @brief Returns the resource by name (or an invalid Resource if not found).
     *
     * The @p name parameter will be trimmed.
     */
    Resource resourceByName(const QString &name) const;

    /**
     * @brief Returns UUID string for the given resource name (empty if missing).
     */
    QString uuidForName(const QString &name) const;

    /**
     * @brief Returns resource name for the given UUID (empty if missing).
     */
    QString nameForUuid(const QString &uuid) const;

    const QList<Resource> &items() const;

    bool addResource(const Resource &resource);
    bool updateResource(const Resource &resource);
    bool deleteResource(const QString &name);

protected:
    QString keyFromEntry(const Resource &resource) const override;
};

#endif // RESOURCEMODEL_H
