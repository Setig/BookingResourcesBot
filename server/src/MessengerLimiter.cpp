#include "MessengerLimiter.h"

#include "UserModel.h"
#include "BlacklistModel.h"
#include "ServerDatabase.h"
#include "ServerRuntimeSettings.h"
#include "PendingAccessUserEntry.h"

#include <QDateTime>
#include <QStringList>
#include <QAbstractItemModel>

MessengerLimiter::MessengerLimiter(ServerDatabase *database, QObject *parent)
    : QObject(parent)
    , m_database(nullptr)
    , m_userModel(nullptr)
    , m_blacklistModel(nullptr)
    , m_limit(10)
    , m_warningThreshold(3)
{
    setDatabase(database);
}

ServerDatabase *MessengerLimiter::database() const
{
    return m_database;
}

void MessengerLimiter::setDatabase(ServerDatabase *database)
{
    if (m_database == database)
        return;

    detachFromModels();
    m_database = database;
    attachToModels();

    // Ensure the counter table does not keep data for users with active access.
    clearCountersForActiveAccessUsers();
    clearCountersForBlacklistedUsers();
}

int MessengerLimiter::limit() const
{
    return m_limit;
}

void MessengerLimiter::setLimit(int limit)
{
    m_limit = qMax(1, limit);
    if (m_warningThreshold > m_limit)
        m_warningThreshold = m_limit;
}

int MessengerLimiter::warningThreshold() const
{
    return m_warningThreshold;
}

void MessengerLimiter::setWarningThreshold(int threshold)
{
    if (threshold <= 0) {
        m_warningThreshold = 0;
        return;
    }
    m_warningThreshold = qMin(threshold, m_limit);
}

MessengerLimiter::AttemptResult MessengerLimiter::registerAttempt(const MessengerUserKey &user)
{
    return registerAttemptInternal(user);
}

void MessengerLimiter::clear(const MessengerUserKey &user)
{
    clearInternal(user);
}

int MessengerLimiter::attemptCount(const MessengerUserKey &user) const
{
    if (!user.isValid())
        return 0;
    return m_userCounters.value(user, 0);
}

MessengerLimiter::AccessDecision MessengerLimiter::evaluate(const MessengerUserKey &user,
                                                            const AbstractBot::UserAddress &sender)
{
    AccessDecision decision;

    if (!user.isValid())
        return decision;

    // Local CLI does not require user access/auth checks.
    if (user.messenger() == MessengerType::LocalCli)
        return decision;

    if (!isDatabaseReady())
        return decision;

    const UserModel *userModel = m_database ? m_database->userModel() : nullptr;
    const User activeUser = userModel ? userModel->userByKey(user) : User();
    if (activeUser.isValid() && activeUser.isFixedAdmin()) {
        clearInternal(user);
        return decision;
    }

    // If a user is already blocked, deny early.
    BlacklistEntry blockedEntry;
    if (m_database && m_database->isBlacklisted(user, &blockedEntry)) {
        clearInternal(user);

        const bool accessActive = userModel && userModel->exists(user);
        if (accessActive) {
            decision.allow = false;
            decision.showBlockedUserReply = true;
            decision.blacklistEntry = blockedEntry;
        } else {
            decision.allow = false;
        }
        return decision;
    }

    // Users with active access are not subject to the limiter.
    if (userModel && userModel->exists(user)) {
        clearInternal(user);
        return decision;
    }

    // Access-pending user attempt: record and apply limit.
    recordPendingAccessUser(user, sender);

    static constexpr int kAutoBlacklistDurationDays = 7;
    const AttemptResult attempt = registerAttemptInternal(user);

    if (attempt.reachedLimit) {
        clearInternal(user);

        if (!m_database)
            return decision;

        const QDateTime blockedAtUtc = QDateTime::currentDateTimeUtc();
        const QDateTime blockedUntilUtc = blockedAtUtc.addDays(kAutoBlacklistDurationDays);
        const MessengerUserKey systemActor(MessengerType::LocalCli,
                                           QStringLiteral("messenger_limiter"));

        const BlacklistEntry entry(user,
                                   tr("Access-pending user message spam"),
                                   systemActor,
                                   blockedAtUtc,
                                   blockedUntilUtc);
        const ServerDatabaseError error = m_database->saveBlacklistEntry(entry, systemActor);
        if (!error) {
            Q_EMIT autoBlacklisted(user, entry);
        }

        decision.allow = false;
        return decision;
    }

    decision.allow = false;
    decision.shouldReply = true;

    decision.replyText = buildPendingAccessUserReply(attempt, user);
    return decision;
}

