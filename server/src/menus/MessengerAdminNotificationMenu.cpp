#include "MessengerAdminNotificationMenu.h"

#include "MessengerMenuIds.h"
#include "MessengerMenuManager.h"

#include "UserModel.h"
#include "MessengerBot.h"
#include "ServerDatabase.h"
#include "MessengerUserKey.h"
#include "MessengerBotPoll.h"
#include "MessengerTextRender.h"
#include "MessengerUserFacingErrors.h"

#include <QCollator>
#include <QStringList>
#include <QRegularExpression>

namespace {
constexpr int kDefaultPageSize = MessengerMenuIds::kSelectorPageSize;
constexpr int kPreviewListLimit = 10;

QString normalizeMessageText(const QString &text)
{
    return MessengerTextRender::normalizeNewlines(text).trimmed();
}

bool isSendToken(const QString &text)
{
    const QString normalized = text.trimmed().toCaseFolded();
    return normalized == QStringLiteral("send") || normalized == QStringLiteral("отправить");
}

QString userLabelForButton(const User &user)
{
    if (!user.isValid())
        return MessengerAdminNotificationMenu::tr("Unknown user");

    const QString display = MessengerTextRender::formatUserDisplayName(user, false).trimmed();
    const QString label = display.isEmpty() ? user.key().toString() : display;
    return label.trimmed();
}

bool canSendDirectNotification(MessengerBot *bot, const MessengerUserKey &user)
{
    if (!bot || !user.isValid())
        return false;

    if (user.messenger() == bot->botType())
        return true;

    MessengerBotPoll *poll = bot->poll();
    return poll && poll->backendBot(user.messenger());
}

bool userMatchesNotificationScope(const User &actor, const User &recipient)
{
    if (!actor.isValid() || !recipient.isValid())
        return false;

    const QSet<Group> groups = Group::normalizedSet(recipient.groups());
    for (const Group &group : groups) {
        if (!actor.canAdministerGroup(group))
            return false;
    }
    return true;
}

QStringList selectedUserLabels(const QSet<QString> &selected,
                               const UserModel *model,
                               MessengerBot *bot,
                               const User &actor)
{
    QStringList labels;
    if (!model || selected.isEmpty())
        return labels;

    QHash<QString, QString> labelByKey;
    const QList<User> users = model->items();
    for (const User &user : users) {
        if (!user.isValid())
            continue;
        if (!canSendDirectNotification(bot, user.key()))
            continue;
        if (!userMatchesNotificationScope(actor, user))
            continue;
        const QString key = user.key().toString();
        if (key.trimmed().isEmpty())
            continue;
        labelByKey.insert(key, userLabelForButton(user));
    }

    for (const QString &key : selected) {
        const QString label = labelByKey.value(key).trimmed();
        if (!label.isEmpty())
            labels << label;
    }

    QCollator collator;
    collator.setNumericMode(true);
    std::sort(labels.begin(), labels.end(), [&collator](const QString &lhs, const QString &rhs) {
        return collator.compare(lhs, rhs) < 0;
    });
    return labels;
}
} // namespace

MessengerAdminNotificationMenu::MessengerAdminNotificationMenu(MessengerMenuManager *manager)
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

QString MessengerAdminNotificationMenu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuAdminNotification);
}

bool MessengerAdminNotificationMenu::adminsOnly() const
{
    return true;
}

MessengerAdminNotificationMenu::SessionState *MessengerAdminNotificationMenu::ensureState(
        const QString &sessionId) const
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

