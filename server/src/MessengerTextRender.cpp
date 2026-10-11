#include "MessengerTextRender.h"

#include "User.h"
#include "Resource.h"
#include "UserModel.h"
#include "ResourceModel.h"
#include "BlacklistModel.h"
#include "ServerDatabase.h"
#include "MessengerUserKey.h"
#include "ResourceQueueModel.h"
#include "ResourceQueueUserMessages.h"
#include "ServerDateTimeFormat.h"
#include "PendingAccessUserModel.h"

#include <QSet>
#include <QHash>
#include <QCollator>
#include <QDateTime>
#include <QStringList>
#include <QRegularExpression>

namespace {
inline QString escapeHtml(const QString &text)
{
    QString escaped = text;
    escaped.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    escaped.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    escaped.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    return escaped;
}

inline bool isLinkableTelegramUserId(const MessengerUserKey &key)
{
    if (!key.isValid())
        return false;
    if (key.messenger() != MessengerType::Telegram)
        return false;

    const QString userId = key.userId().trimmed();
    if (userId.isEmpty())
        return false;

    static const QRegularExpression re(QStringLiteral("^[1-9]\\d*$"));
    return re.match(userId).hasMatch();
}

inline QString wrapUserLinkHtml(const MessengerUserKey &key, const QString &labelHtml)
{
    if (!isLinkableTelegramUserId(key))
        return labelHtml;

    return QStringLiteral("<a href=\"tg://user?id=%1\">%2</a>")
            .arg(key.userId().trimmed(), labelHtml);
}

inline QString formatUserKeyPlain(const MessengerUserKey &key, const QString &invalidText)
{
    return key.isValid() ? key.toString() : invalidText;
}

inline QString formatUserKeyHtml(const MessengerUserKey &key, const QString &invalidText)
{
    if (!key.isValid())
        return escapeHtml(invalidText);

    const QString keyHtml = escapeHtml(key.toString());
    if (isLinkableTelegramUserId(key))
        return wrapUserLinkHtml(key, keyHtml);

    return QStringLiteral("<code>%1</code>").arg(keyHtml);
}

inline QString currentUserMarkerSourceText()
{
    return MessengerTextRender::tr("(you)", "Marker appended to the current user");
}

inline QString currentUserSuffixPlain()
{
    return QStringLiteral(" %1").arg(currentUserMarkerSourceText());
}

inline QString currentUserSuffixHtml()
{
    return QStringLiteral(" %1").arg(escapeHtml(currentUserMarkerSourceText()));
}

inline QString appendCurrentUserSuffix(QString text, bool isCurrentUser, bool useHtml)
{
    if (!isCurrentUser)
        return text;
    return text + (useHtml ? currentUserSuffixHtml() : currentUserSuffixPlain());
}

inline QString describeRelativeInterval(qint64 seconds)
{
    qint64 absSeconds = seconds >= 0 ? seconds : -seconds;
    if (absSeconds < 60)
        return MessengerTextRender::tr("less than 1 min");

    qint64 minutes = (absSeconds + 59) / 60;
    const qint64 hours = minutes / 60;
    const qint64 mins = minutes % 60;

    QStringList parts;
    if (hours > 0)
        parts << MessengerTextRender::tr("%1 h").arg(hours);
    if (mins > 0)
        parts << MessengerTextRender::tr("%1 min").arg(mins);

    if (parts.isEmpty())
        parts << MessengerTextRender::tr("less than 1 min");

    return parts.join(QStringLiteral(" "));
}

inline QString formatBlacklistStatusPlain(const BlacklistEntry &entry,
                                          const QString &dateTimeFormat,
                                          const UserTimeZone &timeZone)
{
    QStringList parts;
    parts << MessengerTextRender::tr("Blocked %1")
                     .arg(MessengerTextRender::describeBlacklistWindow(entry,
                                                                       dateTimeFormat,
                                                                       timeZone));

    const QString reason = MessengerTextRender::collapseWhitespace(entry.reason());
    if (!reason.isEmpty()) {
        parts << MessengerTextRender::tr("Reason: %1").arg(reason);
    }

    return parts.join(QStringLiteral(" - "));
}

inline QString formatBlacklistStatusHtml(const BlacklistEntry &entry,
                                         const QString &dateTimeFormat,
                                         const UserTimeZone &timeZone)
{
    QStringList lines;
    const QString blockedText =
            MessengerTextRender::tr("Blocked %1", "Blacklist status (HTML)")
                    .arg(MessengerTextRender::describeBlacklistWindow(entry,
                                                                      dateTimeFormat,
                                                                      timeZone));
    lines << QStringLiteral("<i>%1</i>").arg(escapeHtml(blockedText));

    const QString reason = entry.reason();
    if (!reason.isEmpty()) {
        const QString reasonLabel = MessengerTextRender::tr("Reason:");
        lines << QStringLiteral("<i>%1</i> %2").arg(escapeHtml(reasonLabel), escapeHtml(reason));
    }

    return lines.join(QStringLiteral("\n"));
}

inline User lookupActiveAccessUser(ServerDatabase *database, const MessengerUserKey &userKey)
{
    if (!userKey.isValid())
        return User();

    if (!database || !database->userModel())
        return User();

    const User user = database->userModel()->userByKey(userKey);
    if (!user.isValid())
        return User();

    return user;
}

UserTimeZone effectiveViewerTimeZone(ServerDatabase *database,
                                     const MessengerUserKey &currentUserKey)
{
    if (!database || !database->userModel() || !currentUserKey.isValid())
        return UserTimeZone();

    const User viewer = database->userModel()->userByKey(currentUserKey);
    if (!viewer.isValid())
        return UserTimeZone();

    return viewer.timezone();
}

bool isViewerAdmin(ServerDatabase *database,
                   MessengerType viewerMessenger,
                   const MessengerUserKey &currentUserKey)
{
    if (viewerMessenger == MessengerType::LocalCli)
        return true;

    if (!database || !database->userModel() || !currentUserKey.isValid())
        return false;

    const User viewer = database->userModel()->userByKey(currentUserKey);
    return viewer.isValid() && viewer.isAdmin();
}

User viewerUser(ServerDatabase *database, const MessengerUserKey &currentUserKey)
{
    if (!database || !database->userModel() || !currentUserKey.isValid())
        return User();

    return database->userModel()->userByKey(currentUserKey);
}

QString baseResourceName(const Resource &resource)
{
    const QString collapsed = MessengerTextRender::collapseWhitespace(resource.name());
    if (!collapsed.isEmpty())
        return collapsed;
    return MessengerTextRender::tr("(untitled)");
}

QString resourceStatusLabel(Resource::Status status)
{
    switch (status) {
    case Resource::Status::On:
        return MessengerTextRender::tr("On", "Resource status label");
    case Resource::Status::TemporarilyUnavailable:
        return MessengerTextRender::tr("TemporarilyUnavailable", "Resource status label");
    case Resource::Status::Off:
        return MessengerTextRender::tr("Off", "Resource status label");
    }
    return MessengerTextRender::tr("On", "Resource status label");
}

QString resourceNameWithStatus(const Resource &resource)
{
    return MessengerTextRender::tr("%1 [%2]").arg(baseResourceName(resource),
                                                  resourceStatusLabel(resource.status()));
}

QString formatUserGroupRolesPlain(const QMap<Group, User::Role> &roles)
{
    if (roles.isEmpty())
        return MessengerTextRender::tr("Root (all groups)");

    QStringList values;
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
        const QString role =
                it.value() == User::Role::FixedAdminRole
                        ? MessengerTextRender::tr("fixed admin", "User role label (fixed admin)")
                : it.value() == User::Role::AdminRole ? MessengerTextRender::tr("admin")
                                                      : MessengerTextRender::tr("user");
        values.append(
                QStringLiteral("%1=%2").arg(MessengerTextRender::formatGroupDisplayName(it.key(),
                                                                                        false),
                                            role));
    }
    return values.join(MessengerTextRender::tr(", "));
}

QString formatGroupsPlain(const QSet<Group> &groups)
{
    return MessengerTextRender::formatGroupList(groups, false);
}

QString formatUserRole(const User &user)
{
    return user.isFixedAdmin()
                   ? MessengerTextRender::tr("fixed admin", "User role label (fixed admin)")
           : user.isAdmin() ? MessengerTextRender::tr("admin", "User role label (admin)")
                            : MessengerTextRender::tr("user", "User role label (non-admin)");
}

using BlacklistLookup = QHash<MessengerUserKey, BlacklistEntry>;

