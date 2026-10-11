#include "MessengerAdminUsersState.h"

#include "MessengerMenuIds.h"

#include "User.h"
#include "UserModel.h"
#include "MessengerBot.h"
#include "ServerDatabase.h"
#include "MessengerBotPoll.h"
#include "MessengerTextRender.h"
#include "MessengerUserFacingErrors.h"

#include <QCollator>
#include <QRegularExpression>

namespace {
static constexpr int kDefaultSelectorPageSize = MessengerMenuIds::kSelectorPageSize;
static constexpr int kPendingAccessSelectorLimit = 100;

QMap<Group, User::Role> groupRolesForRole(User::Role role)
{
    QMap<Group, User::Role> roles;
    roles.insert(Group::root(), role);
    return roles;
}

QMap<Group, User::Role> groupRolesForGroups(const QSet<Group> &groups, User::Role role)
{
    QMap<Group, User::Role> roles;
    for (const Group &group : groups)
        roles.insert(group, role);
    if (roles.isEmpty())
        roles.insert(Group::root(), role);
    return roles;
}

QSet<Group> groupsForGroupRoles(const QMap<Group, User::Role> &roles)
{
    QSet<Group> groups;
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it)
        groups.insert(it.key());
    return groups;
}

QMap<Group, User::Role> defaultGroupRolesForActor(MessengerMenuManager *manager,
                                                  const MessengerMenu::RenderContext &context,
                                                  User::Role role)
{
    MessengerBot *bot = manager ? manager->bot() : nullptr;
    ServerDatabase *database = bot ? bot->database() : nullptr;
    const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
    const QSet<Group> groups = database ? database->administeredGroupRoots(actor) : QSet<Group>();
    return groupRolesForGroups(groups, role);
}

QString formatGroupRoles(const QMap<Group, User::Role> &roles)
{
    if (roles.isEmpty())
        return MessengerAdminUsersState::tr("Root (all groups)");

    QStringList values;
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
        const QString role = it.value() == User::Role::FixedAdminRole
                                     ? MessengerAdminUsersState::tr("fixed admin",
                                                                    "User role label (fixed admin)")
                             : it.value() == User::Role::AdminRole
                                     ? MessengerAdminUsersState::tr("admin")
                                     : MessengerAdminUsersState::tr("user");
        values.append(
                QStringLiteral("%1=%2").arg(MessengerTextRender::formatGroupDisplayName(it.key(),
                                                                                        false),
                                            role));
    }
    return values.join(QStringLiteral(", "));
}

MessengerAdminUsersState::UserWizardStep nextAddUserStep(
        const MessengerAdminUsersState::UserWizard &wizard,
        MessengerAdminUsersState::UserWizardStep regularNextStep)
{
    if (wizard.mode == MessengerAdminUsersState::UserWizardMode::Add && wizard.reviewReached)
        return MessengerAdminUsersState::UserWizardStep::Review;
    if (wizard.mode == MessengerAdminUsersState::UserWizardMode::Add) {
        if (regularNextStep == MessengerAdminUsersState::UserWizardStep::FirstName
            && !wizard.firstName.trimmed().isEmpty()) {
            return nextAddUserStep(wizard, MessengerAdminUsersState::UserWizardStep::LastName);
        }
        if (regularNextStep == MessengerAdminUsersState::UserWizardStep::LastName
            && !wizard.lastName.trimmed().isEmpty()) {
            return MessengerAdminUsersState::UserWizardStep::Role;
        }
    }
    return regularNextStep;
}

User::Role roleForGroupRoles(const QMap<Group, User::Role> &roles)
{
    User user;
    user.setGroupRoles(roles);
    return user.role();
}

QString pendingAccessUserLabelForButton(const PendingAccessUserEntry &entry)
{
    if (!entry.isValid())
        return QString();

    const QString key = entry.user().toString();
    const QString display =
            MessengerTextRender::formatPendingAccessUserDisplayName(entry, false).trimmed();
    if (display.isEmpty() || display == key)
        return key;

    return QStringLiteral("%1 (%2)").arg(display, key);
}

bool fetchUserProfileForWizard(MessengerMenuManager *manager,
                               const MessengerUserKey &userKey,
                               AbstractBot::UserProfile *profileOut,
                               QString *errorOut)
{
    if (!userKey.isValid()) {
        if (errorOut)
            *errorOut = MessengerUserFacingErrors::userIdentifierEmpty();
        return false;
    }

    if (userKey.messenger() == MessengerType::LocalCli) {
        if (errorOut) {
            *errorOut = MessengerUserFacingErrors::invalidUserValue();
        }
        return false;
    }

    MessengerBot *bot = manager ? manager->bot() : nullptr;
    MessengerBotPoll *poll = bot ? bot->poll() : nullptr;
    AbstractBot *backend = poll ? poll->backendBot(userKey.messenger()) : nullptr;
    if (!backend) {
        if (errorOut) {
            *errorOut = MessengerAdminUsersState::tr(
                                "Cannot verify users from %1 with the current bot.")
                                .arg(messengerTypeDisplayName(userKey.messenger()));
        }
        return false;
    }

    QString lookupError;
    if (!backend->fetchUserProfile(userKey.userId(), profileOut, &lookupError)) {
        if (errorOut) {
            *errorOut =
                    MessengerUserFacingErrors::unableToVerifyUser(lookupError, userKey.userId());
        }
        return false;
    }

    return true;
}

