#include "MessengerAdminOccupationDurationsMenu.h"

#include "MessengerMenuIds.h"

#include "MessengerBot.h"
#include "ServerDatabase.h"
#include "MessengerTextRender.h"
#include "OccupationDurationModel.h"
#include "MessengerAbstractCommand.h"
#include "MessengerUserFacingErrors.h"

namespace {

QStringList buildDurationLines(const QList<int> &durations)
{
    QStringList lines;
    if (durations.isEmpty()) {
        lines << MessengerAdminOccupationDurationsMenu::tr(
                "No resource occupation durations are configured.");
        return lines;
    }

    lines << MessengerAdminOccupationDurationsMenu::tr("Resource occupation durations:");
    int index = 1;
    for (int minutes : durations) {
        lines << MessengerAdminOccupationDurationsMenu::tr("%1) %2 (%3 min)")
                         .arg(QString::number(index++),
                              MessengerTextRender::formatDurationMinutes(minutes),
                              QString::number(minutes));
    }
    lines << MessengerAdminOccupationDurationsMenu::tr("0 minutes means indefinite.");
    return lines;
}

} // namespace

MessengerAdminOccupationDurationsMenu::MessengerAdminOccupationDurationsMenu(
        MessengerMenuManager *manager)
    : MessengerAbstractMenu(manager)
{
    if (manager) {
        QObject::connect(manager,
                         &MessengerMenuManager::sessionEnded,
                         this,
                         [this](const QString &sessionId) {
                             m_stateBySessionId.remove(sessionId);
                         });
    }
}

QString MessengerAdminOccupationDurationsMenu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuAdminOccupationDurations);
}

bool MessengerAdminOccupationDurationsMenu::adminsOnly() const
{
    return true;
}

MessengerMenu::AccessLevel MessengerAdminOccupationDurationsMenu::accessLevel() const
{
    return MessengerMenu::AccessLevel::ServerAdminsOnly;
}

QStringList MessengerAdminOccupationDurationsMenu::eventKeys() const
{
    return { QStringLiteral("od") };
}

MessengerMenu::MenuPage MessengerAdminOccupationDurationsMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuAdminOccupationDurations);
    page.adminsOnly = true;
    page.accessLevel = MessengerMenu::AccessLevel::ServerAdminsOnly;
    page.title = tr("Resource occupation durations");

    MessengerMenuManager *mgr = menuManager();
    MessengerBot *bot = mgr ? mgr->bot() : nullptr;
    ServerDatabase *db = bot ? bot->database() : nullptr;
    const OccupationDurationModel *model = db ? db->occupationDurationModel() : nullptr;
    if (!mgr || !bot || !db || !model) {
        page.body = tr("Menu session is not available right now.");
        return page;
    }

    const QString sid = mgr->sessionId(context);
    SessionState *state = ensureState(sid);
    if (!state) {
        page.body = tr("Menu session is not available right now.");
        return page;
    }

    const QString currentMenuId = mgr->currentMenuId(context);
    if (currentMenuId.trimmed() != page.id) {
        state->reset();
    }

    mgr->setCurrentMenuId(context, page.id, false);

    switch (state->view) {
    case ViewMode::AddInput:
        page.body = tr("Send a duration (0 = indefinite).");
        page.rows.append({ mgr->makeEventButton(tr("Back"), QStringLiteral("od:back"), true) });
        break;
    case ViewMode::RemoveSelect: {
        page.body = tr("Select a duration to remove.");
        const QList<int> durations = model->occupationDurations();
        QVector<int> removable;
        for (int minutes : durations) {
            if (minutes > 0)
                removable.append(minutes);
        }
        if (removable.isEmpty()) {
            page.body = tr("No removable durations are configured.");
        } else {
            static constexpr int kButtonsPerRow = 3;
            QVector<MessengerMenu::MenuButton> row;
            row.reserve(kButtonsPerRow);
            for (int minutes : removable) {
                const QString label = MessengerTextRender::formatDurationMinutes(minutes);
                row.append(mgr->makeEventButton(label,
                                                QStringLiteral("od:pick:%1").arg(minutes),
                                                true));
                if (row.size() >= kButtonsPerRow) {
                    page.rows.append(row);
                    row.clear();
                }
            }
            if (!row.isEmpty())
                page.rows.append(row);
        }
        page.rows.append({ mgr->makeEventButton(tr("Back"), QStringLiteral("od:back"), true) });
        break;
    }
    case ViewMode::RemoveConfirm: {
        const int minutes = state->pendingRemoveMinutes;
        const QString label = MessengerTextRender::formatDurationMinutes(minutes);
        page.body = tr("Confirm removing duration: %1").arg(label);
        page.rows.append(
                { mgr->makeEventButton(tr("Remove"), QStringLiteral("od:confirm"), true) });
        page.rows.append({ mgr->makeEventButton(tr("Back"), QStringLiteral("od:remove"), true) });
        break;
    }
    case ViewMode::List:
    default:
        page.body = tr("Allowed resource occupation durations.");
        page.dynamicText = [model, mgr](const MessengerMenu::RenderContext &ctx) {
            Q_UNUSED(ctx);
            MessengerMenu::MenuPage::DynamicText out;
            out.format = AbstractBot::MessageFormat::PlainText;
            QString text = buildDurationLines(model->occupationDurations())
                                   .join(QStringLiteral("\n"))
                                   .trimmed();
            const bool showAsOf =
                    mgr && mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive;
            out.text = showAsOf ? mgr->buildTextWithAsOfCurrentFormat(text, out.format) : text;
            return out;
        };
        if (mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive) {
            page.rows.append({ mgr->makeNavigateButton(tr("Refresh list"), page.id, true) });
        }
        page.rows.append(
                { mgr->makeEventButton(tr("Add duration"), QStringLiteral("od:add"), true) });
        page.rows.append(
                { mgr->makeEventButton(tr("Remove duration"), QStringLiteral("od:remove"), true) });
        page.rows.append({ mgr->makeNavigateButton(tr("Back to admin home"),
                                                   QLatin1String(MessengerMenuIds::kMenuAdminHome),
                                                   true) });
        break;
    }

    if (!state->errorText.trimmed().isEmpty()) {
        MessengerMenuManager::prependErrorBlock(&page, state->errorText);
    }

    if (state->view != ViewMode::List) {
        page.rows.append({ mgr->makeNavigateButton(tr("Back to admin home"),
                                                   QLatin1String(MessengerMenuIds::kMenuAdminHome),
                                                   true) });
    }

    return page;
}

