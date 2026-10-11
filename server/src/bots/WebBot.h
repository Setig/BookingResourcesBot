#ifndef WEBBOT_H
#define WEBBOT_H

#include "AbstractBot.h"

#include <QTimer>
#include <QQueue>
#include <QJsonArray>
#include <QJsonObject>

#include "SharedMemoryServer.h"

class BookingResourcesTranslator;
class MessengerAbstractCommand;

/**
 * @brief AbstractBot implementation that bridges the server to the web UI over shared memory.
 *
 * WebBot owns the server side of the IPC channel, converts incoming web requests into normalized
 * AbstractBot signals, and sends bot responses back to the web process.
 */
class WebBot final : public AbstractBot
{
    Q_OBJECT

public:
    /**
     * @brief Constructs a WebBot instance.
     * @param sharedMemoryKey Shared-memory endpoint key used by the web process.
     * @param parent Optional QObject parent.
     */
    explicit WebBot(const QString &sharedMemoryKey = QString(), QObject *parent = nullptr);

    /**
     * @brief Destroys the WebBot instance.
     */
    ~WebBot() override;

    [[nodiscard]] MessengerType messenger() const override;
    [[nodiscard]] Capabilities capabilities() const override;
    [[nodiscard]] bool isRunning() const override;
    bool fetchUserProfile(const QString &userId,
                          AbstractBot::UserProfile *profileOut,
                          QString *errorDescription) override;
    void setTranslator(BookingResourcesTranslator *translator);
    void setSharedMemorySecret(const QString &secret);

public slots:
    /**
     * @brief Starts the shared-memory IPC server.
     */
    void start() override;

    /**
     * @brief Stops the shared-memory IPC server and drops pending outgoing frames.
     */
    void stop() override;

    void startShutdown() override;
    bool waitShutdown(int msecs = 5000) override;
    void sendMessage(const AbstractBot::ChatAddress &chat,
                     const QString &text,
                     const AbstractBot::MessageOptions &options) override;
    void sendFile(const AbstractBot::ChatAddress &chat,
                  const QString &filePath,
                  const QString &caption = QString(),
                  AbstractBot::MessageFormat captionFormat =
                          AbstractBot::MessageFormat::PlainText) override;
    void editMessage(const AbstractBot::ChatAddress &chat,
                     const QString &messageId,
                     const QString &text,
                     const AbstractBot::MessageOptions &options) override;

    /**
     * @brief Sends the result of a web authorization request back to the web process.
     */
    void sendAuthorizationResponse(const QString &requestId, const QJsonObject &payload);

    /**
     * @brief Sends the result of a web user-settings request back to the web process.
     */
    void sendUserSettingsResponse(const QString &requestId, const QJsonObject &payload);

    /**
     * @brief Notifies the web process that command cache for a user is stale.
     */
    void sendCommandCacheInvalidated(const MessengerUserKey &user);
    /**
     * @brief Notifies the web process that command cache for all users is stale.
     */
    void sendCommandCacheInvalidated();

private slots:
    void handleRequest(const SharedMemoryProtocol::Frame &frame);
    void flushPendingFrames();

signals:
    void userProfileResponseReceived(const QString &requestId, const QJsonObject &payload);
    void userAuthorizationRequested(const QString &requestId, const QJsonObject &payload);
    void userSettingsRequested(const QString &requestId,
                               const QString &action,
                               const QJsonObject &payload);

private:
    [[nodiscard]] AbstractBot::ChatAddress chatFromPayload(const QJsonObject &payload) const;
    [[nodiscard]] AbstractBot::UserAddress userFromPayload(const QJsonObject &payload) const;
    [[nodiscard]] QJsonObject messageOptionsToJson(
            const AbstractBot::MessageOptions &options) const;
    [[nodiscard]] QJsonArray commandListToJson(
            const QString &locale = QString(),
            const MessengerUserKey &user = MessengerUserKey()) const;
    [[nodiscard]] QString commandHelpText(const MessengerAbstractCommand *command) const;
    void sendResult(const QString &requestId, const QString &status, const QString &message);
    void queueFrame(const SharedMemoryProtocol::Frame &frame);
    void emitMessageSentForFrame(const SharedMemoryProtocol::Frame &frame);
    bool waitForPendingFrames(int timeoutMs);
    void sendOutgoingFrame(const QString &type,
                           const AbstractBot::ChatAddress &chat,
                           const QString &messageId,
                           const QString &text,
                           const AbstractBot::MessageOptions &options,
                           const QJsonObject &extraPayload = QJsonObject());

    SharedMemoryServer m_server;
    QQueue<SharedMemoryProtocol::Frame> m_pendingFrames;
    QTimer m_flushTimer;
    BookingResourcesTranslator *m_translator;
    quint64 m_droppedFrameCount;

protected:
    void syncCommandsWithPlatform() override;
};

#endif // WEBBOT_H
