#ifndef MESSENGERUSERFACINGERRORS_H
#define MESSENGERUSERFACINGERRORS_H

#include "User.h"

#include <QCoreApplication>

#include <QMap>
#include <QSet>
#include <QString>

/**
 * @brief Shared user-facing texts for access, identity, and parse failures.
 *
 * Commands and menus use the same source strings so the reason stays consistent
 * across slash commands and menu wizards.
 */
class MessengerUserFacingErrors
{
    Q_DECLARE_TR_FUNCTIONS(MessengerUserFacingErrors)

public:
    static QString supportedMessengerList();

    static QString accessDeniedAdminsOnly();
    static QString accessDeniedAccessNotActivated();
    static QString accessDeniedAccessNotActivated(const MessengerUserKey &userKey);

    static QString invalidUserValue();
    static QString invalidUserValue(const QString &token);
    static QString userNotFound(const QString &userKey);
    static QString userIdentifierEmpty();
    static QString userNotInBlacklist(const QString &userKey);
    static QString roleCannotBeChangedForFixedAdmin();
    static QString unableToVerifyUser(const QString &reason, const QString &userId = QString());

    static QString invalidStartTime(const QString &dateTimeFormat = QString());
    static QString invalidEndTime(const QString &dateTimeFormat = QString());
    static QString invalidFormat(const QString &token, const QString &expected);
    static QString invalidReportKind(const QString &token, const QString &expected);
    static QString statisticsNotAvailable();
    static QString statisticsFormatExpected();
    static QString failedToBuildReport();
    static QString endTimeMustBeAfterStartTime();

    static QString alertTextCannotBeEmpty();
    static QString invalidStatus(const QString &token);

    static QString userModelNotAvailable();
    static QString occupationDurationModelNotAvailable();

    static QString unknownAction(const QString &action, const QString &expected);
    static QString valueCannotBeEmpty();
    static QString invalidNonNegativeDuration();
    static QString invalidTimeZoneValue();
    static QString invalidNotifyMode();

    static QString invalidSoonestCount(const QString &token);
    static QString invalidAdminValue(const QString &token);

    static QString invalidGroupRole(const QString &token);
    static QString invalidRole(const QString &token);
    static QString useDashForNoGroups();
    static QString invalidGroupPath(const QString &token);
    static QString emptyGroupPathSegments(const QString &token);

    static bool parseGroupList(const QString &token, QSet<Group> *groupsOut, QString *errorOut);
    static bool parseUserGroupRoles(const QString &token,
                                    QMap<Group, User::Role> *rolesOut,
                                    QString *errorOut);

private:
    static QString withExpectedFormat(const QString &message, const QString &dateTimeFormat);
    static bool isRootGroupAlias(const QString &token);
    static bool parseGroupPath(const QString &token, Group *groupOut, QString *errorOut);
};

#endif // MESSENGERUSERFACINGERRORS_H