QString formatUsersPlain(const QList<User> &users,
                         const BlacklistLookup &blacklist,
                         MessengerTextRender::UserListFlags flags,
                         const MessengerUserKey &currentUserKey,
                         const QString &dateTimeFormat,
                         const UserTimeZone &timeZone)
{
    if (users.isEmpty())
        return QString();

    QStringList lines;
    const bool showTitle = flags.testFlag(MessengerTextRender::ShowUserListTitle);
    const bool showIndex = flags.testFlag(MessengerTextRender::ShowUserIndex);
    const bool showUserKey = flags.testFlag(MessengerTextRender::ShowUserUserKey);
    const bool showRole = flags.testFlag(MessengerTextRender::ShowUserRole);
    const bool showBlacklistStatus = flags.testFlag(MessengerTextRender::ShowUserBlacklistStatus);
    const bool showUserIdLabel = flags.testFlag(MessengerTextRender::ShowUserIdLabel);
    const bool showRoleLabel = flags.testFlag(MessengerTextRender::ShowUserRoleLabel);
    const bool showGroups = flags.testFlag(MessengerTextRender::ShowUserGroups);

    if (showTitle) {
        lines << MessengerTextRender::tr("Users (%1):", "User list header (plain)")
                         .arg(users.size());
    }

    int index = 1;
    for (const User &user : users) {
        const bool isCurrentUser = currentUserKey.isValid() && user.key() == currentUserKey;

        const QString first = MessengerTextRender::collapseWhitespace(user.firstName());
        const QString last = MessengerTextRender::collapseWhitespace(user.lastName());
        const bool hasName = !first.isEmpty() || !last.isEmpty();
        const bool displayIsKey = !hasName && user.key().isValid();
        QString displayName = MessengerTextRender::formatUserDisplayName(user, false);

        const int currentIndex = index++;
        const QString role = showRole ? formatUserRole(user) : QString();
        const QString groups =
                showGroups ? formatUserGroupRolesPlain(user.groupRoles()) : QString();

        const QString userKeyDisplay =
                showUserKey ? formatUserKeyPlain(user.key(),
                                                 MessengerTextRender::tr(
                                                         "unknown", "Placeholder for missing user"))
                            : QString();

        const bool showUserKeyInDetails =
                showUserKey && !userKeyDisplay.trimmed().isEmpty() && !displayIsKey;

        displayName =
                appendCurrentUserSuffix(displayName, isCurrentUser && !showUserKeyInDetails, false);

        QString userKey = showUserKeyInDetails ? userKeyDisplay : QString();
        userKey = appendCurrentUserSuffix(userKey, isCurrentUser && showUserKeyInDetails, false);

        QString entryLine;
        if (showIndex && showUserKeyInDetails && showRole && showGroups && showUserIdLabel
            && showRoleLabel) {
            entryLine =
                    MessengerTextRender::tr("%1) %2 (id: %3, role: %4, groups: %5)",
                                            "Verbose user list entry (plain)")
                            .arg(QString::number(currentIndex), displayName, userKey, role, groups);
        } else if (showIndex && showUserKeyInDetails && showRole && showUserIdLabel
                   && showRoleLabel) {
            entryLine = MessengerTextRender::tr("%1) %2 (id: %3, role: %4)",
                                                "Verbose user list entry (plain)")
                                .arg(QString::number(currentIndex), displayName, userKey, role);
        } else if (showIndex && !showUserKey && !showRole) {
            entryLine = QStringLiteral("%1) %2").arg(QString::number(currentIndex), displayName);
        } else {
            const QString prefix =
                    showIndex ? QStringLiteral("%1) ").arg(QString::number(currentIndex))
                              : QString();
            entryLine = prefix + displayName;

            QStringList details;
            if (showUserKeyInDetails) {
                if (showUserIdLabel) {
                    details << MessengerTextRender::tr("id: %1", "User id label (plain)")
                                       .arg(userKey);
                } else {
                    details << userKey;
                }
            }
            if (showRole) {
                if (showRoleLabel) {
                    details << MessengerTextRender::tr("role: %1", "User role label (plain)")
                                       .arg(role);
                } else {
                    details << role;
                }
            }
            if (showGroups) {
                details << MessengerTextRender::tr("groups: %1", "User groups label (plain)")
                                   .arg(groups);
            }

            if (!details.isEmpty()) {
                entryLine += QStringLiteral(" (%1)").arg(details.join(QStringLiteral(", ")));
            }
        }
        lines << entryLine;

        if (showBlacklistStatus) {
            if (user.key().isValid()) {
                const BlacklistLookup::const_iterator it = blacklist.constFind(user.key());
                if (it != blacklist.cend()) {
                    lines << QStringLiteral("   %1").arg(
                            formatBlacklistStatusPlain(it.value(), dateTimeFormat, timeZone));
                }
            }
        }
    }

    return lines.join(QStringLiteral("\n"));
}

QString formatUsersHtml(const QList<User> &users,
                        const BlacklistLookup &blacklist,
                        MessengerTextRender::UserListFlags flags,
                        const MessengerUserKey &currentUserKey,
                        const QString &dateTimeFormat,
                        const UserTimeZone &timeZone)
{
    if (users.isEmpty())
        return QString();

    QStringList lines;
    const bool showTitle = flags.testFlag(MessengerTextRender::ShowUserListTitle);
    const bool showIndex = flags.testFlag(MessengerTextRender::ShowUserIndex);
    const bool showUserId = flags.testFlag(MessengerTextRender::ShowUserUserKey);
    const bool showRole = flags.testFlag(MessengerTextRender::ShowUserRole);
    const bool showBlacklistStatus = flags.testFlag(MessengerTextRender::ShowUserBlacklistStatus);
    const bool showGroups = flags.testFlag(MessengerTextRender::ShowUserGroups);

    if (showTitle) {
        const QString usersHeader =
                MessengerTextRender::tr("Users (%1)", "User list header (HTML)").arg(users.size());
        lines << QStringLiteral("<b>%1</b>").arg(escapeHtml(usersHeader));
    }

    int index = 1;
    for (const User &user : users) {
        const bool isCurrentUser = currentUserKey.isValid() && user.key() == currentUserKey;

        const QString first = MessengerTextRender::collapseWhitespace(user.firstName());
        const QString last = MessengerTextRender::collapseWhitespace(user.lastName());
        const bool hasName = !first.isEmpty() || !last.isEmpty();
        const bool displayIsKey = !hasName && user.key().isValid();
        const bool showUserIdInDetails = showUserId && user.key().isValid() && !displayIsKey;

        QString displayName = MessengerTextRender::formatUserDisplayName(user, true);
        if (!showUserIdInDetails) {
            displayName = appendCurrentUserSuffix(displayName, isCurrentUser, true);
        }

        const int currentIndex = index++;
        const QString idText =
                showUserIdInDetails
                        ? formatUserKeyHtml(user.key(),
                                            MessengerTextRender::tr(
                                                    "n/a", "Placeholder for missing user (HTML)"))
                        : QString();
        const QString role = showRole ? formatUserRole(user) : QString();
        const QString groups =
                showGroups ? formatUserGroupRolesPlain(user.groupRoles()) : QString();

        const QString marker =
                (showUserIdInDetails && isCurrentUser) ? currentUserSuffixHtml() : QString();

        bool hasExtraLine = false;

        if (showIndex && showUserIdInDetails && showRole && showGroups) {
            const QString groupsLabel = MessengerTextRender::tr("groups:");
            lines << QStringLiteral("<b>%1.</b> %2 %3%4 - <i>%5</i>; <i>%6</i> %7")
                             .arg(QString::number(currentIndex),
                                  displayName,
                                  idText,
                                  marker,
                                  escapeHtml(role),
                                  escapeHtml(groupsLabel),
                                  escapeHtml(groups));
            hasExtraLine = true;
        } else if (showIndex && showUserIdInDetails && showRole) {
            lines << QStringLiteral("<b>%1.</b> %2 %3%4 - <i>%5</i>")
                             .arg(QString::number(currentIndex),
                                  displayName,
                                  idText,
                                  marker,
                                  escapeHtml(role));
            hasExtraLine = true;
        } else if (showIndex && !showUserId && !showRole) {
            lines << QStringLiteral("<b>%1.</b> %2")
                             .arg(QString::number(currentIndex), displayName);
        } else {
            QString entryLine;
            if (showIndex) {
                entryLine = QStringLiteral("<b>%1.</b> %2")
                                    .arg(QString::number(currentIndex), displayName);
            } else {
                entryLine = displayName;
            }
            if (showUserIdInDetails) {
                entryLine += QStringLiteral(" %1").arg(idText);
                entryLine += marker;
            }
            if (showRole) {
                entryLine += QStringLiteral(" - <i>%1</i>").arg(escapeHtml(role));
            }
            if (showGroups) {
                const QString groupsLabel = MessengerTextRender::tr("Groups:");
                entryLine += QStringLiteral(" <i>%1</i> %2")
                                     .arg(escapeHtml(groupsLabel), escapeHtml(groups));
            }
            lines << entryLine;
        }

        if (showBlacklistStatus) {
            if (user.key().isValid()) {
                const BlacklistLookup::const_iterator it = blacklist.constFind(user.key());
                if (it != blacklist.cend()) {
                    lines << formatBlacklistStatusHtml(it.value(), dateTimeFormat, timeZone);
                    hasExtraLine = true;
                }
            }
        }
        if (hasExtraLine && index <= users.size())
            lines << QString();
    }

    return lines.join(QStringLiteral("\n"));
}

QString formatResourcesPlain(const QList<Resource> &resources,
                             MessengerTextRender::ResourceListFlags flags)
{
    if (resources.isEmpty())
        return QString();

    QStringList lines;
    const bool showTitle = flags.testFlag(MessengerTextRender::ShowResourceListTitle);
    const bool showDescription = flags.testFlag(MessengerTextRender::ShowResourceDescription);
    const bool showStartMessage = flags.testFlag(MessengerTextRender::ShowResourceStartMessage);
    const bool showDescriptionLabel =
            flags.testFlag(MessengerTextRender::ShowResourceDescriptionLabel);
    const bool showStartLabel = flags.testFlag(MessengerTextRender::ShowResourceStartLabel);
    const bool showStatus = flags.testFlag(MessengerTextRender::ShowResourceStatus);
    const bool showGroups = flags.testFlag(MessengerTextRender::ShowResourceGroups);

    if (showTitle) {
        lines << MessengerTextRender::tr("Resources (%1):").arg(resources.size());
    }

    int index = 1;
    for (const Resource &resource : resources) {
        QString name = showStatus
                               ? MessengerTextRender::formatResourceNameWithStatus(resource, false)
                               : baseResourceName(resource);

        lines << MessengerTextRender::tr("%1) %2").arg(QString::number(index++), name);

        if (showGroups) {
            lines << MessengerTextRender::tr("   Groups: %1")
                             .arg(formatGroupsPlain(resource.groups()));
        }

        if (showDescription) {
            QString description = MessengerTextRender::collapseWhitespace(resource.description());
            if (description.isEmpty())
                description = MessengerTextRender::tr("(empty)");
            if (showDescriptionLabel) {
                lines << MessengerTextRender::tr("   Description: %1").arg(description);
            } else {
                lines << QStringLiteral("   %1").arg(description);
            }
        }

        if (showStartMessage) {
            QString start = MessengerTextRender::collapseWhitespace(resource.startMessage());
            if (start.isEmpty())
                start = MessengerTextRender::tr("(empty)");
            if (showStartLabel) {
                lines << MessengerTextRender::tr("   Start: %1").arg(start);
            } else {
                lines << QStringLiteral("   %1").arg(start);
            }
        }

        if (index <= resources.size())
            lines << QString();
    }

    return lines.join(QStringLiteral("\n"));
}

