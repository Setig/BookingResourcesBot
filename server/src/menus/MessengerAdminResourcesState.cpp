#include "MessengerAdminResourcesState.h"

#include "MessengerMenuIds.h"

#include "Resource.h"
#include "MessengerBot.h"
#include "ResourceModel.h"
#include "ServerDatabase.h"
#include "MessengerTextRender.h"
#include "MessengerUserFacingErrors.h"

#include <QCollator>
#include <QRegularExpression>

namespace {
static constexpr int kDefaultSelectorPageSize = MessengerMenuIds::kSelectorPageSize;

QString formatGroups(const QSet<Group> &groups)
{
    return MessengerTextRender::formatGroupList(groups, false);
}

bool parseGroups(const QString &text, QSet<Group> *groupsOut, QString *errorOut)
{
    return MessengerUserFacingErrors::parseGroupList(text, groupsOut, errorOut);
}

QSet<Group> defaultGroupsForActor(MessengerMenuManager *manager,
                                  const MessengerMenu::RenderContext &context)
{
    MessengerBot *bot = manager ? manager->bot() : nullptr;
    ServerDatabase *database = bot ? bot->database() : nullptr;
    const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
    QSet<Group> groups = database ? database->administeredGroupRoots(actor) : QSet<Group>();
    if (groups.isEmpty())
        groups.insert(Group::root());
    return groups;
}

MessengerAdminResourcesState::ResourceWizardStep nextAddResourceStep(
        const MessengerAdminResourcesState::ResourceWizard &wizard,
        MessengerAdminResourcesState::ResourceWizardStep regularNextStep)
{
    if (wizard.mode == MessengerAdminResourcesState::ResourceWizardMode::Add
        && wizard.reviewReached)
        return MessengerAdminResourcesState::ResourceWizardStep::Review;
    return regularNextStep;
}
} // namespace

MessengerAdminResourcesState::MessengerAdminResourcesState(MessengerMenuManager *manager)
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

MessengerAdminResourcesState::~MessengerAdminResourcesState() = default;

MessengerAdminResourcesState::State *MessengerAdminResourcesState::ensure(const QString &sessionId)
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

