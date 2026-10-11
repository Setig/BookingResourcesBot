#include "MessengerMainMenu.h"

#include "MessengerMenuIds.h"

#include "MessengerBot.h"
#include "ResourceModel.h"
#include "ServerDatabase.h"
#include "ResourceQueueModel.h"
#include "MessengerTextRender.h"
#include "ServerDateTimeFormat.h"
#include "ServerRuntimeSettings.h"

#include <QCollator>

MessengerMainMenu::MessengerMainMenu(MessengerMenuManager *manager)
    : MessengerAbstractMenu(manager)
{
    // do nothing
}

QString MessengerMainMenu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuMain);
}

MessengerMenu::MenuPage MessengerMainMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuMain);
    page.title = tr("Main menu");
    page.body = tr("Choose a section: resource operations, user list, "
                   "or administration (if available).");

    page.dynamicText = [this](const MessengerMenu::RenderContext &ctx) {
        MessengerTextRender::RenderedText rendered;

        MessengerMenuManager *mgr = menuManager();
        MessengerBot *bot = mgr ? mgr->bot() : nullptr;
        const bool showAsOf =
                mgr && mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive;

        const AbstractBot *backendBot = bot ? bot->backendBot() : nullptr;
        const bool useHtml =
                MessengerTextRender::shouldUseHtmlLists(ctx.chat.messenger, backendBot);
        rendered.format =
                useHtml ? AbstractBot::MessageFormat::Html : AbstractBot::MessageFormat::PlainText;

        ServerDatabase *database = bot ? bot->database() : nullptr;
        if (!database || !database->isReady() || !database->resourceModel()
            || !database->resourceQueueModel()) {
            rendered.text = tr("Database is not available right now.");
            MessengerMenu::MenuPage::DynamicText out;
            out.text = showAsOf
                               ? mgr->buildTextWithAsOfCurrentFormat(rendered.text, rendered.format)
                               : rendered.text.trimmed();
            out.format = rendered.format;
            return out;
        }

        const ResourceModel *resourceModel = database->resourceModel();
        const ResourceQueueModel *queueModel = database->resourceQueueModel();
        const QString dateTimeFormat = ServerDateTimeFormat::effectiveFormat(database);
        const QString userId = ctx.sender.userId.trimmed();
        const MessengerUserKey self(ctx.chat.messenger, userId);
        const UserTimeZone viewerTimeZone =
                bot ? bot->effectiveTimeZoneForUser(self) : UserTimeZone();
        const QDateTime nowUtc = QDateTime::currentDateTimeUtc();

        QList<QString> occupyingNow;
        QList<ResourceQueueEntry> scheduledUpcoming;

        const QList<Resource> resources = resourceModel->items();
        occupyingNow.reserve(resources.size());
        for (const Resource &resource : resources) {
            if (!MessengerTextRender::isResourceVisibleToViewer(resource,
                                                                database,
                                                                ctx.chat.messenger,
                                                                self))
                continue;
            const QString resourceName = resource.name().trimmed();
            if (resourceName.isEmpty())
                continue;

            const ResourceQueueEntry occupant =
                    queueModel->currentOccupantForResource(resourceName);
            if (!occupant.isValid())
                continue;
            if (occupant.user() != self)
                continue;

            occupyingNow.append(MessengerTextRender::formatResourceNameWithStatus(resource, false));
        }

        const QList<ResourceQueueEntry> entries = queueModel->items();
        scheduledUpcoming.reserve(entries.size());
        for (const ResourceQueueEntry &entry : entries) {
            if (entry.user() != self)
                continue;
            if (!entry.startAtUtc().isValid())
                continue;
            if (entry.startAtUtc().toUTC() <= nowUtc)
                continue; // past or active

            scheduledUpcoming.append(entry);
        }

        QCollator collator;
        collator.setNumericMode(true);

        std::sort(occupyingNow.begin(),
                  occupyingNow.end(),
                  [&collator](const QString &lhs, const QString &rhs) {
                      return collator.compare(lhs, rhs) < 0;
                  });

        QHash<QString, QString> resourceNameCache;
        const auto resourceNameForEntry = [&](const ResourceQueueEntry &entry) -> QString {
            const QString uuid = entry.resourceUuid();
            const QHash<QString, QString>::const_iterator it = resourceNameCache.constFind(uuid);
            if (it != resourceNameCache.cend())
                return it.value();
            const Resource resource =
                    resourceModel ? resourceModel->resourceByUuid(uuid) : Resource();
            QString name;
            if (MessengerTextRender::isResourceVisibleToViewer(resource,
                                                               database,
                                                               ctx.chat.messenger,
                                                               self)) {
                name = MessengerTextRender::formatResourceNameWithStatus(resource, false).trimmed();
            }
            resourceNameCache.insert(uuid, name);
            return name;
        };

        scheduledUpcoming.erase(std::remove_if(scheduledUpcoming.begin(),
                                               scheduledUpcoming.end(),
                                               [&resourceNameForEntry](
                                                       const ResourceQueueEntry &entry) {
                                                   return resourceNameForEntry(entry).isEmpty();
                                               }),
                                scheduledUpcoming.end());

        std::sort(scheduledUpcoming.begin(),
                  scheduledUpcoming.end(),
                  [&collator, &resourceNameForEntry](const ResourceQueueEntry &lhs,
                                                     const ResourceQueueEntry &rhs) {
                      const QDateTime lhsStart = lhs.startAtUtc().toUTC();
                      const QDateTime rhsStart = rhs.startAtUtc().toUTC();
                      if (lhsStart != rhsStart)
                          return lhsStart < rhsStart;
                      return collator.compare(resourceNameForEntry(lhs), resourceNameForEntry(rhs))
                             < 0;
                  });

        QStringList lines;
        const QString statusHeader = tr("Your status:");
        if (useHtml) {
            lines << QStringLiteral("<b>%1</b>")
                             .arg(MessengerMenuManager::escapeHtml(statusHeader));
        } else {
            lines << statusHeader;
        }

        if (occupyingNow.isEmpty()) {
            const QString text = tr("Occupying now: none.");
            lines << (useHtml ? QStringLiteral("<i>%1</i>")
                                        .arg(MessengerMenuManager::escapeHtml(text))
                              : text);
        } else {
            const QString header =
                    tr("Occupying now (%1):").arg(QString::number(occupyingNow.size()));
            lines << (useHtml ? QStringLiteral("<i>%1</i>")
                                        .arg(MessengerMenuManager::escapeHtml(header))
                              : header);
            int index = 1;
            for (const QString &resourceName : occupyingNow) {
                if (useHtml) {
                    lines << QStringLiteral("<b>%1.</b> %2")
                                     .arg(QString::number(index++),
                                          MessengerMenuManager::escapeHtml(resourceName));
                } else {
                    lines << QStringLiteral("%1) %2").arg(QString::number(index++), resourceName);
                }
            }
        }

        if (!occupyingNow.isEmpty() && !scheduledUpcoming.isEmpty())
            lines << QString();

        if (scheduledUpcoming.isEmpty()) {
            const QString text = tr("Scheduled: none.");
            lines << (useHtml ? QStringLiteral("<i>%1</i>")
                                        .arg(MessengerMenuManager::escapeHtml(text))
                              : text);
        } else {
            const QString header =
                    tr("Scheduled (%1):").arg(QString::number(scheduledUpcoming.size()));
            lines << (useHtml ? QStringLiteral("<i>%1</i>")
                                        .arg(MessengerMenuManager::escapeHtml(header))
                              : header);
            int index = 1;
            for (const ResourceQueueEntry &entry : scheduledUpcoming) {
                const QString resourceName = resourceNameForEntry(entry);
                if (resourceName.isEmpty())
                    continue;

                const QString startLocal =
                        MessengerTextRender::formatLocalDateTime(entry.startAtUtc(),
                                                                 QString(),
                                                                 dateTimeFormat,
                                                                 viewerTimeZone);
                const QString endLocal = MessengerTextRender::formatLocalDateTime(
                        entry.endAtUtc(),
                        MessengerTextRender::formatDurationMinutes(0),
                        dateTimeFormat,
                        viewerTimeZone);
                if (useHtml) {
                    lines << QStringLiteral("<b>%1.</b> %2: %3 → %4")
                                     .arg(QString::number(index++),
                                          MessengerMenuManager::escapeHtml(resourceName),
                                          MessengerMenuManager::escapeHtml(startLocal),
                                          MessengerMenuManager::escapeHtml(endLocal));
                } else {
                    lines << QStringLiteral("%1) %2: %3 → %4")
                                     .arg(QString::number(index++),
                                          resourceName,
                                          startLocal,
                                          endLocal);
                }
            }
        }

        rendered.text = lines.join(QStringLiteral("\n")).trimmed();
        MessengerMenu::MenuPage::DynamicText out;
        out.text = showAsOf ? mgr->buildTextWithAsOfCurrentFormat(rendered.text, rendered.format)
                            : rendered.text.trimmed();
        out.format = rendered.format;
        return out;
    };

    MessengerMenuManager *mgr = menuManager();
    if (mgr) {
        bool statsEnabled =
                ServerRuntimeSettings::defaultValue(ServerRuntimeSettings::Setting::StatsEnabled)
                        .toBool();
        bool statsAdminsOnly =
                ServerRuntimeSettings::defaultValue(ServerRuntimeSettings::Setting::StatsAdminsOnly)
                        .toBool();
        if (const MessengerBot *bot = mgr->bot()) {
            if (const ServerDatabase *database = bot->database()) {
                statsEnabled =
                        database->serverSettingValue(ServerRuntimeSettings::Setting::StatsEnabled)
                                .toBool();
                statsAdminsOnly = database->serverSettingValue(
                                                  ServerRuntimeSettings::Setting::StatsAdminsOnly)
                                          .toBool();
            }
        }

        if (mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive) {
            page.rows.append(
                    { mgr->makeNavigateButton(tr("Refresh"),
                                              QLatin1String(MessengerMenuIds::kMenuMain)) });
        }
        page.rows.append(
                { mgr->makeNavigateButton(tr("Booking Resources"),
                                          QLatin1String(MessengerMenuIds::kMenuResourcesHome)) });
        page.rows.append(
                { mgr->makeNavigateButton(tr("User list"),
                                          QLatin1String(MessengerMenuIds::kMenuUsersList)) });
        if (statsEnabled && (!statsAdminsOnly || context.isAdmin)) {
            page.rows.append(
                    { mgr->makeNavigateButton(tr("Statistics"),
                                              QLatin1String(MessengerMenuIds::kMenuStatistics)) });
        }
        page.rows.append(
                { mgr->makeNavigateButton(tr("User settings"),
                                          QLatin1String(MessengerMenuIds::kMenuUserSettings)) });
        if (context.isAdmin) {
            page.rows.append(
                    { mgr->makeNavigateButton(tr("Administration"),
                                              QLatin1String(MessengerMenuIds::kMenuAdminHome),
                                              true) });
        }
    }

    return page;
}