QString formatResourcesHtml(const QList<Resource> &resources,
                            MessengerTextRender::ResourceListFlags flags)
{
    if (resources.isEmpty())
        return QString();

    QStringList lines;
    const bool showTitle = flags.testFlag(MessengerTextRender::ShowResourceListTitle);
    const bool showDescription = flags.testFlag(MessengerTextRender::ShowResourceDescription);
    const bool showStartMessage = flags.testFlag(MessengerTextRender::ShowResourceStartMessage);
    const bool showDescriptionLabel =
            flags.testFlag(MessengerTextRender::ShowResourceDescriptionLabel);
    const bool showStartLabel = flags.testFlag(MessengerTextRender::ShowResourceStartLabel);
    const bool showStatus = flags.testFlag(MessengerTextRender::ShowResourceStatus);
    const bool showGroups = flags.testFlag(MessengerTextRender::ShowResourceGroups);

    if (showTitle) {
        const QString header =
                MessengerTextRender::tr("Resources (%1)", "Resource list header (HTML)")
                        .arg(resources.size());
        lines << QStringLiteral("<b>%1</b>").arg(escapeHtml(header));
    }

    int index = 1;
    for (const Resource &resource : resources) {
        QString name = showStatus
                               ? MessengerTextRender::formatResourceNameWithStatus(resource, true)
                               : escapeHtml(baseResourceName(resource));

        lines << QStringLiteral("<b>%1.</b> %2").arg(QString::number(index++), name);

        if (showGroups) {
            const QString groupsLabel = MessengerTextRender::tr("Groups:");
            lines << QStringLiteral("<i>%1</i> %2")
                             .arg(escapeHtml(groupsLabel),
                                  escapeHtml(formatGroupsPlain(resource.groups())));
        }

        if (showDescription) {
            QString descriptionHtml;
            if (resource.description().trimmed().isEmpty()) {
                const QString noDescription =
                        MessengerTextRender::tr("No description",
                                                "Placeholder for missing resource description");
                descriptionHtml = QStringLiteral("<i>%1</i>").arg(escapeHtml(noDescription));
            } else {
                descriptionHtml =
                        escapeHtml(MessengerTextRender::normalizeNewlines(resource.description()));
            }

            if (showDescriptionLabel) {
                const QString descriptionLabel = MessengerTextRender::tr("Description:");
                lines << QStringLiteral("<i>%1</i> %2")
                                 .arg(escapeHtml(descriptionLabel), descriptionHtml);
            } else {
                lines << descriptionHtml;
            }
        }

        if (showStartMessage) {
            QString startBlock;
            if (resource.startMessage().trimmed().isEmpty()) {
                const QString noStart =
                        MessengerTextRender::tr("No start message",
                                                "Placeholder for missing start instructions");
                startBlock = QStringLiteral("<i>%1</i>").arg(escapeHtml(noStart));
            } else {
                startBlock = QStringLiteral("<pre><code>%1</code></pre>")
                                     .arg(escapeHtml(MessengerTextRender::normalizeNewlines(
                                             resource.startMessage())));
            }

            if (showStartLabel) {
                const QString startLabel = MessengerTextRender::tr("Start:");
                lines << QStringLiteral("<i>%1</i>").arg(escapeHtml(startLabel));
            }
            lines << startBlock;
        }
        lines << QString();
    }

    return lines.join(QStringLiteral("\n"));
}

QString formatBlacklistEntriesPlain(const QList<BlacklistEntry> &entries,
                                    ServerDatabase *database,
                                    const MessengerUserKey &currentUserKey,
                                    const UserTimeZone &timeZone)
{
    if (entries.isEmpty())
        return MessengerTextRender::tr("Blacklist is empty.");

    QHash<MessengerUserKey, User> userCache;
    const QString dateTimeFormat = ServerDateTimeFormat::effectiveFormat(database);
    const auto lookupUserCached = [&](const MessengerUserKey &key) -> User {
        const QHash<MessengerUserKey, User>::const_iterator it = userCache.constFind(key);
        if (it != userCache.cend())
            return it.value();
        const User user = lookupActiveAccessUser(database, key);
        userCache.insert(key, user);
        return user;
    };

    QStringList lines;
    lines << MessengerTextRender::tr("Blacklisted users (%1):").arg(entries.size());

    int index = 1;
    for (const BlacklistEntry &entry : entries) {
        const bool isCurrentUser = currentUserKey.isValid() && entry.user() == currentUserKey;
        const QString displayUserKey =
                formatUserKeyPlain(entry.user(),
                                   MessengerTextRender::tr("unknown",
                                                           "Placeholder for missing user"));
        const User blockedUser = lookupUserCached(entry.user());
        QString displayUser = blockedUser.isValid()
                                      ? MessengerTextRender::formatUserLine(blockedUser, false)
                                      : displayUserKey;
        displayUser = appendCurrentUserSuffix(displayUser, isCurrentUser, false);
        lines << QStringLiteral("%1) %2").arg(QString::number(index++), displayUser);
        lines << QStringLiteral("   %1").arg(
                formatBlacklistStatusPlain(entry, dateTimeFormat, timeZone));

        if (entry.blockedBy().isValid()) {
            const User blockedByUser = lookupUserCached(entry.blockedBy());
            const bool isCurrentBlockedBy =
                    currentUserKey.isValid() && entry.blockedBy() == currentUserKey;
            const bool canShowBlocker =
                    isCurrentBlockedBy
                    || (database && database->canManageServerSettings(currentUserKey))
                    || (database && blockedByUser.isValid()
                        && database->canAdministerUser(currentUserKey, blockedByUser));
            QString blockedByDisplay =
                    canShowBlocker
                            ? (blockedByUser.isValid()
                                       ? MessengerTextRender::formatUserLine(blockedByUser, false)
                                       : entry.blockedBy().toString())
                            : MessengerTextRender::tr("another administrator");
            if (canShowBlocker) {
                blockedByDisplay =
                        appendCurrentUserSuffix(blockedByDisplay, isCurrentBlockedBy, false);
            }
            lines << MessengerTextRender::tr("   By: %1").arg(blockedByDisplay);
        }

        lines << QString();
    }

    return lines.join(QStringLiteral("\n")).trimmed();
}

QString formatBlacklistEntriesHtml(const QList<BlacklistEntry> &entries,
                                   ServerDatabase *database,
                                   const MessengerUserKey &currentUserKey,
                                   const UserTimeZone &timeZone)
{
    if (entries.isEmpty()) {
        const QString emptyText = MessengerTextRender::tr("Blacklist is empty.",
                                                          "Empty blacklist placeholder (HTML)");
        return QStringLiteral("<i>%1</i>").arg(escapeHtml(emptyText));
    }

    QHash<MessengerUserKey, User> userCache;
    const QString dateTimeFormat = ServerDateTimeFormat::effectiveFormat(database);
    const auto lookupUserCached = [&](const MessengerUserKey &key) -> User {
        const QHash<MessengerUserKey, User>::const_iterator it = userCache.constFind(key);
        if (it != userCache.cend())
            return it.value();
        const User user = lookupActiveAccessUser(database, key);
        userCache.insert(key, user);
        return user;
    };

    QStringList lines;
    const QString header =
            MessengerTextRender::tr("Blacklisted users (%1)", "Blacklist header (HTML)")
                    .arg(entries.size());
    lines << QStringLiteral("<b>%1</b>").arg(escapeHtml(header));

    int index = 1;
    for (const BlacklistEntry &entry : entries) {
        const bool isCurrentUser = currentUserKey.isValid() && entry.user() == currentUserKey;
        const QString marker = isCurrentUser ? currentUserSuffixHtml() : QString();
        const QString displayUserKey =
                formatUserKeyHtml(entry.user(),
                                  MessengerTextRender::tr("n/a",
                                                          "Placeholder for missing user (HTML)"));
        const User blockedUser = lookupUserCached(entry.user());
        const QString displayUser = blockedUser.isValid()
                                            ? MessengerTextRender::formatUserLine(blockedUser, true)
                                            : displayUserKey;
        lines << QStringLiteral("<b>%1.</b> %2%3")
                         .arg(QString::number(index++), displayUser, marker);
        lines << formatBlacklistStatusHtml(entry, dateTimeFormat, timeZone);

        if (entry.blockedBy().isValid()) {
            const User blockedByUser = lookupUserCached(entry.blockedBy());
            const bool isCurrentBlockedBy =
                    currentUserKey.isValid() && entry.blockedBy() == currentUserKey;
            const QString blockedByMarker =
                    isCurrentBlockedBy ? currentUserSuffixHtml() : QString();
            const QString idText = formatUserKeyHtml(
                    entry.blockedBy(),
                    MessengerTextRender::tr("n/a", "Placeholder for missing user (HTML)"));
            const QString byLabel = MessengerTextRender::tr("By:");
            const bool canShowBlocker =
                    isCurrentBlockedBy
                    || (database && database->canManageServerSettings(currentUserKey))
                    || (database && blockedByUser.isValid()
                        && database->canAdministerUser(currentUserKey, blockedByUser));
            const QString blockedByDisplay =
                    canShowBlocker
                            ? (blockedByUser.isValid()
                                       ? MessengerTextRender::formatUserLine(blockedByUser, true)
                                       : idText)
                            : escapeHtml(MessengerTextRender::tr("another administrator"));
            lines << QStringLiteral("<i>%1</i> %2%3")
                             .arg(escapeHtml(byLabel),
                                  blockedByDisplay,
                                  canShowBlocker ? blockedByMarker : QString());
        }

        lines << QString();
    }

    return lines.join(QStringLiteral("\n")).trimmed();
}

