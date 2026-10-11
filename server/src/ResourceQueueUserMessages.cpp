#include "ResourceQueueUserMessages.h"

QString ResourceQueueUserMessages::resourceNotFound(const QString &resourceName)
{
    return tr("Resource '%1' was not found.").arg(resourceName);
}

QString ResourceQueueUserMessages::resourceTemporarilyUnavailable(const QString &resourceName)
{
    return tr("Resource '%1' is temporarily unavailable.").arg(resourceName);
}

QString ResourceQueueUserMessages::resourceTurnedOff(const QString &resourceName)
{
    return tr("Resource '%1' is turned off.").arg(resourceName);
}

QString ResourceQueueUserMessages::accessNotActivated()
{
    return tr("Access is not activated.");
}

QString ResourceQueueUserMessages::accessNotActivatedForUser(const QString &userDisplayName)
{
    return tr("Access is not activated for user '%1'.").arg(userDisplayName);
}

QString ResourceQueueUserMessages::accessNotActivated(bool forSelf, const QString &userDisplayName)
{
    if (forSelf)
        return accessNotActivated();
    return accessNotActivatedForUser(userDisplayName);
}

QString ResourceQueueUserMessages::userNotSpecified(const QString &userDisplayName)
{
    return tr("User '%1' is not specified.").arg(userDisplayName);
}

QString ResourceQueueUserMessages::durationNotAllowed(int minutes)
{
    return tr("Duration %1 minutes is not allowed.").arg(minutes);
}

QString ResourceQueueUserMessages::unknownDuration(const QString &token)
{
    return tr("Unknown duration '%1'.").arg(token);
}

QString ResourceQueueUserMessages::indefiniteOccupationNotAllowed()
{
    return tr("Indefinite occupation is not allowed by policy.");
}

QString ResourceQueueUserMessages::occupationDurationExceedsLimit(uint maxMinutes)
{
    return tr("Occupation duration exceeds the allowed limit (%1 minutes).")
            .arg(QString::number(maxMinutes));
}

QString ResourceQueueUserMessages::endMustBeAfterStart()
{
    return tr("End time must be after start time.");
}

QString ResourceQueueUserMessages::resourceLimitReached(uint limit)
{
    return tr("Resource limit reached: at most %1 resources at the same time.")
            .arg(QString::number(limit));
}

QString ResourceQueueUserMessages::alreadyOccupying(bool forSelf,
                                                    const QString &userDisplayName,
                                                    const QString &resourceName)
{
    if (forSelf)
        return tr("You are already occupying '%1'.").arg(resourceName);
    return tr("User '%1' is already occupying '%2'.").arg(userDisplayName, resourceName);
}

QString ResourceQueueUserMessages::alreadyWaitingForRelease(bool forSelf,
                                                            const QString &userDisplayName,
                                                            const QString &resourceName)
{
    if (forSelf)
        return tr("You are already waiting for '%1' to be released.").arg(resourceName);
    return tr("User '%1' is already waiting for '%2' to be released.")
            .arg(userDisplayName, resourceName);
}

QString ResourceQueueUserMessages::notOccupying(bool forSelf,
                                                const QString &userDisplayName,
                                                const QString &resourceName)
{
    if (forSelf)
        return tr("You are not occupying '%1'.").arg(resourceName);
    return tr("User '%1' is not occupying '%2'.").arg(userDisplayName, resourceName);
}

QString ResourceQueueUserMessages::hasScheduledReservations(const QString &resourceName)
{
    return tr("Resource '%1' has scheduled reservations. Schedule a fixed time window instead.")
            .arg(resourceName);
}

QString ResourceQueueUserMessages::cannotScheduleWhileWaiting(const QString &resourceName)
{
    return tr("Cannot schedule '%1' while users are waiting for the resource to be released.")
            .arg(resourceName);
}

QString ResourceQueueUserMessages::scheduleOverlaps(const QString &startLocal,
                                                    const QString &endLocal)
{
    const QString startTrimmed = startLocal.trimmed();
    const QString endTrimmed = endLocal.trimmed();
    if (startTrimmed.isEmpty())
        return tr("Schedule overlaps with another reservation.");
    if (endTrimmed.isEmpty()) {
        return tr("Schedule overlaps with another reservation starting at %1.").arg(startTrimmed);
    }
    return tr("Schedule overlaps with another reservation: %1 → %2.").arg(startTrimmed, endTrimmed);
}

QString ResourceQueueUserMessages::invalidScheduleStart(const QString &dateTimeFormat)
{
    return withExpectedFormat(tr("Invalid schedule start."), dateTimeFormat);
}

QString ResourceQueueUserMessages::invalidScheduleEnd(const QString &dateTimeFormat)
{
    return withExpectedFormat(tr("Invalid schedule end."), dateTimeFormat);
}

QString ResourceQueueUserMessages::invalidScheduleDate()
{
    return tr("Invalid schedule date.");
}

QString ResourceQueueUserMessages::invalidScheduleTime()
{
    return tr("Invalid schedule time.");
}

QString ResourceQueueUserMessages::scheduleStartInPast()
{
    return tr("Schedule start time must not be in the past.");
}

QString ResourceQueueUserMessages::noSchedulesFound(const QString &resourceName)
{
    return tr("No schedules found for '%1'.").arg(resourceName);
}

QString ResourceQueueUserMessages::noCurrentOccupant(const QString &resourceName)
{
    return tr("No current occupant for '%1'.").arg(resourceName);
}

QString ResourceQueueUserMessages::noQueueEntries(const QString &resourceName)
{
    return tr("Resource '%1' has no queue entries.").arg(resourceName);
}

QString ResourceQueueUserMessages::failedToLocateActiveEntry()
{
    return tr("Failed to locate active entry.");
}

QString ResourceQueueUserMessages::failedToPersistScheduleEntry(const QString &resourceName)
{
    return tr("Failed to persist schedule entry for '%1'.").arg(resourceName);
}

QString ResourceQueueUserMessages::failedToPersistScheduleRemoval(const QString &resourceName)
{
    return tr("Failed to persist schedule removal for '%1'.").arg(resourceName);
}

QString ResourceQueueUserMessages::failedToPersistQueueRemoval(const QString &resourceName)
{
    return tr("Failed to persist queue removal for '%1'.").arg(resourceName);
}

QString ResourceQueueUserMessages::failedToPersistQueueRemovalForUser(
        const QString &userDisplayName)
{
    return tr("Failed to persist queue removal for user '%1'.").arg(userDisplayName);
}

QString ResourceQueueUserMessages::withExpectedFormat(const QString &message,
                                                      const QString &dateTimeFormat)
{
    const QString trimmedFormat = dateTimeFormat.trimmed();
    if (trimmedFormat.isEmpty())
        return message;
    return QStringLiteral("%1\n%2").arg(message, tr("Expected format: %1").arg(trimmedFormat));
}
