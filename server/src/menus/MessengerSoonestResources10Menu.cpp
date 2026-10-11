#include "MessengerSoonestResources10Menu.h"

#include "MessengerMenuIds.h"

#include "MessengerBot.h"
#include "MessengerTextRender.h"

MessengerSoonestResources10Menu::MessengerSoonestResources10Menu(MessengerMenuManager *manager)
    : MessengerAbstractMenu(manager)
{
    // do nothing
}

QString MessengerSoonestResources10Menu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuResourcesSoonest10);
}

MessengerMenu::MenuPage MessengerSoonestResources10Menu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    Q_UNUSED(context);

    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuResourcesSoonest10);
    page.title = tr("Releases soon");
    page.body = tr("Next releases: resource, current user, finish time.");

    page.dynamicText = [this](const MessengerMenu::RenderContext &ctx) {
        MessengerMenu::MenuPage::DynamicText out;

        MessengerMenuManager *mgr = menuManager();
        MessengerBot *bot = mgr ? mgr->bot() : nullptr;
        const bool showAsOf =
                mgr && mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive;

        const MessengerTextRender::RenderedText rendered =
                MessengerTextRender::renderSoonestResources(
                        bot ? bot->database() : nullptr,
                        ctx.chat.messenger,
                        10,
                        AbstractBot::senderKey(ctx.chat, ctx.sender),
                        bot ? bot->backendBot() : nullptr,
                        MessengerTextRender::ShowResourceListTitle
                                | MessengerTextRender::ShowResourceStatus
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
        page.rows.append({ mgr->makeNavigateButton(
                tr("Decrease depth"), QLatin1String(MessengerMenuIds::kMenuResourcesSoonest)) });
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