bool applyAddUserKeyToWizard(MessengerMenuManager *manager,
                             MessengerAdminUsersState::UserWizard *wizard,
                             const MessengerUserKey &userKey,
                             QString *errorOut)
{
    if (!wizard)
        return false;

    AbstractBot::UserProfile remoteProfile;
    QString lookupError;
    if (!fetchUserProfileForWizard(manager, userKey, &remoteProfile, &lookupError)) {
        if (errorOut)
            *errorOut = lookupError;
        return false;
    }

    const QString resolvedId = remoteProfile.userId.trimmed();
    wizard->user = MessengerUserKey(userKey.messenger(),
                                    resolvedId.isEmpty() ? userKey.userId() : resolvedId);
    wizard->firstName = remoteProfile.firstName.trimmed();
    wizard->lastName = remoteProfile.lastName.trimmed();
    if (wizard->firstName.isEmpty() && wizard->lastName.isEmpty())
        wizard->firstName = remoteProfile.userId.trimmed();
    wizard->step = nextAddUserStep(*wizard, MessengerAdminUsersState::UserWizardStep::FirstName);
    return true;
}
} // namespace

MessengerAdminUsersState::MessengerAdminUsersState(MessengerMenuManager *manager)
    : QObject(manager)
    , m_manager(manager)
{
    if (m_manager) {
        connect(m_manager,
                &MessengerMenuManager::sessionEnded,
                this,
                [this](const QString &sessionId) { clear(sessionId); });
        connect(m_manager,
                &MessengerMenuManager::menuChanged,
                this,
                [this](const QString &sessionId,
                       const QString &fromMenuId,
                       const QString &toMenuId,
                       bool resetFlows) {
                    Q_UNUSED(fromMenuId);
                    Q_UNUSED(toMenuId);
                    if (resetFlows) {
                        clear(sessionId);
                    }
                });
    }
}

MessengerAdminUsersState::~MessengerAdminUsersState() = default;

MessengerAdminUsersState::State *MessengerAdminUsersState::ensure(const QString &sessionId)
{
    const QString sid = sessionId.trimmed();
    if (sid.isEmpty())
        return nullptr;

    QHash<QString, State>::iterator it = m_states.find(sid);
    if (it == m_states.end()) {
        State created;
        it = m_states.insert(sid, created);
    }
    return &it.value();
}

const MessengerAdminUsersState::State *MessengerAdminUsersState::find(
        const QString &sessionId) const
{
    const QString sid = sessionId.trimmed();
    if (sid.isEmpty())
        return nullptr;

    const QHash<QString, State>::const_iterator it = m_states.constFind(sid);
    if (it == m_states.cend())
        return nullptr;
    return &it.value();
}

void MessengerAdminUsersState::clear(const QString &sessionId)
{
    const QString sid = sessionId.trimmed();
    if (sid.isEmpty())
        return;
    m_states.remove(sid);
}

void MessengerAdminUsersState::invalidateSelectors()
{
    for (QHash<QString, State>::iterator it = m_states.begin(); it != m_states.end(); ++it) {
        it.value().selector.updatedAtUtc = QDateTime();
    }
}

QString MessengerAdminUsersState::userDisplayNameForButton(const User &user)
{
    if (!user.isValid())
        return tr("Unknown user");

    return MessengerTextRender::formatUserDisplayName(user, false);
}

QString MessengerAdminUsersState::userLabelForButton(const User &user)
{
    const QString display = userDisplayNameForButton(user).trimmed();
    QString label = display;
    if (label.isEmpty() && user.key().isValid())
        label = user.key().toString();
    const QString userKey = user.key().toString().trimmed();
    if (!userKey.isEmpty() && label != userKey) {
        return QStringLiteral("%1 (%2)")
                .arg(label.trimmed(),
                     MessengerTextRender::tr("id: %1", "User id label (plain)").arg(userKey));
    }
    return label.trimmed();
}