QString formatPendingAccessUsersPlain(const QList<PendingAccessUserEntry> &entries,
                                      const BlacklistLookup &blacklist,
                                      const MessengerUserKey &currentUserKey)
{
    constexpr int kRecentPendingAccessLimit = 100;
    constexpr int kPendingAccessRetentionDays = 3;

    if (entries.isEmpty())
        return MessengerTextRender::tr("No users awaiting access activation recorded recently.");

    QStringList lines;
    lines << MessengerTextRender::tr(
                     "Users awaiting access activation in the last %1 day(s) (up to %2 entries):")
                     .arg(QString::number(kPendingAccessRetentionDays),
                          QString::number(kRecentPendingAccessLimit));

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    int index = 1;
    for (const PendingAccessUserEntry &entry : entries) {
        const bool isCurrentUser = currentUserKey.isValid() && entry.user() == currentUserKey;
        const QString displayName =
                MessengerTextRender::formatPendingAccessUserDisplayName(entry, false);

        const QString idx = QString::number(index++);
        QString line =
                MessengerTextRender::tr("%1) [%2] %3")
                        .arg(idx, messengerTypeDisplayName(entry.user().messenger()), displayName);
        line = appendCurrentUserSuffix(line, isCurrentUser, false);
        if (entry.lastMessageAtUtc().isValid()) {
            const qint64 secondsAgo = entry.lastMessageAtUtc().secsTo(nowUtc);
            line += MessengerTextRender::tr(" - last seen %1 ago")
                            .arg(describeRelativeInterval(secondsAgo));
        }

        if (blacklist.contains(entry.user())) {
            line += MessengerTextRender::tr(" - blacklisted");
        }

        lines << line;
    }

    return lines.join(QStringLiteral("\n"));
}

QString formatPendingAccessUsersHtml(const QList<PendingAccessUserEntry> &entries,
                                     const BlacklistLookup &blacklist,
                                     const MessengerUserKey &currentUserKey)
{
    constexpr int kRecentPendingAccessLimit = 100;
    constexpr int kPendingAccessRetentionDays = 3;

    if (entries.isEmpty()) {
        const QString emptyText =
                MessengerTextRender::tr("No users awaiting access activation recorded recently.");
        return QStringLiteral("<b>%1</b>").arg(escapeHtml(emptyText));
    }

    QStringList lines;
    const QString heading =
            MessengerTextRender::tr(
                    "Users awaiting access activation in the last %1 day(s) (up to %2 entries):")
                    .arg(QString::number(kPendingAccessRetentionDays),
                         QString::number(kRecentPendingAccessLimit));
    lines << QStringLiteral("<b>%1</b>").arg(escapeHtml(heading));

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    int index = 1;
    for (const PendingAccessUserEntry &entry : entries) {
        const bool isCurrentUser = currentUserKey.isValid() && entry.user() == currentUserKey;
        const QString displayName =
                MessengerTextRender::formatPendingAccessUserDisplayName(entry, true);

        const QString idx = QString::number(index++);
        QString item = QStringLiteral("<b>%1)</b> [%2] %3")
                               .arg(idx,
                                    escapeHtml(messengerTypeDisplayName(entry.user().messenger())),
                                    displayName);
        item = appendCurrentUserSuffix(item, isCurrentUser, true);
        if (entry.lastMessageAtUtc().isValid()) {
            const qint64 secondsAgo = entry.lastMessageAtUtc().secsTo(nowUtc);
            const QString lastSeenText = MessengerTextRender::tr("last seen %1 ago")
                                                 .arg(describeRelativeInterval(secondsAgo));
            item += QStringLiteral(" - <i>%1</i>").arg(escapeHtml(lastSeenText));
        }
        if (blacklist.contains(entry.user())) {
            const QString badge = MessengerTextRender::tr("blacklisted");
            item += QStringLiteral(" - <i>%1</i>").arg(escapeHtml(badge));
        }
        lines << item;
    }

    return lines.join(QStringLiteral("\n"));
}

QString formatFreeResourcesMessage(QList<QString> resources,
                                   bool useHtml,
                                   MessengerTextRender::ResourceListFlags flags)
{
    QCollator collator;
    collator.setNumericMode(true);

    std::sort(resources.begin(),
              resources.end(),
              [&collator](const QString &lhs, const QString &rhs) {
                  return collator.compare(lhs, rhs) < 0;
              });

    if (resources.isEmpty()) {
        const QString emptyText =
                MessengerTextRender::tr("No free resources right now.",
                                        useHtml ? "Free resources empty placeholder (HTML)"
                                                : "Free resources empty placeholder (plain)");
        if (useHtml) {
            return QStringLiteral("<i>%1</i>").arg(escapeHtml(emptyText));
        }
        return emptyText;
    }

    QStringList lines;
    const bool showTitle = flags.testFlag(MessengerTextRender::ShowResourceListTitle);
    if (useHtml) {
        if (showTitle) {
            const QString header =
                    MessengerTextRender::tr("Free resources (%1)", "Free resources header (HTML)")
                            .arg(resources.size());
            lines << QStringLiteral("<b>%1</b>").arg(escapeHtml(header));
        }
    } else {
        if (showTitle)
            lines << MessengerTextRender::tr("Free resources (%1):").arg(resources.size());
    }

    int index = 1;
    for (const QString &resource : resources) {
        if (useHtml) {
            lines << QStringLiteral("<b>%1.</b> %2")
                             .arg(QString::number(index++), escapeHtml(resource));
        } else {
            lines << QStringLiteral("%1) %2").arg(QString::number(index++), resource);
        }
    }

    return lines.join(QStringLiteral("\n"));
}

QString formatSoonestResourcesMessage(const QList<ResourceQueueEntry> &entries,
                                      ServerDatabase *database,
                                      bool useHtml,
                                      MessengerTextRender::ResourceListFlags flags,
                                      const MessengerUserKey &currentUserKey,
                                      const UserTimeZone &timeZone)
{
    if (entries.isEmpty()) {
        const QString emptyText =
                MessengerTextRender::tr("No predictable release windows.",
                                        useHtml ? "Soonest resources empty placeholder (HTML)"
                                                : "Soonest resources empty placeholder (plain)");
        if (useHtml) {
            return QStringLiteral("<i>%1</i>").arg(escapeHtml(emptyText));
        }
        return emptyText;
    }

    QStringList lines;
    const bool showTitle = flags.testFlag(MessengerTextRender::ShowResourceListTitle);
    if (useHtml) {
        if (showTitle) {
            const QString header =
                    MessengerTextRender::tr("Fastest to release", "Soonest header (HTML)");
            lines << QStringLiteral("<b>%1</b>").arg(escapeHtml(header));
        }
    } else {
        if (showTitle)
            lines << MessengerTextRender::tr("Fastest to release:");
    }

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    const QString dateTimeFormat = ServerDateTimeFormat::effectiveFormat(database);
    const bool showStatus = flags.testFlag(MessengerTextRender::ShowResourceStatus);
    const bool hideOffResources = flags.testFlag(MessengerTextRender::HideOffResources);
    QHash<MessengerUserKey, QString> userDisplayCache;
    const ResourceModel *resourceModel = database ? database->resourceModel() : nullptr;
    const bool viewerAdmin =
            database ? isViewerAdmin(database, currentUserKey.messenger(), currentUserKey) : false;
    const User viewer = viewerUser(database, currentUserKey);
    const auto formatUserDisplayCached = [&](const MessengerUserKey &userKey) -> QString {
        const QHash<MessengerUserKey, QString>::const_iterator it =
                userDisplayCache.constFind(userKey);
        if (it != userDisplayCache.cend())
            return it.value();
        const User user = (database && database->userModel())
                                  ? database->userModel()->userByKey(userKey)
                                  : User();
        const QString display = MessengerTextRender::formatUserDisplayName(user, useHtml);
        userDisplayCache.insert(userKey, display);
        return display;
    };
    const auto formatResourceName = [&](const ResourceQueueEntry &entry,
                                        bool *visibleOut) -> QString {
        if (visibleOut)
            *visibleOut = true;

        if (!resourceModel)
            return MessengerTextRender::tr("(unknown)");

        const Resource resource = resourceModel->resourceByUuid(entry.resourceUuid());
        if (!resource.isValid())
            return MessengerTextRender::tr("(unknown)");

        if (currentUserKey.isValid()
            && !MessengerTextRender::isResourceVisibleToViewer(resource, viewer, viewerAdmin)) {
            if (visibleOut)
                *visibleOut = false;
            return QString();
        }

        if (hideOffResources && resource.status() != Resource::Status::On) {
            if (visibleOut)
                *visibleOut = false;
            return QString();
        }

        if (showStatus)
            return MessengerTextRender::formatResourceNameWithStatus(resource, false);
        return baseResourceName(resource);
    };

    int index = 1;
    bool hasVisibleEntries = false;
    for (const ResourceQueueEntry &entry : entries) {
        bool visible = true;
        const QString resourceName = formatResourceName(entry, &visible);
        if (!visible)
            continue;

        hasVisibleEntries = true;
        const bool isCurrentUser = currentUserKey.isValid() && entry.user() == currentUserKey;
        const QString occupant = appendCurrentUserSuffix(formatUserDisplayCached(entry.user()),
                                                         isCurrentUser,
                                                         useHtml);
        QString eta;

        QDateTime handoverUtc = entry.endAtUtc().toUTC();

        if (!handoverUtc.isValid()) {
            eta = MessengerTextRender::tr("ETA unknown");
        } else {
            const qint64 delta = nowUtc.secsTo(handoverUtc);
            const QString handoverLocal = MessengerTextRender::formatLocalDateTime(handoverUtc,
                                                                                   QString(),
                                                                                   dateTimeFormat,
                                                                                   timeZone);
            if (delta >= 0) {
                eta = MessengerTextRender::tr("ETA %1 (in %2)")
                              .arg(handoverLocal, describeRelativeInterval(delta));
            } else {
                eta = MessengerTextRender::tr("ETA passed %1 (%2 ago)")
                              .arg(handoverLocal, describeRelativeInterval(delta));
            }
        }

        if (useHtml) {
            lines << QStringLiteral("<b>%1.</b> %2 - %3")
                             .arg(QString::number(index++), escapeHtml(resourceName), occupant);
            lines << QStringLiteral("<i>%1</i>").arg(escapeHtml(eta));
        } else {
            lines << QStringLiteral("%1) %2 - %3")
                             .arg(QString::number(index++), resourceName, occupant);
            lines << QStringLiteral("   %1").arg(eta);
        }
        lines << QString();
    }

    if (!hasVisibleEntries) {
        const QString emptyText =
                MessengerTextRender::tr("No predictable release windows.",
                                        useHtml ? "Soonest resources empty placeholder (HTML)"
                                                : "Soonest resources empty placeholder (plain)");
        if (useHtml)
            return QStringLiteral("<i>%1</i>").arg(escapeHtml(emptyText));
        return emptyText;
    }

    return lines.join(QStringLiteral("\n"));
}

