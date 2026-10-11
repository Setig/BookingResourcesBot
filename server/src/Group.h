#ifndef GROUP_H
#define GROUP_H

#include <QSet>
#include <QString>
#include <QStringList>

/**
 * @brief Hierarchical group path used for users and resources.
 *
 * Groups are stored as normalized slash-separated paths. The empty string is the root group,
 * and empty group collections are treated as root-only collections.
 */
class Group : public QString
{
public:
    Group();
    Group(const QString &path);
    Group(const char *path);

    bool isRoot() const;
    Group parent() const;
    QString leafName() const;
    QStringList segments() const;
    QList<Group> ancestors(bool includeSelf = true) const;
    bool isAncestorOf(const Group &other) const;
    bool contains(const Group &other) const;

    static Group root();
    static QString normalizePath(const QString &path);
    static QString rootDisplayName();
    static QString displayName(const Group &group);
    static QSet<Group> normalizedSet(const QSet<Group> &groups);
    static QStringList toStringList(const QSet<Group> &groups, bool displayRoot = false);
    static QSet<Group> fromStringList(const QStringList &paths);
};

bool operator<(const Group &left, const Group &right);
bool operator==(const Group &left, const Group &right);
uint qHash(const Group &group, uint seed = 0);

#endif // GROUP_H