bool MessengerAdminUsersState::ensureSelector(State *state,
                                              SelectorPurpose purpose,
                                              const MessengerUserKey &currentUserKey,
                                              bool forceRefresh)
{
    if (!state)
        return false;

    if (purpose == SelectorPurpose::None)
        return false;

    const bool changed = (state->selector.purpose != purpose);
    if (changed) {
        state->selector.clear();
        state->selector.purpose = purpose;
    }

    if (!forceRefresh && !changed && !state->selector.items.isEmpty()
        && state->selector.updatedAtUtc.isValid()) {
        return true;
    }

    MessengerBot *bot = m_manager ? m_manager->bot() : nullptr;
    ServerDatabase *database = bot ? bot->database() : nullptr;
    if (!database || !database->isReady())
        return false;

    if (purpose == SelectorPurpose::AdminPendingAccessUserAdd) {
        const PendingAccessUserModel *model = database->pendingAccessUserModel();
        if (!model)
            return false;

        const QList<PendingAccessUserEntry> entries =
                model->recentUsers(kPendingAccessSelectorLimit);
        QVector<SelectorItem> items;
        items.reserve(entries.size());
        for (const PendingAccessUserEntry &entry : entries) {
            const QString id = entry.user().toString();
            const QString label = pendingAccessUserLabelForButton(entry);
            if (!id.trimmed().isEmpty() && !label.trimmed().isEmpty()) {
                SelectorItem item;
                item.id = id;
                item.label = label;
                items.append(item);
            }
        }

        state->selector.items = items;
        state->selector.page = 0;
        state->selector.pageSize = kDefaultSelectorPageSize;
        state->selector.updatedAtUtc = QDateTime::currentDateTimeUtc();
        return true;
    }

    const UserModel *model = database->userModel();
    if (!model)
        return false;

    const QList<User> users = model->items();
    QVector<SelectorItem> items;
    items.reserve(users.size());
    for (const User &user : users) {
        if (!user.isValid())
            continue;
        if (purpose == SelectorPurpose::AdminUserDelete && user.isFixedAdmin()) {
            // Fixed admins cannot be deleted.
            continue;
        }
        if (!database->canAdministerUser(currentUserKey, user))
            continue;
        SelectorItem item;
        item.id = user.key().toString();
        item.label = userLabelForButton(user);
        if (currentUserKey.isValid() && user.key() == currentUserKey) {
            item.label += QStringLiteral(" %1").arg(
                    MessengerTextRender::tr("(you)", "Marker appended to the current user"));
        }
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
    state->selector.page = 0;
    state->selector.pageSize = kDefaultSelectorPageSize;
    state->selector.updatedAtUtc = QDateTime::currentDateTimeUtc();
    return true;
}

MessengerMenu::MenuPage MessengerAdminUsersState::buildSelectorPage(const QString &currentMenuId,
                                                                    const QString &title,
                                                                    const QString &emptyText,
                                                                    const QString &backMenuId,
                                                                    bool adminsOnlyBack,
                                                                    State *state) const
{
    MessengerMenu::MenuPage page;
    page.id = currentMenuId.trimmed();
    if (page.id.isEmpty())
        page.id = QLatin1String(MessengerMenuIds::kMenuAdminUsers);
    page.title = title;

    MessengerMenuManager *mgr = m_manager;
    if (!mgr || !state) {
        page.body = emptyText;
        if (mgr) {
            page.rows.append({ mgr->makeNavigateButton(tr("Back"), backMenuId, adminsOnlyBack) });
        }
        return page;
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
                                                   AbstractBot::MessageFormat::PlainText);
    }
    if (!asOf.trimmed().isEmpty())
        bodyLines << asOf;

    if (total <= 0) {
        bodyLines << emptyText;
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
        page.rows.append({ mgr->makeEventButton(tr("Refresh list"),
                                                QStringLiteral("refresh"),
                                                adminsOnlyBack) });
    }

    if (total > 0) {
        const int start = state->selector.page * pageSize;
        const int end = qMin(total, start + pageSize);
        for (int i = start; i < end; ++i) {
            const SelectorItem &item = state->selector.items.at(i);
            page.rows.append({ mgr->makeEventButton(item.label, QStringLiteral("sel:%1").arg(i)) });
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

    if (state->selector.purpose == SelectorPurpose::AdminPendingAccessUserAdd) {
        page.rows.append({ mgr->makeEventButton(tr("Back"),
                                                QStringLiteral("uw:select:back"),
                                                adminsOnlyBack) });
    } else {
        page.rows.append({ mgr->makeNavigateButton(tr("Back"), backMenuId, adminsOnlyBack) });
    }
    return page;
}

bool MessengerAdminUsersState::handleEvent(const QString &event,
                                           const QString &sessionId,
                                           const MessengerMenu::RenderContext &context,
                                           State *state,
                                           MessengerMenuManager::ActionResult *result)
{
    if (!state || !result)
        return false;

    MessengerMenuManager *mgr = m_manager;
    if (!mgr)
        return false;

    const QString trimmed = event.trimmed();
    if (trimmed.isEmpty())
        return false;

    // Selector dispatch (only when selector is active).
    if (state->selector.purpose != SelectorPurpose::None) {
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
            ensureSelector(state,
                           state->selector.purpose,
                           AbstractBot::senderKey(context.chat, context.sender),
                           true);
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

            const SelectorItem item = state->selector.items.at(index);
            const SelectorPurpose purpose = state->selector.purpose;
            MessengerBot *bot = mgr->bot();
            ServerDatabase *database = bot ? bot->database() : nullptr;
            User selectedUser;
            MessengerUserKey selectedKey;
            const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
            if (purpose == SelectorPurpose::AdminPendingAccessUserAdd
                && (!database || !database->canManageServerSettings(actor))) {
                state->selector.clear();
                state->wizard.errorText = MessengerUserFacingErrors::accessDeniedAdminsOnly();
                result->handled = true;
                result->sendReply = true;
                return true;
            }
            if (purpose != SelectorPurpose::AdminPendingAccessUserAdd) {
                bool selectedOk = false;
                selectedKey = MessengerUserKey::fromString(item.id, &selectedOk);
                const UserModel *userModel =
                        (database && database->isReady()) ? database->userModel() : nullptr;
                if (selectedOk && userModel)
                    selectedUser = userModel->userByKey(selectedKey);
                if (!selectedUser.isValid() || !database->canAdministerUser(actor, selectedUser)) {
                    state->selector.clear();
                    state->selectedUserKey.clear();
                    state->wizard.errorText = tr("User not found.");
                    result->handled = true;
                    result->sendReply = true;
                    return true;
                }
            }
            state->selector.clear();

            state->selectedUserKey = item.id;

            if (purpose == SelectorPurpose::AdminUserEdit) {
                state->wizard.clear();
                state->wizard.mode = UserWizardMode::Edit;
                state->wizard.step = UserWizardStep::Review;
                state->wizard.user = selectedKey;

                state->wizard.existingFirstName = selectedUser.firstName();
                state->wizard.existingLastName = selectedUser.lastName();
                state->wizard.existingFixedAdmin = selectedUser.isFixedAdmin();
                state->wizard.existingGroupRoles = selectedUser.groupRoles();
                state->wizard.firstName = selectedUser.firstName();
                state->wizard.lastName = selectedUser.lastName();
                state->wizard.groupRoles = selectedUser.groupRoles();

                MessengerMenuManager::requestSendAsNewMessageAndEditOrigin(
                        result,
                        MessengerMenuManager::buildPlainTextReply(tr("Edit menu opened.")),
                        mgr->lastMenuMessageId(context));
            } else if (purpose == SelectorPurpose::AdminUserDelete) {
                state->pendingDeleteUserKey = item.id;
            } else if (purpose == SelectorPurpose::AdminPendingAccessUserAdd) {
                bool selectedOk = false;
                const MessengerUserKey selectedKey =
                        MessengerUserKey::fromString(item.id, &selectedOk);
                state->wizard.mode = UserWizardMode::Add;
                state->wizard.step = UserWizardStep::UserKey;
                if (!selectedOk) {
                    state->wizard.errorText = MessengerUserFacingErrors::invalidUserValue();
                } else {
                    QString lookupError;
                    if (!applyAddUserKeyToWizard(mgr, &state->wizard, selectedKey, &lookupError)) {
                        state->wizard.errorText = lookupError;
                    } else {
                        state->wizard.errorText.clear();
                        MessengerMenuManager::requestEditOriginMessage(
                                result,
                                MessengerMenuManager::buildPlainTextReply(
                                        tr("User identifier received.")),
                                mgr->lastMenuMessageId(context));
                    }
                }
            }

            result->handled = true;
            result->sendReply = true;
            return true;
        }
    }

    // Admin user actions.
    if (trimmed == QStringLiteral("uc:edit")) {
        const QString selected = state->selectedUserKey.trimmed();
        if (selected.isEmpty())
            return false;

        bool selectedOk = false;
        MessengerUserKey selectedKey = MessengerUserKey::fromString(selected, &selectedOk);
        if (!selectedOk)
            return false;

        MessengerBot *bot = mgr->bot();
        ServerDatabase *database = bot ? bot->database() : nullptr;
        const UserModel *userModel =
                (database && database->isReady()) ? database->userModel() : nullptr;
        const User existing = userModel ? userModel->userByKey(selectedKey) : User();
        const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
        if (!existing.isValid() || !database->canAdministerUser(actor, existing)) {
            state->resetFlows();
            result->handled = true;
            result->sendReply = true;
            result->reply = MessengerMenuManager::buildPlainTextReply(tr("User not found."));
            return true;
        }

        state->resetFlows();
        state->selectedUserKey = selectedKey.toString();

        state->wizard.clear();
        state->wizard.mode = UserWizardMode::Edit;
        state->wizard.step = UserWizardStep::Review;
        state->wizard.user = selectedKey;

        state->wizard.existingFirstName = existing.firstName();
        state->wizard.existingLastName = existing.lastName();
        state->wizard.existingFixedAdmin = existing.isFixedAdmin();
        state->wizard.existingGroupRoles = existing.groupRoles();
        state->wizard.firstName = existing.firstName();
        state->wizard.lastName = existing.lastName();
        state->wizard.groupRoles = existing.groupRoles();

        mgr->setCurrentMenuId(context, QLatin1String(MessengerMenuIds::kMenuAdminUserEdit), false);
        MessengerMenuManager::requestSendAsNewMessageAndEditOrigin(
                result,
                MessengerMenuManager::buildPlainTextReply(tr("Edit menu opened.")),
                mgr->lastMenuMessageId(context));
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (trimmed == QStringLiteral("uc:delete")) {
        const QString selected = state->selectedUserKey.trimmed();
        if (selected.isEmpty())
            return false;

        bool selectedOk = false;
        const MessengerUserKey selectedKey = MessengerUserKey::fromString(selected, &selectedOk);
        MessengerBot *bot = mgr->bot();
        ServerDatabase *database = bot ? bot->database() : nullptr;
        const UserModel *userModel =
                (database && database->isReady()) ? database->userModel() : nullptr;
        const User existing =
                (selectedOk && userModel) ? userModel->userByKey(selectedKey) : User();
        const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
        if (!existing.isValid() || !database->canAdministerUser(actor, existing)) {
            state->resetFlows();
            result->handled = true;
            result->sendReply = true;
            result->reply = MessengerMenuManager::buildPlainTextReply(tr("User not found."));
            return true;
        }

        state->pendingDeleteUserKey = selected;
        mgr->setCurrentMenuId(context,
                              QLatin1String(MessengerMenuIds::kMenuAdminUserDelete),
                              false);
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (trimmed == QStringLiteral("delu:yes")) {
        const QString key = state->pendingDeleteUserKey.trimmed();
        if (key.isEmpty())
            return false;

        if (!context.isAdmin) {
            result->handled = true;
            result->sendReply = true;
            result->reply = MessengerMenuManager::buildPlainTextReply(
                    MessengerUserFacingErrors::accessDeniedAdminsOnly());
            return true;
        }

        MessengerBot *bot = mgr->bot();
        ServerDatabase *database = bot ? bot->database() : nullptr;
        if (!database || !database->isReady()) {
            result->handled = true;
            result->sendReply = true;
            result->reply = MessengerMenuManager::buildPlainTextReply(
                    tr("Database is not available right now."));
            return true;
        }

        bool subjectOk = false;
        MessengerUserKey subjectKey = MessengerUserKey::fromString(key, &subjectOk);
        if (!subjectOk) {
            result->handled = true;
            result->sendReply = true;
            result->reply = MessengerMenuManager::buildPlainTextReply(
                    MessengerUserFacingErrors::invalidUserValue());
            return true;
        }

        const QString normalizedKey = subjectKey.toString().trimmed();
        QString deletedUserLabel = normalizedKey.isEmpty() ? key : normalizedKey;
        const UserModel *userModel = database->userModel();
        if (userModel) {
            const User existing = userModel->userByKey(subjectKey);
            if (existing.isValid()) {
                const QString display = userLabelForButton(existing).trimmed();
                if (!display.isEmpty() && !normalizedKey.isEmpty() && display != normalizedKey) {
                    deletedUserLabel = QStringLiteral("%1 (%2)").arg(display, normalizedKey);
                } else if (!display.isEmpty()) {
                    deletedUserLabel = display;
                }
            }
        }

        const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
        const ServerDatabaseError dbError = database->deleteUser(subjectKey, actor);
        if (dbError) {
            result->handled = true;
            result->sendReply = true;
            result->reply = MessengerMenuManager::buildPlainTextReply(dbError.errorString());
            return true;
        }

        const MessengerMenuManager::Reply originReply = MessengerMenuManager::buildPlainTextReply(
                tr("User deleted: %1").arg(deletedUserLabel));
        MessengerMenuManager::requestSendAsNewMessageAndEditOrigin(result,
                                                                   originReply,
                                                                   mgr->lastMenuMessageId(context));

        state->resetFlows();
        mgr->setCurrentMenuId(context, QLatin1String(MessengerMenuIds::kMenuAdminUsers), true);

        result->handled = true;
        result->sendReply = true;
        return true;
    }

    // User wizard events.
    if (trimmed.startsWith(QStringLiteral("uw:"))) {
        if (trimmed == QStringLiteral("uw:select:back")) {
            if (state->selector.purpose == SelectorPurpose::AdminPendingAccessUserAdd) {
                state->selector.clear();
                state->wizard.errorText.clear();
                state->wizard.mode = UserWizardMode::Add;
                state->wizard.step = UserWizardStep::UserKey;
                result->handled = true;
                result->sendReply = true;
            }
            if (!result->sendReply) {
                result->handled = true;
                result->sendReply = true;
            }
            return true;
        }

        if (trimmed == QStringLiteral("uw:back")) {
            state->wizard.errorText.clear();

            if (state->wizard.mode == UserWizardMode::Add) {
                switch (state->wizard.step) {
                case UserWizardStep::FirstName:
                    state->wizard.step = UserWizardStep::UserKey;
                    break;
                case UserWizardStep::LastName:
                    state->wizard.step = UserWizardStep::FirstName;
                    break;
                case UserWizardStep::Role:
                    state->wizard.step = UserWizardStep::LastName;
                    break;
                case UserWizardStep::Groups:
                    state->wizard.step = UserWizardStep::Role;
                    break;
                case UserWizardStep::Review:
                    state->wizard.step = UserWizardStep::Groups;
                    break;
                default:
                    break;
                }
                result->handled = true;
                result->sendReply = true;
            } else if (state->wizard.mode == UserWizardMode::Edit) {
                state->wizard.step = UserWizardStep::Review;
                result->handled = true;
                result->sendReply = true;
            }
            if (!result->sendReply) {
                result->handled = true;
                result->sendReply = true;
            }
            return true;
        }

        if (trimmed == QStringLiteral("uw:skip")) {
            state->wizard.errorText.clear();

            if (state->wizard.mode == UserWizardMode::Add) {
                if (state->wizard.step == UserWizardStep::FirstName) {
                    state->wizard.firstName.clear();
                    state->wizard.step = nextAddUserStep(state->wizard, UserWizardStep::LastName);
                    result->handled = true;
                    result->sendReply = true;
                } else if (state->wizard.step == UserWizardStep::LastName) {
                    state->wizard.lastName.clear();
                    state->wizard.step = nextAddUserStep(state->wizard, UserWizardStep::Role);
                    result->handled = true;
                    result->sendReply = true;
                }
            } else if (state->wizard.mode == UserWizardMode::Edit) {
                if (state->wizard.step == UserWizardStep::FirstName) {
                    state->wizard.firstName = state->wizard.existingFirstName;
                    state->wizard.step = UserWizardStep::Review;
                    result->handled = true;
                    result->sendReply = true;
                } else if (state->wizard.step == UserWizardStep::LastName) {
                    state->wizard.lastName = state->wizard.existingLastName;
                    state->wizard.step = UserWizardStep::Review;
                    result->handled = true;
                    result->sendReply = true;
                } else if (state->wizard.step == UserWizardStep::Groups) {
                    state->wizard.groupRoles = state->wizard.existingGroupRoles;
                    state->wizard.step = UserWizardStep::Review;
                    result->handled = true;
                    result->sendReply = true;
                }
            }
            if (!result->sendReply) {
                result->handled = true;
                result->sendReply = true;
            }
            return true;
        }

        if (trimmed == QStringLiteral("uw:choose:pending_access")) {
            if (state->wizard.mode == UserWizardMode::Add
                && state->wizard.step == UserWizardStep::UserKey) {
                state->wizard.errorText.clear();
                MessengerBot *bot = mgr->bot();
                ServerDatabase *database = bot ? bot->database() : nullptr;
                const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
                if (!database || !database->canManageServerSettings(actor)) {
                    state->selector.clear();
                    state->wizard.errorText = MessengerUserFacingErrors::accessDeniedAdminsOnly();
                } else if (!ensureSelector(state,
                                           SelectorPurpose::AdminPendingAccessUserAdd,
                                           actor,
                                           true)) {
                    state->selector.clear();
                    state->wizard.errorText = tr("Database is not available right now.");
                }
                result->handled = true;
                result->sendReply = true;
            }
            if (!result->sendReply) {
                result->handled = true;
                result->sendReply = true;
            }
            return true;
        }

        if (trimmed == QStringLiteral("uw:groups:default")) {
            if (state->wizard.mode == UserWizardMode::Add
                && state->wizard.step == UserWizardStep::Groups) {
                state->wizard.errorText.clear();
                state->wizard.groupRoles =
                        defaultGroupRolesForActor(mgr,
                                                  context,
                                                  roleForGroupRoles(state->wizard.groupRoles));
                state->wizard.step = UserWizardStep::Review;
                state->wizard.reviewReached = true;
                MessengerMenuManager::requestEditOriginMessage(
                        result,
                        MessengerMenuManager::buildPlainTextReply(tr("Groups received.")),
                        mgr->lastMenuMessageId(context));
                result->handled = true;
                result->sendReply = true;
            }
            if (!result->sendReply) {
                result->handled = true;
                result->sendReply = true;
            }
            return true;
        }

        if (trimmed == QStringLiteral("uw:groups:none")) {
            if (state->wizard.step == UserWizardStep::Groups) {
                state->wizard.errorText.clear();
                state->wizard.groupRoles =
                        groupRolesForRole(roleForGroupRoles(state->wizard.groupRoles));
                state->wizard.step = UserWizardStep::Review;
                state->wizard.reviewReached = true;
                MessengerMenuManager::requestEditOriginMessage(
                        result,
                        MessengerMenuManager::buildPlainTextReply(tr("Groups received.")),
                        mgr->lastMenuMessageId(context));
                result->handled = true;
                result->sendReply = true;
            }
            if (!result->sendReply) {
                result->handled = true;
                result->sendReply = true;
            }
            return true;
        }

        if (trimmed == QStringLiteral("uw:edit:id")) {
            if (state->wizard.mode == UserWizardMode::Add) {
                state->wizard.errorText.clear();
                state->wizard.step = UserWizardStep::UserKey;
                result->handled = true;
                result->sendReply = true;
            }
            if (!result->sendReply) {
                result->handled = true;
                result->sendReply = true;
            }
            return true;
        }

        if (trimmed == QStringLiteral("uw:edit:first")) {
            if (state->wizard.mode != UserWizardMode::None) {
                state->wizard.errorText.clear();
                state->wizard.step = UserWizardStep::FirstName;
                result->handled = true;
                result->sendReply = true;
            }
            if (!result->sendReply) {
                result->handled = true;
                result->sendReply = true;
            }
            return true;
        }

        if (trimmed == QStringLiteral("uw:edit:last")) {
            if (state->wizard.mode != UserWizardMode::None) {
                state->wizard.errorText.clear();
                state->wizard.step = UserWizardStep::LastName;
                result->handled = true;
                result->sendReply = true;
            }
            if (!result->sendReply) {
                result->handled = true;
                result->sendReply = true;
            }
            return true;
        }

        if (trimmed == QStringLiteral("uw:edit:role")) {
            if (state->wizard.mode != UserWizardMode::None) {
                state->wizard.errorText.clear();
                if (state->wizard.mode == UserWizardMode::Edit
                    && state->wizard.existingFixedAdmin) {
                    state->wizard.errorText =
                            MessengerUserFacingErrors::roleCannotBeChangedForFixedAdmin();
                    state->wizard.step = UserWizardStep::Review;
                } else {
                    state->wizard.step = UserWizardStep::Role;
                }
                result->handled = true;
                result->sendReply = true;
            }
            if (!result->sendReply) {
                result->handled = true;
                result->sendReply = true;
            }
            return true;
        }

        if (trimmed == QStringLiteral("uw:edit:groups")) {
            if (state->wizard.mode != UserWizardMode::None) {
                state->wizard.errorText.clear();
                state->wizard.step = UserWizardStep::Groups;
                result->handled = true;
                result->sendReply = true;
            }
            if (!result->sendReply) {
                result->handled = true;
                result->sendReply = true;
            }
            return true;
        }

        if (trimmed == QStringLiteral("uw:role:user")) {
            if (state->wizard.mode != UserWizardMode::None) {
                state->wizard.errorText.clear();
                if (state->wizard.mode == UserWizardMode::Edit
                    && state->wizard.existingFixedAdmin) {
                    state->wizard.errorText =
                            MessengerUserFacingErrors::roleCannotBeChangedForFixedAdmin();
                } else {
                    state->wizard.groupRoles =
                            state->wizard.mode == UserWizardMode::Add
                                    ? defaultGroupRolesForActor(mgr, context, User::Role::UserRole)
                                    : groupRolesForGroups(groupsForGroupRoles(
                                                                  state->wizard.groupRoles),
                                                          User::Role::UserRole);
                }
                state->wizard.step =
                        (state->wizard.mode == UserWizardMode::Edit)
                                ? UserWizardStep::Review
                                : nextAddUserStep(state->wizard, UserWizardStep::Groups);
                result->handled = true;
                result->sendReply = true;
            }
            if (!result->sendReply) {
                result->handled = true;
                result->sendReply = true;
            }
            return true;
        }

        if (trimmed == QStringLiteral("uw:role:admin")) {
            if (state->wizard.mode != UserWizardMode::None) {
                state->wizard.errorText.clear();
                if (state->wizard.mode == UserWizardMode::Edit
                    && state->wizard.existingFixedAdmin) {
                    // Fixed admin role cannot be changed here.
                } else {
                    state->wizard.groupRoles =
                            state->wizard.mode == UserWizardMode::Add
                                    ? defaultGroupRolesForActor(mgr, context, User::Role::AdminRole)
                                    : groupRolesForGroups(groupsForGroupRoles(
                                                                  state->wizard.groupRoles),
                                                          User::Role::AdminRole);
                }
                state->wizard.step =
                        (state->wizard.mode == UserWizardMode::Edit)
                                ? UserWizardStep::Review
                                : nextAddUserStep(state->wizard, UserWizardStep::Groups);
                result->handled = true;
                result->sendReply = true;
            }
            if (!result->sendReply) {
                result->handled = true;
                result->sendReply = true;
            }
            return true;
        }

        if (trimmed == QStringLiteral("uw:confirm")) {
            state->wizard.errorText.clear();

            MessengerBot *bot = mgr->bot();
            ServerDatabase *database = bot ? bot->database() : nullptr;

            if (state->wizard.mode == UserWizardMode::Add) {
                if (!context.isAdmin) {
                    state->wizard.errorText = MessengerUserFacingErrors::accessDeniedAdminsOnly();
                    result->handled = true;
                    result->sendReply = true;
                    return true;
                }

                if (!database || !database->isReady()) {
                    state->wizard.errorText = tr("Database is not available right now.");
                    result->handled = true;
                    result->sendReply = true;
                    return true;
                }

                const MessengerUserKey userKey(state->wizard.user.messenger(),
                                               state->wizard.user.userId());

                AbstractBot::UserProfile remoteProfile;
                QString lookupError;
                if (!fetchUserProfileForWizard(mgr, userKey, &remoteProfile, &lookupError)) {
                    state->wizard.errorText = lookupError;
                    result->handled = true;
                    result->sendReply = true;
                    return true;
                }

                QString first = state->wizard.firstName.trimmed();
                QString last = state->wizard.lastName.trimmed();

                if (first.isEmpty())
                    first = remoteProfile.firstName.trimmed();
                if (last.isEmpty())
                    last = remoteProfile.lastName.trimmed();

                if (first.isEmpty() && last.isEmpty())
                    first = remoteProfile.userId.trimmed();

                User user(userKey, first, last, User::Role::UserRole);
                user.setGroupRoles(state->wizard.groupRoles);
                bool removedFromBlacklist = false;
                const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
                const ServerDatabaseError addError =
                        database->addUser(user, actor, &removedFromBlacklist);
                if (addError) {
                    state->wizard.errorText = addError.errorString();
                    result->handled = true;
                    result->sendReply = true;
                    return true;
                }
                bot->notifyAccessActivated(user.key());

                const QString userLine = MessengerTextRender::formatUserLine(user, false);

                QStringList successLines;
                successLines << tr("User added: %1").arg(userLine);
                if (removedFromBlacklist) {
                    successLines << tr("User was removed from the blacklist.");
                }

                const MessengerMenuManager::Reply originReply =
                        MessengerMenuManager::buildPlainTextReply(
                                successLines.join(QStringLiteral("\n")).trimmed());
                MessengerMenuManager::requestSendAsNewMessageAndEditOrigin(result,
                                                                           originReply,
                                                                           mgr->lastMenuMessageId(
                                                                                   context));
            } else if (state->wizard.mode == UserWizardMode::Edit) {
                if (!context.isAdmin) {
                    state->wizard.errorText = MessengerUserFacingErrors::accessDeniedAdminsOnly();
                    result->handled = true;
                    result->sendReply = true;
                    return true;
                }

                if (!database || !database->isReady()) {
                    state->wizard.errorText = tr("Database is not available right now.");
                    result->handled = true;
                    result->sendReply = true;
                    return true;
                }

                const UserModel *model = database->userModel();
                if (!model) {
                    state->wizard.errorText = MessengerUserFacingErrors::userModelNotAvailable();
                    result->handled = true;
                    result->sendReply = true;
                    return true;
                }

                const MessengerUserKey key = state->wizard.user;
                const User existing = model->userByKey(key);
                if (!existing.isValid()) {
                    state->wizard.errorText =
                            MessengerUserFacingErrors::userNotFound(key.toString());
                    result->handled = true;
                    result->sendReply = true;
                    return true;
                }

                User updated = existing;
                updated.setFirstName(state->wizard.firstName.trimmed());
                updated.setLastName(state->wizard.lastName.trimmed());
                updated.setGroupRoles(state->wizard.groupRoles);
                const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
                const ServerDatabaseError updateError = database->updateUser(updated, actor);
                if (updateError) {
                    state->wizard.errorText = updateError.errorString();
                    result->handled = true;
                    result->sendReply = true;
                    return true;
                }
            } else {
                result->handled = true;
                result->sendReply = true;
                return true;
            }

            state->resetFlows();
            mgr->setCurrentMenuId(context, QLatin1String(MessengerMenuIds::kMenuAdminUsers), true);

            result->handled = true;
            result->sendReply = true;
            return true;
        }

        // Unknown uw:* event, most likely from an outdated menu message.
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    return false;
}

bool MessengerAdminUsersState::handleTextInput(const QString &text,
                                               const QString &sessionId,
                                               const MessengerMenu::RenderContext &context,
                                               State *state,
                                               MessengerMenuManager::ActionResult *result)
{
    Q_UNUSED(sessionId);

    if (!state || !result)
        return false;

    MessengerMenuManager *mgr = m_manager;
    if (!mgr)
        return false;

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return false;

    if (state->wizard.mode == UserWizardMode::None)
        return false;

    if (state->wizard.mode == UserWizardMode::Add
        && state->wizard.step == UserWizardStep::UserKey) {
        bool userKeyOk = false;
        MessengerUserKey userKey = MessengerUserKey::fromString(trimmed, &userKeyOk);
        if (!userKeyOk) {
            state->wizard.errorText = MessengerUserFacingErrors::invalidUserValue();
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        state->wizard.errorText.clear();
        QString lookupError;
        if (!applyAddUserKeyToWizard(mgr, &state->wizard, userKey, &lookupError)) {
            state->wizard.errorText = lookupError;
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        MessengerMenuManager::requestEditOriginMessage(result,
                                                       MessengerMenuManager::buildPlainTextReply(
                                                               tr("User identifier received.")),
                                                       mgr->lastMenuMessageId(context));
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (state->wizard.step == UserWizardStep::FirstName) {
        state->wizard.errorText.clear();
        const QString value =
                (trimmed == QStringLiteral("-") && state->wizard.mode == UserWizardMode::Edit)
                        ? state->wizard.existingFirstName
                        : (trimmed == QStringLiteral("-") ? QString() : trimmed);
        state->wizard.firstName = value.trimmed();
        state->wizard.step = (state->wizard.mode == UserWizardMode::Edit)
                                     ? UserWizardStep::Review
                                     : nextAddUserStep(state->wizard, UserWizardStep::LastName);
        MessengerMenuManager::requestEditOriginMessage(result,
                                                       MessengerMenuManager::buildPlainTextReply(
                                                               tr("First name received.")),
                                                       mgr->lastMenuMessageId(context));
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (state->wizard.step == UserWizardStep::LastName) {
        state->wizard.errorText.clear();
        const QString value =
                (trimmed == QStringLiteral("-") && state->wizard.mode == UserWizardMode::Edit)
                        ? state->wizard.existingLastName
                        : (trimmed == QStringLiteral("-") ? QString() : trimmed);
        state->wizard.lastName = value.trimmed();
        state->wizard.step = (state->wizard.mode == UserWizardMode::Edit)
                                     ? UserWizardStep::Review
                                     : nextAddUserStep(state->wizard, UserWizardStep::Role);
        MessengerMenuManager::requestEditOriginMessage(result,
                                                       MessengerMenuManager::buildPlainTextReply(
                                                               tr("Last name received.")),
                                                       mgr->lastMenuMessageId(context));
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (state->wizard.step == UserWizardStep::Groups) {
        QMap<Group, User::Role> roles;
        if (trimmed == QStringLiteral("-")) {
            roles = groupRolesForRole(roleForGroupRoles(state->wizard.groupRoles));
        } else {
            QString groupsError;
            if (!MessengerUserFacingErrors::parseUserGroupRoles(trimmed, &roles, &groupsError)) {
                state->wizard.errorText =
                        QStringLiteral("%1\n%2")
                                .arg(groupsError, MessengerUserFacingErrors::useDashForNoGroups());
                result->handled = true;
                result->sendReply = true;
                return true;
            }
        }
        state->wizard.errorText.clear();
        state->wizard.groupRoles = roles;
        state->wizard.step = UserWizardStep::Review;
        state->wizard.reviewReached = true;
        MessengerMenuManager::requestEditOriginMessage(result,
                                                       MessengerMenuManager::buildPlainTextReply(
                                                               tr("Groups received.")),
                                                       mgr->lastMenuMessageId(context));
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    state->wizard.errorText = tr("Use menu buttons to continue.");
    result->handled = true;
    result->sendReply = true;
    return true;
}
