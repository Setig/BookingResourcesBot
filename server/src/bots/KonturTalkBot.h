#ifndef KONTURTALKBOT_H
#define KONTURTALKBOT_H

#include "AbstractBot.h"

class QUrl;
class QNetworkReply;

class KonturTalkBotPrivate;

/**
 * @brief Kontur.Talk bot client that polls for updates using the Talk Chats Bot API.
 *
 * This class implements the AbstractBot transport contract for the Matrix-like
 * Kontur.Talk Bot API.
 */
class KonturTalkBot : public AbstractBot
{
    Q_OBJECT

public:
    /**
     * @brief Constructs a KonturTalkBot instance.
     * @param token Kontur.Talk bot JWT token.
     * @param parent Optional QObject parent.
     */
    explicit KonturTalkBot(const QString &token = QString(), QObject *parent = nullptr);

    /**
     * @brief Destroys the KonturTalkBot instance.
     */
    ~KonturTalkBot() override;

    [[nodiscard]] MessengerType messenger() const override;
    [[nodiscard]] Capabilities capabilities() const override;
    [[nodiscard]] bool isRunning() const override;

public slots:
    /**
     * @brief Starts polling Kontur.Talk for updates.
     */
    void start() override;

    /**
     * @brief Stops polling Kontur.Talk for updates.
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

    /**
     * @brief Returns the number of pending outgoing Kontur.Talk API requests.
     */
    [[nodiscard]] int pendingRequestCount() const;

    /**
     * @brief Waits for all pending outgoing requests to finish.
     */
    bool waitForPendingRequests(int timeoutMs);

signals:
    void pendingRequestCountChanged(int pendingCount);

private:
    QUrl buildMethodUrl(const QString &method) const;
    void fetchUpdates();
    bool handleUpdatesReply(QNetworkReply *reply);
    void onTimeout();
    void onUpdatesFinished(QNetworkReply *reply);
    bool rememberEventId(const QString &eventId);
    void trackPendingRequest(QNetworkReply *reply);
    void untrackPendingRequest(QNetworkReply *reply);

    KonturTalkBotPrivate *d;
};

#endif // KONTURTALKBOT_H
