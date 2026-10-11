#include "MessengerResourcesHomeMenu.h"

#include "MessengerMenuIds.h"

#include "UserModel.h"
#include "MessengerBot.h"
#include "ResourceModel.h"
#include "ServerDatabase.h"
#include "ResourceQueueModel.h"
#include "MessengerTextRender.h"
#include "ServerDateTimeFormat.h"

#include <QCollator>

namespace {
constexpr int kReleaseOccupiedResourcesThreshold = 2;
constexpr char kReleaseOccupiedResourcesEvent[] = "rh:release_occupied";

QString escapeHtmlLocal(const QString &text)
{
    return MessengerMenuManager::escapeHtml(text);
}
} // namespace

MessengerResourcesHomeMenu::MessengerResourcesHomeMenu(MessengerMenuManager *manager)
    : MessengerAbstractMenu(manager)
{
    // do nothing
}

QString MessengerResourcesHomeMenu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuResourcesHome);
}

QStringList MessengerResourcesHomeMenu::eventKeys() const
{
    return { QStringLiteral("rh") };
}

MessengerMenu::MenuPage MessengerResourcesHomeMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuResourcesHome);
    page.title = tr("Booking Resources");
    page.body = tr("Available resources (name, short description, status). "
                   "Pick a resource for details or use quick filters.");

    page.dynamicText = [this](const MessengerMenu::RenderContext &ctx) {
        MessengerMenu::MenuPage::DynamicText out;

        MessengerMenuManager *mgr = menuManager();
        MessengerBot *bot = mgr ? mgr->bot() : nullptr;
        const bool showAsOf =
                mgr && mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive;

        const AbstractBot *backendBot = bot ? bot->backendBot() : nullptr;
        const bool useHtml =
                MessengerTextRender::shouldUseHtmlLists(ctx.chat.messenger, backendBot);
        out.format =
                useHtml ? AbstractBot::MessageFormat::Html : AbstractBot::MessageFormat::PlainText;

        ServerDatabase *database = bot ? bot->database() : nullptr;
        if (!database || !database->isReady()) {
            const QString text = tr("Database is not available right now.");
            out.text = showAsOf ? mgr->buildTextWithAsOfCurrentFormat(text, out.format) : text;
            return out;
        }

        const ResourceModel *resourceModel = database->resourceModel();
        if (!resourceModel) {
            const QString text = tr("Resource model is not available right now.");
            out.text = showAsOf ? mgr->buildTextWithAsOfCurrentFormat(text, out.format) : text;
            return out;
        }

        const MessengerUserKey viewerKey = AbstractBot::senderKey(ctx.chat, ctx.sender);
        QList<Resource> resources = resourceModel->items();
        resources.erase(std::remove_if(resources.begin(),
                                       resources.end(),
                                       [database, &ctx, &viewerKey](const Resource &resource) {
                                           return !MessengerTextRender::isResourceVisibleToViewer(
                                                   resource,
                                                   database,
                                                   ctx.chat.messenger,
                                                   viewerKey);
                                       }),
                        resources.end());
        if (resources.isEmpty()) {
            const QString text = tr("No resources in database.");
            out.text = showAsOf ? mgr->buildTextWithAsOfCurrentFormat(text, out.format) : text;
            return out;
        }

        QCollator collator;
        collator.setNumericMode(true);
        std::sort(resources.begin(),
                  resources.end(),
                  [&collator](const Resource &lhs, const Resource &rhs) {
                      const QString lhsKey = lhs.name().trimmed();
                      const QString rhsKey = rhs.name().trimmed();

                      const bool lhsEmpty = lhsKey.isEmpty();
                      const bool rhsEmpty = rhsKey.isEmpty();
                      if (lhsEmpty != rhsEmpty)
                          return rhsEmpty; // Put empty names at the end.

                      return collator.compare(lhsKey, rhsKey) < 0;
                  });

        const UserModel *userModel = database->userModel();
        const ResourceQueueModel *queueModel = database->resourceQueueModel();
        const QString dateTimeFormat = ServerDateTimeFormat::effectiveFormat(database);
        const MessengerUserKey self(ctx.chat.messenger, ctx.sender.userId.trimmed());
        const UserTimeZone viewerTimeZone =
                bot ? bot->effectiveTimeZoneForUser(self) : UserTimeZone();

        QHash<MessengerUserKey, QString> userDisplayCache;
        const auto formatUserDisplayOrKey = [&](const MessengerUserKey &userKey) -> QString {
            const QHash<MessengerUserKey, QString>::const_iterator cached =
                    userDisplayCache.constFind(userKey);
            if (cached != userDisplayCache.cend())
                return cached.value();

            QString display;
            if (userModel && userKey.isValid()) {
                const User userEntry = userModel->userByKey(userKey);
                if (userEntry.isValid()) {
                    display = MessengerTextRender::formatUserDisplayName(userEntry, useHtml);
                }
            }

            if (display.isEmpty()) {
                const User unresolvedUser(userKey, QString(), QString(), User::Role::UserRole);
                display = MessengerTextRender::formatUserDisplayName(unresolvedUser, useHtml);
            }

            userDisplayCache.insert(userKey, display);
            return display;
        };

        static constexpr int kSchedulePreviewLimit = 3;
        const QDateTime nowUtc = QDateTime::currentDateTimeUtc();

        QStringList lines;
        int index = 1;
        for (const Resource &resource : resources) {
            const QString name = resource.name().trimmed();
            const QString displayName =
                    MessengerTextRender::formatResourceNameWithStatus(resource, false);
            const QString description = resource.description().trimmed();

            const QString numberedName =
                    useHtml ? QStringLiteral("<b>%1.</b> %2")
                                      .arg(QString::number(index), escapeHtmlLocal(displayName))
                            : QStringLiteral("%1) %2").arg(QString::number(index), displayName);
            lines << numberedName;

            if (!description.isEmpty()) {
                const QString descLine = tr("Description: %1").arg(description);
                lines << QStringLiteral("   %1").arg(
                        useHtml ? escapeHtmlLocal(descLine)
                                : MessengerTextRender::collapseWhitespace(descLine));
            }

            ResourceQueueEntry occupant;
            QList<ResourceQueueEntry> schedules;
            if (queueModel && !name.isEmpty()) {
                occupant = queueModel->currentOccupantForResource(name);
                schedules = queueModel->schedulesForResource(name);
            }

            QList<ResourceQueueEntry> renderedSchedules = schedules;
            if (occupant.isValid()) {
                const ResourceQueueKey occupantKey = occupant.key();
                for (int i = renderedSchedules.size() - 1; i >= 0; --i) {
                    if (renderedSchedules.at(i).key() == occupantKey)
                        renderedSchedules.removeAt(i);
                }
            }

            if (useHtml) {
                if (!occupant.isValid()) {
                    lines << QStringLiteral("   %1").arg(escapeHtmlLocal(tr("Currently: free")));
                } else {
                    const QString display = formatUserDisplayOrKey(occupant.user());
                    const QString occupiedLine =
                            escapeHtmlLocal(tr("Currently: occupied - %1")).arg(display);
                    lines << QStringLiteral("   %1").arg(occupiedLine);
                    const QString startLocal =
                            MessengerTextRender::formatLocalDateTime(occupant.startAtUtc(),
                                                                     QString(),
                                                                     dateTimeFormat,
                                                                     viewerTimeZone);
                    const QString endLocal = MessengerTextRender::formatLocalDateTime(
                            occupant.endAtUtc(),
                            MessengerTextRender::formatDurationMinutes(0),
                            dateTimeFormat,
                            viewerTimeZone);
                    lines << QStringLiteral("   %1").arg(
                            escapeHtmlLocal(tr("Window: %1 → %2").arg(startLocal, endLocal)));
                }

                if (renderedSchedules.isEmpty()) {
                    lines << QStringLiteral("   %1").arg(escapeHtmlLocal(tr("Schedule: empty")));
                } else {
                    lines << QStringLiteral("   %1").arg(escapeHtmlLocal(
                            tr("Schedule (%1):").arg(QString::number(renderedSchedules.size()))));
                    const int renderCount = qMin(renderedSchedules.size(), kSchedulePreviewLimit);
                    for (int i = 0; i < renderCount; ++i) {
                        const ResourceQueueEntry &entry = renderedSchedules.at(i);
                        const QString display = formatUserDisplayOrKey(entry.user());
                        lines << QStringLiteral("      %1) %2")
                                         .arg(QString::number(i + 1), display);

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
                        const QString status =
                                entry.isActiveAtUtc(nowUtc) ? tr("active") : tr("upcoming");
                        lines << QStringLiteral("         %1")
                                         .arg(escapeHtmlLocal(
                                                 tr("Window: %1 → %2 | Status: %3")
                                                         .arg(startLocal, endLocal, status)));
                    }
                    if (renderedSchedules.size() > kSchedulePreviewLimit) {
                        lines << QStringLiteral("      ...");
                    }
                }
            } else {
                if (!occupant.isValid()) {
                    lines << QStringLiteral("   %1").arg(tr("Currently: free"));
                } else {
                    const QString display = formatUserDisplayOrKey(occupant.user());
                    lines << QStringLiteral("   %1").arg(
                            tr("Currently: occupied - %1").arg(display));
                    const QString startLocal =
                            MessengerTextRender::formatLocalDateTime(occupant.startAtUtc(),
                                                                     QString(),
                                                                     dateTimeFormat,
                                                                     viewerTimeZone);
                    const QString endLocal = MessengerTextRender::formatLocalDateTime(
                            occupant.endAtUtc(),
                            MessengerTextRender::formatDurationMinutes(0),
                            dateTimeFormat,
                            viewerTimeZone);
                    lines << QStringLiteral("   %1").arg(
                            tr("Window: %1 → %2").arg(startLocal, endLocal));
                }

                if (renderedSchedules.isEmpty()) {
                    lines << QStringLiteral("   %1").arg(tr("Schedule: empty"));
                } else {
                    lines << QStringLiteral("   %1").arg(
                            tr("Schedule (%1):").arg(QString::number(renderedSchedules.size())));

                    const int renderCount = qMin(renderedSchedules.size(), kSchedulePreviewLimit);
                    for (int i = 0; i < renderCount; ++i) {
                        const ResourceQueueEntry &entry = renderedSchedules.at(i);
                        const QString display = formatUserDisplayOrKey(entry.user());
                        lines << QStringLiteral("      %1) %2")
                                         .arg(QString::number(i + 1), display);

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
                        const QString status =
                                entry.isActiveAtUtc(nowUtc) ? tr("active") : tr("upcoming");
                        lines << QStringLiteral("         %1")
                                         .arg(tr("Window: %1 → %2 | Status: %3")
                                                      .arg(startLocal, endLocal, status));
                    }
                    if (renderedSchedules.size() > kSchedulePreviewLimit) {
                        lines << QStringLiteral("      ...");
                    }
                }
            }

            if (index <= resources.size())
                lines << QString();
            ++index;
        }

        const QString rendered = lines.join(QStringLiteral("\n")).trimmed();
        out.text = showAsOf ? mgr->buildTextWithAsOfCurrentFormat(rendered, out.format) : rendered;
        return out;
    };

    MessengerMenuManager *mgr = menuManager();
    if (mgr) {
        MessengerBot *bot = mgr->bot();
        ServerDatabase *database = bot ? bot->database() : nullptr;
        const MessengerUserKey viewerKey = AbstractBot::senderKey(context.chat, context.sender);

        if (mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive) {
            page.rows.append({ mgr->makeNavigateButton(
                    tr("Refresh list"), QLatin1String(MessengerMenuIds::kMenuResourcesHome)) });
        }
        const QStringList currentOccupations =
                MessengerTextRender::currentOccupationResourceNames(database,
                                                                    context.chat.messenger,
                                                                    viewerKey);
        if (currentOccupations.size() >= kReleaseOccupiedResourcesThreshold) {
            page.rows.append(
                    { mgr->makeEventButton(tr("Release occupied resources"),
                                           QLatin1String(kReleaseOccupiedResourcesEvent)) });
        }
        page.rows.append({ mgr->makeNavigateButton(
                tr("Resource cards"), QLatin1String(MessengerMenuIds::kMenuResourceCardUser)) });
        page.rows.append(
                { mgr->makeNavigateButton(tr("Free resources"),
                                          QLatin1String(MessengerMenuIds::kMenuResourcesFree)),
                  mgr->makeNavigateButton(tr("Releases soon"),
                                          QLatin1String(
                                                  MessengerMenuIds::kMenuResourcesSoonest)) });
        page.rows.append({ mgr->makeNavigateButton(tr("Back"),
                                                   QLatin1String(MessengerMenuIds::kMenuMain)) });
    }

    return page;
}

