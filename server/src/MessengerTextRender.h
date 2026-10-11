#ifndef MESSENGERTEXTRENDER_H
#define MESSENGERTEXTRENDER_H

#include <QCoreApplication>

#include <QSet>
#include <QFlags>
#include <QString>
#include <QDateTime>
#include <QStringList>

#include "Group.h"
#include "AbstractBot.h"
#include "UserTimeZone.h"
#include "MessengerType.h"

class User;
class Resource;
class ServerDatabase;
class BlacklistEntry;
class MessengerUserKey;
class PendingAccessUserEntry;

/**
 * @brief Builds human-readable text blocks (plain/HTML) for common bot lists.
 *
 * This class centralizes text rendering so that both command handlers and interactive menus
 * can reuse the same formatting for users/resources/blacklists and other list-like outputs.
 */
class MessengerTextRender
{
    Q_DECLARE_TR_FUNCTIONS(MessengerTextRender);

public:
    struct RenderedText
    {
        QString text;
        AbstractBot::MessageFormat format = AbstractBot::MessageFormat::PlainText;

        [[nodiscard]] bool isEmpty() const { return text.trimmed().isEmpty(); }
    };

    static QString collapseWhitespace(const QString &text);
    static QString normalizeNewlines(const QString &text);
    static QString formatUserAlertMessage(const QString &message, bool html = false);
    static QString describeBlacklistWindow(const BlacklistEntry &entry,
                                           const QString &dateTimeFormat = QString(),
                                           const UserTimeZone &timeZone = UserTimeZone());
    static QString formatUserDisplayName(const User &user, bool html);
    static QString formatPendingAccessUserDisplayName(const PendingAccessUserEntry &entry,
                                                      bool html);
    static QString formatGroupDisplayName(const Group &group, bool html);
    static QString formatGroupList(const QSet<Group> &groups, bool html);
    static QString formatUserGroupRoles(const User &user, bool html);
    static QString formatResourceGroups(const Resource &resource, bool html);
    static QString formatUserLine(const User &user, bool html);
    static QString formatResourceLine(const Resource &resource, bool html);
    static QString formatDurationMinutes(int minutes);
    static QString formatResourceNameWithStatus(const Resource &resource, bool html);
    static bool isResourceVisibleToViewer(const Resource &resource, bool viewerIsAdmin);
    static bool isResourceVisibleToViewer(const Resource &resource,
                                          const User &viewer,
                                          bool viewerIsAdmin);
    static bool isResourceVisibleToViewer(const Resource &resource,
                                          ServerDatabase *database,
                                          MessengerType viewerMessenger,
                                          const MessengerUserKey &currentUserKey);
    static QStringList currentOccupationResourceNames(ServerDatabase *database,
                                                      MessengerType messenger,
                                                      const MessengerUserKey &user);
    static QString formatReleasedOccupationReply(ServerDatabase *database,
                                                 const QString &resourceName,
                                                 const MessengerUserKey &actor);
    static QString formatLocalDateTime(const QDateTime &dt,
                                       const QString &invalidText = QString(),
                                       const QString &dateTimeFormat = QString(),
                                       const UserTimeZone &timeZone = UserTimeZone());

    static bool shouldUseHtmlLists(MessengerType messenger, const AbstractBot *backendBot);

    enum UserListFlag {
        ShowUserNone = 0x0,
        ShowUserListTitle = 0x1,
        ShowUserIndex = 0x2,
        ShowUserUserKey = 0x4,
        ShowUserRole = 0x8,
        ShowUserBlacklistStatus = 0x10,
        ShowUserIdLabel = 0x20,
        ShowUserRoleLabel = 0x40,
        ShowUserGroups = 0x80,
        ShowUserAll = ShowUserListTitle | ShowUserIndex | ShowUserUserKey | ShowUserRole
                      | ShowUserBlacklistStatus | ShowUserIdLabel | ShowUserRoleLabel
                      | ShowUserGroups
    };
    Q_DECLARE_FLAGS(UserListFlags, UserListFlag)

    static RenderedText renderResourceQueueStatus(ServerDatabase *database,
                                                  MessengerType viewerMessenger,
                                                  const QString &resourceName,
                                                  const MessengerUserKey &currentUserKey,
                                                  const AbstractBot *backendBot);

    static RenderedText renderUsersList(ServerDatabase *database,
                                        MessengerType viewerMessenger,
                                        bool viewerIsAdmin,
                                        const MessengerUserKey &currentUserKey,
                                        const AbstractBot *backendBot,
                                        UserListFlags flags = ShowUserAll);

    static RenderedText renderBlacklist(ServerDatabase *database,
                                        MessengerType viewerMessenger,
                                        const MessengerUserKey &currentUserKey,
                                        const AbstractBot *backendBot);

    static RenderedText renderPendingAccessUsers(ServerDatabase *database,
                                                 MessengerType viewerMessenger,
                                                 const MessengerUserKey &currentUserKey,
                                                 const AbstractBot *backendBot);

    enum ResourceListFlag {
        ShowResourceNone = 0x0,
        ShowResourceListTitle = 0x1,
        ShowResourceDescription = 0x2,
        ShowResourceStartMessage = 0x4,
        ShowResourceDescriptionLabel = 0x8,
        ShowResourceStartLabel = 0x10,
        ShowResourceStatus = 0x20,
        HideOffResources = 0x40,
        ShowResourceGroups = 0x80,
        ShowResourceAll = ShowResourceListTitle | ShowResourceDescription | ShowResourceStartMessage
                          | ShowResourceDescriptionLabel | ShowResourceStartLabel
                          | ShowResourceStatus | ShowResourceGroups
    };
    Q_DECLARE_FLAGS(ResourceListFlags, ResourceListFlag)

    static RenderedText renderResourcesList(
            ServerDatabase *database,
            MessengerType viewerMessenger,
            const AbstractBot *backendBot,
            ResourceListFlags flags = ResourceListFlags(ShowResourceAll));
    static RenderedText renderResourcesList(
            ServerDatabase *database,
            MessengerType viewerMessenger,
            const MessengerUserKey &currentUserKey,
            const AbstractBot *backendBot,
            ResourceListFlags flags = ResourceListFlags(ShowResourceAll));

    static RenderedText renderFreeResources(ServerDatabase *database,
                                            MessengerType viewerMessenger,
                                            const AbstractBot *backendBot,
                                            ResourceListFlags flags = ResourceListFlags(
                                                    ShowResourceListTitle | ShowResourceStatus));
    static RenderedText renderFreeResources(ServerDatabase *database,
                                            MessengerType viewerMessenger,
                                            const MessengerUserKey &currentUserKey,
                                            const AbstractBot *backendBot,
                                            ResourceListFlags flags = ResourceListFlags(
                                                    ShowResourceListTitle | ShowResourceStatus));

    static RenderedText renderSoonestResources(ServerDatabase *database,
                                               MessengerType viewerMessenger,
                                               int limit,
                                               const MessengerUserKey &currentUserKey,
                                               const AbstractBot *backendBot,
                                               ResourceListFlags flags = ResourceListFlags(
                                                       ShowResourceListTitle | ShowResourceStatus));
};

Q_DECLARE_OPERATORS_FOR_FLAGS(MessengerTextRender::ResourceListFlags)
Q_DECLARE_OPERATORS_FOR_FLAGS(MessengerTextRender::UserListFlags)

#endif // MESSENGERTEXTRENDER_H
