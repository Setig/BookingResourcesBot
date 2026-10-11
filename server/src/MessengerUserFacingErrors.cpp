#include "MessengerUserFacingErrors.h"

#include "MessengerType.h"
#include "MessengerUserKey.h"
#include "ResourceQueueUserMessages.h"

#include <QStringList>

QString MessengerUserFacingErrors::supportedMessengerList()
{
    QStringList names;
    for (MessengerType type : allMessengerTypes()) {
        if (type == MessengerType::LocalCli)
            continue;
        names.append(messengerTypeToString(type));
    }
    return names.join(tr(", "));
}

QString MessengerUserFacingErrors::accessDeniedAdminsOnly()
{
    return tr("Access denied. Admins only.");
}

QString MessengerUserFacingErrors::accessDeniedAccessNotActivated()
{
    return tr("Access denied. Access is not activated.");
}

QString MessengerUserFacingErrors::accessDeniedAccessNotActivated(const MessengerUserKey &userKey)
{
    QString reply = accessDeniedAccessNotActivated();
    if (userKey.isValid())
        reply += tr("\nYour ID: %1").arg(userKey.toString());
    return reply;
}

QString MessengerUserFacingErrors::invalidUserValue()
{
    return tr("Invalid user value. Use <messenger:id> "
              "(e.g. telegram:1234). Supported: %1.")
            .arg(supportedMessengerList());
}

QString MessengerUserFacingErrors::invalidUserValue(const QString &token)
{
    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty())
        return invalidUserValue();
    return tr("Invalid user value '%1'. Use <messenger:id> "
              "(e.g. telegram:1234). Supported: %2.")
            .arg(trimmed, supportedMessengerList());
}

QString MessengerUserFacingErrors::userNotFound(const QString &userKey)
{
    const QString trimmed = userKey.trimmed();
    if (trimmed.isEmpty())
        return tr("User '%1' was not found.").arg(tr("(unknown)"));
    return tr("User '%1' was not found.").arg(trimmed);
}

QString MessengerUserFacingErrors::userIdentifierEmpty()
{
    return tr("User identifier is empty.");
}

QString MessengerUserFacingErrors::userNotInBlacklist(const QString &userKey)
{
    const QString trimmed = userKey.trimmed();
    if (trimmed.isEmpty())
        return tr("User '%1' is not in blacklist.").arg(tr("(unknown)"));
    return tr("User '%1' is not in blacklist.").arg(trimmed);
}

QString MessengerUserFacingErrors::roleCannotBeChangedForFixedAdmin()
{
    return tr("Role cannot be changed for a fixed admin user.");
}

QString MessengerUserFacingErrors::unableToVerifyUser(const QString &reason, const QString &userId)
{
    const QString trimmed = reason.trimmed();
    if (trimmed.isEmpty())
        return userNotFound(userId);
    if (trimmed == userIdentifierEmpty() || trimmed == userNotFound(userId))
        return trimmed;
    return tr("Unable to verify user: %1").arg(trimmed);
}

QString MessengerUserFacingErrors::invalidStartTime(const QString &dateTimeFormat)
{
    return withExpectedFormat(tr("Invalid start time."), dateTimeFormat);
}

QString MessengerUserFacingErrors::invalidEndTime(const QString &dateTimeFormat)
{
    return withExpectedFormat(tr("Invalid end time."), dateTimeFormat);
}

QString MessengerUserFacingErrors::invalidFormat(const QString &token, const QString &expected)
{
    return tr("Invalid format: %1 (expected %2).").arg(token, expected);
}

QString MessengerUserFacingErrors::invalidReportKind(const QString &token, const QString &expected)
{
    return tr("Invalid report kind: %1 (expected %2).").arg(token, expected);
}

QString MessengerUserFacingErrors::statisticsNotAvailable()
{
    return tr("Statistics are not available right now.");
}

QString MessengerUserFacingErrors::statisticsFormatExpected()
{
    return QStringLiteral("csv|table|all");
}

QString MessengerUserFacingErrors::failedToBuildReport()
{
    return tr("Failed to build report.");
}

QString MessengerUserFacingErrors::endTimeMustBeAfterStartTime()
{
    return ResourceQueueUserMessages::endMustBeAfterStart();
}

QString MessengerUserFacingErrors::alertTextCannotBeEmpty()
{
    return tr("Alert text cannot be empty.");
}

QString MessengerUserFacingErrors::invalidStatus(const QString &token)
{
    return tr("Invalid status: %1. Expected one of: On, TemporarilyUnavailable, Off.").arg(token);
}

QString MessengerUserFacingErrors::userModelNotAvailable()
{
    return tr("User model is not available right now.");
}

QString MessengerUserFacingErrors::occupationDurationModelNotAvailable()
{
    return tr("Occupation duration model is not available right now.");
}

QString MessengerUserFacingErrors::unknownAction(const QString &action, const QString &expected)
{
    return tr("Unknown action: %1 (expected %2)").arg(action, expected);
}

QString MessengerUserFacingErrors::valueCannotBeEmpty()
{
    return tr("Value cannot be empty.");
}

QString MessengerUserFacingErrors::invalidNonNegativeDuration()
{
    return tr("Invalid value. Expected a non-negative duration.");
}

QString MessengerUserFacingErrors::invalidTimeZoneValue()
{
    return tr("Invalid value. Expected time zone value "
              "(e.g. UTC+3:00, UTC+03:00, +03:00, 03:00, -03:00, 180) or "
              "default/auto/system.");
}

QString MessengerUserFacingErrors::invalidNotifyMode()
{
    return tr("Invalid value. Expected all/mine/off.");
}

