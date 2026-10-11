#ifndef RESOURCE_H
#define RESOURCE_H

#include <QSharedDataPointer>

#include <QSet>
#include <QUuid>
#include <QString>

#include "Group.h"

/**
 * @brief Lightweight resource value type using Qt implicit sharing.
 *
 * Stores resource UUID, name, description, start message and status, following the rule of five.
 */
class Resource final
{
public:
    enum class Status {
        On = 0,
        TemporarilyUnavailable,
        Off
    };

    Resource();
    explicit Resource(const QUuid &uuid,
                      const QString &name,
                      const QString &description,
                      const QString &startMessage,
                      Status status = Status::On);
    explicit Resource(const QString &name,
                      const QString &description,
                      const QString &startMessage,
                      Status status = Status::On);

    Resource(const Resource &other);
    Resource(Resource &&other) noexcept;
    Resource &operator=(const Resource &other);
    Resource &operator=(Resource &&other) noexcept;

    ~Resource();

    QUuid uuid() const;
    QString uuidString() const;
    QString name() const;
    QString description() const;
    QString startMessage() const;
    Status status() const;
    QSet<Group> groups() const;
    bool isInGroup(const Group &group) const;

    void setUuid(const QUuid &uuid);
    void setName(const QString &name);
    void setDescription(const QString &description);
    void setStartMessage(const QString &message);
    void setStatus(Status status);
    void setGroups(const QSet<Group> &groups);

    bool isValid() const;

    static QUuid uuidFromString(const QString &value);
    static QString uuidToString(const QUuid &uuid);
    static QString normalizeUuidString(const QString &value);
    static QString statusToStorageString(Status status);
    static Status statusFromStorageString(const QString &value);
    static bool tryParseStatusToken(const QString &value, Status *statusOut);
    static QString statusToToken(Status status);

private:
    class ResourceData;
    QSharedDataPointer<ResourceData> d;
};

#endif // RESOURCE_H
