#ifndef MESSENGERLIMITER_H
#define MESSENGERLIMITER_H

#include <QObject>

#include <QHash>
#include <QString>

#include "AbstractBot.h"
#include "BlacklistEntry.h"
#include "MessengerUserKey.h"

class ServerDatabase;
class UserModel;
class BlacklistModel;

/**
 * @brief High-level incoming update limiter and access gate.
 *
 * The limiter is designed to be used at the entry point of incoming updates (messages,
 * callback queries, etc.). It can deny processing for:
 * - blacklisted users (by consulting ServerDatabase blacklist model)
 * - users awaiting access activation (by counting attempts and applying an automatic blacklist entry
 *   after a configured limit is exceeded)
 *
 * The class is a QObject so it can subscribe to UserModel / BlacklistModel changes and keep its
 * internal counters in sync without requiring explicit calls from commands or menus.
 */
class MessengerLimiter : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Low-level counter result for access-pending interactions.
     *
     * This API is primarily intended for unit tests. Production code should use evaluate().
     */
    struct AttemptResult
    {
        bool valid = false;
        bool reachedLimit = false;
        bool showWarning = false;
        int attempts = 0;
        int remainingBeforeBlock = 0;
    };

    /**
     * @brief Access decision for an incoming user interaction.
     */
    struct AccessDecision
    {
        bool allow = true;
        bool shouldReply = false;
        QString replyText;

        bool showBlockedUserReply = false;
        BlacklistEntry blacklistEntry;
    };

    explicit MessengerLimiter(ServerDatabase *database = nullptr, QObject *parent = nullptr);

    ServerDatabase *database() const;
    void setDatabase(ServerDatabase *database);

    int limit() const;
    void setLimit(int limit);

    int warningThreshold() const;
    void setWarningThreshold(int threshold);

    AttemptResult registerAttempt(const MessengerUserKey &user);
    void clear(const MessengerUserKey &user);
    int attemptCount(const MessengerUserKey &user) const;

    /**
     * @brief Evaluates whether an incoming interaction should be processed.
     *
     * If allow is false, the caller should stop processing the update.
     *
     * The limiter does not send any messages by itself. If shouldReply is true, the caller may
     * send replyText. If showBlockedUserReply is true, the caller may use blacklistEntry to build
     * a detailed "blocked" message.
     *
     * @param user Stable user identifier.
     * @param sender Sender metadata (used for recording users awaiting access activation).
     * @return AccessDecision describing whether processing should continue.
     */
    [[nodiscard]] AccessDecision evaluate(const MessengerUserKey &user,
                                          const AbstractBot::UserAddress &sender);

signals:
    void autoBlacklisted(const MessengerUserKey &user, const BlacklistEntry &entry);

private:
    void attachToModels();
    void detachFromModels();
    void onUserModelChanged();
    void onBlacklistModelChanged();
    void clearCountersForActiveAccessUsers();
    void clearCountersForBlacklistedUsers();

    AttemptResult registerAttemptInternal(const MessengerUserKey &user);

    void clearInternal(const MessengerUserKey &user);

    void recordPendingAccessUser(const MessengerUserKey &user,
                                 const AbstractBot::UserAddress &sender);

    [[nodiscard]] bool isDatabaseReady() const;
    [[nodiscard]] QString buildPendingAccessUserReply(const AttemptResult &attempt,
                                                      const MessengerUserKey &user) const;

    ServerDatabase *m_database;             // non-owning
    const UserModel *m_userModel;           // non-owning
    const BlacklistModel *m_blacklistModel; // non-owning

    int m_limit;
    int m_warningThreshold;
    QHash<MessengerUserKey, int> m_userCounters;
};

#endif // MESSENGERLIMITER_H
