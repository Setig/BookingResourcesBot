#include "MessengerSoonestResourcesMenu.h"

#include "MessengerMenuIds.h"

#include "MessengerBot.h"
#include "ServerDatabase.h"
#include "MessengerTextRender.h"

MessengerSoonestResourcesMenu::MessengerSoonestResourcesMenu(MessengerMenuManager *manager)
    : MessengerAbstractMenu(manager)
{
    // do nothing
}

QString MessengerSoonestResourcesMenu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuResourcesSoonest);
}

MessengerMenu::MenuPage MessengerSoonestResourcesMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    Q_UNUSED(context);

    const int baseLimit = 5;

    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuResourcesSoonest);
    page.title = tr("Releases soon");
    page.body = tr("Next releases: resource, current user, finish time.");

    page.dynamicText = [this, baseLimit](const MessengerMenu::RenderContext &ctx) {
        MessengerMenu::MenuPage::DynamicText out;

        MessengerMenuManager *mgr = menuManager();
        MessengerBot *bot = mgr ? mgr->bot() : nullptr;
        const bool showAsOf =
                mgr && mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive;

        const MessengerTextRender::RenderedText rendered =
                MessengerTextRender::renderSoonestResources(
                        bot ? bot->database() : nullptr,
                        ctx.chat.messenger,
                        baseLimit,
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

        bool hasMore = false;
        const MessengerBot *bot = mgr->bot();
        const ServerDatabase *db = bot ? bot->database() : nullptr;
        if (db && db->isReady()) {
            const ResourceQueueModel *queueModel = db->resourceQueueModel();
            if (queueModel) {
                hasMore = queueModel->soonestReleasingResources(baseLimit + 1).size() > baseLimit;
            }
        }
        if (hasMore) {
            page.rows.append({ mgr->makeNavigateButton(
                    tr("Increase depth"),
                    QLatin1String(MessengerMenuIds::kMenuResourcesSoonest10)) });
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
