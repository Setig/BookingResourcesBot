#ifndef MESSENGER_ADMIN_RESOURCES_STATE_H
#define MESSENGER_ADMIN_RESOURCES_STATE_H

#include <QObject>

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>
#include <QDateTime>

#include "Resource.h"
#include "MessengerMenuManager.h"

/**
 * @brief Per-session state and flow logic for admin resource menus.
 *
 * Keeps selector + resource wizard state outside of MessengerMenuManager.
 */
class MessengerAdminResourcesState : public QObject
{
    Q_OBJECT

public:
    explicit MessengerAdminResourcesState(MessengerMenuManager *manager);
    ~MessengerAdminResourcesState() override;

    struct SelectorItem
    {
        QString id;
        QString label;
    };

    enum class SelectorPurpose {
        None = 0,
        AdminResourceCard,
        AdminResourceEdit,
        AdminResourceDelete
    };

    struct SelectorState
    {
        QVector<SelectorItem> items;
        int page = 0;
        int pageSize = 10;
        QDateTime updatedAtUtc;
        SelectorPurpose purpose = SelectorPurpose::None;

        void clear()
        {
            items.clear();
            page = 0;
            pageSize = 10;
            updatedAtUtc = QDateTime();
            purpose = SelectorPurpose::None;
        }
    };

    enum class ResourceWizardMode {
        None = 0,
        Add,
        Edit
    };

    enum class ResourceWizardStep {
        None = 0,
        Name,
        Description,
        StartMessage,
        Groups,
        Status,
        Review
    };

    struct ResourceWizard
    {
        ResourceWizardMode mode = ResourceWizardMode::None;
        ResourceWizardStep step = ResourceWizardStep::None;
        QString errorText;

        QString name;
        QString description;
        QString startMessage;
        QSet<Group> groups;
        bool reviewReached = false;

        QString existingName;
        QString existingDescription;
        QString existingStartMessage;
        QSet<Group> existingGroups;
        Resource::Status status = Resource::Status::On;
        Resource::Status existingStatus = Resource::Status::On;

        void clear()
        {
            mode = ResourceWizardMode::None;
            step = ResourceWizardStep::None;
            errorText.clear();
            name.clear();
            description.clear();
            startMessage.clear();
            groups.clear();
            groups.insert(Group::root());
            reviewReached = false;
            existingName.clear();
            existingDescription.clear();
            existingStartMessage.clear();
            existingGroups.clear();
            existingGroups.insert(Group::root());
            status = Resource::Status::On;
            existingStatus = Resource::Status::On;
        }
    };

    struct State
    {
        QString selectedResourceUuid;
        QString pendingDeleteResourceUuid;
        SelectorState selector;
        ResourceWizard wizard;

        void resetFlows()
        {
            selectedResourceUuid.clear();
            pendingDeleteResourceUuid.clear();
            selector.clear();
            wizard.clear();
        }
    };

    [[nodiscard]] State *ensure(const QString &sessionId);
    [[nodiscard]] const State *find(const QString &sessionId) const;
    void clear(const QString &sessionId);
    void invalidateSelectors();

    bool ensureSelector(State *state,
                        SelectorPurpose purpose,
                        const MessengerUserKey &currentUserKey,
                        bool forceRefresh);
    MessengerMenu::MenuPage buildSelectorPage(const QString &currentMenuId,
                                              const QString &title,
                                              const QString &emptyText,
                                              const QString &backMenuId,
                                              bool adminsOnlyBack,
                                              State *state) const;

    bool handleEvent(const QString &event,
                     const QString &sessionId,
                     const MessengerMenu::RenderContext &context,
                     State *state,
                     MessengerMenuManager::ActionResult *result);

    bool handleTextInput(const QString &text,
                         const QString &sessionId,
                         const MessengerMenu::RenderContext &context,
                         State *state,
                         MessengerMenuManager::ActionResult *result);

    static QString resourceLabelForButton(const Resource &resource);

private:
    MessengerMenuManager *m_manager; // non-owning
    QHash<QString, State> m_states;
};

#endif // MESSENGER_ADMIN_RESOURCES_STATE_H
