#ifndef RESOURCEQUEUEUSERMESSAGES_H
#define RESOURCEQUEUEUSERMESSAGES_H

#include <QCoreApplication>

#include <QString>

/**
 * @brief Shared user-facing texts for occupation and schedule failures.
 *
 * Commands, menus, and ResourceQueueModel use the same source strings so the reason stays
 * consistent across slash commands and menu wizards.
 */
class ResourceQueueUserMessages
{
    Q_DECLARE_TR_FUNCTIONS(ResourceQueueUserMessages)

public:
    static QString resourceNotFound(const QString &resourceName);
    static QString resourceTemporarilyUnavailable(const QString &resourceName);
    static QString resourceTurnedOff(const QString &resourceName);

    static QString accessNotActivated();
    static QString accessNotActivatedForUser(const QString &userDisplayName);
    static QString accessNotActivated(bool forSelf, const QString &userDisplayName);
    static QString userNotSpecified(const QString &userDisplayName);

    static QString durationNotAllowed(int minutes);
    static QString unknownDuration(const QString &token);
    static QString indefiniteOccupationNotAllowed();
    static QString occupationDurationExceedsLimit(uint maxMinutes);
    static QString endMustBeAfterStart();
    static QString resourceLimitReached(uint limit);

    static QString alreadyOccupying(bool forSelf,
                                    const QString &userDisplayName,
                                    const QString &resourceName);
    static QString alreadyWaitingForRelease(bool forSelf,
                                            const QString &userDisplayName,
                                            const QString &resourceName);
    static QString notOccupying(bool forSelf,
                                const QString &userDisplayName,
                                const QString &resourceName);
    static QString hasScheduledReservations(const QString &resourceName);
    static QString cannotScheduleWhileWaiting(const QString &resourceName);

    static QString scheduleOverlaps(const QString &startLocal, const QString &endLocal);
    static QString invalidScheduleStart(const QString &dateTimeFormat = QString());
    static QString invalidScheduleEnd(const QString &dateTimeFormat = QString());
    static QString invalidScheduleDate();
    static QString invalidScheduleTime();
    static QString scheduleStartInPast();

    static QString noSchedulesFound(const QString &resourceName);
    static QString noCurrentOccupant(const QString &resourceName);
    static QString noQueueEntries(const QString &resourceName);
    static QString failedToLocateActiveEntry();

    static QString failedToPersistScheduleEntry(const QString &resourceName);
    static QString failedToPersistScheduleRemoval(const QString &resourceName);
    static QString failedToPersistQueueRemoval(const QString &resourceName);
    static QString failedToPersistQueueRemovalForUser(const QString &userDisplayName);

private:
    static QString withExpectedFormat(const QString &message, const QString &dateTimeFormat);
};

#endif // RESOURCEQUEUEUSERMESSAGES_H
