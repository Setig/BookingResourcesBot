#include "Resource.h"

class Resource::ResourceData : public QSharedData
{
public:
    ResourceData()
        : status(Resource::Status::On)
    {
        // do nothing
    }

    QUuid uuid;
    QString name;
    QString description;
    QString startMessage;
    Resource::Status status;
    QSet<Group> groups;
};

Resource::Resource()
    : d(new ResourceData)
{
    d->groups.insert(Group::root());
    // do nothing
}

Resource::Resource(const QUuid &uuid,
                   const QString &name,
                   const QString &description,
                   const QString &startMessage,
                   Resource::Status status)
    : d(new ResourceData)
{
    d->uuid = uuid;
    d->name = name.trimmed();
    d->description = description.trimmed();
    d->startMessage = startMessage.trimmed();
    d->status = status;
    d->groups.insert(Group::root());
}

Resource::Resource(const QString &name,
                   const QString &description,
                   const QString &startMessage,
                   Resource::Status status)
    : d(new ResourceData)
{
    d->name = name.trimmed();
    d->description = description.trimmed();
    d->startMessage = startMessage.trimmed();
    d->status = status;
    d->groups.insert(Group::root());
}

Resource::Resource(const Resource &other)
    : d(other.d)
{
    // do nothing
}

Resource::Resource(Resource &&other) noexcept
    : d(std::move(other.d))
{
    // do nothing
}

Resource &Resource::operator=(const Resource &other)
{
    if (this != &other) {
        d = other.d;
    }
    return *this;
}

Resource &Resource::operator=(Resource &&other) noexcept
{
    if (this != &other) {
        d.swap(other.d);
    }
    return *this;
}

Resource::~Resource()
{
    // do nothing
}

QUuid Resource::uuid() const
{
    return d ? d->uuid : QUuid();
}

QString Resource::uuidString() const
{
    return uuidToString(uuid());
}

QString Resource::name() const
{
    return d ? d->name : QString();
}

QString Resource::description() const
{
    return d ? d->description : QString();
}

QString Resource::startMessage() const
{
    return d ? d->startMessage : QString();
}

Resource::Status Resource::status() const
{
    return d ? d->status : Resource::Status::On;
}

QSet<Group> Resource::groups() const
{
    if (!d)
        return QSet<Group>();

    if (d->groups.isEmpty()) {
        QSet<Group> rootOnly;
        rootOnly.insert(Group::root());
        return rootOnly;
    }

    return d->groups;
}

bool Resource::isInGroup(const Group &group) const
{
    return groups().contains(Group(group));
}

void Resource::setUuid(const QUuid &uuid)
{
    if (!d)
        return;

    const QSharedDataPointer<ResourceData> &cd = d;
    if (cd->uuid == uuid)
        return;

    d->uuid = uuid;
}

void Resource::setName(const QString &name)
{
    if (!d)
        return;

    const QString trimmed = name.trimmed();
    const QSharedDataPointer<ResourceData> &cd = d;
    if (cd->name == trimmed)
        return;

    d->name = trimmed;
}

void Resource::setDescription(const QString &description)
{
    if (!d)
        return;

    const QString trimmed = description.trimmed();
    const QSharedDataPointer<ResourceData> &cd = d;
    if (cd->description == trimmed)
        return;

    d->description = trimmed;
}

void Resource::setStartMessage(const QString &message)
{
    if (!d)
        return;

    const QString trimmed = message.trimmed();
    const QSharedDataPointer<ResourceData> &cd = d;
    if (cd->startMessage == trimmed)
        return;

    d->startMessage = trimmed;
}

void Resource::setStatus(Resource::Status status)
{
    if (!d)
        return;

    const QSharedDataPointer<ResourceData> &cd = d;
    if (cd->status == status)
        return;

    d->status = status;
}

void Resource::setGroups(const QSet<Group> &groups)
{
    if (!d)
        return;

    QSet<Group> normalized = Group::normalizedSet(groups);
    if (normalized.isEmpty())
        normalized.insert(Group::root());

    const QSharedDataPointer<ResourceData> &cd = d;
    if (cd->groups == normalized)
        return;

    d->groups = normalized;
}

bool Resource::isValid() const
{
    return d && !d->uuid.isNull() && !d->name.isEmpty();
}

QUuid Resource::uuidFromString(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty())
        return QUuid();
    if (trimmed.startsWith('{') && trimmed.endsWith('}'))
        return QUuid(trimmed);
    return QUuid(QStringLiteral("{%1}").arg(trimmed));
}

QString Resource::uuidToString(const QUuid &uuid)
{
    if (uuid.isNull())
        return QString();
    return uuid.toString(QUuid::WithoutBraces).toLower();
}

QString Resource::normalizeUuidString(const QString &value)
{
    return uuidToString(uuidFromString(value));
}

QString Resource::statusToStorageString(Resource::Status status)
{
    switch (status) {
    case Resource::Status::On:
        return QStringLiteral("on");
    case Resource::Status::TemporarilyUnavailable:
        return QStringLiteral("temporarily_unavailable");
    case Resource::Status::Off:
        return QStringLiteral("off");
    }
    return QStringLiteral("on");
}

Resource::Status Resource::statusFromStorageString(const QString &value)
{
    Resource::Status status = Resource::Status::On;
    if (tryParseStatusToken(value, &status))
        return status;
    return Resource::Status::On;
}

bool Resource::tryParseStatusToken(const QString &value, Resource::Status *statusOut)
{
    if (!statusOut)
        return false;

    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("on")) {
        *statusOut = Resource::Status::On;
        return true;
    }

    if (normalized == QStringLiteral("temporarilyunavailable")
        || normalized == QStringLiteral("temporarily_unavailable")
        || normalized == QStringLiteral("temporarily-unavailable")) {
        *statusOut = Resource::Status::TemporarilyUnavailable;
        return true;
    }

    if (normalized == QStringLiteral("off")) {
        *statusOut = Resource::Status::Off;
        return true;
    }

    return false;
}

QString Resource::statusToToken(Resource::Status status)
{
    switch (status) {
    case Resource::Status::On:
        return QStringLiteral("On");
    case Resource::Status::TemporarilyUnavailable:
        return QStringLiteral("TemporarilyUnavailable");
    case Resource::Status::Off:
        return QStringLiteral("Off");
    }
    return QStringLiteral("On");
}
