#ifndef ABSTRACTBOT_H
#define ABSTRACTBOT_H

#include <QObject>

#include <QVector>

#include "MessengerType.h"
#include "MessengerUserKey.h"

class MessengerCommandManager;

/**
 * @brief Messenger-agnostic base class for bot implementations.
 *
 * Concrete implementations (e.g. LocalCliBot, TelegramBot) supply the transport/network logic,
 * while higher-level components interact exclusively with this abstraction.
 */
class AbstractBot : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Identifies a chat, channel, or direct conversation.
     *
     * The structure contains messenger-specific identifiers so that
     * implementations can persist whatever is required by the target API.
     */
    struct ChatAddress
    {
        MessengerType messenger = MessengerType::Telegram;
        QString chatId;
        QString threadId;

        [[nodiscard]] bool isValid() const { return !chatId.isEmpty(); }
    };

    /**
     * @brief Identifies the sender of a message.
     */
    struct UserAddress
    {
        QString userId;
        QString username;
        QString firstName;
        QString lastName;

        [[nodiscard]] bool isValid() const { return !userId.isEmpty(); }
    };

    /**
     * @brief Describes detailed information about a messenger user.
     */
    struct UserProfile
    {
        QString userId;
        QString username;
        QString firstName;
        QString lastName;
        QString languageCode;
        bool isBot = false;

        [[nodiscard]] bool isValid() const { return !userId.isEmpty(); }
    };

    /**
     * @brief Supported text formatting options for outgoing messages.
     */
    enum class MessageFormat {
        PlainText = 0,
        Markdown,
        Html
    };

    /**
     * @brief Additional options for outgoing messages.
     */
    struct MessageOptions
    {
        MessageFormat format = MessageFormat::PlainText;
        QString replyToMessageId;
        QString threadId;

        struct InlineKeyboardButton
        {
            QString text;
            QString callbackData;

            [[nodiscard]] bool isValid() const
            {
                return !text.trimmed().isEmpty() && !callbackData.trimmed().isEmpty();
            }
        };

        struct InlineKeyboard
        {
            QVector<QVector<InlineKeyboardButton>> rows;

            [[nodiscard]] bool isEmpty() const
            {
                for (const QVector<InlineKeyboardButton> &row : rows) {
                    for (const InlineKeyboardButton &button : row) {
                        if (button.isValid())
                            return false;
                    }
                }
                return true;
            }

            void clear() { rows.clear(); }
        };

        InlineKeyboard inlineKeyboard;
    };

    /**
     * @brief Payload emitted for each incoming message.
     */
    struct IncomingMessage
    {
        ChatAddress chat;
        UserAddress sender;
        QString languageCode; ///< Optional locale reported by the messenger (e.g. "en", "ru_RU").
        QString text;
        QString messageId;
    };

    /**
     * @brief Payload describing a pressed inline keyboard button.
     */
    struct CallbackQuery
    {
        QString id;
        ChatAddress chat;
        UserAddress sender;
        QString languageCode; ///< Optional locale reported by the messenger (e.g. "en", "ru_RU").
        QString data;
        QString originalMessageId;

        [[nodiscard]] bool isValid() const
        {
            return !id.isEmpty() && chat.isValid() && !data.isEmpty() && sender.isValid();
        }
    };

    /**
     * @brief Capability flags describing optional platform features.
     */
    enum class CapabilityFlag : quint32 {
        /**
         * @brief Messenger accepts Markdown-formatted outgoing messages.
         */
        MarkdownFormatting = 0x1,
        /**
         * @brief Messenger accepts HTML-formatted outgoing messages.
         */
        HtmlFormatting = 0x2,
        /**
         * @brief Messenger supports inline keyboards with callback data.
         */
        InlineKeyboards = 0x4
    };
    Q_DECLARE_FLAGS(Capabilities, CapabilityFlag)

    /**
     * @brief Constructs an AbstractBot instance.
     * @param parent Optional QObject parent.
     */
    explicit AbstractBot(QObject *parent = nullptr);

    /**
     * @brief Destroys the AbstractBot instance.
     */
    ~AbstractBot() override;

    /**
     * @brief Builds a MessengerUserKey for the sender of an incoming message.
     */
    [[nodiscard]] static MessengerUserKey senderKey(const AbstractBot::IncomingMessage &message);

    /**
     * @brief Builds a MessengerUserKey for the sender of a callback query.
     */
    [[nodiscard]] static MessengerUserKey senderKey(const AbstractBot::CallbackQuery &query);

    /**
     * @brief Builds a MessengerUserKey for the specified chat and sender addresses.
     *
     * This helper is primarily intended for cases where an incoming update has already been
     * normalized into chat/sender addresses (e.g. menu rendering contexts).
     */
    [[nodiscard]] static MessengerUserKey senderKey(const AbstractBot::ChatAddress &chat,
                                                    const AbstractBot::UserAddress &sender);

    /**
     * @brief Returns the messenger handled by this bot instance.
     */
    virtual MessengerType messenger() const = 0;

    /**
     * @brief Returns capability flags supported by the implementation.
     *
     * Consumers can use this to decide whether features such as command
     * synchronization or HTML formatting should be attempted.
     */
    virtual Capabilities capabilities() const = 0;

    /**
     * @brief Returns true if the bot is currently connected/polling.
     */
    virtual bool isRunning() const = 0;

    /**
     * @brief Fetches profile information for the specified messenger user.
     *
     * @param userId Messenger-specific identifier (e.g. Telegram numeric id).
     * @param profileOut Optional pointer that receives the resolved profile info.
     * @param errorDescription Optional pointer that receives error details.
     * @return true if the user exists and the profile was resolved.
     */
    virtual bool fetchUserProfile(const QString &userId,
                                  AbstractBot::UserProfile *profileOut,
                                  QString *errorDescription);

