#include "MessengerUserResourcesState.h"

#include "MessengerMenuManager.h"

MessengerUserResourcesState::MessengerUserResourcesState(MessengerMenuManager *manager)
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

MessengerUserResourcesState::~MessengerUserResourcesState() = default;

MessengerUserResourcesState::State *MessengerUserResourcesState::ensure(const QString &sessionId)
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

const MessengerUserResourcesState::State *MessengerUserResourcesState::find(
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

void MessengerUserResourcesState::clear(const QString &sessionId)
{
    const QString sid = sessionId.trimmed();
    if (sid.isEmpty())
        return;
    m_states.remove(sid);
}

void MessengerUserResourcesState::invalidateSelectors()
{
    for (QHash<QString, State>::iterator it = m_states.begin(); it != m_states.end(); ++it) {
        it.value().selector.updatedAtUtc = QDateTime();
    }
}
