#include "Group.h"

Group::Group()
    : QString(QStringLiteral(""))
{
    // do nothing
}

Group::Group(const QString &path)
    : QString(normalizePath(path))
{
    // do nothing
}

Group::Group(const char *path)
    : QString(normalizePath(QString::fromUtf8(path ? path : "")))
{
    // do nothing
}

bool Group::isRoot() const
{
    return isEmpty();
}

Group Group::parent() const
{
    if (isRoot())
        return Group();

    const int slash = lastIndexOf(QLatin1Char('/'));
    if (slash <= 0)
        return Group();

    return Group(left(slash));
}

QString Group::leafName() const
{
    if (isRoot())
        return rootDisplayName();

    const int slash = lastIndexOf(QLatin1Char('/'));
    if (slash < 0)
        return *this;

    return mid(slash + 1);
}

QStringList Group::segments() const
{
    if (isRoot())
        return QStringList();

    return split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

QList<Group> Group::ancestors(bool includeSelf) const
{
    QList<Group> result;
    Group current = includeSelf ? *this : parent();
    while (!current.isRoot()) {
        result.prepend(current);
        current = current.parent();
    }
    result.prepend(Group());
    return result;
}

bool Group::isAncestorOf(const Group &other) const
{
    if (isRoot())
        return true;

    if (*this == other)
        return true;

    const QString prefix = QString(*this) + QLatin1Char('/');
    return QString(other).startsWith(prefix);
}

bool Group::contains(const Group &other) const
{
    return isAncestorOf(other);
}

Group Group::root()
{
    return Group();
}

QString Group::normalizePath(const QString &path)
{
    QString normalized = path.trimmed();
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));

    QStringList parts;
    const QStringList rawParts = normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &rawPart : rawParts) {
        const QString part = rawPart.trimmed();
        if (!part.isEmpty())
            parts.append(part);
    }

    return parts.join(QLatin1Char('/'));
}

QString Group::rootDisplayName()
{
    return QStringLiteral("Root (all groups)");
}

QString Group::displayName(const Group &group)
{
    return group.isRoot() ? rootDisplayName() : QString(group);
}

QSet<Group> Group::normalizedSet(const QSet<Group> &groups)
{
    QSet<Group> result;
    for (const Group &group : groups) {
        result.insert(Group(group));
    }
    if (result.isEmpty())
        result.insert(Group::root());
    return result;
}

QStringList Group::toStringList(const QSet<Group> &groups, bool displayRoot)
{
    QStringList result;
    const QSet<Group> normalized = normalizedSet(groups);
    for (const Group &group : normalized) {
        result.append(displayRoot ? displayName(group) : QString(group));
    }
    std::sort(result.begin(), result.end(), [](const QString &left, const QString &right) {
        return QString::localeAwareCompare(left, right) < 0;
    });
    return result;
}

QSet<Group> Group::fromStringList(const QStringList &paths)
{
    QSet<Group> result;
    for (const QString &path : paths) {
        result.insert(Group(path));
    }
    if (result.isEmpty())
        result.insert(Group::root());
    return result;
}

bool operator<(const Group &left, const Group &right)
{
    return QString::compare(QString(left), QString(right), Qt::CaseSensitive) < 0;
}

bool operator==(const Group &left, const Group &right)
{
    return QString(left) == QString(right);
}

uint qHash(const Group &group, uint seed)
{
    return ::qHash(group.toUtf8(), seed);
}