public slots:
    /**
     * @brief Starts interacting with the remote messenger (polling/webhooks).
     */
    virtual void start() = 0;

    /**
     * @brief Stops interacting with the remote messenger.
     */
    virtual void stop() = 0;

    /**
     * @brief Starts a best-effort graceful shutdown sequence.
     *
     * This is the first phase of shutdown. Implementations should initiate shutdown actions
     * (e.g. stop polling, send disconnect messages, flush queued work) without blocking.
     *
     * The default implementation calls stop().
     */
    virtual void startShutdown();

    /**
     * @brief Waits for a graceful shutdown sequence to complete.
     *
     * This is the second phase of shutdown. It is expected to be called after startShutdown()
     * to let the implementation finish pending asynchronous work (e.g. drain outgoing requests).
     *
     * Timeout semantics follow QProcess::waitForFinished():
     * - @p msecs > 0: wait up to @p msecs milliseconds;
     * - @p msecs == 0: do not block;
     * - @p msecs < 0: wait indefinitely.
     *
     * @param msecs Maximum time to wait in milliseconds.
     * @return true if shutdown completed; false if it is still in progress when the timeout
     *         expires (or when @p msecs == 0).
     *
     * The default implementation returns true immediately.
     */
    virtual bool waitShutdown(int msecs = 5000);

    /**
     * @brief Sends a text message to the specified chat.
     * @param chat Target chat/channel.
     * @param text Text payload.
     * @param options Optional formatting/threading metadata.
     */
    virtual void sendMessage(const AbstractBot::ChatAddress &chat,
                             const QString &text,
                             const AbstractBot::MessageOptions &options) = 0;

    /**
     * @brief Sends a file to the specified chat.
     * @param chat Target chat/channel.
     * @param filePath Local file path to send.
     * @param caption Optional caption to include with the file.
     * @param captionFormat Optional caption formatting mode.
     */
    virtual void sendFile(
            const AbstractBot::ChatAddress &chat,
            const QString &filePath,
            const QString &caption = QString(),
            AbstractBot::MessageFormat captionFormat = AbstractBot::MessageFormat::PlainText) = 0;

    /**
     * @brief Updates a previously sent message with new text/options.
     *
     * Implementations that do not support message editing may safely fall back to sending a new
     * message. The default AbstractBot implementation does exactly that.
     *
     * @param chat Target chat/channel.
     * @param messageId Platform-specific identifier of the original message.
     * @param text New text payload.
     * @param options Optional formatting and inline keyboard metadata.
     */
    virtual void editMessage(const AbstractBot::ChatAddress &chat,
                             const QString &messageId,
                             const QString &text,
                             const AbstractBot::MessageOptions &options);

    /**
     * @brief Sets the command manager used to expose and synchronize bot commands.
     *
     * Stores a non-owning pointer to the provided manager and triggers
     * syncCommandsWithPlatform().
     *
     * Implementations that do not support command synchronization should safely ignore
     * the request by keeping the default syncCommandsWithPlatform() implementation.
     *
     * @param commandManager Command manager with the registered command set (non-owning).
     */
    void setCommandManager(const MessengerCommandManager *commandManager);

protected:
    const MessengerCommandManager *commandManager() const;

    /**
     * @brief Synchronizes the current command set with the messenger platform.
     *
     * Called automatically after setCommandManager() is invoked.
     * Implementations may override this method to push the command list to the remote platform
     * (e.g. Telegram setMyCommands).
     *
     * The default implementation does nothing.
     */
    virtual void syncCommandsWithPlatform();

signals:
    /**
     * @brief Emitted for each incoming text message from a user.
     */
    void messageReceived(const AbstractBot::IncomingMessage &message);

    /**
     * @brief Emitted when a messenger inline button has been pressed.
     */
    void callbackQueryReceived(const AbstractBot::CallbackQuery &query);

    /**
     * @brief Emitted after a message has been successfully sent.
     *
     * Some messengers return the platform message id only asynchronously (after the network
     * request completes). Implementations may emit this signal when such id becomes available.
     *
     * @param chat Chat address for the sent message.
     * @param messageId Platform message id (e.g. Telegram message_id).
     */
    void messageSent(const AbstractBot::ChatAddress &chat, const QString &messageId);

    /**
     * @brief Emitted when a networking or parsing error occurs.
     * @param description Human-readable error description.
     */
    void errorOccurred(const QString &description);

private:
    const MessengerCommandManager *m_commandManager; // non-owning
};

Q_DECLARE_OPERATORS_FOR_FLAGS(AbstractBot::Capabilities)
Q_DECLARE_METATYPE(AbstractBot::ChatAddress)
Q_DECLARE_METATYPE(AbstractBot::UserAddress)
Q_DECLARE_METATYPE(AbstractBot::UserProfile)
Q_DECLARE_METATYPE(AbstractBot::MessageOptions)
Q_DECLARE_METATYPE(AbstractBot::IncomingMessage)
Q_DECLARE_METATYPE(AbstractBot::CallbackQuery)

#endif // ABSTRACTBOT_H
