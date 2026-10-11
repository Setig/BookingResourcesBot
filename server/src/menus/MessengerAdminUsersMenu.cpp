#include "MessengerAdminUsersMenu.h"

#include "MessengerMenuIds.h"

#include "MessengerBot.h"
#include "MessengerTextRender.h"

MessengerAdminUsersMenu::MessengerAdminUsersMenu(MessengerMenuManager *manager)
    : MessengerAbstractMenu(manager)
{
    // do nothing
}

QString MessengerAdminUsersMenu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuAdminUsers);
}

bool MessengerAdminUsersMenu::adminsOnly() const
{
    return true;
}

MessengerMenu::MenuPage MessengerAdminUsersMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuAdminUsers);
    page.title = tr("User management");
    page.body = tr("Overview, addition, editing, deletion of users.");

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
            page.rows.append({ mgr->makeNavigateButton(tr("Refresh list"), page.id, true) });
        }
        page.rows.append(
                { mgr->makeNavigateButton(tr("User card"),
                                          QLatin1String(MessengerMenuIds::kMenuAdminUserCard),
                                          true) });
        page.rows.append(
                { mgr->makeNavigateButton(tr("Add user"),
                                          QLatin1String(MessengerMenuIds::kMenuAdminUserAdd),
                                          true) });
        page.rows.append({ mgr->makeNavigateButton(tr("Back to admin home"),
                                                   QLatin1String(MessengerMenuIds::kMenuAdminHome),
                                                   true) });
    }

    return page;
}