bool MessengerAdminNotificationMenu::ensureSelector(SessionState *state,
                                                    MessengerBot *bot,
                                                    const MessengerUserKey &actorKey,
                                                    bool forceRefresh) const
{
    if (!state || !bot)
        return false;

    if (!forceRefresh && !state->selector.items.isEmpty() && state->selector.updatedAtUtc.isValid())
        return true;

    ServerDatabase *database = bot->database();
    if (!database || !database->isReady())
        return false;

    const UserModel *model = database->userModel();
    if (!model)
        return false;

    const User actor = model->userByKey(actorKey);
    const QList<User> users = model->items();
    QVector<SelectorItem> items;
    items.reserve(users.size());
    for (const User &user : users) {
        if (!user.isValid())
            continue;
        if (!canSendDirectNotification(bot, user.key()))
            continue;
        if (!userMatchesNotificationScope(actor, user))
            continue;
        SelectorItem item;
        item.id = user.key().toString();
        item.label = userLabelForButton(user);
        if (!item.id.trimmed().isEmpty() && !item.label.trimmed().isEmpty())
            items.append(item);
    }

    QCollator collator;
    collator.setNumericMode(true);
    std::sort(items.begin(),
              items.end(),
              [&collator](const SelectorItem &lhs, const SelectorItem &rhs) {
                  return collator.compare(lhs.label, rhs.label) < 0;
              });

    state->selector.items = items;
    state->selector.pageSize = kDefaultPageSize;
    state->selector.updatedAtUtc = QDateTime::currentDateTimeUtc();

    if (!state->selectedUsers.isEmpty()) {
        QSet<QString> available;
        for (const SelectorItem &item : std::as_const(items))
            available.insert(item.id);
        for (QSet<QString>::iterator it = state->selectedUsers.begin();
             it != state->selectedUsers.end();) {
            if (!available.contains(*it)) {
                it = state->selectedUsers.erase(it);
            } else {
                ++it;
            }
        }
    }

    return true;
}

QString MessengerAdminNotificationMenu::recipientSummary(const SessionState *state,
                                                         MessengerBot *bot,
                                                         ServerDatabase *database,
                                                         const MessengerUserKey &actorKey) const
{
    if (!state)
        return QString();

    if (state->recipientMode == RecipientMode::AllUsers) {
        int count = 0;
        const UserModel *model = database ? database->userModel() : nullptr;
        if (model && bot) {
            const User actor = model->userByKey(actorKey);
            const QList<User> users = model->items();
            for (const User &user : users) {
                if (!user.isValid())
                    continue;
                if (!canSendDirectNotification(bot, user.key()))
                    continue;
                if (actorKey.isValid() && user.key() == actorKey)
                    continue;
                if (!userMatchesNotificationScope(actor, user))
                    continue;
                ++count;
            }
        }
        if (count > 0)
            return tr("all users (%1)").arg(QString::number(count));
        return tr("all users");
    }

    if (state->recipientMode == RecipientMode::SelectedUsers) {
        const int count = state->selectedUsers.size();
        if (count > 0)
            return tr("selected users (%1)").arg(QString::number(count));
        return tr("selected users");
    }

    return tr("no recipients");
}