QString formatResourceQueueStatusPlain(ServerDatabase *database,
                                       const QString &resourceName,
                                       const QString &resourceDisplayName,
                                       const MessengerUserKey &currentUserKey,
                                       const UserTimeZone &timeZone)
{
    const QString normalizedResource =
            MessengerTextRender::collapseWhitespace(resourceDisplayName).isEmpty()
                    ? MessengerTextRender::tr("(unknown)")
                    : MessengerTextRender::collapseWhitespace(resourceDisplayName);

    const ResourceQueueModel *queueModel = database ? database->resourceQueueModel() : nullptr;
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    const QString dateTimeFormat = ServerDateTimeFormat::effectiveFormat(database);
    const ResourceQueueEntry occupant =
            queueModel ? queueModel->currentOccupantForResource(resourceName)
                       : ResourceQueueEntry();
    const QList<ResourceQueueEntry> schedules =
            queueModel ? queueModel->schedulesForResource(resourceName)
                       : QList<ResourceQueueEntry>();
    QHash<MessengerUserKey, QString> userDisplayCache;
    const auto formatUserDisplayCached = [&](const MessengerUserKey &userKey) -> QString {
        const QHash<MessengerUserKey, QString>::const_iterator it =
                userDisplayCache.constFind(userKey);
        if (it != userDisplayCache.cend())
            return it.value();
        const User user = (database && database->userModel())
                                  ? database->userModel()->userByKey(userKey)
                                  : User();
        const QString display = MessengerTextRender::formatUserDisplayName(user, false);
        userDisplayCache.insert(userKey, display);
        return display;
    };

    QStringList lines;
    lines << MessengerTextRender::tr("Status for '%1':").arg(normalizedResource);

    if (!occupant.isValid()) {
        lines << MessengerTextRender::tr("Currently: free");
    } else {
        const bool isCurrentUser = currentUserKey.isValid() && occupant.user() == currentUserKey;
        const QString display = appendCurrentUserSuffix(formatUserDisplayCached(occupant.user()),
                                                        isCurrentUser,
                                                        false);
        lines << MessengerTextRender::tr("Currently: occupied - %1").arg(display);
        const QString startLocal = MessengerTextRender::formatLocalDateTime(occupant.startAtUtc(),
                                                                            QString(),
                                                                            dateTimeFormat,
                                                                            timeZone);
        const QString endLocal =
                MessengerTextRender::formatLocalDateTime(occupant.endAtUtc(),
                                                         MessengerTextRender::formatDurationMinutes(
                                                                 0),
                                                         dateTimeFormat,
                                                         timeZone);
        lines << MessengerTextRender::tr("Window: %1 → %2").arg(startLocal, endLocal);
    }

    lines << QString();

    if (schedules.isEmpty()) {
        lines << MessengerTextRender::tr("Schedule: empty");
    } else {
        lines << MessengerTextRender::tr("Schedule (%1):").arg(QString::number(schedules.size()));
        int index = 1;
        for (const ResourceQueueEntry &entry : schedules) {
            const bool isCurrentUser = currentUserKey.isValid() && entry.user() == currentUserKey;
            const QString display = appendCurrentUserSuffix(formatUserDisplayCached(entry.user()),
                                                            isCurrentUser,
                                                            false);
            const bool waitsForRelease = entry.waitsForRelease();
            const QString startLocal =
                    waitsForRelease ? MessengerTextRender::tr("when the resource is released")
                                    : MessengerTextRender::formatLocalDateTime(entry.startAtUtc(),
                                                                               QString(),
                                                                               dateTimeFormat,
                                                                               timeZone);
            const QString endLocal =
                    waitsForRelease ? MessengerTextRender::formatDurationMinutes(
                                              entry.requestedDurationMinutes())
                                    : MessengerTextRender::formatLocalDateTime(
                                              entry.endAtUtc(),
                                              MessengerTextRender::formatDurationMinutes(0),
                                              dateTimeFormat,
                                              timeZone);
            const QString status =
                    waitsForRelease
                            ? MessengerTextRender::tr("waiting")
                            : (entry.isActiveAtUtc(nowUtc) ? MessengerTextRender::tr("active")
                                                           : MessengerTextRender::tr("upcoming"));

            lines << QStringLiteral("%1) %2").arg(QString::number(index++), display);
            lines << QStringLiteral("   %1").arg(
                    MessengerTextRender::tr("Window: %1 → %2 | Status: %3")
                            .arg(startLocal, endLocal, status));
        }
    }

    return lines.join(QStringLiteral("\n")).trimmed();
}

QString formatResourceQueueStatusHtml(ServerDatabase *database,
                                      const QString &resourceName,
                                      const QString &resourceDisplayName,
                                      const MessengerUserKey &currentUserKey,
                                      const UserTimeZone &timeZone)
{
    const QString normalizedResource = resourceDisplayName.trimmed().isEmpty()
                                               ? MessengerTextRender::tr("(unknown)")
                                               : resourceDisplayName.trimmed();

    const ResourceQueueModel *queueModel = database ? database->resourceQueueModel() : nullptr;
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    const QString dateTimeFormat = ServerDateTimeFormat::effectiveFormat(database);
    const ResourceQueueEntry occupant =
            queueModel ? queueModel->currentOccupantForResource(resourceName)
                       : ResourceQueueEntry();
    const QList<ResourceQueueEntry> schedules =
            queueModel ? queueModel->schedulesForResource(resourceName)
                       : QList<ResourceQueueEntry>();
    QHash<MessengerUserKey, QString> userDisplayCache;
    const auto formatUserDisplayCached = [&](const MessengerUserKey &userKey) -> QString {
        const QHash<MessengerUserKey, QString>::const_iterator it =
                userDisplayCache.constFind(userKey);
        if (it != userDisplayCache.cend())
            return it.value();
        const User user = (database && database->userModel())
                                  ? database->userModel()->userByKey(userKey)
                                  : User();
        const QString display = MessengerTextRender::formatUserDisplayName(user, true);
        userDisplayCache.insert(userKey, display);
        return display;
    };

    QStringList lines;
    lines << QStringLiteral("<b>%1</b>")
                     .arg(escapeHtml(
                             MessengerTextRender::tr("Status for '%1':").arg(normalizedResource)));

    if (!occupant.isValid()) {
        lines << QStringLiteral("<i>%1</i>")
                         .arg(escapeHtml(MessengerTextRender::tr("Currently: free")));
    } else {
        const bool isCurrentUser = currentUserKey.isValid() && occupant.user() == currentUserKey;
        const QString display = appendCurrentUserSuffix(formatUserDisplayCached(occupant.user()),
                                                        isCurrentUser,
                                                        true);
        lines << QStringLiteral("<i>%1</i> %2")
                         .arg(escapeHtml(MessengerTextRender::tr("Currently: occupied -")),
                              display);
    }

    lines << QString();

    const auto fmtLocal = [&dateTimeFormat, &timeZone](const QDateTime &dt,
                                                       const QString &invalidText = QString()) {
        return MessengerTextRender::formatLocalDateTime(dt, invalidText, dateTimeFormat, timeZone);
    };

    if (schedules.isEmpty()) {
        lines << QStringLiteral("<i>%1</i>")
                         .arg(escapeHtml(MessengerTextRender::tr("Schedule: empty")));
    } else {
        lines << QStringLiteral("<b>%1</b>")
                         .arg(escapeHtml(
                                 MessengerTextRender::tr("Schedule (%1):").arg(schedules.size())));
        int idx = 1;
        for (const ResourceQueueEntry &entry : schedules) {
            const bool isCurrentUser = currentUserKey.isValid() && entry.user() == currentUserKey;
            const QString display = appendCurrentUserSuffix(formatUserDisplayCached(entry.user()),
                                                            isCurrentUser,
                                                            true);
            lines << QStringLiteral("<b>%1.</b> %2").arg(QString::number(idx++), display);
            const bool waitsForRelease = entry.waitsForRelease();
            const QString status =
                    waitsForRelease
                            ? MessengerTextRender::tr("waiting")
                            : (entry.isActiveAtUtc(nowUtc) ? MessengerTextRender::tr("active")
                                                           : MessengerTextRender::tr("upcoming"));
            const QString startLocal =
                    waitsForRelease ? MessengerTextRender::tr("when the resource is released")
                                    : fmtLocal(entry.startAtUtc());
            const QString endLocal =
                    waitsForRelease ? MessengerTextRender::formatDurationMinutes(
                                              entry.requestedDurationMinutes())
                                    : fmtLocal(entry.endAtUtc(),
                                               MessengerTextRender::formatDurationMinutes(0));
            lines << QStringLiteral("<i>%1</i> <code>%2</code> → <code>%3</code> (<i>%4</i>)")
                             .arg(escapeHtml(MessengerTextRender::tr("Window:")),
                                  escapeHtml(startLocal),
                                  escapeHtml(endLocal),
                                  escapeHtml(status));
            lines << QString();
        }
    }

    return lines.join(QStringLiteral("\n")).trimmed();
}
} // namespace

