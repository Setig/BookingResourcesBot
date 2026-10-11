#include "MessengerResourceScheduleMenu.h"

#include "MessengerMenuIds.h"
#include "MessengerUserResourcesState.h"

#include "Resource.h"
#include "MessengerBot.h"
#include "ResourceModel.h"
#include "ServerDatabase.h"
#include "ResourceQueueModel.h"
#include "MessengerTextRender.h"
#include "ServerDateTimeFormat.h"
#include "ResourceQueueUserMessages.h"

#include <QCollator>
#include <QDateTime>
#include <QTimeZone>
#include <QRegularExpression>

namespace {
QString resourceLabelForButton(const Resource &resource)
{
    return MessengerTextRender::formatResourceNameWithStatus(resource, false);
}

QTimeZone effectiveTimeZone(const UserTimeZone &timeZone)
{
    return ServerDateTimeFormat::effectiveTimeZone(timeZone);
}

bool parseScheduleDateTimeToken(const QString &token,
                                const QString &dateTimeFormat,
                                const UserTimeZone &timeZone,
                                QDateTime *utcOut)
{
    return ServerDateTimeFormat::parseUserDateTime(token,
                                                   dateTimeFormat,
                                                   ServerDateTimeFormat::ParseMode::LocalToUtc,
                                                   utcOut,
                                                   timeZone);
}

MessengerUserResourcesState::WizardStep scheduleStepAfterStart(
        const MessengerUserResourcesState::Wizard &wizard, const QDateTime &startUtc)
{
    if (!wizard.scheduleReviewReached)
        return MessengerUserResourcesState::WizardStep::ScheduleEnd;
    if (!wizard.scheduleEndUtc.isValid() || wizard.scheduleEndUtc > startUtc)
        return MessengerUserResourcesState::WizardStep::ScheduleReview;
    return MessengerUserResourcesState::WizardStep::ScheduleEnd;
}
} // namespace

MessengerResourceScheduleMenu::MessengerResourceScheduleMenu(MessengerMenuManager *manager,
                                                             MessengerUserResourcesState *state)
    : MessengerAbstractMenu(manager)
    , m_state(state)
{
    // do nothing
}

QString MessengerResourceScheduleMenu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuResourceSchedule);
}

