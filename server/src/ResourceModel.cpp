#include "ResourceModel.h"

#include <QUuid>

using Base = AbstractServerDatabaseModel<Resource, QString>;
using BaseStorageAdapter = Base::StorageAdapter;

ResourceModel::ResourceModel(QObject *parent)
    : Base(InsertPolicy::PreserveInsertionOrder, parent)
{
    // do nothing
}

ResourceModel::~ResourceModel()
{
    // do nothing
}

void ResourceModel::setStorageAdapter(StorageAdapter adapter)
{
    BaseStorageAdapter baseAdapter;
    baseAdapter.upsert = std::move(adapter.upsert);
    baseAdapter.remove = std::move(adapter.remove);
    Base::setStorageAdapter(std::move(baseAdapter));
}

QVariant ResourceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= entryCount())
        return QVariant();

    const Resource &resource = entryAt(index.row());
    switch (role) {
    case NameRole:
        return resource.name();
    case DescriptionRole:
        return resource.description();
    case StartMessageRole:
        return resource.startMessage();
    case StatusRole:
        return static_cast<int>(resource.status());
    case GroupsRole:
        return Group::toStringList(resource.groups());
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> ResourceModel::roleNames() const
{
    return {
        { NameRole, QByteArrayLiteral("name") },
        { DescriptionRole, QByteArrayLiteral("description") },
        { StartMessageRole, QByteArrayLiteral("startMessage") },
        { StatusRole, QByteArrayLiteral("status") },
        { GroupsRole, QByteArrayLiteral("groups") },
    };
}

void ResourceModel::replaceAll(const QList<Resource> &resources)
{
    replaceAllEntries(resources);
}

bool ResourceModel::upsert(const Resource &resource)
{
    return upsertEntry(resource);
}

bool ResourceModel::removeByUuid(const QString &uuid)
{
    const QString key = Resource::normalizeUuidString(uuid);
    if (key.isEmpty())
        return false;
    return Base::removeEntryByKey(key);
}

bool ResourceModel::existsByUuid(const QString &uuid) const
{
    const QString key = Resource::normalizeUuidString(uuid);
    if (key.isEmpty())
        return false;
    return containsKey(key);
}

bool ResourceModel::existsByName(const QString &name) const
{
    return resourceByName(name).isValid();
}

Resource ResourceModel::resourceByUuid(const QString &uuid) const
{
    const QString key = Resource::normalizeUuidString(uuid);
    if (key.isEmpty())
        return Resource();
    return Base::entryByKey(key);
}

Resource ResourceModel::resourceByName(const QString &name) const
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return Resource();

    const QList<Resource> &resources = entriesRef();
    for (const Resource &resource : resources) {
        if (resource.name() == trimmed)
            return resource;
    }

    return Resource();
}

QString ResourceModel::uuidForName(const QString &name) const
{
    const Resource resource = resourceByName(name);
    return resource.isValid() ? resource.uuidString() : QString();
}

QString ResourceModel::nameForUuid(const QString &uuid) const
{
    const Resource resource = resourceByUuid(uuid);
    return resource.isValid() ? resource.name() : QString();
}

const QList<Resource> &ResourceModel::items() const
{
    return entriesRef();
}

bool ResourceModel::addResource(const Resource &resource)
{
    const QString name = resource.name().trimmed();
    if (name.isEmpty()) {
        emit errorOccurred(QStringLiteral("Resource name is empty"));
        return false;
    }

    if (existsByName(name)) {
        emit errorOccurred(QStringLiteral("Resource '%1' already exists").arg(name));
        return false;
    }

    Resource normalized = resource;
    if (normalized.uuid().isNull()) {
        for (int attempts = 0; attempts < 3; ++attempts) {
            normalized.setUuid(QUuid::createUuid());
            if (!existsByUuid(normalized.uuidString()))
                break;
        }
    }

    const QString uuidKey = normalized.uuidString();
    if (uuidKey.isEmpty() || existsByUuid(uuidKey)) {
        emit errorOccurred(QStringLiteral("Failed to generate a unique resource identifier."));
        return false;
    }

    if (!persistUpsert(normalized)) {
        emit errorOccurred(QStringLiteral("Failed to persist resource '%1'.").arg(name));
        return false;
    }

    upsert(normalized);
    return true;
}

bool ResourceModel::updateResource(const Resource &resource)
{
    const QString name = resource.name().trimmed();
    if (name.isEmpty()) {
        emit errorOccurred(QStringLiteral("Resource name is empty"));
        return false;
    }

    const QString uuidKey = resource.uuidString();
    const Resource existing = uuidKey.isEmpty() ? resourceByName(name) : resourceByUuid(uuidKey);
    if (!existing.isValid()) {
        emit errorOccurred(QStringLiteral("Resource '%1' does not exist").arg(name));
        return false;
    }

    if (name != existing.name() && existsByName(name)) {
        emit errorOccurred(QStringLiteral("Resource '%1' already exists").arg(name));
        return false;
    }

    const Resource::Status resolvedStatus = existing.status();
    Resource updated(existing.uuid(),
                     name,
                     resource.description(),
                     resource.startMessage(),
                     resolvedStatus);
    updated.setGroups(resource.groups());
    if (!persistUpsert(updated)) {
        emit errorOccurred(QStringLiteral("Failed to persist resource '%1'.").arg(existing.name()));
        return false;
    }

    upsert(updated);
    return true;
}

bool ResourceModel::deleteResource(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        emit errorOccurred(QStringLiteral("Resource name is empty"));
        return false;
    }

    const Resource existing = resourceByName(trimmed);
    if (!existing.isValid()) {
        emit errorOccurred(QStringLiteral("Resource '%1' does not exist").arg(trimmed));
        return false;
    }

    if (!persistRemove(existing.uuidString())) {
        emit errorOccurred(
                QStringLiteral("Failed to persist resource '%1' deletion.").arg(existing.name()));
        return false;
    }

    removeByUuid(existing.uuidString());
    return true;
}

QString ResourceModel::keyFromEntry(const Resource &resource) const
{
    return resource.uuidString();
}
