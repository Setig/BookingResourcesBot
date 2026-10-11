#include "MessengerUsersListMenu.h"

#include "MessengerMenuIds.h"

#include "MessengerBot.h"
#include "MessengerTextRender.h"

MessengerUsersListMenu::MessengerUsersListMenu(MessengerMenuManager *manager)
    : MessengerAbstractMenu(manager)
{
    // do nothing
}

QString MessengerUsersListMenu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuUsersList);
}

MessengerMenu::MenuPage MessengerUsersListMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuUsersList);
    page.title = tr("Users with active access");
    page.body = tr("Users with roles and statuses. Use search or filters.");

    page.dynamicText = [this](const MessengerMenu::RenderContext &ctx) {
        MessengerMenu::MenuPage::DynamicText out;

        MessengerMenuManager *mgr = menuManager();
        MessengerBot *bot = mgr ? mgr->bot() : nullptr;
        const bool showAsOf =
                mgr && mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive;

        const MessengerTextRender::RenderedText rendered =
                MessengerTextRender::renderUsersList(bot ? bot->database() : nullptr,
                                                     ctx.chat.messenger,
                                                     ctx.isAdmin,
                                                     AbstractBot::senderKey(ctx.chat, ctx.sender),
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
            page.rows.append({ mgr->makeNavigateButton(tr("Refresh list"), page.id) });
        }
        if (context.isAdmin) {
            page.rows.append(
                    { mgr->makeNavigateButton(tr("User card"),
                                              QLatin1String(MessengerMenuIds::kMenuAdminUserCard),
                                              true) });
        }
        page.rows.append({ mgr->makeNavigateButton(tr("Back to main"),
                                                   QLatin1String(MessengerMenuIds::kMenuMain)) });
    }

    return page;
}