MessengerMenu::MenuPage MessengerResourceScheduleMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuResourceSchedule);
    page.title = tr("Scheduling");

    MessengerMenuManager *mgr = menuManager();
    if (!mgr || !m_state) {
        page.body = tr("Menu session is not available right now.");
        return page;
    }

    const QString sid = mgr->sessionId(context);
    MessengerUserResourcesState::State *state = m_state->ensure(sid);
    if (!state) {
        page.body = tr("Menu session is not available right now.");
        page.rows.append(
                { mgr->makeNavigateButton(tr("Back to resources"),
                                          QLatin1String(MessengerMenuIds::kMenuResourcesHome)) });
        return page;
    }

    mgr->setCurrentMenuId(context, page.id, false);

    ServerDatabase *database = mgr->bot() ? mgr->bot()->database() : nullptr;
    const QString dateTimeFormat = ServerDateTimeFormat::effectiveFormat(database);
    const MessengerUserKey viewerKey = AbstractBot::senderKey(context.chat, context.sender);
    const UserTimeZone viewerTimeZone =
            mgr->bot() ? mgr->bot()->effectiveTimeZoneForUser(viewerKey) : UserTimeZone();
    const ResourceModel *resourceModel =
            (database && database->isReady()) ? database->resourceModel() : nullptr;

    QString selectedResourceUuid = state->selectedResourceUuid.trimmed();
    Resource selectedResource;
    if (!selectedResourceUuid.isEmpty() && resourceModel) {
        selectedResource = resourceModel->resourceByUuid(selectedResourceUuid);
        if (!MessengerTextRender::isResourceVisibleToViewer(selectedResource,
                                                            database,
                                                            context.chat.messenger,
                                                            viewerKey)) {
            state->selectedResourceUuid.clear();
            state->wizard.clear();
            selectedResourceUuid.clear();
            selectedResource = Resource();
        }
    }

    QString selectedResourceName;
    if (selectedResource.isValid())
        selectedResourceName = selectedResource.name().trimmed();
    const QString selectedResourceLabel =
            selectedResource.isValid()
                    ? MessengerTextRender::formatResourceNameWithStatus(selectedResource, false)
                    : tr("(unknown)");
    const bool selectedResourceTemporarilyUnavailable =
            selectedResource.isValid()
            && selectedResource.status() == Resource::Status::TemporarilyUnavailable;

    if (selectedResourceUuid.isEmpty()) {
        // Selector mode.
        page.title = tr("Select a resource");

        const bool needRefresh =
                state->selector.items.isEmpty() || !state->selector.updatedAtUtc.isValid();
        if (needRefresh) {
            state->selector.clear();
            state->selector.pageSize = MessengerMenuIds::kSelectorPageSize;
            state->selector.updatedAtUtc = QDateTime::currentDateTimeUtc();

            if (resourceModel) {
                const QList<Resource> resources = resourceModel->items();
                QVector<MessengerUserResourcesState::SelectorItem> items;
                items.reserve(resources.size());
                for (const Resource &r : resources) {
                    if (!r.isValid())
                        continue;
                    if (!MessengerTextRender::isResourceVisibleToViewer(r,
                                                                        database,
                                                                        context.chat.messenger,
                                                                        viewerKey))
                        continue;
                    const QString id = r.uuidString();
                    const QString label = resourceLabelForButton(r);
                    if (id.isEmpty() || label.trimmed().isEmpty())
                        continue;
                    MessengerUserResourcesState::SelectorItem item;
                    item.id = id;
                    item.label = label;
                    items.append(item);
                }

                QCollator collator;
                collator.setNumericMode(true);
                std::sort(items.begin(),
                          items.end(),
                          [&collator](const MessengerUserResourcesState::SelectorItem &lhs,
                                      const MessengerUserResourcesState::SelectorItem &rhs) {
                              return collator.compare(lhs.label, rhs.label) < 0;
                          });
                state->selector.items = items;
            }
        }

        const int total = state->selector.items.size();
        const int pageSize = qMax(1, state->selector.pageSize);
        const int pageCount = (total + pageSize - 1) / pageSize;
        if (pageCount > 0) {
            state->selector.page = qBound(0, state->selector.page, pageCount - 1);
        } else {
            state->selector.page = 0;
        }

        QStringList bodyLines;
        QString asOf;
        if (mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive) {
            asOf = MessengerMenuManager::buildAsOfLine(state->selector.updatedAtUtc,
                                                       AbstractBot::MessageFormat::PlainText,
                                                       QString(),
                                                       viewerTimeZone);
        }
        if (!asOf.trimmed().isEmpty())
            bodyLines << asOf;

        if (!resourceModel) {
            bodyLines << tr("Database is not available right now.");
        } else if (total <= 0) {
            bodyLines << tr("No resources in database.");
        } else {
            bodyLines << tr("Items: %1").arg(QString::number(total));
            if (pageCount > 1) {
                bodyLines << tr("Page: %1/%2")
                                     .arg(QString::number(state->selector.page + 1),
                                          QString::number(pageCount));
            }
        }

        page.body = bodyLines.join(QStringLiteral("\n")).trimmed();

        if (mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive) {
            page.rows.append(
                    { mgr->makeEventButton(tr("Refresh list"), QStringLiteral("refresh")) });
        }

        if (total > 0) {
            const int start = state->selector.page * pageSize;
            const int end = qMin(total, start + pageSize);
            for (int i = start; i < end; ++i) {
                const MessengerUserResourcesState::SelectorItem &item = state->selector.items.at(i);
                page.rows.append(
                        { mgr->makeEventButton(item.label, QStringLiteral("sel:%1").arg(i)) });
            }
        }

        QVector<MessengerMenu::MenuButton> navRow;
        if (pageCount > 1 && state->selector.page > 0) {
            navRow.append(mgr->makeEventButton(tr("Previous page"), QStringLiteral("page:prev")));
        }
        if (pageCount > 1 && state->selector.page + 1 < pageCount) {
            navRow.append(mgr->makeEventButton(tr("Next page"), QStringLiteral("page:next")));
        }
        if (!navRow.isEmpty())
            page.rows.append(navRow);

        page.rows.append(
                { mgr->makeNavigateButton(tr("Back to resources"),
                                          QLatin1String(MessengerMenuIds::kMenuResourcesHome)) });
        return page;
    }

    // Review step.
    if (state->wizard.mode == MessengerUserResourcesState::WizardMode::Schedule
        && state->wizard.step == MessengerUserResourcesState::WizardStep::ScheduleReview) {
        state->wizard.scheduleReviewReached = true;
        const QString startLocal =
                MessengerTextRender::formatLocalDateTime(state->wizard.scheduleStartUtc,
                                                         QString(),
                                                         dateTimeFormat,
                                                         viewerTimeZone);
        const QString endLocal =
                MessengerTextRender::formatLocalDateTime(state->wizard.scheduleEndUtc,
                                                         MessengerTextRender::formatDurationMinutes(
                                                                 0),
                                                         dateTimeFormat,
                                                         viewerTimeZone);

        QStringList lines;
        lines << tr("Resource: %1").arg(selectedResourceLabel);
        lines << tr("Start: %1").arg(startLocal);
        lines << tr("End: %1").arg(endLocal);
        page.body = lines.join(QStringLiteral("\n")).trimmed();

        page.rows.append(
                { mgr->makeEventButton(tr("Edit start"), QStringLiteral("ur:sch:edit:start")),
                  mgr->makeEventButton(tr("Edit end"), QStringLiteral("ur:sch:edit:end")) });
        page.rows.append({ mgr->makeEventButton(tr("Accept"), QStringLiteral("ur:sch:apply")) });
        page.rows.append(
                { mgr->makeEventButton(tr("Back to resource"), QStringLiteral("ur:nav:card")) });

        MessengerMenuManager::prependErrorBlock(&page, state->wizard.errorText);
        return page;
    }

    // Start/end picking step.
    if (state->wizard.mode == MessengerUserResourcesState::WizardMode::Schedule
        && (state->wizard.step == MessengerUserResourcesState::WizardStep::ScheduleStart
            || state->wizard.step == MessengerUserResourcesState::WizardStep::ScheduleEnd)) {
        const QString stepLabel =
                (state->wizard.step == MessengerUserResourcesState::WizardStep::ScheduleStart)
                        ? tr("Send schedule start time (%1):").arg(dateTimeFormat)
                        : tr("Send schedule end time (%1):").arg(dateTimeFormat);
        page.body = tr("Resource: %1\n%2").arg(selectedResourceLabel, stepLabel);

        const QDate today = ServerDateTimeFormat::toLocalDateTime(QDateTime::currentDateTimeUtc(),
                                                                  viewerTimeZone)
                                    .date();
        const QDate pickedDate = state->wizard.schedulePickedLocalDate;
        const QTime pickedTime = state->wizard.schedulePickedLocalTime;

        QVector<MessengerMenu::MenuButton> daysRow;
        for (int i = 0; i < 7; ++i) {
            const QDate date = today.addDays(i);
            QString label = date.toString(Qt::ISODate);
            if (pickedDate.isValid() && date == pickedDate) {
                label = QStringLiteral("[%1]").arg(label);
            }
            const QString value =
                    QStringLiteral("ur:sch:day:%1").arg(date.toString(QStringLiteral("yyyyMMdd")));
            daysRow.append(mgr->makeEventButton(label, value));
            if (daysRow.size() >= 3) {
                page.rows.append(daysRow);
                daysRow.clear();
            }
        }
        if (!daysRow.isEmpty())
            page.rows.append(daysRow);

        if (pickedDate.isValid()) {
            const int resolvedPage = (state->wizard.scheduleTimePage <= 0) ? 0 : 1;
            const int startHour = resolvedPage * 12;

            QVector<MessengerMenu::MenuButton> timeRow;
            for (int hour = startHour; hour < startHour + 12; ++hour) {
                for (int minute : { 0, 30 }) {
                    const QString label = QStringLiteral("%1:%2")
                                                  .arg(hour, 2, 10, QLatin1Char('0'))
                                                  .arg(minute, 2, 10, QLatin1Char('0'));
                    const QString renderedLabel = (pickedTime.isValid() && pickedTime.hour() == hour
                                                   && pickedTime.minute() == minute)
                                                          ? QStringLiteral("[%1]").arg(label)
                                                          : label;
                    const QString value = QStringLiteral("ur:sch:time:%1%2")
                                                  .arg(hour, 2, 10, QLatin1Char('0'))
                                                  .arg(minute, 2, 10, QLatin1Char('0'));
                    timeRow.append(mgr->makeEventButton(renderedLabel, value));
                    if (timeRow.size() >= 4) {
                        page.rows.append(timeRow);
                        timeRow.clear();
                    }
                }
            }
            if (!timeRow.isEmpty())
                page.rows.append(timeRow);

            QVector<MessengerMenu::MenuButton> navRow;
            if (resolvedPage > 0) {
                navRow.append(mgr->makeEventButton(tr("Previous page"),
                                                   QStringLiteral("ur:sch:timepage:prev")));
            }
            if (resolvedPage < 1) {
                navRow.append(mgr->makeEventButton(tr("Next page"),
                                                   QStringLiteral("ur:sch:timepage:next")));
            }
            if (!navRow.isEmpty())
                page.rows.append(navRow);
        }

        const bool canConfirm = pickedDate.isValid() && pickedTime.isValid();
        if (state->wizard.step == MessengerUserResourcesState::WizardStep::ScheduleEnd) {
            page.rows.append({ mgr->makeEventButton(tr("Indefinite"),
                                                    QStringLiteral("ur:sch:end:indefinite")) });
        }
        if (canConfirm) {
            page.rows.append(
                    { mgr->makeEventButton(tr("Accept"), QStringLiteral("ur:sch:confirm")) });
        }
        page.rows.append({ mgr->makeEventButton(tr("Back"), QStringLiteral("ur:cancel")) });

        MessengerMenuManager::prependErrorBlock(&page, state->wizard.errorText);
        return page;
    }

    // Default scheduling home view.
    page.body = tr("Choose start/end windows. Available slots will be shown here.");
    page.dynamicText = [mgr, selectedResourceUuid](const MessengerMenu::RenderContext &ctx) {
        MessengerMenu::MenuPage::DynamicText out;
        MessengerBot *bot = mgr ? mgr->bot() : nullptr;

        QString resourceName;
        ServerDatabase *database = bot ? bot->database() : nullptr;
        if (database && database->isReady() && database->resourceModel()) {
            const Resource resource =
                    database->resourceModel()->resourceByUuid(selectedResourceUuid);
            if (resource.isValid())
                resourceName = resource.name();
        }

        const MessengerTextRender::RenderedText rendered =
                MessengerTextRender::renderResourceQueueStatus(bot ? bot->database() : nullptr,
                                                               ctx.chat.messenger,
                                                               resourceName,
                                                               AbstractBot::senderKey(ctx.chat,
                                                                                      ctx.sender),
                                                               bot ? bot->backendBot() : nullptr);
        out.text = rendered.text.trimmed();
        out.format = rendered.format;
        return out;
    };

    bool canUnschedule = false;
    MessengerBot *bot = mgr->bot();
    if (bot && bot->database() && bot->database()->isReady()
        && bot->database()->resourceQueueModel()) {
        const ResourceQueueModel *queueModel = bot->database()->resourceQueueModel();
        const MessengerUserKey currentUser = AbstractBot::senderKey(context.chat, context.sender);
        const QList<ResourceQueueEntry> schedules =
                queueModel->schedulesForResource(selectedResourceName);
        for (const ResourceQueueEntry &entry : schedules) {
            if (entry.user() == currentUser) {
                canUnschedule = true;
                break;
            }
        }
    }

    QVector<MessengerMenu::MenuButton> row;
    if (!selectedResourceTemporarilyUnavailable) {
        row.append(mgr->makeEventButton(tr("Schedule slot"), QStringLiteral("ur:schedule:start")));
    }
    if (canUnschedule) {
        row.append(mgr->makeEventButton(tr("Cancel my slot"), QStringLiteral("ur:unschedule")));
    }
    if (!row.isEmpty())
        page.rows.append(row);
    page.rows.append(
            { mgr->makeEventButton(tr("Back to resource"), QStringLiteral("ur:nav:card")) });

    MessengerMenuManager::prependErrorBlock(&page, state->wizard.errorText);

    return page;
}

