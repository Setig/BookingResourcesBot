#ifndef MESSENGER_USER_RESOURCES_STATE_H
#define MESSENGER_USER_RESOURCES_STATE_H

#include <QObject>

#include <QHash>
#include <QVector>
#include <QDateTime>

class MessengerMenuManager;

/**
 * @brief Per-session state for user-facing resource menus (card/queue/schedule).
 *
 * This keeps wizard and selection state outside of MessengerMenuManager.
 */
class MessengerUserResourcesState : public QObject
{
    Q_OBJECT

public:
    explicit MessengerUserResourcesState(MessengerMenuManager *manager);
    ~MessengerUserResourcesState() override;

    struct SelectorItem
    {
        QString id;
        QString label;
    };

    struct SelectorState
    {
        QVector<SelectorItem> items;
        int page = 0;
        int pageSize = 10;
        QDateTime updatedAtUtc;

        void clear()
        {
            items.clear();
            page = 0;
            pageSize = 10;
            updatedAtUtc = QDateTime();
        }
    };

    enum class WizardMode {
        None = 0,
        Occupy,
        Schedule
    };

    enum class WizardStep {
        None = 0,
        OccupyDuration,
        ScheduleStart,
        ScheduleEnd,
        ScheduleReview
    };

    struct Wizard
    {
        WizardMode mode = WizardMode::None;
        WizardStep step = WizardStep::None;
        QString errorText;

        int durationMinutes = 0;
        QDateTime scheduleStartUtc;
        QDateTime scheduleEndUtc;
        QDate schedulePickedLocalDate;
        QTime schedulePickedLocalTime;
        int scheduleTimePage = 0;
        bool scheduleReviewReached = false;

        void clear()
        {
            mode = WizardMode::None;
            step = WizardStep::None;
            errorText.clear();
            durationMinutes = 0;
            scheduleStartUtc = QDateTime();
            scheduleEndUtc = QDateTime();
            schedulePickedLocalDate = QDate();
            schedulePickedLocalTime = QTime();
            scheduleTimePage = 0;
            scheduleReviewReached = false;
        }
    };

    struct State
    {
        QString selectedResourceUuid;
        SelectorState selector;
        Wizard wizard;
    };

    [[nodiscard]] State *ensure(const QString &sessionId);
    [[nodiscard]] const State *find(const QString &sessionId) const;
    void clear(const QString &sessionId);
    void invalidateSelectors();

private:
    MessengerMenuManager *m_manager; // non-owning
    QHash<QString, State> m_states;
};

#endif // MESSENGER_USER_RESOURCES_STATE_H