MessengerMenu::MenuPage MessengerAdminNotificationMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuAdminNotification);
    page.adminsOnly = true;
    page.title = tr("Send alert to users");

    MessengerMenuManager *mgr = menuManager();
    MessengerBot *bot = mgr ? mgr->bot() : nullptr;
    ServerDatabase *database = bot ? bot->database() : nullptr;
    if (!mgr || !bot) {
        page.body = tr("Menu session is not available right now.");
        return page;
    }

    const QString sid = mgr->sessionId(context);
    const MessengerUserKey actorKey = AbstractBot::senderKey(context.chat, context.sender);
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

    if (state->recipientMode == RecipientMode::None && !state->selectedUsers.isEmpty()) {
        state->recipientMode = RecipientMode::SelectedUsers;
    }

    if (state->step == FlowStep::Preview && state->message.trimmed().isEmpty()) {
        state->step = FlowStep::ComposeMessage;
        state->errorText = MessengerUserFacingErrors::alertTextCannotBeEmpty();
    }

    if (state->recipientMode == RecipientMode::None && state->step != FlowStep::ChooseRecipients) {
        state->step = FlowStep::ChooseRecipients;
        state->errorText = tr("Select recipients to continue.");
    }

    if (state->recipientMode == RecipientMode::SelectedUsers && state->selectedUsers.isEmpty()
        && state->step != FlowStep::SelectUsers && state->step != FlowStep::ChooseRecipients) {
        state->step = FlowStep::SelectUsers;
        state->errorText = tr("Select at least one user.");
    }

    switch (state->step) {
    case FlowStep::ChooseRecipients: {
        page.body = tr("Choose recipients for the alert.");
        page.rows.append({ mgr->makeEventButton(tr("All users"), QStringLiteral("nt:all"), true) });
        page.rows.append({ mgr->makeEventButton(tr("Specific users"),
                                                QStringLiteral("nt:specific"),
                                                true) });
        page.rows.append({ mgr->makeNavigateButton(tr("Back to admin home"),
                                                   QLatin1String(MessengerMenuIds::kMenuAdminHome),
                                                   true) });
        break;
    }
    case FlowStep::SelectUsers: {
        if (!database || !database->isReady() || !database->userModel()) {
            page.body = tr("Database is not available right now.");
            page.rows.append(
                    { mgr->makeEventButton(tr("Back"), QStringLiteral("nt:users:back"), true) });
            page.rows.append(
                    { mgr->makeNavigateButton(tr("Back to admin home"),
                                              QLatin1String(MessengerMenuIds::kMenuAdminHome),
                                              true) });
            break;
        }

        const bool forceRefresh =
                mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Active;
        ensureSelector(state, bot, actorKey, forceRefresh);

        QStringList bodyLines;
        const bool showAsOf =
                mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive;
        if (showAsOf) {
            const QString asOf =
                    MessengerMenuManager::buildAsOfLine(state->selector.updatedAtUtc,
                                                        AbstractBot::MessageFormat::PlainText);
            if (!asOf.trimmed().isEmpty())
                bodyLines << asOf;
        }
        bodyLines << tr("Select users to receive the alert.");
        bodyLines << tr("Selected: %1").arg(QString::number(state->selectedUsers.size()));

        if (state->selector.items.isEmpty()) {
            bodyLines << tr("No users are available for this messenger and your groups.");
            page.body = bodyLines.join(QStringLiteral("\n")).trimmed();
            page.rows.append(
                    { mgr->makeEventButton(tr("Back"), QStringLiteral("nt:users:back"), true) });
            page.rows.append(
                    { mgr->makeNavigateButton(tr("Back to admin home"),
                                              QLatin1String(MessengerMenuIds::kMenuAdminHome),
                                              true) });
            break;
        }

        page.body = bodyLines.join(QStringLiteral("\n")).trimmed();

        if (showAsOf) {
            page.rows.append({ mgr->makeEventButton(tr("Refresh list"),
                                                    QStringLiteral("nt:refresh"),
                                                    true) });
        }

        const int total = state->selector.items.size();
        const int pageSize = qMax(1, state->selector.pageSize);
        const int pageCount = (total + pageSize - 1) / pageSize;
        if (pageCount > 0) {
            state->selector.page = qBound(0, state->selector.page, pageCount - 1);
        } else {
            state->selector.page = 0;
        }

        const int start = state->selector.page * pageSize;
        const int end = qMin(total, start + pageSize);
        for (int i = start; i < end; ++i) {
            const SelectorItem &item = state->selector.items.at(i);
            const bool selected = state->selectedUsers.contains(item.id);
            const QString prefix = selected ? QStringLiteral("[x] ") : QStringLiteral("[ ] ");
            page.rows.append({ mgr->makeEventButton(prefix + item.label,
                                                    QStringLiteral("sel:%1").arg(i),
                                                    true) });
        }

        QVector<MessengerMenu::MenuButton> navRow;
        if (pageCount > 1 && state->selector.page > 0) {
            navRow.append(
                    mgr->makeEventButton(tr("Previous page"), QStringLiteral("page:prev"), true));
        }
        if (pageCount > 1 && state->selector.page + 1 < pageCount) {
            navRow.append(mgr->makeEventButton(tr("Next page"), QStringLiteral("page:next"), true));
        }
        if (!navRow.isEmpty())
            page.rows.append(navRow);

        QVector<MessengerMenu::MenuButton> actionRow;
        if (!state->selectedUsers.isEmpty()) {
            actionRow.append(
                    mgr->makeEventButton(tr("Continue"), QStringLiteral("nt:users:done"), true));
            actionRow.append(mgr->makeEventButton(tr("Clear selection"),
                                                  QStringLiteral("nt:users:clear"),
                                                  true));
        }
        if (!actionRow.isEmpty())
            page.rows.append(actionRow);

        page.rows.append(
                { mgr->makeEventButton(tr("Back"), QStringLiteral("nt:users:back"), true) });
        page.rows.append({ mgr->makeNavigateButton(tr("Back to admin home"),
                                                   QLatin1String(MessengerMenuIds::kMenuAdminHome),
                                                   true) });
        break;
    }
    case FlowStep::ComposeMessage: {
        QStringList bodyLines;
        const QString summary = recipientSummary(state, bot, database, actorKey);
        bodyLines << tr("Recipients: %1").arg(summary.isEmpty() ? tr("(unknown)") : summary);
        bodyLines << tr("Send the alert text.");
        if (!state->message.trimmed().isEmpty()) {
            bodyLines << tr("Draft:");
            bodyLines << state->message;
        }
        page.body = bodyLines.join(QStringLiteral("\n")).trimmed();
        page.rows.append(
                { mgr->makeEventButton(tr("Back"), QStringLiteral("nt:compose:back"), true) });
        page.rows.append({ mgr->makeNavigateButton(tr("Back to admin home"),
                                                   QLatin1String(MessengerMenuIds::kMenuAdminHome),
                                                   true) });
        break;
    }
    case FlowStep::Preview: {
        QStringList bodyLines;
        const QString summary = recipientSummary(state, bot, database, actorKey);
        bodyLines << tr("Recipients: %1").arg(summary.isEmpty() ? tr("(unknown)") : summary);

        if (state->recipientMode == RecipientMode::SelectedUsers && database) {
            const UserModel *model = database->userModel();
            const User actor = model ? model->userByKey(actorKey) : User();
            const QStringList labels = selectedUserLabels(state->selectedUsers, model, bot, actor);
            if (!labels.isEmpty()) {
                bodyLines << tr("Recipients list:");
                const int limit = qMin(kPreviewListLimit, labels.size());
                for (int i = 0; i < limit; ++i) {
                    bodyLines << QStringLiteral("- %1").arg(labels.at(i));
                }
                if (labels.size() > limit) {
                    bodyLines << tr("... and %1 more.").arg(QString::number(labels.size() - limit));
                }
            }
        }

        bodyLines << tr("Message preview:");
        bodyLines << MessengerTextRender::formatUserAlertMessage(state->message);
        bodyLines << tr("Type \"send\" or \"отправить\" to send the alert.");
        page.body = bodyLines.join(QStringLiteral("\n")).trimmed();
        page.rows.append(
                { mgr->makeEventButton(tr("Send"), QStringLiteral("nt:preview:send"), true) });
        page.rows.append({ mgr->makeEventButton(tr("Edit message"),
                                                QStringLiteral("nt:preview:edit"),
                                                true) });
        page.rows.append({ mgr->makeEventButton(tr("Change recipients"),
                                                QStringLiteral("nt:preview:recipients"),
                                                true) });
        page.rows.append({ mgr->makeNavigateButton(tr("Back to admin home"),
                                                   QLatin1String(MessengerMenuIds::kMenuAdminHome),
                                                   true) });
        break;
    }
    }

    if (!state->errorText.trimmed().isEmpty()) {
        MessengerMenuManager::prependErrorBlock(&page, state->errorText);
    }

    return page;
}

