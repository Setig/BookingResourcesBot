#include "MessengerAdminHomeMenu.h"

#include "MessengerMenuIds.h"

MessengerAdminHomeMenu::MessengerAdminHomeMenu(MessengerMenuManager *manager)
    : MessengerAbstractMenu(manager)
{
    // do nothing
}

QString MessengerAdminHomeMenu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuAdminHome);
}

bool MessengerAdminHomeMenu::adminsOnly() const
{
    return true;
}

MessengerMenu::MenuPage MessengerAdminHomeMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    Q_UNUSED(context);

    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuAdminHome);
    page.title = tr("Administration");
    page.body = tr("Administrative sections: user management, users awaiting access activation, "
                   "resources, blacklist, user alerts.");

    MessengerMenuManager *mgr = menuManager();
    if (mgr) {
        page.rows.append(
                { mgr->makeNavigateButton(tr("Manage resources"),
                                          QLatin1String(MessengerMenuIds::kMenuAdminResources),
                                          true) });
        page.rows.append({ mgr->makeNavigateButton(tr("Manage users"),
                                                   QLatin1String(MessengerMenuIds::kMenuAdminUsers),
                                                   true) });
        page.rows.append(
                { mgr->makeNavigateButton(tr("Users awaiting access activation"),
                                          QLatin1String(MessengerMenuIds::kMenuAdminPendingAccess),
                                          MessengerMenu::AccessLevel::ServerAdminsOnly) });
        page.rows.append(
                { mgr->makeNavigateButton(tr("Send alert to users"),
                                          QLatin1String(MessengerMenuIds::kMenuAdminNotification),
                                          true) });
        page.rows.append(
                { mgr->makeNavigateButton(tr("User blacklist"),
                                          QLatin1String(MessengerMenuIds::kMenuAdminBlacklist),
                                          true) });
        page.rows.append(
                { mgr->makeNavigateButton(tr("Resource occupation durations"),
                                          QLatin1String(
                                                  MessengerMenuIds::kMenuAdminOccupationDurations),
                                          MessengerMenu::AccessLevel::ServerAdminsOnly) });
        page.rows.append(
                { mgr->makeNavigateButton(tr("Server settings"),
                                          QLatin1String(MessengerMenuIds::kMenuAdminSettings),
                                          MessengerMenu::AccessLevel::ServerAdminsOnly) });
        page.rows.append({ mgr->makeNavigateButton(tr("Back to main"),
                                                   QLatin1String(MessengerMenuIds::kMenuMain),
                                                   true) });
    }

    return page;
}
