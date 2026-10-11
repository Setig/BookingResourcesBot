#include "MessengerAdminBlacklistMenu.h"

#include "MessengerMenuIds.h"

#include "MessengerBot.h"
#include "MessengerTextRender.h"

MessengerAdminBlacklistMenu::MessengerAdminBlacklistMenu(MessengerMenuManager *manager)
    : MessengerAbstractMenu(manager)
{
    // do nothing
}

QString MessengerAdminBlacklistMenu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuAdminBlacklist);
}

bool MessengerAdminBlacklistMenu::adminsOnly() const
{
    return true;
}

MessengerMenu::MenuPage MessengerAdminBlacklistMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    Q_UNUSED(context);

    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuAdminBlacklist);
    page.title = tr("User blacklist");
    page.body = tr("Active blocks: user, groups, until time, reason.");

    page.dynamicText = [this](const MessengerMenu::RenderContext &ctx) {
        MessengerMenu::MenuPage::DynamicText out;

        MessengerMenuManager *mgr = menuManager();
        MessengerBot *bot = mgr ? mgr->bot() : nullptr;
        const bool showAsOf =
                mgr && mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive;

        const MessengerTextRender::RenderedText rendered =
                MessengerTextRender::renderBlacklist(bot ? bot->database() : nullptr,
                                                     ctx.chat.messenger,
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
            page.rows.append(
                    { mgr->makeNavigateButton(tr("Refresh list"),
                                              QLatin1String(MessengerMenuIds::kMenuAdminBlacklist),
                                              true) });
        }
        page.rows.append({ mgr->makeNavigateButton(tr("Back to admin home"),
                                                   QLatin1String(MessengerMenuIds::kMenuAdminHome),
                                                   true) });
    }

    return page;
}