QString MessengerUserFacingErrors::invalidSoonestCount(const QString &token)
{
    return tr("Invalid count: %1. Expected a positive integer (max 20).").arg(token);
}

QString MessengerUserFacingErrors::invalidAdminValue(const QString &token)
{
    return tr("Invalid admin value: %1. Expected 1/0, true/false, yes/no, on/off, or admin/user.")
            .arg(token);
}

QString MessengerUserFacingErrors::invalidGroupRole(const QString &token)
{
    return tr("Invalid group role '%1'. Use group=user or group=admin.").arg(token);
}

QString MessengerUserFacingErrors::invalidRole(const QString &token)
{
    return tr("Invalid role '%1'. Expected user or admin.").arg(token);
}

QString MessengerUserFacingErrors::useDashForNoGroups()
{
    return tr("Use - for Root (all groups).");
}

QString MessengerUserFacingErrors::invalidGroupPath(const QString &token)
{
    return tr("Invalid group '%1'. Expected a slash-separated path without '='.").arg(token);
}

QString MessengerUserFacingErrors::emptyGroupPathSegments(const QString &token)
{
    return tr("Invalid group '%1'. Empty path segments are not allowed.").arg(token);
}

bool MessengerUserFacingErrors::isRootGroupAlias(const QString &token)
{
    const QString trimmed = token.trimmed();
    return trimmed == QStringLiteral("-")
           || trimmed.compare(Group::rootDisplayName(), Qt::CaseInsensitive) == 0
           || trimmed.compare(tr("Root (all groups)"), Qt::CaseInsensitive) == 0
           || trimmed.compare(tr("No group"), Qt::CaseInsensitive) == 0
           || trimmed.compare(QStringLiteral("no_group"), Qt::CaseInsensitive) == 0
           || trimmed.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0;
}

bool MessengerUserFacingErrors::parseGroupPath(const QString &token,
                                               Group *groupOut,
                                               QString *errorOut)
{
    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty() || isRootGroupAlias(trimmed)) {
        if (groupOut)
            *groupOut = Group::root();
        return true;
    }

    if (trimmed.contains(QLatin1Char('='))) {
        if (errorOut)
            *errorOut = invalidGroupPath(trimmed);
        return false;
    }

    QString normalized = trimmed;
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QStringList rawParts = normalized.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    QStringList parts;
    for (int i = 0; i < rawParts.size(); ++i) {
        const QString part = rawParts.at(i).trimmed();
        if (part.isEmpty()) {
            const bool edge = (i == 0 || i == rawParts.size() - 1);
            if (edge)
                continue;
            if (errorOut)
                *errorOut = emptyGroupPathSegments(trimmed);
            return false;
        }
        parts.append(part);
    }

    if (groupOut)
        *groupOut = parts.isEmpty() ? Group::root() : Group(parts.join(QLatin1Char('/')));
    return true;
}

bool MessengerUserFacingErrors::parseGroupList(const QString &token,
                                               QSet<Group> *groupsOut,
                                               QString *errorOut)
{
    if (!groupsOut)
        return false;

    groupsOut->clear();
    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty()) {
        groupsOut->insert(Group::root());
        if (errorOut)
            errorOut->clear();
        return true;
    }

    const QStringList parts = trimmed.split(QLatin1Char(','), Qt::KeepEmptyParts);
    for (const QString &part : parts) {
        const QString item = part.trimmed();
        if (item.isEmpty()) {
            if (errorOut)
                *errorOut = invalidGroupPath(item);
            return false;
        }

        Group group;
        if (!parseGroupPath(item, &group, errorOut))
            return false;
        groupsOut->insert(group);
    }

    if (groupsOut->isEmpty())
        groupsOut->insert(Group::root());

    if (errorOut)
        errorOut->clear();
    return true;
}

bool MessengerUserFacingErrors::parseUserGroupRoles(const QString &token,
                                                    QMap<Group, User::Role> *rolesOut,
                                                    QString *errorOut)
{
    if (!rolesOut)
        return false;

    rolesOut->clear();
    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty() || trimmed == QStringLiteral("-")) {
        if (errorOut)
            errorOut->clear();
        return true;
    }

    const QStringList entries = trimmed.split(QLatin1Char(','), Qt::KeepEmptyParts);
    for (const QString &entry : entries) {
        const QString normalizedEntry = entry.trimmed();
        if (normalizedEntry.isEmpty()) {
            if (errorOut)
                *errorOut = invalidGroupRole(normalizedEntry);
            return false;
        }

        const int separator = normalizedEntry.lastIndexOf(QLatin1Char('='));
        if (separator < 0) {
            if (errorOut)
                *errorOut = invalidGroupRole(normalizedEntry);
            return false;
        }

        const QString groupText = normalizedEntry.left(separator).trimmed();
        const QString roleText = normalizedEntry.mid(separator + 1).trimmed().toLower();

        Group group;
        if (!parseGroupPath(groupText, &group, errorOut))
            return false;

        User::Role role = User::Role::UserRole;
        if (roleText == QStringLiteral("admin")) {
            role = User::Role::AdminRole;
        } else if (roleText == QStringLiteral("user")) {
            role = User::Role::UserRole;
        } else {
            if (errorOut)
                *errorOut = invalidRole(roleText);
            return false;
        }

        rolesOut->insert(group, role);
    }

    if (errorOut)
        errorOut->clear();
    return true;
}

QString MessengerUserFacingErrors::withExpectedFormat(const QString &message,
                                                      const QString &dateTimeFormat)
{
    const QString trimmedFormat = dateTimeFormat.trimmed();
    if (trimmedFormat.isEmpty())
        return message;
    return QStringLiteral("%1\n%2").arg(message, tr("Expected format: %1").arg(trimmedFormat));
}
