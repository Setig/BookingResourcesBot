#ifndef TELEGRAMBOT_H
#define TELEGRAMBOT_H

#include "AbstractBot.h"

class QUrl;
class QNetworkReply;

class TelegramBotPrivate;

/**
 * @brief Telegram bot client that polls for updates using the Telegram Bot API.
 *
 * This class uses QNetworkAccessManager and QTimer to periodically call
 * the Telegram Bot API and emit signals for incoming text messages.
 * It implements the AbstractBot interface.
 */
class TelegramBot : public AbstractBot
{
    Q_OBJECT

public:
    /**
     * @brief Constructs a TelegramBot instance.
     * @param token Telegram bot API token.
     * @param parent Optional QObject parent.
     */
    explicit TelegramBot(const QString &token = QString(), QObject *parent = nullptr);

    /**
     * @brief Destroys the TelegramBot instance.
     */
    ~TelegramBot() override;

    [[nodiscard]] MessengerType messenger() const override;
    [[nodiscard]] Capabilities capabilities() const override;
    [[nodiscard]] bool isRunning() const override;
    bool fetchUserProfile(const QString &userId,
                          AbstractBot::UserProfile *profileOut,
                          QString *errorDescription) override;

public slots:
    /**
     * @brief Starts polling Telegram for updates.
     *
     * If the token is empty, the bot will not start and an error will be emitted.
     */
    void start() override;

    /**
     * @brief Stops polling Telegram for updates.
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
     * @brief Returns the number of pending outgoing Telegram API requests.
     */
    [[nodiscard]] int pendingRequestCount() const;

    /**
     * @brief Waits for all pending outgoing requests to finish.
     *
     * This is intended for graceful shutdown sequences (e.g. to let menu-closing edits reach the
     * Telegram API). The wait is bounded by @p timeoutMs and runs a local event loop.
     *
     * @return true if all pending requests finished before timeout, false otherwise.
     */
    bool waitForPendingRequests(int timeoutMs);

signals:
    void pendingRequestCountChanged(int pendingCount);

private:
    /**
     * @brief Builds full Telegram API method URL for the given method name.
     * @param method Name of the Telegram Bot API method (e.g. "getUpdates").
     * @return Fully qualified URL including bot token and method name.
     */
    QUrl buildMethodUrl(const QString &method) const;

    /**
     * @brief Sends a request to Telegram to fetch new updates.
     *
     * Uses the stored last update identifier to only request new updates.
     */
    void fetchUpdates();

    /**
     * @brief Handles a finished getUpdates network reply.
     * @param reply Pointer to the finished QNetworkReply.
     * @return true if the reply was successfully processed and the next
     *         request can be issued immediately; false if an error occurred
     *         and a delayed retry should be scheduled.
     */
    bool handleUpdatesReply(QNetworkReply *reply);

    /**
     * @brief Timer callback used to retry polling after an error.
     */
    void onTimeout();

    /**
     * @brief Slot-like helper called when a network reply with updates finishes.
     * @param reply Pointer to the finished QNetworkReply.
     */
    void onUpdatesFinished(QNetworkReply *reply);

    /**
     * @brief Sends answerCallbackQuery to dismiss Telegram client's loading indicators.
     */
    void answerCallbackQuery(const QString &callbackId, const QString &text = QString());

    void trackPendingRequest(QNetworkReply *reply);
    void untrackPendingRequest(QNetworkReply *reply);

    TelegramBotPrivate *d;

protected:
    void syncCommandsWithPlatform() override;
};

#endif // TELEGRAMBOT_H
