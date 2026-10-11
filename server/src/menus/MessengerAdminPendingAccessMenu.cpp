#include "MessengerAdminPendingAccessMenu.h"

#include "MessengerMenuIds.h"

#include "MessengerBot.h"
#include "MessengerTextRender.h"

MessengerAdminPendingAccessMenu::MessengerAdminPendingAccessMenu(MessengerMenuManager *manager)
    : MessengerAbstractMenu(manager)
{
    // do nothing
}

QString MessengerAdminPendingAccessMenu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuAdminPendingAccess);
}

bool MessengerAdminPendingAccessMenu::adminsOnly() const
{
    return true;
}

MessengerMenu::AccessLevel MessengerAdminPendingAccessMenu::accessLevel() const
{
    return MessengerMenu::AccessLevel::ServerAdminsOnly;
}

MessengerMenu::MenuPage MessengerAdminPendingAccessMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    Q_UNUSED(context);

    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuAdminPendingAccess);
    page.accessLevel = MessengerMenu::AccessLevel::ServerAdminsOnly;
    page.title = tr("Users awaiting access activation");
    page.body = tr("Recent attempts: messenger:id, time, message.");

    page.dynamicText = [this](const MessengerMenu::RenderContext &ctx) {
        MessengerMenu::MenuPage::DynamicText out;

        MessengerMenuManager *mgr = menuManager();
        MessengerBot *bot = mgr ? mgr->bot() : nullptr;
        const bool showAsOf =
                mgr && mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive;

        const MessengerTextRender::RenderedText rendered =
                MessengerTextRender::renderPendingAccessUsers(bot ? bot->database() : nullptr,
                                                              ctx.chat.messenger,
                                                              AbstractBot::senderKey(ctx.chat,
                                                                                     ctx.sender),
                                                              bot ? bot->backendBot() : nullptr);
        const QString trimmedText = rendered.text.trimmed();
        out.text = showAsOf ? mgr->buildTextWithAsOfCurrentFormat(trimmedText, rendered.format)
                            : trimmedText;
        out.format = rendered.format;
        return out;
    };

    MessengerMenuManager *mgr = menuManager();
    if (mgr) {
        if (mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive) {
            page.rows.append(
                    { mgr->makeNavigateButton(tr("Refresh list"),
                                              QLatin1String(
                                                      MessengerMenuIds::kMenuAdminPendingAccess),
                                              true) });
        }
        page.rows.append(
                { mgr->makeNavigateButton(tr("Activate access"),
                                          QLatin1String(MessengerMenuIds::kMenuAdminUserAdd),
                                          true) });
        page.rows.append({ mgr->makeNavigateButton(tr("Back to admin home"),
                                                   QLatin1String(MessengerMenuIds::kMenuAdminHome),
                                                   true) });
    }

    return page;
}
