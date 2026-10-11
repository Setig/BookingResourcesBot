#include "MessengerFreeResourcesMenu.h"

#include "MessengerMenuIds.h"

#include "MessengerBot.h"
#include "MessengerTextRender.h"

MessengerFreeResourcesMenu::MessengerFreeResourcesMenu(MessengerMenuManager *manager)
    : MessengerAbstractMenu(manager)
{
    // do nothing
}

QString MessengerFreeResourcesMenu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuResourcesFree);
}

MessengerMenu::MenuPage MessengerFreeResourcesMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    Q_UNUSED(context);

    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuResourcesFree);
    page.title = tr("Free resources");
    page.body = tr("Currently idle resources with remaining free time.");

    page.dynamicText = [this](const MessengerMenu::RenderContext &ctx) {
        MessengerMenu::MenuPage::DynamicText out;

        MessengerMenuManager *mgr = menuManager();
        MessengerBot *bot = mgr ? mgr->bot() : nullptr;
        const bool showAsOf =
                mgr && mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive;

        const MessengerTextRender::RenderedText rendered = MessengerTextRender::renderFreeResources(
                bot ? bot->database() : nullptr,
                ctx.chat.messenger,
                AbstractBot::senderKey(ctx.chat, ctx.sender),
                bot ? bot->backendBot() : nullptr,
                MessengerTextRender::ShowResourceListTitle | MessengerTextRender::ShowResourceStatus
                        | MessengerTextRender::HideOffResources);
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
        page.rows.append(
                { mgr->makeNavigateButton(tr("Open resource card"),
                                          QLatin1String(
                                                  MessengerMenuIds::kMenuResourceCardUser)) });
        page.rows.append(
                { mgr->makeNavigateButton(tr("Back to resources"),
                                          QLatin1String(MessengerMenuIds::kMenuResourcesHome)) });
    }

    return page;
}
