#ifndef MESSENGER_USER_SETTINGS_MENU_H
#define MESSENGER_USER_SETTINGS_MENU_H

#include "MessengerAbstractMenu.h"

#include <QHash>

class MessengerUserSettingsMenu final : public MessengerAbstractMenu
{
    Q_OBJECT

public:
    explicit MessengerUserSettingsMenu(MessengerMenuManager *manager);

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
    enum class SettingsViewMode {
        List = 0,
        Details,
        Presets,
        ManualInput
    };

    struct SessionState
    {
        SettingsViewMode view = SettingsViewMode::List;
        QString selectedKey;
        QString errorText;

        void reset()
        {
            view = SettingsViewMode::List;
            selectedKey.clear();
            errorText.clear();
        }
    };

    SessionState *ensureState(const QString &sessionId) const;

    mutable QHash<QString, SessionState> m_stateBySessionId;
};

#endif // MESSENGER_USER_SETTINGS_MENU_H