const MessengerAdminResourcesState::State *MessengerAdminResourcesState::find(
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

void MessengerAdminResourcesState::clear(const QString &sessionId)
{
    const QString sid = sessionId.trimmed();
    if (sid.isEmpty())
        return;
    m_states.remove(sid);
}

void MessengerAdminResourcesState::invalidateSelectors()
{
    for (QHash<QString, State>::iterator it = m_states.begin(); it != m_states.end(); ++it) {
        it.value().selector.updatedAtUtc = QDateTime();
    }
}

QString MessengerAdminResourcesState::resourceLabelForButton(const Resource &resource)
{
    return tr("%1 (groups: %2)")
            .arg(MessengerTextRender::formatResourceNameWithStatus(resource, false),
                 MessengerTextRender::formatResourceGroups(resource, false));
}

bool MessengerAdminResourcesState::ensureSelector(State *state,
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

    const ResourceModel *model = database->resourceModel();
    if (!model)
        return false;

    const QList<Resource> resources = model->items();
    QVector<SelectorItem> items;
    items.reserve(resources.size());
    for (const Resource &resource : resources) {
        if (!resource.isValid())
            continue;
        if (!database->canAdministerResource(currentUserKey, resource))
            continue;
        const QString id = resource.uuidString();
        const QString label = resourceLabelForButton(resource);
        if (id.isEmpty() || label.trimmed().isEmpty())
            continue;
        SelectorItem item;
        item.id = id;
        item.label = label;
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

MessengerMenu::MenuPage MessengerAdminResourcesState::buildSelectorPage(
        const QString &currentMenuId,
        const QString &title,
        const QString &emptyText,
        const QString &backMenuId,
        bool adminsOnlyBack,
        State *state) const
{
    MessengerMenu::MenuPage page;
    page.id = currentMenuId.trimmed();
    if (page.id.isEmpty())
        page.id = QLatin1String(MessengerMenuIds::kMenuAdminResources);
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

    page.rows.append({ mgr->makeNavigateButton(tr("Back"), backMenuId, adminsOnlyBack) });
    return page;
}

bool MessengerAdminResourcesState::handleEvent(const QString &event,
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
            const ResourceModel *resourceModel =
                    (database && database->isReady()) ? database->resourceModel() : nullptr;
            const Resource selectedResource =
                    resourceModel ? resourceModel->resourceByUuid(item.id) : Resource();
            const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
            if (!selectedResource.isValid()
                || !database->canAdministerResource(actor, selectedResource)) {
                state->selector.clear();
                state->selectedResourceUuid.clear();
                state->wizard.errorText = tr("Resource not found.");
                result->handled = true;
                result->sendReply = true;
                return true;
            }
            state->selector.clear();

            state->selectedResourceUuid = item.id;

            if (purpose == SelectorPurpose::AdminResourceEdit) {
                state->wizard.clear();
                state->wizard.mode = ResourceWizardMode::Edit;
                state->wizard.step = ResourceWizardStep::Review;

                state->wizard.existingName = selectedResource.name();
                state->wizard.name = selectedResource.name();
                state->wizard.existingDescription = selectedResource.description();
                state->wizard.existingStartMessage = selectedResource.startMessage();
                state->wizard.description = selectedResource.description();
                state->wizard.startMessage = selectedResource.startMessage();
                state->wizard.existingStatus = selectedResource.status();
                state->wizard.status = selectedResource.status();
                state->wizard.existingGroups = selectedResource.groups();
                state->wizard.groups = selectedResource.groups();

                MessengerMenuManager::requestSendAsNewMessageAndEditOrigin(
                        result,
                        MessengerMenuManager::buildPlainTextReply(tr("Edit menu opened.")),
                        mgr->lastMenuMessageId(context));
            } else if (purpose == SelectorPurpose::AdminResourceDelete) {
                state->pendingDeleteResourceUuid = item.id;
            }

            result->handled = true;
            result->sendReply = true;
            return true;
        }
    }

    // Admin resource actions.
    if (trimmed == QStringLiteral("rc:edit")) {
        const QString selectedUuid = state->selectedResourceUuid.trimmed();
        if (selectedUuid.isEmpty())
            return false;

        MessengerBot *bot = mgr->bot();
        ServerDatabase *database = bot ? bot->database() : nullptr;
        const ResourceModel *resourceModel =
                (database && database->isReady()) ? database->resourceModel() : nullptr;
        const Resource resource =
                resourceModel ? resourceModel->resourceByUuid(selectedUuid) : Resource();
        const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
        if (!resource.isValid() || !database->canAdministerResource(actor, resource)) {
            state->resetFlows();
            result->handled = true;
            result->sendReply = true;
            result->reply = MessengerMenuManager::buildPlainTextReply(tr("Resource not found."));
            return true;
        }

        state->resetFlows();
        state->selectedResourceUuid = selectedUuid;

        state->wizard.clear();
        state->wizard.mode = ResourceWizardMode::Edit;
        state->wizard.step = ResourceWizardStep::Review;

        state->wizard.existingName = resource.name();
        state->wizard.name = resource.name();
        state->wizard.existingDescription = resource.description();
        state->wizard.existingStartMessage = resource.startMessage();
        state->wizard.description = resource.description();
        state->wizard.startMessage = resource.startMessage();
        state->wizard.existingStatus = resource.status();
        state->wizard.status = resource.status();
        state->wizard.existingGroups = resource.groups();
        state->wizard.groups = resource.groups();

        mgr->setCurrentMenuId(context,
                              QLatin1String(MessengerMenuIds::kMenuAdminResourceEdit),
                              false);
        MessengerMenuManager::requestSendAsNewMessageAndEditOrigin(
                result,
                MessengerMenuManager::buildPlainTextReply(tr("Edit menu opened.")),
                mgr->lastMenuMessageId(context));
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (trimmed == QStringLiteral("rc:delete")) {
        const QString selected = state->selectedResourceUuid.trimmed();
        if (selected.isEmpty())
            return false;

        MessengerBot *bot = mgr->bot();
        ServerDatabase *database = bot ? bot->database() : nullptr;
        const ResourceModel *resourceModel =
                (database && database->isReady()) ? database->resourceModel() : nullptr;
        const Resource resource =
                resourceModel ? resourceModel->resourceByUuid(selected) : Resource();
        const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
        if (!resource.isValid() || !database->canAdministerResource(actor, resource)) {
            state->resetFlows();
            result->handled = true;
            result->sendReply = true;
            result->reply = MessengerMenuManager::buildPlainTextReply(tr("Resource not found."));
            return true;
        }

        state->pendingDeleteResourceUuid = selected;
        mgr->setCurrentMenuId(context,
                              QLatin1String(MessengerMenuIds::kMenuAdminResourceDelete),
                              false);
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (trimmed == QStringLiteral("delr:yes")) {
        const QString resourceUuid = state->pendingDeleteResourceUuid.trimmed();
        if (resourceUuid.isEmpty())
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

        const ResourceModel *resourceModel = database->resourceModel();
        const Resource resource =
                resourceModel ? resourceModel->resourceByUuid(resourceUuid) : Resource();
        const QString resourceName = resource.isValid() ? resource.name() : QString();
        const QString resourceLabel =
                resource.isValid() ? MessengerTextRender::formatResourceLine(resource, false)
                                   : resourceName;
        if (resourceName.trimmed().isEmpty()) {
            result->handled = true;
            result->sendReply = true;
            result->reply = MessengerMenuManager::buildPlainTextReply(tr("Select a resource."));
            return true;
        }

        const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
        const ServerDatabaseError dbError = database->deleteResource(resourceName, actor);
        if (dbError) {
            result->handled = true;
            result->sendReply = true;
            result->reply = MessengerMenuManager::buildPlainTextReply(dbError.errorString());
            return true;
        }

        const MessengerMenuManager::Reply originReply = MessengerMenuManager::buildPlainTextReply(
                tr("Resource deleted: %1").arg(resourceLabel));
        MessengerMenuManager::requestSendAsNewMessageAndEditOrigin(result,
                                                                   originReply,
                                                                   mgr->lastMenuMessageId(context));

        state->resetFlows();
        mgr->setCurrentMenuId(context, QLatin1String(MessengerMenuIds::kMenuAdminResources), true);

        result->handled = true;
        result->sendReply = true;
        return true;
    }

    // Resource wizard events.
    if (!trimmed.startsWith(QStringLiteral("rw:")))
        return false;

    if (trimmed == QStringLiteral("rw:back")) {
        state->wizard.errorText.clear();

        if (state->wizard.mode == ResourceWizardMode::Add) {
            switch (state->wizard.step) {
            case ResourceWizardStep::Description:
                state->wizard.step = ResourceWizardStep::Name;
                break;
            case ResourceWizardStep::StartMessage:
                state->wizard.step = ResourceWizardStep::Description;
                break;
            case ResourceWizardStep::Groups:
                state->wizard.step = ResourceWizardStep::StartMessage;
                break;
            case ResourceWizardStep::Review:
                state->wizard.step = ResourceWizardStep::Groups;
                break;
            default:
                break;
            }
            result->handled = true;
            result->sendReply = true;
        } else if (state->wizard.mode == ResourceWizardMode::Edit) {
            // In edit mode, return to the summary screen.
            state->wizard.step = ResourceWizardStep::Review;
            result->handled = true;
            result->sendReply = true;
        }
        if (!result->sendReply) {
            result->handled = true;
            result->sendReply = true;
        }
        return true;
    }

    if (trimmed == QStringLiteral("rw:skip")) {
        state->wizard.errorText.clear();

        if (state->wizard.mode == ResourceWizardMode::Add) {
            if (state->wizard.step == ResourceWizardStep::Description) {
                state->wizard.description.clear();
                state->wizard.step =
                        nextAddResourceStep(state->wizard, ResourceWizardStep::StartMessage);
                result->handled = true;
                result->sendReply = true;
            } else if (state->wizard.step == ResourceWizardStep::StartMessage) {
                state->wizard.startMessage.clear();
                state->wizard.step = nextAddResourceStep(state->wizard, ResourceWizardStep::Groups);
                result->handled = true;
                result->sendReply = true;
            } else if (state->wizard.step == ResourceWizardStep::Groups) {
                state->wizard.groups = defaultGroupsForActor(mgr, context);
                state->wizard.step = ResourceWizardStep::Review;
                state->wizard.reviewReached = true;
                result->handled = true;
                result->sendReply = true;
            }
        } else if (state->wizard.mode == ResourceWizardMode::Edit) {
            if (state->wizard.step == ResourceWizardStep::Name) {
                state->wizard.name = state->wizard.existingName;
                state->wizard.step = ResourceWizardStep::Review;
                result->handled = true;
                result->sendReply = true;
            } else if (state->wizard.step == ResourceWizardStep::Description) {
                state->wizard.description = state->wizard.existingDescription;
                state->wizard.step = ResourceWizardStep::Review;
                result->handled = true;
                result->sendReply = true;
            } else if (state->wizard.step == ResourceWizardStep::StartMessage) {
                state->wizard.startMessage = state->wizard.existingStartMessage;
                state->wizard.step = ResourceWizardStep::Review;
                result->handled = true;
                result->sendReply = true;
            } else if (state->wizard.step == ResourceWizardStep::Status) {
                state->wizard.status = state->wizard.existingStatus;
                state->wizard.step = ResourceWizardStep::Review;
                result->handled = true;
                result->sendReply = true;
            } else if (state->wizard.step == ResourceWizardStep::Groups) {
                state->wizard.groups = state->wizard.existingGroups;
                state->wizard.step = ResourceWizardStep::Review;
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

    if (trimmed == QStringLiteral("rw:edit:name")) {
        if (state->wizard.mode != ResourceWizardMode::None) {
            state->wizard.errorText.clear();
            state->wizard.step = ResourceWizardStep::Name;
            result->handled = true;
            result->sendReply = true;
        }
        if (!result->sendReply) {
            result->handled = true;
            result->sendReply = true;
        }
        return true;
    }

    if (trimmed == QStringLiteral("rw:edit:desc")) {
        if (state->wizard.mode != ResourceWizardMode::None) {
            state->wizard.errorText.clear();
            state->wizard.step = ResourceWizardStep::Description;
            result->handled = true;
            result->sendReply = true;
        }
        if (!result->sendReply) {
            result->handled = true;
            result->sendReply = true;
        }
        return true;
    }

    if (trimmed == QStringLiteral("rw:edit:start")) {
        if (state->wizard.mode != ResourceWizardMode::None) {
            state->wizard.errorText.clear();
            state->wizard.step = ResourceWizardStep::StartMessage;
            result->handled = true;
            result->sendReply = true;
        }
        if (!result->sendReply) {
            result->handled = true;
            result->sendReply = true;
        }
        return true;
    }

    if (trimmed == QStringLiteral("rw:edit:status")) {
        if (state->wizard.mode != ResourceWizardMode::None) {
            state->wizard.errorText.clear();
            state->wizard.step = ResourceWizardStep::Status;
            result->handled = true;
            result->sendReply = true;
        }
        if (!result->sendReply) {
            result->handled = true;
            result->sendReply = true;
        }
        return true;
    }

    if (trimmed == QStringLiteral("rw:edit:groups")) {
        if (state->wizard.mode != ResourceWizardMode::None) {
            state->wizard.errorText.clear();
            state->wizard.step = ResourceWizardStep::Groups;
            result->handled = true;
            result->sendReply = true;
        }
        if (!result->sendReply) {
            result->handled = true;
            result->sendReply = true;
        }
        return true;
    }

    if (trimmed == QStringLiteral("rw:setstatus:on")) {
        state->wizard.errorText.clear();
        state->wizard.status = Resource::Status::On;
        state->wizard.step = ResourceWizardStep::Review;
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (trimmed == QStringLiteral("rw:setstatus:temp")) {
        state->wizard.errorText.clear();
        state->wizard.status = Resource::Status::TemporarilyUnavailable;
        state->wizard.step = ResourceWizardStep::Review;
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (trimmed == QStringLiteral("rw:setstatus:off")) {
        state->wizard.errorText.clear();
        state->wizard.status = Resource::Status::Off;
        state->wizard.step = ResourceWizardStep::Review;
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (trimmed == QStringLiteral("rw:confirm")) {
        state->wizard.errorText.clear();

        MessengerBot *bot = mgr->bot();
        ServerDatabase *database = bot ? bot->database() : nullptr;

        if (state->wizard.mode == ResourceWizardMode::Add) {
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

            const QString name = state->wizard.name.trimmed();
            const QString description = state->wizard.description.trimmed();
            const QString startMessage = state->wizard.startMessage.trimmed();
            Resource resource(name, description, startMessage);
            resource.setGroups(state->wizard.groups);
            const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
            const ServerDatabaseError dbError = database->addResource(resource, actor);
            if (dbError) {
                state->wizard.errorText = dbError.errorString();
                result->handled = true;
                result->sendReply = true;
                return true;
            }

            const QString resourceLine = MessengerTextRender::formatResourceLine(resource, false);
            const MessengerMenuManager::Reply originReply =
                    MessengerMenuManager::buildPlainTextReply(
                            tr("Resource added: %1").arg(resourceLine));
            MessengerMenuManager::requestSendAsNewMessageAndEditOrigin(result,
                                                                       originReply,
                                                                       mgr->lastMenuMessageId(
                                                                               context));

            state->resetFlows();
            mgr->setCurrentMenuId(context,
                                  QLatin1String(MessengerMenuIds::kMenuAdminResources),
                                  true);

            result->handled = true;
            result->sendReply = true;
            return true;
        }

        if (state->wizard.mode != ResourceWizardMode::Edit) {
            result->handled = true;
            result->sendReply = true;
            return true;
        }

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

        const QString oldName = state->wizard.existingName.trimmed();
        const QString newName = state->wizard.name.trimmed();
        if (oldName.isEmpty() || newName.isEmpty()) {
            state->wizard.errorText = tr("Resource name cannot be empty.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
        Resource updated(newName,
                         state->wizard.description,
                         state->wizard.startMessage,
                         state->wizard.status);
        updated.setGroups(state->wizard.groups);
        const ServerDatabaseError updateError = database->replaceResource(oldName, updated, actor);
        if (updateError) {
            state->wizard.errorText = updateError.errorString();
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        state->resetFlows();
        mgr->setCurrentMenuId(context, QLatin1String(MessengerMenuIds::kMenuAdminResources), true);

        result->handled = true;
        result->sendReply = true;
        return true;
    }

    return false;
}

bool MessengerAdminResourcesState::handleTextInput(const QString &text,
                                                   const QString &sessionId,
                                                   const MessengerMenu::RenderContext &context,
                                                   State *state,
                                                   MessengerMenuManager::ActionResult *result)
{
    Q_UNUSED(sessionId);
    Q_UNUSED(context);

    if (!state || !result)
        return false;

    MessengerMenuManager *mgr = m_manager;
    if (!mgr)
        return false;

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return false;

    if (state->wizard.mode == ResourceWizardMode::None)
        return false;

    if (state->wizard.step == ResourceWizardStep::Name) {
        if (trimmed == QStringLiteral("-")) {
            state->wizard.errorText = tr("Resource name cannot be empty.");
            result->handled = true;
            result->sendReply = true;
        } else {
            state->wizard.errorText.clear();
            state->wizard.name = trimmed;
            state->wizard.step =
                    (state->wizard.mode == ResourceWizardMode::Edit)
                            ? ResourceWizardStep::Review
                            : nextAddResourceStep(state->wizard, ResourceWizardStep::Description);
            result->handled = true;
            result->sendReply = true;
            MessengerMenuManager::requestEditOriginMessage(
                    result,
                    MessengerMenuManager::buildPlainTextReply(tr("Resource name received.")),
                    mgr->lastMenuMessageId(context));
        }
        return true;
    }

    if (state->wizard.step == ResourceWizardStep::Description) {
        state->wizard.errorText.clear();
        if (state->wizard.mode == ResourceWizardMode::Edit && trimmed == QStringLiteral("-")) {
            state->wizard.description = state->wizard.existingDescription;
        } else {
            state->wizard.description = (trimmed == QStringLiteral("-")) ? QString() : trimmed;
        }
        state->wizard.step =
                (state->wizard.mode == ResourceWizardMode::Edit)
                        ? ResourceWizardStep::Review
                        : nextAddResourceStep(state->wizard, ResourceWizardStep::StartMessage);
        result->handled = true;
        result->sendReply = true;
        MessengerMenuManager::requestEditOriginMessage(result,
                                                       MessengerMenuManager::buildPlainTextReply(
                                                               tr("Description received.")),
                                                       mgr->lastMenuMessageId(context));
        return true;
    }

    if (state->wizard.step == ResourceWizardStep::StartMessage) {
        state->wizard.errorText.clear();
        if (state->wizard.mode == ResourceWizardMode::Edit && trimmed == QStringLiteral("-")) {
            state->wizard.startMessage = state->wizard.existingStartMessage;
        } else {
            state->wizard.startMessage = (trimmed == QStringLiteral("-")) ? QString() : trimmed;
        }
        state->wizard.step =
                (state->wizard.mode == ResourceWizardMode::Edit)
                        ? ResourceWizardStep::Review
                        : nextAddResourceStep(state->wizard, ResourceWizardStep::Groups);
        result->handled = true;
        result->sendReply = true;
        MessengerMenuManager::requestEditOriginMessage(result,
                                                       MessengerMenuManager::buildPlainTextReply(
                                                               tr("Start message received.")),
                                                       mgr->lastMenuMessageId(context));
        return true;
    }

    if (state->wizard.step == ResourceWizardStep::Groups) {
        state->wizard.errorText.clear();
        if (state->wizard.mode == ResourceWizardMode::Edit && trimmed == QStringLiteral("-")) {
            state->wizard.groups = state->wizard.existingGroups;
        } else {
            QString groupsError;
            if (!parseGroups(trimmed, &state->wizard.groups, &groupsError)) {
                state->wizard.errorText = groupsError;
                result->handled = true;
                result->sendReply = true;
                return true;
            }
        }
        state->wizard.step = ResourceWizardStep::Review;
        state->wizard.reviewReached = true;
        result->handled = true;
        result->sendReply = true;
        MessengerMenuManager::requestEditOriginMessage(result,
                                                       MessengerMenuManager::buildPlainTextReply(
                                                               tr("Groups received.")),
                                                       mgr->lastMenuMessageId(context));
        return true;
    }

    if (state->wizard.step == ResourceWizardStep::Status) {
        Resource::Status parsedStatus = Resource::Status::On;
        if (!Resource::tryParseStatusToken(trimmed, &parsedStatus)) {
            state->wizard.errorText = MessengerUserFacingErrors::invalidStatus(trimmed);
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        state->wizard.errorText.clear();
        state->wizard.status = parsedStatus;
        state->wizard.step = ResourceWizardStep::Review;
        result->handled = true;
        result->sendReply = true;
        MessengerMenuManager::requestEditOriginMessage(result,
                                                       MessengerMenuManager::buildPlainTextReply(
                                                               tr("Status received.")),
                                                       mgr->lastMenuMessageId(context));
        return true;
    }

    state->wizard.errorText = tr("Use menu buttons to continue.");
    result->handled = true;
    result->sendReply = true;
    return true;
}