QString MessengerTextRender::collapseWhitespace(const QString &text)
{
    QString collapsed = text;
    collapsed.replace(QRegularExpression(QStringLiteral("[\\r\\n]+")), QStringLiteral(" "));
    return collapsed.trimmed();
}

QString MessengerTextRender::normalizeNewlines(const QString &text)
{
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return normalized;
}

QString MessengerTextRender::formatUserAlertMessage(const QString &message, bool html)
{
    const QString normalized = normalizeNewlines(message).trimmed();
    if (normalized.isEmpty())
        return QString();

    const QString title = tr("Alert:");
    QString result = html ? QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped()) : title;
    result.append(QLatin1Char('\n'));
    result.append(html ? normalized.toHtmlEscaped() : normalized);
    return result;
}

QString MessengerTextRender::describeBlacklistWindow(const BlacklistEntry &entry,
                                                     const QString &dateTimeFormat,
                                                     const UserTimeZone &timeZone)
{
    if (entry.isPermanent() || !entry.blockedUntilUtc().isValid())
        return tr("permanently");

    const QDateTime untilUtc = entry.blockedUntilUtc();
    const QString localTime = formatLocalDateTime(untilUtc, QString(), dateTimeFormat, timeZone);
    const qint64 delta = QDateTime::currentDateTimeUtc().secsTo(untilUtc);
    const QString relative = describeRelativeInterval(delta);
    if (delta >= 0) {
        return tr("until %1 (%2)").arg(localTime, relative);
    }
    return tr("until %1").arg(localTime);
}

QString MessengerTextRender::formatDurationMinutes(int minutes)
{
    if (minutes <= 0)
        return tr("indefinite");

    const int hours = minutes / 60;
    const int mins = minutes % 60;
    QStringList parts;
    if (hours > 0) {
        parts << tr("%1 h").arg(hours);
    }
    if (mins > 0) {
        parts << tr("%1 min").arg(mins);
    }
    if (parts.isEmpty()) {
        parts << tr("less than 1 min");
    }
    return parts.join(QStringLiteral(" "));
}

QString MessengerTextRender::formatResourceNameWithStatus(const Resource &resource, bool html)
{
    const QString collapsedName = collapseWhitespace(resource.name());
    if (!resource.isValid() && collapsedName.isEmpty()) {
        const QString unknown = tr("(unknown)");
        return html ? escapeHtml(unknown) : unknown;
    }

    const QString text = resourceNameWithStatus(resource);
    return html ? escapeHtml(text) : text;
}

bool MessengerTextRender::isResourceVisibleToViewer(const Resource &resource, bool viewerIsAdmin)
{
    if (!resource.isValid())
        return false;
    if (!viewerIsAdmin && resource.status() == Resource::Status::Off)
        return false;
    return true;
}

bool MessengerTextRender::isResourceVisibleToViewer(const Resource &resource,
                                                    const User &viewer,
                                                    bool viewerIsAdmin)
{
    if (!isResourceVisibleToViewer(resource, viewerIsAdmin))
        return false;

    if (!viewer.isValid())
        return viewerIsAdmin;

    if (viewer.isFixedAdmin())
        return true;

    const QSet<Group> resourceGroups = resource.groups();
    for (const Group &group : resourceGroups) {
        if (viewer.hasAccessToGroup(group))
            return true;
    }

    return false;
}

bool MessengerTextRender::isResourceVisibleToViewer(const Resource &resource,
                                                    ServerDatabase *database,
                                                    MessengerType viewerMessenger,
                                                    const MessengerUserKey &currentUserKey)
{
    const bool viewerAdmin = isViewerAdmin(database, viewerMessenger, currentUserKey);
    const User viewer = viewerUser(database, currentUserKey);
    return isResourceVisibleToViewer(resource, viewer, viewerAdmin);
}

QStringList MessengerTextRender::currentOccupationResourceNames(ServerDatabase *database,
                                                                MessengerType messenger,
                                                                const MessengerUserKey &user)
{
    QStringList resourceNames;
    if (!database || !database->isReady() || !user.isValid())
        return resourceNames;

    const ResourceModel *resourceModel = database->resourceModel();
    const ResourceQueueModel *queueModel = database->resourceQueueModel();
    if (!resourceModel || !queueModel)
        return resourceNames;

    const QList<Resource> resources = resourceModel->items();
    resourceNames.reserve(resources.size());
    for (const Resource &resource : resources) {
        if (!isResourceVisibleToViewer(resource, database, messenger, user))
            continue;

        const QString resourceName = resource.name().trimmed();
        if (resourceName.isEmpty())
            continue;

        const ResourceQueueEntry occupant = queueModel->currentOccupantForResource(resourceName);
        if (occupant.isValid() && occupant.user() == user)
            resourceNames.append(resourceName);
    }

    QCollator collator;
    collator.setNumericMode(true);
    std::sort(resourceNames.begin(),
              resourceNames.end(),
              [&collator](const QString &lhs, const QString &rhs) {
                  return collator.compare(lhs, rhs) < 0;
              });
    return resourceNames;
}

QString MessengerTextRender::formatReleasedOccupationReply(ServerDatabase *database,
                                                           const QString &resourceName,
                                                           const MessengerUserKey &actor)
{
    const ResourceQueueEntry next =
            (database && database->resourceQueueModel())
                    ? database->resourceQueueModel()->currentOccupantForResource(resourceName)
                    : ResourceQueueEntry();
    if (next.isValid()) {
        const User nextUser = (database && database->userModel())
                                      ? database->userModel()->userByKey(next.user())
                                      : User();
        QString nextDisplay = formatUserDisplayName(nextUser, false);
        nextDisplay = appendCurrentUserSuffix(nextDisplay,
                                              actor.isValid() && next.user() == actor,
                                              false);
        return tr("Resource '%1' released. Now occupied by: %2.").arg(resourceName, nextDisplay);
    }

    return tr("Resource '%1' is now free.").arg(resourceName);
}

QString MessengerTextRender::formatLocalDateTime(const QDateTime &dt,
                                                 const QString &invalidText,
                                                 const QString &dateTimeFormat,
                                                 const UserTimeZone &timeZone)
{
    return ServerDateTimeFormat::formatLocalDateTime(dt, dateTimeFormat, invalidText, timeZone);
}

QString MessengerTextRender::formatUserDisplayName(const User &user, bool html)
{
    const QString first = collapseWhitespace(user.firstName());
    const QString last = collapseWhitespace(user.lastName());
    const QString name = QStringLiteral("%1 %2").arg(first, last).trimmed();

    if (!name.isEmpty()) {
        if (html) {
            return wrapUserLinkHtml(user.key(), escapeHtml(name));
        }
        return name;
    }

    if (user.key().isValid()) {
        const QString keyText = user.key().toString();
        if (html)
            return formatUserKeyHtml(user.key(), tr("n/a", "Placeholder for missing user (HTML)"));
        return keyText;
    }

    const QString noName = html ? tr("No name", "Placeholder for missing user name (HTML)")
                                : tr("No name", "Placeholder for missing user name (plain)");
    if (html) {
        return QStringLiteral("<i>%1</i>").arg(escapeHtml(noName));
    }
    return QStringLiteral("<%1>").arg(noName);
}

QString MessengerTextRender::formatPendingAccessUserDisplayName(const PendingAccessUserEntry &entry,
                                                                bool html)
{
    const QString first = collapseWhitespace(entry.firstName());
    const QString last = collapseWhitespace(entry.lastName());
    const QString name = QStringLiteral("%1 %2").arg(first, last).trimmed();

    if (!name.isEmpty()) {
        if (html)
            return wrapUserLinkHtml(entry.user(), escapeHtml(name));
        return name;
    }

    if (entry.user().isValid()) {
        const QString keyText = entry.user().toString();
        if (html) {
            return formatUserKeyHtml(entry.user(),
                                     tr("n/a", "Placeholder for missing user (HTML)"));
        }
        return keyText;
    }

    const QString noName = html ? tr("No name", "Placeholder for missing user name (HTML)")
                                : tr("No name", "Placeholder for missing user name (plain)");
    if (html) {
        return QStringLiteral("<i>%1</i>").arg(escapeHtml(noName));
    }
    return QStringLiteral("<%1>").arg(noName);
}

