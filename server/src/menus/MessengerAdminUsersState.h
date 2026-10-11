#ifndef MESSENGER_ADMIN_USERS_STATE_H
#define MESSENGER_ADMIN_USERS_STATE_H

#include <QObject>

#include <QMap>
#include <QHash>
#include <QString>
#include <QVector>
#include <QDateTime>

#include "User.h"
#include "MessengerUserKey.h"
#include "MessengerMenuManager.h"

/**
 * @brief Per-session state and flow logic for admin user menus.
 *
 * Keeps selector + user wizard state outside of MessengerMenuManager.
 */
class MessengerAdminUsersState : public QObject
{
    Q_OBJECT

public:
    explicit MessengerAdminUsersState(MessengerMenuManager *manager);
    ~MessengerAdminUsersState() override;

    struct SelectorItem
    {
        QString id;
        QString label;
    };

    enum class SelectorPurpose {
        None = 0,
        AdminUserCard,
        AdminUserEdit,
        AdminUserDelete,
        AdminPendingAccessUserAdd
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

    enum class UserWizardMode {
        None = 0,
        Add,
        Edit
    };

    enum class UserWizardStep {
        None = 0,
        UserKey,
        FirstName,
        LastName,
        Role,
        Groups,
        Review
    };

    struct UserWizard
    {
        UserWizardMode mode = UserWizardMode::None;
        UserWizardStep step = UserWizardStep::None;
        QString errorText;

        MessengerUserKey user;
        QString firstName;
        QString lastName;
        QMap<Group, User::Role> groupRoles;
        bool reviewReached = false;

        QString existingFirstName;
        QString existingLastName;
        bool existingFixedAdmin = false;
        QMap<Group, User::Role> existingGroupRoles;

        void clear()
        {
            mode = UserWizardMode::None;
            step = UserWizardStep::None;
            errorText.clear();
            user = MessengerUserKey();
            firstName.clear();
            lastName.clear();
            groupRoles.clear();
            reviewReached = false;
            existingFirstName.clear();
            existingLastName.clear();
            existingFixedAdmin = false;
            existingGroupRoles.clear();
        }
    };

    struct State
    {
        QString selectedUserKey;
        QString pendingDeleteUserKey;
        SelectorState selector;
        UserWizard wizard;

        void resetFlows()
        {
            selectedUserKey.clear();
            pendingDeleteUserKey.clear();
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

    static QString userDisplayNameForButton(const User &user);
    static QString userLabelForButton(const User &user);

private:
    MessengerMenuManager *m_manager; // non-owning
    QHash<QString, State> m_states;
};

#endif // MESSENGER_ADMIN_USERS_STATE_H
