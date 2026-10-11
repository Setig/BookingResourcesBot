#include "MessengerAdminResourcesMenu.h"

#include "MessengerMenuIds.h"

#include "MessengerBot.h"
#include "MessengerTextRender.h"

MessengerAdminResourcesMenu::MessengerAdminResourcesMenu(MessengerMenuManager *manager)
    : MessengerAbstractMenu(manager)
{
    // do nothing
}

QString MessengerAdminResourcesMenu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuAdminResources);
}

bool MessengerAdminResourcesMenu::adminsOnly() const
{
    return true;
}

MessengerMenu::MenuPage MessengerAdminResourcesMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    Q_UNUSED(context);

    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuAdminResources);
    page.title = tr("Resource management");
    page.body = tr("Resources with schedules, instructions, admin actions.");

    page.dynamicText = [this](const MessengerMenu::RenderContext &ctx) {
        MessengerMenu::MenuPage::DynamicText out;

        MessengerMenuManager *mgr = menuManager();
        MessengerBot *bot = mgr ? mgr->bot() : nullptr;
        const bool showAsOf =
                mgr && mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive;

        const MessengerTextRender::RenderedText rendered =
                MessengerTextRender::renderResourcesList(bot ? bot->database() : nullptr,
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
            page.rows.append({ mgr->makeNavigateButton(tr("Refresh list"), page.id, true) });
        }
        page.rows.append(
                { mgr->makeNavigateButton(tr("Resource card"),
                                          QLatin1String(MessengerMenuIds::kMenuAdminResourceCard),
                                          true) });
        page.rows.append(
                { mgr->makeNavigateButton(tr("Add resource"),
                                          QLatin1String(MessengerMenuIds::kMenuAdminResourceAdd),
                                          true) });
        page.rows.append({ mgr->makeNavigateButton(tr("Back to admin home"),
                                                   QLatin1String(MessengerMenuIds::kMenuAdminHome),
                                                   true) });
    }

    return page;
}