QString MessengerTextRender::formatGroupDisplayName(const Group &group, bool html)
{
    const QString display = group.isRoot() ? tr("Root (all groups)") : QString(group);
    return html ? escapeHtml(display) : display;
}

QString MessengerTextRender::formatGroupList(const QSet<Group> &groups, bool html)
{
    QStringList values;
    const QSet<Group> normalized = Group::normalizedSet(groups);
    for (const Group &group : normalized) {
        values.append(formatGroupDisplayName(group, html));
    }
    std::sort(values.begin(), values.end(), [](const QString &left, const QString &right) {
        return QString::localeAwareCompare(left, right) < 0;
    });
    return values.isEmpty() ? formatGroupDisplayName(Group::root(), html) : values.join(tr(", "));
}

QString MessengerTextRender::formatUserGroupRoles(const User &user, bool html)
{
    const QString groups = formatUserGroupRolesPlain(user.groupRoles());
    return html ? escapeHtml(groups) : groups;
}

QString MessengerTextRender::formatResourceGroups(const Resource &resource, bool html)
{
    const QString groups = formatGroupsPlain(resource.groups());
    return html ? escapeHtml(groups) : groups;
}

QString MessengerTextRender::formatUserLine(const User &user, bool html)
{
    const QString keyText = user.key().toString();
    const QString displayName = formatUserDisplayName(user, html);
    const QString role = formatUserRole(user);

    if (!html) {
        const QString primary = displayName.trimmed().isEmpty() ? keyText : displayName;
        return QStringLiteral("%1 [%2; %3]").arg(primary, role, formatUserGroupRoles(user, false));
    }

    const QString primary = displayName.trimmed().isEmpty() ? keyText.toHtmlEscaped() : displayName;
    return QStringLiteral("%1 [<i>%2</i>; %3]")
            .arg(primary, role.toHtmlEscaped(), formatUserGroupRoles(user, true));
}

QString MessengerTextRender::formatResourceLine(const Resource &resource, bool html)
{
    if (!html) {
        return QStringLiteral("%1 - %2 [%3]")
                .arg(formatResourceNameWithStatus(resource, false),
                     resource.description(),
                     formatResourceGroups(resource, false));
    }

    return QStringLiteral("%1 - %2 [%3]")
            .arg(formatResourceNameWithStatus(resource, true),
                 escapeHtml(resource.description()),
                 formatResourceGroups(resource, true));
}

bool MessengerTextRender::shouldUseHtmlLists(MessengerType messenger, const AbstractBot *backendBot)
{
    if (messenger != MessengerType::Telegram && messenger != MessengerType::Web)
        return false;
    if (!backendBot)
        return false;

    const AbstractBot::Capabilities caps = backendBot->capabilities();
    return caps.testFlag(AbstractBot::CapabilityFlag::HtmlFormatting);
}

MessengerTextRender::RenderedText MessengerTextRender::renderResourceQueueStatus(
        ServerDatabase *database,
        MessengerType viewerMessenger,
        const QString &resourceName,
        const MessengerUserKey &currentUserKey,
        const AbstractBot *backendBot)
{
    RenderedText out;
    const bool useHtml = shouldUseHtmlLists(viewerMessenger, backendBot);
    out.format = useHtml ? AbstractBot::MessageFormat::Html : AbstractBot::MessageFormat::PlainText;

    if (!database || !database->isReady()) {
        out.text = tr("Database is not available right now.");
        return out;
    }

    const ResourceModel *resourceModel = database->resourceModel();
    if (!resourceModel) {
        out.text = tr("Resource model is not available right now.");
        return out;
    }

    const QString trimmedName = resourceName.trimmed();
    if (trimmedName.isEmpty()) {
        out.text = ResourceQueueUserMessages::resourceNotFound(tr("(unknown)"));
        return out;
    }

    const Resource resource = resourceModel->resourceByName(trimmedName);
    if (!resource.isValid()) {
        out.text = ResourceQueueUserMessages::resourceNotFound(trimmedName);
        return out;
    }

    const bool viewerAdmin = isViewerAdmin(database, viewerMessenger, currentUserKey);
    const User viewer = viewerUser(database, currentUserKey);
    if (!isResourceVisibleToViewer(resource, viewer, viewerAdmin)) {
        out.text = ResourceQueueUserMessages::resourceNotFound(trimmedName);
        return out;
    }

    const QString displayName = formatResourceNameWithStatus(resource, false);

    const ResourceQueueModel *queueModel = database->resourceQueueModel();
    const ResourceQueueEntry occupant =
            queueModel ? queueModel->currentOccupantForResource(trimmedName) : ResourceQueueEntry();
    const QList<ResourceQueueEntry> schedules =
            queueModel ? queueModel->schedulesForResource(trimmedName)
                       : QList<ResourceQueueEntry>();

    if (!occupant.isValid() && schedules.isEmpty()) {
        const QString text =
                tr("Resource '%1' is free. No queue or schedule entries.").arg(displayName);
        out.text = useHtml ? QStringLiteral("<i>%1</i>").arg(escapeHtml(text)) : text;
        return out;
    }

    const UserTimeZone viewerTimeZone = effectiveViewerTimeZone(database, currentUserKey);
    out.text = useHtml ? formatResourceQueueStatusHtml(database,
                                                       trimmedName,
                                                       displayName,
                                                       currentUserKey,
                                                       viewerTimeZone)
                       : formatResourceQueueStatusPlain(database,
                                                        trimmedName,
                                                        displayName,
                                                        currentUserKey,
                                                        viewerTimeZone);
    return out;
}

MessengerTextRender::RenderedText MessengerTextRender::renderUsersList(
        ServerDatabase *database,
        MessengerType viewerMessenger,
        bool viewerIsAdmin,
        const MessengerUserKey &currentUserKey,
        const AbstractBot *backendBot,
        UserListFlags flags)
{
    RenderedText out;
    const bool useHtml = shouldUseHtmlLists(viewerMessenger, backendBot);
    out.format = useHtml ? AbstractBot::MessageFormat::Html : AbstractBot::MessageFormat::PlainText;

    if (!database || !database->isReady()) {
        out.text = tr("Database is not available right now.");
        return out;
    }

    const UserModel *model = database->userModel();
    if (!model) {
        out.text = tr("User model is not available right now.");
        return out;
    }

    QList<User> users = model->items();
    const User viewer = model->userByKey(currentUserKey);
    if (currentUserKey.isValid() && currentUserKey.messenger() != MessengerType::LocalCli) {
        users.erase(std::remove_if(users.begin(),
                                   users.end(),
                                   [database, currentUserKey, viewer, viewerIsAdmin](
                                           const User &candidate) {
                                       if (!candidate.isValid())
                                           return true;
                                       if (candidate.key() == currentUserKey)
                                           return false;
                                       if (viewerIsAdmin) {
                                           return !database->canAdministerUser(currentUserKey,
                                                                               candidate);
                                       }
                                       for (const Group &group : candidate.groups()) {
                                           if (viewer.hasAccessToGroup(group))
                                               return false;
                                       }
                                       return true;
                                   }),
                    users.end());
    }

    if (users.isEmpty()) {
        out.text = tr("No users in database.");
        return out;
    }

    QCollator collator;
    collator.setNumericMode(true);
    std::sort(users.begin(), users.end(), [&collator](const User &lhs, const User &rhs) {
        const auto sortKeyFor = [](const User &user) -> QString {
            const QString first = MessengerTextRender::collapseWhitespace(user.firstName());
            const QString last = MessengerTextRender::collapseWhitespace(user.lastName());
            const QString fullName = QStringLiteral("%1 %2").arg(first, last).trimmed();
            if (!fullName.isEmpty())
                return fullName;

            if (user.key().isValid())
                return user.key().toString();

            return QString();
        };

        const QString lhsKey = sortKeyFor(lhs);
        const QString rhsKey = sortKeyFor(rhs);
        const bool lhsEmpty = lhsKey.trimmed().isEmpty();
        const bool rhsEmpty = rhsKey.trimmed().isEmpty();
        if (lhsEmpty != rhsEmpty)
            return rhsEmpty; // Put empty/unknown names at the end.

        const int cmp = collator.compare(lhsKey, rhsKey);
        if (cmp != 0)
            return cmp < 0;

        const QString lhsId = lhs.key().toString();
        const QString rhsId = rhs.key().toString();
        return collator.compare(lhsId, rhsId) < 0;
    });

    UserListFlags effectiveFlags = flags;
    if (!viewerIsAdmin) {
        effectiveFlags &= ~(ShowUserUserKey | ShowUserRole | ShowUserBlacklistStatus
                            | ShowUserIdLabel | ShowUserRoleLabel | ShowUserGroups);
    }

    BlacklistLookup blacklistLookup;
    if (effectiveFlags.testFlag(ShowUserBlacklistStatus)) {
        if (const BlacklistModel *blacklistModel = database->blacklistModel()) {
            const QList<BlacklistEntry> entries = blacklistModel->entries();
            if (!entries.isEmpty()) {
                QSet<MessengerUserKey> userKeys;
                for (const User &user : users) {
                    if (user.key().isValid())
                        userKeys.insert(user.key());
                }
                for (const BlacklistEntry &e : entries) {
                    if (userKeys.contains(e.user()))
                        blacklistLookup.insert(e.user(), e);
                }
            }
        }
    }

    const QString dateTimeFormat = ServerDateTimeFormat::effectiveFormat(database);
    const UserTimeZone viewerTimeZone = effectiveViewerTimeZone(database, currentUserKey);
    out.text = useHtml ? formatUsersHtml(users,
                                         blacklistLookup,
                                         effectiveFlags,
                                         currentUserKey,
                                         dateTimeFormat,
                                         viewerTimeZone)
                       : formatUsersPlain(users,
                                          blacklistLookup,
                                          effectiveFlags,
                                          currentUserKey,
                                          dateTimeFormat,
                                          viewerTimeZone);
    return out;
}