bool MessengerAdminOccupationDurationsMenu::handleEvent(
        const QString &event,
        const QString &sessionId,
        const MessengerMenu::RenderContext &context,
        MessengerMenuManager::ActionResult *result) const
{
    if (!result)
        return false;

    MessengerMenuManager *mgr = menuManager();
    MessengerBot *bot = mgr ? mgr->bot() : nullptr;
    ServerDatabase *db = bot ? bot->database() : nullptr;
    const OccupationDurationModel *model = db ? db->occupationDurationModel() : nullptr;
    if (!mgr || !bot || !db || !model)
        return false;

    const QString trimmed = event.trimmed();
    if (!trimmed.startsWith(QStringLiteral("od:")))
        return false;

    SessionState *state = ensureState(sessionId);
    if (!state)
        return false;

    const QStringList parts = trimmed.split(QLatin1Char(':'), Qt::KeepEmptyParts);
    const QString action = parts.value(1).trimmed();
    const QString valueToken = parts.value(2).trimmed();

    if (action == QStringLiteral("back")) {
        state->reset();
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (action == QStringLiteral("add")) {
        state->view = ViewMode::AddInput;
        state->errorText.clear();
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (action == QStringLiteral("remove")) {
        state->view = ViewMode::RemoveSelect;
        state->pendingRemoveMinutes = -1;
        state->errorText.clear();
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (action == QStringLiteral("pick")) {
        bool ok = false;
        const int minutes = valueToken.toInt(&ok);
        if (!ok || minutes <= 0) {
            state->errorText = tr("Select a duration.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        const QList<int> durations = model->occupationDurations();
        if (!durations.contains(minutes)) {
            state->errorText = tr("Select a duration.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        state->pendingRemoveMinutes = minutes;
        state->view = ViewMode::RemoveConfirm;
        state->errorText.clear();
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (action == QStringLiteral("confirm")) {
        const int minutes = state->pendingRemoveMinutes;
        if (minutes <= 0) {
            state->errorText = tr("Select a duration.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
        const ServerDatabaseError error = db->removeOccupationDuration(minutes, actor);
        if (error) {
            state->errorText = error.errorString();
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        state->reset();
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    return false;
}

bool MessengerAdminOccupationDurationsMenu::handleTextInput(
        const QString &text,
        const QString &sessionId,
        const MessengerMenu::RenderContext &context,
        MessengerMenuManager::ActionResult *result) const
{
    if (!result)
        return false;

    MessengerMenuManager *mgr = menuManager();
    MessengerBot *bot = mgr ? mgr->bot() : nullptr;
    ServerDatabase *db = bot ? bot->database() : nullptr;
    if (!mgr || !bot || !db)
        return false;

    SessionState *state = ensureState(sessionId);
    if (!state)
        return false;

    if (state->view != ViewMode::AddInput)
        return false;

    int minutes = 0;
    if (!MessengerAbstractCommand::parseDurationMinutesToken(text, &minutes)) {
        state->errorText = MessengerUserFacingErrors::invalidNonNegativeDuration();
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
    const ServerDatabaseError error = db->addOccupationDuration(minutes, actor);
    if (error) {
        state->errorText = error.errorString();
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    state->reset();
    result->handled = true;
    result->sendReply = true;
    return true;
}

MessengerAdminOccupationDurationsMenu::SessionState *
MessengerAdminOccupationDurationsMenu::ensureState(const QString &sessionId) const
{
    const QString sid = sessionId.trimmed();
    if (sid.isEmpty())
        return nullptr;

    QHash<QString, SessionState>::iterator it = m_stateBySessionId.find(sid);
    if (it == m_stateBySessionId.end()) {
        SessionState created;
        it = m_stateBySessionId.insert(sid, created);
    }
    return &it.value();
}

BOOKING_RESOURCES_REGISTER_MENU(MessengerAdminOccupationDurationsMenu)