bool MessengerResourceScheduleMenu::handleEvent(const QString &event,
                                                const QString &sessionId,
                                                const MessengerMenu::RenderContext &context,
                                                MessengerMenuManager::ActionResult *result) const
{
    if (!result || !m_state)
        return false;

    MessengerMenuManager *mgr = menuManager();
    if (!mgr)
        return false;
    const MessengerBot *ownerBot = mgr->bot();
    const MessengerUserKey viewerKey = AbstractBot::senderKey(context.chat, context.sender);
    const UserTimeZone viewerTimeZone =
            ownerBot ? ownerBot->effectiveTimeZoneForUser(viewerKey) : UserTimeZone();
    const QString dateTimeFormat =
            ServerDateTimeFormat::effectiveFormat(ownerBot ? ownerBot->database() : nullptr);

    MessengerUserResourcesState::State *state = m_state->ensure(sessionId);
    if (!state)
        return false;

    const QString trimmed = event.trimmed();
    if (trimmed.isEmpty())
        return false;

    // Selector events when resource is not selected yet.
    if (state->selectedResourceUuid.trimmed().isEmpty()) {
        if (trimmed == QStringLiteral("page:prev")) {
            if (state->selector.page > 0) {
                state->selector.page -= 1;
            }
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        if (trimmed == QStringLiteral("page:next")) {
            const int total = state->selector.items.size();
            const int pageSize = qMax(1, state->selector.pageSize);
            const int pageCount = (total + pageSize - 1) / pageSize;
            if (state->selector.page + 1 < pageCount) {
                state->selector.page += 1;
            }
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        if (trimmed == QStringLiteral("refresh")) {
            state->selector.clear();
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        static const QRegularExpression selRegex(QStringLiteral("^sel:(\\d+)$"));
        const QRegularExpressionMatch selMatch = selRegex.match(trimmed);
        if (selMatch.hasMatch()) {
            bool ok = false;
            const int index = selMatch.captured(1).toInt(&ok);
            if (!ok || index < 0 || index >= state->selector.items.size()) {
                result->handled = true;
                result->sendReply = true;
                return true;
            }

            const MessengerUserResourcesState::SelectorItem item = state->selector.items.at(index);
            state->selector.clear();
            state->wizard.clear();
            state->selectedResourceUuid = item.id;

            result->handled = true;
            result->sendReply = true;
            return true;
        }
    }

    if (trimmed == QStringLiteral("ur:cancel")) {
        state->wizard.clear();
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (trimmed == QStringLiteral("ur:nav:card")) {
        state->wizard.clear();
        mgr->setCurrentMenuId(context,
                              QLatin1String(MessengerMenuIds::kMenuResourceCardUser),
                              false);
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (trimmed == QStringLiteral("ur:unschedule")) {
        state->wizard.errorText.clear();

        const QString resourceUuid = state->selectedResourceUuid.trimmed();
        if (resourceUuid.isEmpty()) {
            state->wizard.errorText = tr("Select a resource.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        MessengerBot *bot = mgr->bot();
        if (!bot || !bot->database() || !bot->database()->isReady()) {
            state->wizard.errorText = tr("Database is not available right now.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        const ResourceModel *resourceModel = bot->database()->resourceModel();
        const Resource resource =
                resourceModel ? resourceModel->resourceByUuid(resourceUuid) : Resource();
        const QString resourceName = resource.isValid() ? resource.name() : QString();
        if (resourceName.trimmed().isEmpty()) {
            state->wizard.errorText = tr("Select a resource.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
        const ServerDatabaseError scheduleError =
                bot->database()->removeUserSchedulesForResource(resourceName, actor, actor);
        if (scheduleError) {
            state->wizard.errorText = scheduleError.errorString();
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        state->wizard.errorText.clear();
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (trimmed == QStringLiteral("ur:schedule:start")) {
        const QString resourceUuid = state->selectedResourceUuid.trimmed();
        if (resourceUuid.isEmpty()) {
            state->wizard.errorText = tr("Select a resource.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        ServerDatabase *database = ownerBot ? ownerBot->database() : nullptr;
        const ResourceModel *resourceModel =
                (database && database->isReady()) ? database->resourceModel() : nullptr;
        const Resource resource =
                resourceModel ? resourceModel->resourceByUuid(resourceUuid) : Resource();
        if (!MessengerTextRender::isResourceVisibleToViewer(
                    resource,
                    database,
                    context.chat.messenger,
                    AbstractBot::senderKey(context.chat, context.sender))) {
            state->wizard.errorText = tr("Select a resource.");
            state->selectedResourceUuid.clear();
            state->wizard.clear();
            result->handled = true;
            result->sendReply = true;
            return true;
        }
        if (resource.isValid() && resource.status() == Resource::Status::TemporarilyUnavailable) {
            state->wizard.errorText = ResourceQueueUserMessages::resourceTemporarilyUnavailable(
                    resource.name().trimmed());
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        state->wizard.clear();
        state->wizard.mode = MessengerUserResourcesState::WizardMode::Schedule;
        state->wizard.step = MessengerUserResourcesState::WizardStep::ScheduleStart;
        mgr->setCurrentMenuId(context,
                              QLatin1String(MessengerMenuIds::kMenuResourceSchedule),
                              false);

        result->handled = true;
        result->sendReply = true;
        MessengerMenuManager::requestPreferEdit(result, mgr->lastMenuMessageId(context));
        return true;
    }

    if (trimmed.startsWith(QStringLiteral("ur:sch:day:"))) {
        if (state->wizard.mode != MessengerUserResourcesState::WizardMode::Schedule
            || (state->wizard.step != MessengerUserResourcesState::WizardStep::ScheduleStart
                && state->wizard.step != MessengerUserResourcesState::WizardStep::ScheduleEnd)) {
            return true;
        }

        const QString token = trimmed.mid(QStringLiteral("ur:sch:day:").size()).trimmed();
        const QDate date = QDate::fromString(token, QStringLiteral("yyyyMMdd"));
        if (!date.isValid()) {
            state->wizard.errorText = ResourceQueueUserMessages::invalidScheduleDate();
        } else {
            state->wizard.errorText.clear();
            state->wizard.schedulePickedLocalDate = date;
            state->wizard.scheduleTimePage = 0;
        }

        result->handled = true;
        result->sendReply = true;
        MessengerMenuManager::requestPreferEdit(result, mgr->lastMenuMessageId(context));
        return true;
    }

    if (trimmed == QStringLiteral("ur:sch:timepage:prev")
        || trimmed == QStringLiteral("ur:sch:timepage:next")) {
        if (state->wizard.mode != MessengerUserResourcesState::WizardMode::Schedule
            || (state->wizard.step != MessengerUserResourcesState::WizardStep::ScheduleStart
                && state->wizard.step != MessengerUserResourcesState::WizardStep::ScheduleEnd)) {
            return true;
        }
        if (!state->wizard.schedulePickedLocalDate.isValid())
            return true;

        if (trimmed == QStringLiteral("ur:sch:timepage:prev")) {
            if (state->wizard.scheduleTimePage > 0)
                state->wizard.scheduleTimePage -= 1;
        } else {
            if (state->wizard.scheduleTimePage < 1)
                state->wizard.scheduleTimePage += 1;
        }

        result->handled = true;
        result->sendReply = true;
        MessengerMenuManager::requestPreferEdit(result, mgr->lastMenuMessageId(context));
        return true;
    }

    if (trimmed.startsWith(QStringLiteral("ur:sch:time:"))) {
        if (state->wizard.mode != MessengerUserResourcesState::WizardMode::Schedule
            || (state->wizard.step != MessengerUserResourcesState::WizardStep::ScheduleStart
                && state->wizard.step != MessengerUserResourcesState::WizardStep::ScheduleEnd)) {
            return true;
        }

        const QString timeToken = trimmed.mid(QStringLiteral("ur:sch:time:").size()).trimmed();
        if (timeToken.size() != 4) {
            state->wizard.errorText = ResourceQueueUserMessages::invalidScheduleTime();
            result->handled = true;
            result->sendReply = true;
            MessengerMenuManager::requestPreferEdit(result, mgr->lastMenuMessageId(context));
            return true;
        }

        auto parseTwoDigits = [](const QString &s, const int offset, bool *ok) -> int {
            if (ok)
                *ok = false;
            if (offset < 0 || offset + 1 >= s.size())
                return 0;
            const int d1 = s.at(offset).digitValue();
            const int d2 = s.at(offset + 1).digitValue();
            if (d1 < 0 || d2 < 0)
                return 0;
            if (ok)
                *ok = true;
            return d1 * 10 + d2;
        };

        bool okH = false;
        const int hour = parseTwoDigits(timeToken, 0, &okH);
        bool okM = false;
        const int minute = parseTwoDigits(timeToken, 2, &okM);
        if (!okH || !okM || hour < 0 || hour > 23 || (minute != 0 && minute != 30)) {
            state->wizard.errorText = ResourceQueueUserMessages::invalidScheduleTime();
            result->handled = true;
            result->sendReply = true;
            MessengerMenuManager::requestPreferEdit(result, mgr->lastMenuMessageId(context));
            return true;
        }

        state->wizard.errorText.clear();
        state->wizard.schedulePickedLocalTime = QTime(hour, minute);
        state->wizard.scheduleTimePage = (hour >= 12) ? 1 : 0;

        result->handled = true;
        result->sendReply = true;
        MessengerMenuManager::requestPreferEdit(result, mgr->lastMenuMessageId(context));
        return true;
    }

    if (trimmed == QStringLiteral("ur:sch:confirm")) {
        if (state->wizard.mode != MessengerUserResourcesState::WizardMode::Schedule
            || (state->wizard.step != MessengerUserResourcesState::WizardStep::ScheduleStart
                && state->wizard.step != MessengerUserResourcesState::WizardStep::ScheduleEnd)) {
            return true;
        }

        const QString resourceUuid = state->selectedResourceUuid.trimmed();
        if (resourceUuid.isEmpty()) {
            state->wizard.errorText = tr("Select a resource.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        MessengerBot *bot = mgr->bot();
        if (!bot || !bot->database() || !bot->database()->isReady()) {
            state->wizard.errorText = tr("Database is not available right now.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        const ResourceModel *resourceModel = bot->database()->resourceModel();
        const Resource resource =
                resourceModel ? resourceModel->resourceByUuid(resourceUuid) : Resource();
        const QString resourceName = resource.isValid() ? resource.name() : QString();
        if (resourceName.trimmed().isEmpty()) {
            state->wizard.errorText = tr("Select a resource.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        const QDate date = state->wizard.schedulePickedLocalDate;
        const QTime time = state->wizard.schedulePickedLocalTime;
        const bool pickingStart =
                state->wizard.step == MessengerUserResourcesState::WizardStep::ScheduleStart;
        const auto invalidBoundMessage = [&]() {
            return pickingStart ? ResourceQueueUserMessages::invalidScheduleStart()
                                : ResourceQueueUserMessages::invalidScheduleEnd();
        };
        if (!date.isValid() || !time.isValid()) {
            state->wizard.errorText = invalidBoundMessage();
            result->handled = true;
            result->sendReply = true;
            MessengerMenuManager::requestPreferEdit(result, mgr->lastMenuMessageId(context));
            return true;
        }

        const QDateTime local(date, time, effectiveTimeZone(viewerTimeZone));
        const QDateTime utc = local.toUTC();
        if (!utc.isValid()) {
            state->wizard.errorText = invalidBoundMessage();
            result->handled = true;
            result->sendReply = true;
            MessengerMenuManager::requestPreferEdit(result, mgr->lastMenuMessageId(context));
            return true;
        }

        auto updatePickedFromLocal = [](MessengerUserResourcesState::Wizard *wizard,
                                        const QDateTime &localDt) {
            if (!wizard)
                return;
            if (!localDt.isValid())
                return;
            wizard->schedulePickedLocalDate = localDt.date();
            wizard->schedulePickedLocalTime = localDt.time();
            wizard->scheduleTimePage = (localDt.time().hour() >= 12) ? 1 : 0;
        };

        auto roundUpToHalfHourLocal = [](const QDateTime &localDt) -> QDateTime {
            if (!localDt.isValid())
                return localDt;
            QDateTime dt = localDt;
            const QTime t = dt.time();
            const int mins = t.hour() * 60 + t.minute();
            const int rounded = ((mins + 29) / 30) * 30;
            if (rounded >= 24 * 60) {
                dt.setDate(dt.date().addDays(1));
                dt.setTime(QTime(0, 0));
                return dt;
            }
            dt.setTime(QTime(rounded / 60, rounded % 60));
            return dt;
        };

        if (state->wizard.step == MessengerUserResourcesState::WizardStep::ScheduleStart) {
            if (utc.toUTC() < QDateTime::currentDateTimeUtc()) {
                state->wizard.errorText = ResourceQueueUserMessages::scheduleStartInPast();
                result->handled = true;
                result->sendReply = true;
                MessengerMenuManager::requestPreferEdit(result, mgr->lastMenuMessageId(context));
                return true;
            }

            state->wizard.errorText.clear();
            state->wizard.scheduleStartUtc = utc;
            state->wizard.step = scheduleStepAfterStart(state->wizard, utc);

            const QDateTime startLocal = ServerDateTimeFormat::toLocalDateTime(utc, viewerTimeZone);
            if (state->wizard.scheduleEndUtc.isValid() && state->wizard.scheduleEndUtc > utc) {
                updatePickedFromLocal(&state->wizard,
                                      ServerDateTimeFormat::toLocalDateTime(
                                              state->wizard.scheduleEndUtc, viewerTimeZone));
            } else {
                const QDateTime next = roundUpToHalfHourLocal(startLocal.addSecs(30 * 60));
                updatePickedFromLocal(&state->wizard, next);
            }

            const QString startLocalText = MessengerTextRender::formatLocalDateTime(utc,
                                                                                    QString(),
                                                                                    dateTimeFormat,
                                                                                    viewerTimeZone);
            const MessengerMenuManager::Reply origin = MessengerMenuManager::buildPlainTextReply(
                    QStringLiteral("%1\n%2")
                            .arg(tr("Resource: %1").arg(resourceName),
                                 tr("Start: %1").arg(startLocalText))
                            .trimmed());

            result->handled = true;
            result->sendReply = true;
            MessengerMenuManager::requestSendAsNewMessageAndEditOrigin(result,
                                                                       origin,
                                                                       mgr->lastMenuMessageId(
                                                                               context));
            return true;
        }

        // Schedule end confirmation.
        const QDateTime startUtc = state->wizard.scheduleStartUtc;
        if (!startUtc.isValid()) {
            state->wizard.errorText = ResourceQueueUserMessages::invalidScheduleStart();
            result->handled = true;
            result->sendReply = true;
            MessengerMenuManager::requestPreferEdit(result, mgr->lastMenuMessageId(context));
            return true;
        }
        if (startUtc >= utc) {
            state->wizard.errorText = ResourceQueueUserMessages::endMustBeAfterStart();
            result->handled = true;
            result->sendReply = true;
            MessengerMenuManager::requestPreferEdit(result, mgr->lastMenuMessageId(context));
            return true;
        }

        state->wizard.errorText.clear();
        state->wizard.scheduleEndUtc = utc;
        state->wizard.step = MessengerUserResourcesState::WizardStep::ScheduleReview;
        state->wizard.scheduleReviewReached = true;

        const QString endLocalText = MessengerTextRender::formatLocalDateTime(utc,
                                                                              QString(),
                                                                              dateTimeFormat,
                                                                              viewerTimeZone);
        const MessengerMenuManager::Reply origin = MessengerMenuManager::buildPlainTextReply(
                QStringLiteral("%1\n%2")
                        .arg(tr("Resource: %1").arg(resourceName), tr("End: %1").arg(endLocalText))
                        .trimmed());

        result->handled = true;
        result->sendReply = true;
        MessengerMenuManager::requestSendAsNewMessageAndEditOrigin(result,
                                                                   origin,
                                                                   mgr->lastMenuMessageId(context));
        return true;
    }

    if (trimmed == QStringLiteral("ur:sch:end:indefinite")) {
        if (state->wizard.mode != MessengerUserResourcesState::WizardMode::Schedule
            || state->wizard.step != MessengerUserResourcesState::WizardStep::ScheduleEnd) {
            return true;
        }

        const QString resourceUuid = state->selectedResourceUuid.trimmed();
        if (resourceUuid.isEmpty()) {
            state->wizard.errorText = tr("Select a resource.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        MessengerBot *bot = mgr->bot();
        if (!bot || !bot->database() || !bot->database()->isReady()) {
            state->wizard.errorText = tr("Database is not available right now.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        const ResourceModel *resourceModel = bot->database()->resourceModel();
        const Resource resource =
                resourceModel ? resourceModel->resourceByUuid(resourceUuid) : Resource();
        const QString resourceName = resource.isValid() ? resource.name() : QString();
        if (resourceName.trimmed().isEmpty()) {
            state->wizard.errorText = tr("Select a resource.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        const QDateTime startUtc = state->wizard.scheduleStartUtc;
        if (!startUtc.isValid()) {
            state->wizard.errorText = ResourceQueueUserMessages::invalidScheduleStart();
            result->handled = true;
            result->sendReply = true;
            MessengerMenuManager::requestPreferEdit(result, mgr->lastMenuMessageId(context));
            return true;
        }

        state->wizard.errorText.clear();
        state->wizard.scheduleEndUtc = QDateTime(); // invalid -> indefinite
        state->wizard.step = MessengerUserResourcesState::WizardStep::ScheduleReview;
        state->wizard.scheduleReviewReached = true;

        result->handled = true;
        result->sendReply = true;
        MessengerMenuManager::requestPreferEdit(result, mgr->lastMenuMessageId(context));
        return true;
    }

    if (trimmed == QStringLiteral("ur:sch:edit:start")) {
        if (state->wizard.mode != MessengerUserResourcesState::WizardMode::Schedule)
            return true;

        state->wizard.errorText.clear();
        state->wizard.step = MessengerUserResourcesState::WizardStep::ScheduleStart;
        if (state->wizard.scheduleStartUtc.isValid()) {
            const QDateTime local =
                    ServerDateTimeFormat::toLocalDateTime(state->wizard.scheduleStartUtc,
                                                          viewerTimeZone);
            state->wizard.schedulePickedLocalDate = local.date();
            state->wizard.schedulePickedLocalTime = local.time();
            state->wizard.scheduleTimePage = (local.time().hour() >= 12) ? 1 : 0;
        } else {
            state->wizard.schedulePickedLocalDate = QDate();
            state->wizard.schedulePickedLocalTime = QTime();
            state->wizard.scheduleTimePage = 0;
        }

        result->handled = true;
        result->sendReply = true;
        MessengerMenuManager::requestPreferEdit(result, mgr->lastMenuMessageId(context));
        return true;
    }

    if (trimmed == QStringLiteral("ur:sch:edit:end")) {
        if (state->wizard.mode != MessengerUserResourcesState::WizardMode::Schedule)
            return true;

        state->wizard.errorText.clear();
        state->wizard.step = MessengerUserResourcesState::WizardStep::ScheduleEnd;
        if (state->wizard.scheduleEndUtc.isValid()) {
            const QDateTime local =
                    ServerDateTimeFormat::toLocalDateTime(state->wizard.scheduleEndUtc,
                                                          viewerTimeZone);
            state->wizard.schedulePickedLocalDate = local.date();
            state->wizard.schedulePickedLocalTime = local.time();
            state->wizard.scheduleTimePage = (local.time().hour() >= 12) ? 1 : 0;
        } else if (state->wizard.scheduleStartUtc.isValid()) {
            const QDateTime local =
                    ServerDateTimeFormat::toLocalDateTime(state->wizard.scheduleStartUtc,
                                                          viewerTimeZone);
            state->wizard.schedulePickedLocalDate = local.date();
            state->wizard.schedulePickedLocalTime = local.time();
            state->wizard.scheduleTimePage = (local.time().hour() >= 12) ? 1 : 0;
        } else {
            state->wizard.schedulePickedLocalDate = QDate();
            state->wizard.schedulePickedLocalTime = QTime();
            state->wizard.scheduleTimePage = 0;
        }

        result->handled = true;
        result->sendReply = true;
        MessengerMenuManager::requestPreferEdit(result, mgr->lastMenuMessageId(context));
        return true;
    }

    if (trimmed == QStringLiteral("ur:sch:apply")) {
        if (state->wizard.mode != MessengerUserResourcesState::WizardMode::Schedule
            || state->wizard.step != MessengerUserResourcesState::WizardStep::ScheduleReview) {
            return true;
        }

        state->wizard.errorText.clear();

        const QString resourceUuid = state->selectedResourceUuid.trimmed();
        if (resourceUuid.isEmpty()) {
            state->wizard.errorText = tr("Select a resource.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        const QDateTime startUtc = state->wizard.scheduleStartUtc;
        const QDateTime endUtc = state->wizard.scheduleEndUtc;
        if (!startUtc.isValid()) {
            state->wizard.errorText = ResourceQueueUserMessages::invalidScheduleStart();
            result->handled = true;
            result->sendReply = true;
            MessengerMenuManager::requestPreferEdit(result, mgr->lastMenuMessageId(context));
            return true;
        }
        if (startUtc.toUTC() < QDateTime::currentDateTimeUtc()) {
            state->wizard.errorText = ResourceQueueUserMessages::scheduleStartInPast();
            result->handled = true;
            result->sendReply = true;
            MessengerMenuManager::requestPreferEdit(result, mgr->lastMenuMessageId(context));
            return true;
        }
        if (endUtc.isValid() && startUtc >= endUtc) {
            state->wizard.errorText = ResourceQueueUserMessages::endMustBeAfterStart();
            result->handled = true;
            result->sendReply = true;
            MessengerMenuManager::requestPreferEdit(result, mgr->lastMenuMessageId(context));
            return true;
        }

        MessengerBot *bot = mgr->bot();
        if (!bot || !bot->database() || !bot->database()->isReady()) {
            state->wizard.errorText = tr("Database is not available right now.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        const ResourceModel *resourceModel = bot->database()->resourceModel();
        const Resource resource =
                resourceModel ? resourceModel->resourceByUuid(resourceUuid) : Resource();
        const QString resourceName = resource.isValid() ? resource.name() : QString();
        if (resourceName.trimmed().isEmpty()) {
            state->wizard.errorText = tr("Select a resource.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
        const ServerDatabaseError scheduleError =
                bot->database()->scheduleResourceUser(resourceName, actor, startUtc, endUtc, actor);
        if (scheduleError) {
            state->wizard.errorText = scheduleError.errorString();
        } else {
            state->wizard.clear();
        }

        result->handled = true;
        result->sendReply = true;
        MessengerMenuManager::requestPreferEdit(result, mgr->lastMenuMessageId(context));
        return true;
    }

    return false;
}

bool MessengerResourceScheduleMenu::handleTextInput(
        const QString &text,
        const QString &sessionId,
        const MessengerMenu::RenderContext &context,
        MessengerMenuManager::ActionResult *result) const
{
    if (!result || !m_state)
        return false;

    MessengerMenuManager *mgr = menuManager();
    if (!mgr)
        return false;
    const MessengerBot *ownerBot = mgr->bot();
    const MessengerUserKey viewerKey = AbstractBot::senderKey(context.chat, context.sender);
    const UserTimeZone viewerTimeZone =
            ownerBot ? ownerBot->effectiveTimeZoneForUser(viewerKey) : UserTimeZone();

    MessengerUserResourcesState::State *state = m_state->ensure(sessionId);
    if (!state)
        return false;

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return false;

    if (state->wizard.mode == MessengerUserResourcesState::WizardMode::Schedule) {
        const QString resourceUuid = state->selectedResourceUuid.trimmed();
        if (resourceUuid.isEmpty()) {
            state->wizard.errorText = tr("Select a resource.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        if (state->wizard.step == MessengerUserResourcesState::WizardStep::ScheduleStart
            || state->wizard.step == MessengerUserResourcesState::WizardStep::ScheduleEnd) {
            QDateTime utc;
            const ServerDatabase *database = ownerBot ? ownerBot->database() : nullptr;
            const QString dateTimeFormat = ServerDateTimeFormat::effectiveFormat(database);
            if (!parseScheduleDateTimeToken(trimmed, dateTimeFormat, viewerTimeZone, &utc)
                || !utc.isValid()) {
                state->wizard.errorText =
                        state->wizard.step == MessengerUserResourcesState::WizardStep::ScheduleStart
                                ? ResourceQueueUserMessages::invalidScheduleStart(dateTimeFormat)
                                : ResourceQueueUserMessages::invalidScheduleEnd(dateTimeFormat);
                result->handled = true;
                result->sendReply = true;
                MessengerMenuManager::requestPreferEdit(result, mgr->lastMenuMessageId(context));
                return true;
            }

            // Auto-confirm schedule input from text (same behavior as pressing "Accept").
            state->wizard.errorText.clear();
            const QDateTime local = ServerDateTimeFormat::toLocalDateTime(utc, viewerTimeZone);
            state->wizard.schedulePickedLocalDate = local.date();
            state->wizard.schedulePickedLocalTime = local.time();
            state->wizard.scheduleTimePage = (local.time().hour() >= 12) ? 1 : 0;

            return handleEvent(QStringLiteral("ur:sch:confirm"), sessionId, context, result);
        }

        state->wizard.errorText = tr("Use menu buttons to continue.");
        result->handled = true;
        result->sendReply = true;
        MessengerMenuManager::requestPreferEdit(result, mgr->lastMenuMessageId(context));
        return true;
    }

    return false;
}
