#ifndef RESOURCESBOT_H
#define RESOURCESBOT_H

#include "AbstractBot.h"
#include "UserTimeZone.h"
#include "BookingResourcesTranslator.h"

#include "MessengerType.h"
#include "MessengerUserKey.h"

class ServerDatabase;
class MessengerBotPoll;
class MessengerMenuManager;
class MessengerBotTestHelper;

class MessengerBotPrivate;

/**
 * @brief High-level messenger-agnostic bot wrapper that processes incoming updates.
 *
 * MessengerBot owns an AbstractBot backend, subscribes to its incoming update signals, and
 * implements high-level validation/command handling while delegating menu interactions to
 * MessengerMenuManager.
 */
class MessengerBot : public QObject
{
    Q_OBJECT

public:
    explicit MessengerBot(MessengerType type,
                          const QString &token,
                          ServerDatabase *database = nullptr,
                          BookingResourcesTranslator *translator = nullptr,
                          QObject *parent = nullptr,
                          MessengerBotPoll *poll = nullptr);

    ~MessengerBot() override;

    bool isRunning() const;

    MessengerType botType() const;
    ServerDatabase *database() const;
    AbstractBot *backendBot() const;
    MessengerBotPoll *poll() const;
    BookingResourcesTranslator *translator() const;
    MessengerMenuManager *menuManager() const;

    void sendReply(const AbstractBot::ChatAddress &chat,
                   const QString &text,
                   AbstractBot::MessageFormat format = AbstractBot::MessageFormat::PlainText) const;
    void sendReply(const AbstractBot::ChatAddress &chat,
                   const QString &text,
                   const AbstractBot::MessageOptions &options) const;
    void sendFile(
            const AbstractBot::ChatAddress &chat,
            const QString &filePath,
            const QString &caption = QString(),
            AbstractBot::MessageFormat captionFormat = AbstractBot::MessageFormat::PlainText) const;

    void editReply(const AbstractBot::ChatAddress &chat,
                   const QString &messageId,
                   const QString &text,
                   AbstractBot::MessageFormat format = AbstractBot::MessageFormat::PlainText) const;
    void editReply(const AbstractBot::ChatAddress &chat,
                   const QString &messageId,
                   const QString &text,
                   const AbstractBot::MessageOptions &options) const;

    [[nodiscard]] bool ensureDatabase(const AbstractBot::ChatAddress &chat) const;
    [[nodiscard]] BookingResourcesTranslator::ScopedLocaleOverride applyScopedLocale(
            const QString &locale) const;
    [[nodiscard]] QString effectiveLocaleForUser(const MessengerUserKey &user,
                                                 const QString &reportedLocale = QString()) const;
    [[nodiscard]] UserTimeZone effectiveTimeZoneForUser(const MessengerUserKey &user) const;
    /**
     * @brief Sends a direct user message, optionally using platform formatting.
     */
    void sendDirectMessage(
            const MessengerUserKey &user,
            const QString &text,
            AbstractBot::MessageFormat format = AbstractBot::MessageFormat::PlainText) const;
    /**
     * @brief Returns true when the user's direct-message backend supports the format.
     */
    [[nodiscard]] bool supportsDirectMessageFormat(const MessengerUserKey &user,
                                                   AbstractBot::MessageFormat format) const;
    /**
     * @brief Sends a best-effort notification that the user's access was activated.
     */
    void notifyAccessActivated(const MessengerUserKey &user) const;
    void notifyNextUserQueueStatus(const QString &resourceName,
                                   const MessengerUserKey &actor) const;
    [[nodiscard]] bool isAdminUser(const MessengerUserKey &user) const;
    [[nodiscard]] bool canManageServerSettings(const MessengerUserKey &user) const;
    [[nodiscard]] bool hasActiveAccess(const MessengerUserKey &user) const;

public slots:
    void start();
    void stop();

signals:
    void errorOccurred(const QString &description);

private slots:
    void handleIncomingMessage(const AbstractBot::IncomingMessage &message);
    void handleIncomingCallbackQuery(const AbstractBot::CallbackQuery &query);

private:
    void processMessage(const AbstractBot::IncomingMessage &message);
    [[nodiscard]] bool applyUserAccessPolicy(const MessengerUserKey &senderKey,
                                             const AbstractBot::ChatAddress &chat,
                                             const AbstractBot::UserAddress &sender);
    // Test hook for swapping the backend bot implementation.
    void replaceBackendBot(AbstractBot *bot);

    MessengerBotPrivate *d;

    friend class MessengerBotTestHelper;
};

#endif // RESOURCESBOT_H