void MessengerLimiter::attachToModels()
{
    m_userModel = nullptr;
    m_blacklistModel = nullptr;

    if (!m_database)
        return;

    m_userModel = m_database->userModel();
    m_blacklistModel = m_database->blacklistModel();

    const QAbstractItemModel *users = m_userModel;
    if (users) {
        QObject::connect(users, &QAbstractItemModel::modelReset, this, [this]() {
            onUserModelChanged();
        });
        QObject::connect(users,
                         &QAbstractItemModel::rowsInserted,
                         this,
                         [this](const QModelIndex &, int, int) { onUserModelChanged(); });
        QObject::connect(users,
                         &QAbstractItemModel::rowsRemoved,
                         this,
                         [this](const QModelIndex &, int, int) { onUserModelChanged(); });
        QObject::connect(users,
                         &QAbstractItemModel::dataChanged,
                         this,
                         [this](const QModelIndex &, const QModelIndex &) {
                             onUserModelChanged();
                         });
    }

    const QAbstractItemModel *blacklist = m_blacklistModel;
    if (blacklist) {
        QObject::connect(blacklist, &QAbstractItemModel::modelReset, this, [this]() {
            onBlacklistModelChanged();
        });
        QObject::connect(blacklist,
                         &QAbstractItemModel::rowsInserted,
                         this,
                         [this](const QModelIndex &, int, int) { onBlacklistModelChanged(); });
        QObject::connect(blacklist,
                         &QAbstractItemModel::rowsRemoved,
                         this,
                         [this](const QModelIndex &, int, int) { onBlacklistModelChanged(); });
        QObject::connect(blacklist,
                         &QAbstractItemModel::dataChanged,
                         this,
                         [this](const QModelIndex &, const QModelIndex &) {
                             onBlacklistModelChanged();
                         });
    }
}

void MessengerLimiter::detachFromModels()
{
    if (m_userModel)
        QObject::disconnect(m_userModel, nullptr, this, nullptr);
    if (m_blacklistModel)
        QObject::disconnect(m_blacklistModel, nullptr, this, nullptr);

    m_userModel = nullptr;
    m_blacklistModel = nullptr;
}

void MessengerLimiter::onUserModelChanged()
{
    clearCountersForActiveAccessUsers();
}

void MessengerLimiter::onBlacklistModelChanged()
{
    clearCountersForBlacklistedUsers();
}

void MessengerLimiter::clearCountersForActiveAccessUsers()
{
    if (!m_database || !m_database->isReady())
        return;
    const UserModel *model = m_database->userModel();
    if (!model)
        return;

    QHash<MessengerUserKey, int>::iterator it = m_userCounters.begin();
    while (it != m_userCounters.end()) {
        if (model->exists(it.key())) {
            it = m_userCounters.erase(it);
        } else {
            ++it;
        }
    }
}

void MessengerLimiter::clearCountersForBlacklistedUsers()
{
    if (!m_database || !m_database->isReady())
        return;
    const BlacklistModel *model = m_database->blacklistModel();
    if (!model)
        return;

    QHash<MessengerUserKey, int>::iterator it = m_userCounters.begin();
    while (it != m_userCounters.end()) {
        if (model->exists(it.key())) {
            it = m_userCounters.erase(it);
        } else {
            ++it;
        }
    }
}

MessengerLimiter::AttemptResult MessengerLimiter::registerAttemptInternal(
        const MessengerUserKey &user)
{
    AttemptResult result;
    if (!user.isValid())
        return result;

    int attempts = m_userCounters.value(user, 0);
    attempts += 1;

    result.valid = true;
    result.attempts = attempts;

    if (attempts >= m_limit) {
        result.reachedLimit = true;
        result.remainingBeforeBlock = 0;
        m_userCounters.remove(user);
        return result;
    }

    m_userCounters.insert(user, attempts);
    result.remainingBeforeBlock = qMax(0, m_limit - attempts);
    result.showWarning = (m_warningThreshold > 0 && attempts >= m_warningThreshold);
    return result;
}

void MessengerLimiter::clearInternal(const MessengerUserKey &user)
{
    if (!user.isValid())
        return;
    m_userCounters.remove(user);
}

void MessengerLimiter::recordPendingAccessUser(const MessengerUserKey &user,
                                               const AbstractBot::UserAddress &sender)
{
    if (!m_database || !m_database->isReady())
        return;

    if (!user.isValid())
        return;

    PendingAccessUserEntry entry(user);
    entry.setLastMessageAtUtc(QDateTime::currentDateTimeUtc());

    const QString senderFirst = sender.firstName.trimmed();
    if (!senderFirst.isEmpty())
        entry.setFirstName(senderFirst);

    const QString senderLast = sender.lastName.trimmed();
    if (!senderLast.isEmpty())
        entry.setLastName(senderLast);

    if (entry.firstName().isEmpty()) {
        const QString username = sender.username.trimmed();
        if (!username.isEmpty())
            entry.setFirstName(username);
    }

    m_database->recordPendingAccessUser(entry, entry.user());
}

bool MessengerLimiter::isDatabaseReady() const
{
    return m_database && m_database->isReady();
}

QString MessengerLimiter::buildPendingAccessUserReply(const AttemptResult &attempt,
                                                      const MessengerUserKey &user) const
{
    static constexpr int kAutoBlacklistDurationDays = 7;

    QString reply;
    if (isDatabaseReady()) {
        reply = m_database
                        ->serverSettingValue(
                                ServerRuntimeSettings::Setting::PendingAccessUserReplyMessage)
                        .toString()
                        .trimmed();
    }
    if (reply.isEmpty()) {
        reply = tr("Access denied. Access is not activated. "
                   "Contact an administrator to request access activation.");
    }

    if (user.isValid())
        reply += tr("\nYour ID: %1").arg(user.toString());

    if (attempt.showWarning) {
        const int remaining = qMax(0, attempt.remainingBeforeBlock);
        reply += tr("\nWarning: %1 attempt(s) left before automatic block for %2 day(s).")
                         .arg(QString::number(remaining),
                              QString::number(kAutoBlacklistDurationDays));
    }
    return reply;
}
