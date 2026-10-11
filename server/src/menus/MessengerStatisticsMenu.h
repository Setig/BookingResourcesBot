#ifndef MESSENGERSTATISTICSMENU_H
#define MESSENGERSTATISTICSMENU_H

#include "MessengerAbstractMenu.h"

#include <QSet>
#include <QHash>
#include <QVector>
#include <QDateTime>

#include "ServerStats.h"
#include "MessengerUserKey.h"

class MessengerBot;

class MessengerStatisticsMenu final : public MessengerAbstractMenu
{
    Q_OBJECT

public:
    explicit MessengerStatisticsMenu(MessengerMenuManager *manager);

    QString menuId() const override;
    QStringList eventKeys() const override;
    MessengerMenu::MenuPage buildPage(const MessengerMenu::RenderContext &context) const override;

    bool handleEvent(const QString &event,
                     const QString &sessionId,
                     const MessengerMenu::RenderContext &context,
                     MessengerMenuManager::ActionResult *result) const override;
    bool handleTextInput(const QString &text,
                         const QString &sessionId,
                         const MessengerMenu::RenderContext &context,
                         MessengerMenuManager::ActionResult *result) const override;

private:
    using ReportKind = ServerStats::ReportRequest::ReportKind;

    enum class FilterMode {
        None = 0,
        Resources,
        Users
    };

    enum class FlowStep {
        ChooseFilter = 0,
        EnterTargets,
        EnterStartUtc,
        EnterEndUtc,
        ChooseReport,
        ChooseFormat,
        Running
    };

    struct SelectorItem
    {
        QString id;
        QString label;
        QString value;
    };

    struct SelectorState
    {
        FilterMode filter = FilterMode::None;
        QVector<SelectorItem> items;
        QSet<QString> selectedIds;
        int page = 0;
        int pageSize = 10;
        QDateTime updatedAtUtc;

        void clear()
        {
            filter = FilterMode::None;
            items.clear();
            selectedIds.clear();
            page = 0;
            pageSize = 10;
            updatedAtUtc = QDateTime();
        }
    };

    struct SessionState
    {
        FilterMode filter = FilterMode::None;
        FlowStep step = FlowStep::ChooseFilter;
        bool formatCsvSelected = true;
        bool formatTableSelected = false;
        ReportKind reportKind = ReportKind::UtilizationSummary;
        bool resourcesAllSelected = true;
        bool usersAllSelected = true;
        SelectorState selector;
        QStringList resources;
        QList<MessengerUserKey> users;
        QDateTime startUtc;
        QDateTime endUtc;
        QString errorText;
        QString statusText;
        quint64 generation = 0;

        void reset()
        {
            ++generation;
            filter = FilterMode::None;
            step = FlowStep::ChooseFilter;
            formatCsvSelected = true;
            formatTableSelected = false;
            reportKind = ReportKind::UtilizationSummary;
            resourcesAllSelected = true;
            usersAllSelected = true;
            selector.clear();
            resources.clear();
            users.clear();
            startUtc = QDateTime();
            endUtc = QDateTime();
            errorText.clear();
            statusText.clear();
        }
    };

    SessionState *ensureState(const QString &sessionId) const;
    const SessionState *findState(const QString &sessionId) const;
    bool ensureSelector(SessionState *state,
                        MessengerBot *bot,
                        bool forceRefresh,
                        const MessengerUserKey &viewerKey) const;

    mutable QHash<QString, SessionState> m_stateBySessionId;
};

#endif // MESSENGERSTATISTICSMENU_H
