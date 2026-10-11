#ifndef MESSENGER_ADMIN_NOTIFICATION_MENU_H
#define MESSENGER_ADMIN_NOTIFICATION_MENU_H

#include "MessengerAbstractMenu.h"

#include <QSet>
#include <QHash>
#include <QVector>
#include <QDateTime>

class MessengerBot;
class ServerDatabase;
class MessengerUserKey;

class MessengerAdminNotificationMenu final : public MessengerAbstractMenu
{
    Q_OBJECT

public:
    explicit MessengerAdminNotificationMenu(MessengerMenuManager *manager);

    QString menuId() const override;
    bool adminsOnly() const override;

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
    enum class RecipientMode {
        None = 0,
        AllUsers,
        SelectedUsers
    };

    enum class FlowStep {
        ChooseRecipients = 0,
        SelectUsers,
        ComposeMessage,
        Preview
    };

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

    struct SessionState
    {
        RecipientMode recipientMode = RecipientMode::None;
        FlowStep step = FlowStep::ChooseRecipients;
        SelectorState selector;
        QSet<QString> selectedUsers;
        QString message;
        QString errorText;

        void reset()
        {
            recipientMode = RecipientMode::None;
            step = FlowStep::ChooseRecipients;
            selector.clear();
            selectedUsers.clear();
            message.clear();
            errorText.clear();
        }
    };

    SessionState *ensureState(const QString &sessionId) const;
    bool ensureSelector(SessionState *state,
                        MessengerBot *bot,
                        const MessengerUserKey &actorKey,
                        bool forceRefresh) const;
    QString recipientSummary(const SessionState *state,
                             MessengerBot *bot,
                             ServerDatabase *database,
                             const MessengerUserKey &actorKey) const;
    bool sendNotification(SessionState *state,
                          const MessengerMenu::RenderContext &context,
                          MessengerMenuManager::ActionResult *result) const;

    mutable QHash<QString, SessionState> m_stateBySessionId;
};

BOOKING_RESOURCES_REGISTER_MENU(MessengerAdminNotificationMenu)

#endif // MESSENGER_ADMIN_NOTIFICATION_MENU_H