MessengerTextRender::RenderedText MessengerTextRender::renderBlacklist(
        ServerDatabase *database,
        MessengerType viewerMessenger,
        const MessengerUserKey &currentUserKey,
        const AbstractBot *backendBot)
{
    RenderedText out;
    const bool useHtml = shouldUseHtmlLists(viewerMessenger, backendBot);
    out.format = useHtml ? AbstractBot::MessageFormat::Html : AbstractBot::MessageFormat::PlainText;

    if (!database || !database->isReady()) {
        out.text = tr("Database is not available right now.");
        return out;
    }

    QList<BlacklistEntry> entries;
    if (const BlacklistModel *model = database->blacklistModel())
        entries = model->entries();
    entries.erase(std::remove_if(entries.begin(),
                                 entries.end(),
                                 [database, currentUserKey](const BlacklistEntry &entry) {
                                     return !database->canManageBlacklistTarget(currentUserKey,
                                                                                entry.user());
                                 }),
                  entries.end());

    const UserTimeZone viewerTimeZone = effectiveViewerTimeZone(database, currentUserKey);
    out.text =
            useHtml ? formatBlacklistEntriesHtml(entries, database, currentUserKey, viewerTimeZone)
                    : formatBlacklistEntriesPlain(entries,
                                                  database,
                                                  currentUserKey,
                                                  viewerTimeZone);
    return out;
}

MessengerTextRender::RenderedText MessengerTextRender::renderPendingAccessUsers(
        ServerDatabase *database,
        MessengerType viewerMessenger,
        const MessengerUserKey &currentUserKey,
        const AbstractBot *backendBot)
{
    constexpr int kRecentPendingAccessLimit = 100;
    constexpr int kPendingAccessRetentionDays = 3;

    RenderedText out;
    const bool useHtml = shouldUseHtmlLists(viewerMessenger, backendBot);
    out.format = useHtml ? AbstractBot::MessageFormat::Html : AbstractBot::MessageFormat::PlainText;

    if (!database || !database->isReady()) {
        out.text = tr("Database is not available right now.");
        return out;
    }

    QList<PendingAccessUserEntry> entries;
    if (const PendingAccessUserModel *model = database->pendingAccessUserModel()) {
        entries = model->recentUsers(kRecentPendingAccessLimit);
    }

    if (entries.isEmpty()) {
        out.text = tr("No users awaiting access activation recorded in the last %1 day(s).")
                           .arg(QString::number(kPendingAccessRetentionDays));
        return out;
    }

    BlacklistLookup blacklistLookup;
    if (const BlacklistModel *blacklistModel = database->blacklistModel()) {
        const QList<BlacklistEntry> blacklist = blacklistModel->entries();
        for (const BlacklistEntry &entry : blacklist) {
            blacklistLookup.insert(entry.user(), entry);
        }
    }

    out.text = useHtml ? formatPendingAccessUsersHtml(entries, blacklistLookup, currentUserKey)
                       : formatPendingAccessUsersPlain(entries, blacklistLookup, currentUserKey);
    return out;
}

MessengerTextRender::RenderedText MessengerTextRender::renderResourcesList(
        ServerDatabase *database,
        MessengerType viewerMessenger,
        const AbstractBot *backendBot,
        ResourceListFlags flags)
{
    return renderResourcesList(database, viewerMessenger, MessengerUserKey(), backendBot, flags);
}

MessengerTextRender::RenderedText MessengerTextRender::renderResourcesList(
        ServerDatabase *database,
        MessengerType viewerMessenger,
        const MessengerUserKey &currentUserKey,
        const AbstractBot *backendBot,
        ResourceListFlags flags)
{
    RenderedText out;
    const bool useHtml = shouldUseHtmlLists(viewerMessenger, backendBot);
    out.format = useHtml ? AbstractBot::MessageFormat::Html : AbstractBot::MessageFormat::PlainText;

    if (!database || !database->isReady()) {
        out.text = tr("Database is not available right now.");
        return out;
    }

    const ResourceModel *model = database->resourceModel();
    if (!model) {
        out.text = tr("Resource model is not available right now.");
        return out;
    }

    QList<Resource> resources = model->items();
    ResourceListFlags effectiveFlags = flags;
    if (currentUserKey.isValid()) {
        const bool viewerAdmin = isViewerAdmin(database, viewerMessenger, currentUserKey);
        const User viewer = viewerUser(database, currentUserKey);
        if (!viewerAdmin)
            effectiveFlags &= ~ShowResourceGroups;
        resources.erase(std::remove_if(resources.begin(),
                                       resources.end(),
                                       [&viewer, viewerAdmin](const Resource &resource) {
                                           return !MessengerTextRender::isResourceVisibleToViewer(
                                                   resource, viewer, viewerAdmin);
                                       }),
                        resources.end());
    } else {
        effectiveFlags &= ~ShowResourceGroups;
    }
    if (effectiveFlags.testFlag(HideOffResources)) {
        resources.erase(std::remove_if(resources.begin(),
                                       resources.end(),
                                       [](const Resource &resource) {
                                           return resource.status() == Resource::Status::Off;
                                       }),
                        resources.end());
    }

    if (resources.isEmpty()) {
        out.text = tr("No resources in database.");
        return out;
    }

    QCollator collator;
    collator.setNumericMode(true);
    std::sort(resources.begin(),
              resources.end(),
              [&collator](const Resource &lhs, const Resource &rhs) {
                  const QString lhsKey = lhs.name().trimmed();
                  const QString rhsKey = rhs.name().trimmed();
                  const bool lhsEmpty = lhsKey.isEmpty();
                  const bool rhsEmpty = rhsKey.isEmpty();
                  if (lhsEmpty != rhsEmpty)
                      return rhsEmpty; // Put empty names at the end.

                  const int cmp = collator.compare(lhsKey, rhsKey);
                  if (cmp != 0)
                      return cmp < 0;

                  return collator.compare(lhs.description(), rhs.description()) < 0;
              });

    out.text = useHtml ? formatResourcesHtml(resources, effectiveFlags)
                       : formatResourcesPlain(resources, effectiveFlags);
    return out;
}

MessengerTextRender::RenderedText MessengerTextRender::renderFreeResources(
        ServerDatabase *database,
        MessengerType viewerMessenger,
        const AbstractBot *backendBot,
        ResourceListFlags flags)
{
    return renderFreeResources(database, viewerMessenger, MessengerUserKey(), backendBot, flags);
}

MessengerTextRender::RenderedText MessengerTextRender::renderFreeResources(
        ServerDatabase *database,
        MessengerType viewerMessenger,
        const MessengerUserKey &currentUserKey,
        const AbstractBot *backendBot,
        ResourceListFlags flags)
{
    RenderedText out;
    const bool useHtml = shouldUseHtmlLists(viewerMessenger, backendBot);
    out.format = useHtml ? AbstractBot::MessageFormat::Html : AbstractBot::MessageFormat::PlainText;

    if (!database || !database->isReady()) {
        out.text = tr("Database is not available right now.");
        return out;
    }

    const ResourceQueueModel *queueModel = database->resourceQueueModel();
    const QList<QString> freeResourceNames =
            queueModel ? queueModel->freeResourceNames() : QList<QString>();
    const ResourceModel *resourceModel = database->resourceModel();
    const bool showStatus = flags.testFlag(ShowResourceStatus);
    const bool viewerAdmin = isViewerAdmin(database, viewerMessenger, currentUserKey);
    const User viewer = viewerUser(database, currentUserKey);

    QList<QString> freeResources;
    freeResources.reserve(freeResourceNames.size());
    for (const QString &name : freeResourceNames) {
        const Resource resource = resourceModel ? resourceModel->resourceByName(name) : Resource();
        if (resource.isValid()) {
            if (currentUserKey.isValid()
                && !isResourceVisibleToViewer(resource, viewer, viewerAdmin)) {
                continue;
            }
            if (resource.status() != Resource::Status::On)
                continue;
            freeResources.append(showStatus ? formatResourceNameWithStatus(resource, false)
                                            : baseResourceName(resource));
        } else {
            const QString normalizedName = collapseWhitespace(name);
            if (!normalizedName.isEmpty())
                freeResources.append(normalizedName);
        }
    }

    out.text = formatFreeResourcesMessage(freeResources, useHtml, flags);
    return out;
}

MessengerTextRender::RenderedText MessengerTextRender::renderSoonestResources(
        ServerDatabase *database,
        MessengerType viewerMessenger,
        int limit,
        const MessengerUserKey &currentUserKey,
        const AbstractBot *backendBot,
        ResourceListFlags flags)
{
    RenderedText out;
    const bool useHtml = shouldUseHtmlLists(viewerMessenger, backendBot);
    out.format = useHtml ? AbstractBot::MessageFormat::Html : AbstractBot::MessageFormat::PlainText;

    if (!database || !database->isReady()) {
        out.text = tr("Database is not available right now.");
        return out;
    }

    const int resolvedLimit = (limit <= 0) ? 5 : qMin(limit, 20);
    const ResourceQueueModel *queueModel = database->resourceQueueModel();
    const QList<ResourceQueueEntry> entries =
            queueModel ? queueModel->soonestReleasingResources(resolvedLimit)
                       : QList<ResourceQueueEntry>();
    const UserTimeZone viewerTimeZone = effectiveViewerTimeZone(database, currentUserKey);
    out.text = formatSoonestResourcesMessage(entries,
                                             database,
                                             useHtml,
                                             flags,
                                             currentUserKey,
                                             viewerTimeZone);
    return out;
}