bool MessengerAdminNotificationMenu::handleEvent(const QString &event,
                                                 const QString &sessionId,
                                                 const MessengerMenu::RenderContext &context,
                                                 MessengerMenuManager::ActionResult *result) const
{
    if (!result)
        return false;

    MessengerMenuManager *mgr = menuManager();
    MessengerBot *bot = mgr ? mgr->bot() : nullptr;
    if (!mgr || !bot)
        return false;

    SessionState *state = ensureState(sessionId);
    if (!state)
        return false;

    const QString trimmed = event.trimmed();
    if (trimmed.isEmpty())
        return false;

    const MessengerUserKey actorKey = AbstractBot::senderKey(context.chat, context.sender);

    auto markHandled = [&]() {
        result->handled = true;
        result->sendReply = true;
    };

    if (trimmed == QStringLiteral("nt:all")) {
        state->recipientMode = RecipientMode::AllUsers;
        state->step =
                state->message.trimmed().isEmpty() ? FlowStep::ComposeMessage : FlowStep::Preview;
        state->selector.clear();
        state->selectedUsers.clear();
        state->errorText.clear();
        markHandled();
        return true;
    }

    if (trimmed == QStringLiteral("nt:specific")) {
        state->recipientMode = RecipientMode::SelectedUsers;
        state->step = FlowStep::SelectUsers;
        state->errorText.clear();
        markHandled();
        return true;
    }

    if (state->step == FlowStep::SelectUsers) {
        if (trimmed == QStringLiteral("page:prev")) {
            if (state->selector.page > 0)
                state->selector.page -= 1;
            markHandled();
            return true;
        }

        if (trimmed == QStringLiteral("page:next")) {
            const int total = state->selector.items.size();
            const int pageSize = qMax(1, state->selector.pageSize);
            const int pageCount = (total + pageSize - 1) / pageSize;
            if (state->selector.page + 1 < pageCount)
                state->selector.page += 1;
            markHandled();
            return true;
        }

        if (trimmed == QStringLiteral("nt:refresh")) {
            ensureSelector(state, bot, actorKey, true);
            markHandled();
            return true;
        }

        static const QRegularExpression selRegex(QStringLiteral("^sel:(\\d+)$"));
        const QRegularExpressionMatch selMatch = selRegex.match(trimmed);
        if (selMatch.hasMatch()) {
            bool ok = false;
            const int index = selMatch.captured(1).toInt(&ok);
            if (!ok || index < 0 || index >= state->selector.items.size()) {
                markHandled();
                return true;
            }

            const SelectorItem &item = state->selector.items.at(index);
            if (state->selectedUsers.contains(item.id)) {
                state->selectedUsers.remove(item.id);
            } else if (!item.id.trimmed().isEmpty()) {
                state->selectedUsers.insert(item.id);
            }
            state->errorText.clear();
            markHandled();
            return true;
        }

        if (trimmed == QStringLiteral("nt:users:clear")) {
            state->selectedUsers.clear();
            state->errorText.clear();
            markHandled();
            return true;
        }

        if (trimmed == QStringLiteral("nt:users:done")) {
            if (state->selectedUsers.isEmpty()) {
                state->errorText = tr("Select at least one user.");
            } else {
                state->step = state->message.trimmed().isEmpty() ? FlowStep::ComposeMessage
                                                                 : FlowStep::Preview;
                state->errorText.clear();
            }
            markHandled();
            return true;
        }

        if (trimmed == QStringLiteral("nt:users:back")) {
            state->recipientMode = RecipientMode::None;
            state->step = FlowStep::ChooseRecipients;
            state->errorText.clear();
            markHandled();
            return true;
        }
    }

    if (trimmed == QStringLiteral("nt:compose:back")) {
        if (state->recipientMode == RecipientMode::SelectedUsers) {
            state->step = FlowStep::SelectUsers;
        } else {
            state->recipientMode = RecipientMode::None;
            state->step = FlowStep::ChooseRecipients;
        }
        state->errorText.clear();
        markHandled();
        return true;
    }

    if (trimmed == QStringLiteral("nt:preview:edit")) {
        state->step = FlowStep::ComposeMessage;
        state->errorText.clear();
        markHandled();
        return true;
    }

    if (trimmed == QStringLiteral("nt:preview:send")) {
        state->errorText.clear();
        return sendNotification(state, context, result);
    }

    if (trimmed == QStringLiteral("nt:preview:recipients")) {
        if (state->recipientMode == RecipientMode::SelectedUsers) {
            state->step = FlowStep::SelectUsers;
        } else {
            state->recipientMode = RecipientMode::None;
            state->step = FlowStep::ChooseRecipients;
        }
        state->errorText.clear();
        markHandled();
        return true;
    }

    return false;
}