bool MessengerResourcesHomeMenu::handleEvent(const QString &event,
                                             const QString &sessionId,
                                             const MessengerMenu::RenderContext &context,
                                             MessengerMenuManager::ActionResult *result) const
{
    Q_UNUSED(sessionId);

    if (!result)
        return false;

    const QString trimmed = event.trimmed();
    if (trimmed != QLatin1String(kReleaseOccupiedResourcesEvent))
        return false;

    MessengerMenuManager *mgr = menuManager();
    MessengerBot *bot = mgr ? mgr->bot() : nullptr;
    ServerDatabase *database = bot ? bot->database() : nullptr;
    const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
    const QStringList resourceNames =
            MessengerTextRender::currentOccupationResourceNames(database,
                                                                context.chat.messenger,
                                                                actor);

    result->handled = true;
    result->sendReply = true;

    if (resourceNames.size() < kReleaseOccupiedResourcesThreshold)
        return true;

    for (const QString &resourceName : resourceNames) {
        const ServerDatabaseError releaseError =
                database->releaseUserOccupationForResource(resourceName, actor, actor);
        if (releaseError) {
            result->reply = MessengerMenuManager::buildPlainTextReply(releaseError.errorString());
            return true;
        }

        if (bot)
            bot->notifyNextUserQueueStatus(resourceName, actor);
    }

    return true;
}
