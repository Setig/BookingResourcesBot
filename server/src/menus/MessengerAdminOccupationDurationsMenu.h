#ifndef MESSENGER_ADMIN_OCCUPATION_DURATIONS_MENU_H
#define MESSENGER_ADMIN_OCCUPATION_DURATIONS_MENU_H

#include "MessengerAbstractMenu.h"

#include <QHash>

class MessengerAdminOccupationDurationsMenu final : public MessengerAbstractMenu
{
    Q_OBJECT

public:
    explicit MessengerAdminOccupationDurationsMenu(MessengerMenuManager *manager);

    QString menuId() const override;
    bool adminsOnly() const override;
    MessengerMenu::AccessLevel accessLevel() const override;
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
    enum class ViewMode {
        List = 0,
        AddInput,
        RemoveSelect,
        RemoveConfirm
    };

    struct SessionState
    {
        ViewMode view = ViewMode::List;
        int pendingRemoveMinutes = -1;
        QString errorText;

        void reset()
        {
            view = ViewMode::List;
            pendingRemoveMinutes = -1;
            errorText.clear();
        }
    };

    SessionState *ensureState(const QString &sessionId) const;

    mutable QHash<QString, SessionState> m_stateBySessionId;
};

#endif // MESSENGER_ADMIN_OCCUPATION_DURATIONS_MENU_H