bool MessengerAdminNotificationMenu::handleTextInput(
        const QString &text,
        const QString &sessionId,
        const MessengerMenu::RenderContext &context,
        MessengerMenuManager::ActionResult *result) const
{
    if (!result)
        return false;

    MessengerMenuManager *mgr = menuManager();
    MessengerBot *bot = mgr ? mgr->bot() : nullptr;
    if (!mgr || !bot)
        return false;

    SessionState *state = ensureState(sessionId);
    if (!state)
        return false;

    if (state->step == FlowStep::ComposeMessage) {
        const QString normalized = normalizeMessageText(text);
        if (normalized.isEmpty()) {
            state->errorText = MessengerUserFacingErrors::alertTextCannotBeEmpty();
        } else {
            state->message = normalized;
            state->step = FlowStep::Preview;
            state->errorText.clear();
        }
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (state->step == FlowStep::Preview) {
        if (!isSendToken(text)) {
            state->errorText = tr("Type \"send\" or \"отправить\" to send the alert.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        state->errorText.clear();
        return sendNotification(state, context, result);
    }

    return false;
}

bool MessengerAdminNotificationMenu::sendNotification(
        SessionState *state,
        const MessengerMenu::RenderContext &context,
        MessengerMenuManager::ActionResult *result) const
{
    if (!state || !result)
        return false;

    MessengerMenuManager *mgr = menuManager();
    MessengerBot *bot = mgr ? mgr->bot() : nullptr;
    if (!mgr || !bot)
        return false;

    if (!context.isAdmin) {
        state->errorText = MessengerUserFacingErrors::accessDeniedAdminsOnly();
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    ServerDatabase *database = bot->database();
    if (!database || !database->isReady()) {
        state->errorText = tr("Database is not available right now.");
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    const UserModel *model = database->userModel();
    if (!model) {
        state->errorText = MessengerUserFacingErrors::userModelNotAvailable();
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (MessengerTextRender::formatUserAlertMessage(state->message).isEmpty()) {
        state->errorText = MessengerUserFacingErrors::alertTextCannotBeEmpty();
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    RecipientMode mode = state->recipientMode;
    if (mode == RecipientMode::None && !state->selectedUsers.isEmpty()) {
        mode = RecipientMode::SelectedUsers;
        state->recipientMode = mode;
    }

    const MessengerUserKey actorKey = AbstractBot::senderKey(context.chat, context.sender);
    const User actor = model->userByKey(actorKey);
    QVector<User> targets;

    if (mode == RecipientMode::AllUsers) {
        const QList<User> users = model->items();
        targets.reserve(users.size());
        for (const User &user : users) {
            if (!user.isValid())
                continue;
            if (!canSendDirectNotification(bot, user.key()))
                continue;
            if (actorKey.isValid() && user.key() == actorKey)
                continue;
            if (!userMatchesNotificationScope(actor, user))
                continue;
            targets.append(user);
        }
    } else if (mode == RecipientMode::SelectedUsers) {
        if (state->selectedUsers.isEmpty()) {
            state->errorText = tr("Select at least one user.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        QHash<QString, User> byKey;
        const QList<User> users = model->items();
        for (const User &user : users) {
            if (!user.isValid())
                continue;
            if (!canSendDirectNotification(bot, user.key()))
                continue;
            if (!userMatchesNotificationScope(actor, user))
                continue;
            byKey.insert(user.key().toString(), user);
        }

        for (const QString &key : std::as_const(state->selectedUsers)) {
            const User user = byKey.value(key);
            if (user.isValid())
                targets.append(user);
        }
    } else {
        state->errorText = tr("Select recipients to continue.");
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (targets.isEmpty()) {
        state->errorText = tr("No matching users found.");
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    int sentCount = 0;
    for (const User &user : targets) {
        const MessengerUserKey key = user.key();
        if (!key.isValid())
            continue;
        const bool useHtml =
                bot->supportsDirectMessageFormat(key, AbstractBot::MessageFormat::Html);
        bot->sendDirectMessage(key,
                               MessengerTextRender::formatUserAlertMessage(state->message, useHtml),
                               useHtml ? AbstractBot::MessageFormat::Html
                                       : AbstractBot::MessageFormat::PlainText);
        ++sentCount;
    }

    if (sentCount <= 0) {
        state->errorText = tr("No matching users found.");
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    const QString summary = tr("Alert sent to %1 users.").arg(QString::number(sentCount));
    result->reply = MessengerMenuManager::buildPlainTextReply(summary);
    MessengerMenuManager::requestPreferEdit(result, mgr->lastMenuMessageId(context));

    state->reset();
    result->handled = true;
    result->sendReply = true;
    return true;
}
