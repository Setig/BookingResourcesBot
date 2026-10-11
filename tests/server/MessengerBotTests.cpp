#include <QtTest>
#include "TestRegistry.h"

#include <QCoreApplication>

#include "User.h"
#include "Resource.h"
#include "MessengerBot.h"
#include "ServerDatabase.h"
#include "BlacklistModel.h"
#include "MessengerBotPoll.h"
#include "MessengerMenuIds.h"
#include "ServerSettingsModel.h"
#include "ServerRuntimeSettings.h"

#include <QMap>
#include <QSet>
#include <QDateTime>
#include <QTimeZone>
#include <QTemporaryDir>

class MessengerBotTestHelper
{
public:
    // Test-only helper for swapping the backend bot instance.
    static void replaceBackend(MessengerBot *bot, AbstractBot *backend)
    {
        if (!bot)
            return;
        bot->replaceBackendBot(backend);
    }
};

namespace {
const MessengerUserKey kTestActor(MessengerType::LocalCli, QStringLiteral("test"));
const AbstractBot::Capabilities kTestCapabilities = AbstractBot::CapabilityFlag::MarkdownFormatting
                                                    | AbstractBot::CapabilityFlag::HtmlFormatting
                                                    | AbstractBot::CapabilityFlag::InlineKeyboards;

class TestBot : public AbstractBot
{
    Q_OBJECT

public:
    // Captured outgoing message payload for assertions.
    struct SentMessage
    {
        ChatAddress chat;
        QString text;
        MessageOptions options;
    };

    struct SentFile
    {
        ChatAddress chat;
        QString filePath;
        QString caption;
        MessageFormat captionFormat = MessageFormat::PlainText;
    };

    explicit TestBot(MessengerType type, Capabilities caps, QObject *parent = nullptr)
        : AbstractBot(parent)
        , m_type(type)
        , m_caps(caps)
    {
    }

    MessengerType messenger() const override { return m_type; }

    Capabilities capabilities() const override { return m_caps; }

    bool isRunning() const override { return m_running; }

    void start() override { m_running = true; }

    void stop() override { m_running = false; }

    void addEmptyProfileUserId(const QString &userId)
    {
        const QString trimmed = userId.trimmed();
        if (!trimmed.isEmpty())
            m_emptyProfileUserIds.insert(trimmed);
    }

    bool fetchUserProfile(const QString &userId,
                          AbstractBot::UserProfile *profileOut,
                          QString *errorDescription) override
    {
        const QString trimmed = userId.trimmed();
        if (trimmed.isEmpty()) {
            if (errorDescription)
                *errorDescription = QStringLiteral("User identifier is empty.");
            return false;
        }

        if (profileOut) {
            profileOut->userId = trimmed;
            if (m_emptyProfileUserIds.contains(trimmed)) {
                profileOut->userId.clear();
                profileOut->firstName.clear();
                profileOut->lastName.clear();
            } else {
                profileOut->firstName = QStringLiteral("Remote");
                profileOut->lastName = QStringLiteral("User");
            }
            profileOut->languageCode = QStringLiteral("en");
        }
        if (errorDescription)
            errorDescription->clear();
        return true;
    }

    void sendMessage(const ChatAddress &chat,
                     const QString &text,
                     const MessageOptions &options) override
    {
        m_sent.append({ chat, text, options });
    }

    void sendFile(const ChatAddress &chat,
                  const QString &filePath,
                  const QString &caption,
                  MessageFormat captionFormat) override
    {
        m_sentFiles.append({ chat, filePath, caption, captionFormat });
    }

    // Injects an incoming message into the MessengerBot pipeline.
    void injectMessage(const IncomingMessage &message) { emit messageReceived(message); }

    // Injects a callback query into the MessengerBot pipeline.
    void injectCallbackQuery(const CallbackQuery &query) { emit callbackQueryReceived(query); }

    const QList<SentMessage> &sentMessages() const { return m_sent; }

    const QList<SentFile> &sentFiles() const { return m_sentFiles; }

    void clearMessages()
    {
        m_sent.clear();
        m_sentFiles.clear();
    }

private:
    MessengerType m_type;
    Capabilities m_caps;
    bool m_running = false;
    QSet<QString> m_emptyProfileUserIds;
    QList<SentMessage> m_sent;
    QList<SentFile> m_sentFiles;
};

// Builds a minimal incoming message from a user.
AbstractBot::IncomingMessage buildMessage(const MessengerUserKey &userKey, const QString &text)
{
    AbstractBot::IncomingMessage message;
    message.chat.messenger = userKey.messenger();
    message.chat.chatId = userKey.userId();
    message.sender.userId = userKey.userId();
    message.text = text;
    message.messageId = QStringLiteral("1");
    return message;
}

AbstractBot::CallbackQuery buildCallbackQuery(const MessengerUserKey &userKey,
                                              const QString &data,
                                              const QString &originMessageId = QString())
{
    static int s_callbackId = 0;
    AbstractBot::CallbackQuery query;
    query.id = QStringLiteral("cb-%1").arg(++s_callbackId);
    query.chat.messenger = userKey.messenger();
    query.chat.chatId = userKey.userId();
    query.sender.userId = userKey.userId();
    query.data = data;
    query.originalMessageId = originMessageId;
    return query;
}

// Returns the last sent message for the specified chat id.
const TestBot::SentMessage *lastMessageForChat(const TestBot &bot, const QString &chatId)
{
    const QList<TestBot::SentMessage> &messages = bot.sentMessages();
    for (auto it = messages.crbegin(); it != messages.crend(); ++it) {
        if (it->chat.chatId == chatId)
            return &(*it);
    }
    return nullptr;
}

int messageCountForChat(const TestBot &bot, const QString &chatId)
{
    int count = 0;
    const QList<TestBot::SentMessage> &messages = bot.sentMessages();
    for (const TestBot::SentMessage &message : messages) {
        if (message.chat.chatId == chatId)
            ++count;
    }
    return count;
}

bool expectAnyMessageContains(const TestBot &bot,
                              const MessengerUserKey &userKey,
                              const QString &fragment,
                              const QString &context,
                              QString *errorOut)
{
    const QList<TestBot::SentMessage> &messages = bot.sentMessages();
    for (const TestBot::SentMessage &message : messages) {
        if (message.chat.chatId != userKey.userId())
            continue;
        if (message.text.contains(fragment))
            return true;
    }

    if (errorOut) {
        *errorOut = QStringLiteral("No reply for %1 contains \"%2\".").arg(context, fragment);
    }
    return false;
}

bool expectNoMessageContains(const TestBot &bot,
                             const MessengerUserKey &userKey,
                             const QString &fragment,
                             const QString &context,
                             QString *errorOut)
{
    const QList<TestBot::SentMessage> &messages = bot.sentMessages();
    for (const TestBot::SentMessage &message : messages) {
        if (message.chat.chatId != userKey.userId())
            continue;
        if (message.text.contains(fragment)) {
            if (errorOut) {
                *errorOut = QStringLiteral("Reply for %1 contains \"%2\".").arg(context, fragment);
            }
            return false;
        }
    }
    return true;
}

bool expectNoMessageEquals(const TestBot &bot,
                           const MessengerUserKey &userKey,
                           const QString &text,
                           const QString &context,
                           QString *errorOut)
{
    const QList<TestBot::SentMessage> &messages = bot.sentMessages();
    for (const TestBot::SentMessage &message : messages) {
        if (message.chat.chatId != userKey.userId())
            continue;
        if (message.text == text) {
            if (errorOut) {
                *errorOut = QStringLiteral("Reply for %1 equals \"%2\".").arg(context, text);
            }
            return false;
        }
    }
    return true;
}

bool messageHasCallbackData(const TestBot::SentMessage &message, const QString &callbackData)
{
    const AbstractBot::MessageOptions::InlineKeyboard &keyboard = message.options.inlineKeyboard;
    for (const QVector<AbstractBot::MessageOptions::InlineKeyboardButton> &row : keyboard.rows) {
        for (const AbstractBot::MessageOptions::InlineKeyboardButton &button : row) {
            if (!button.isValid())
                continue;
            if (button.callbackData == callbackData)
                return true;
        }
    }
    return false;
}

QString menuPayload(const QString &typeToken, const QString &value)
{
    return QStringLiteral("mm|3|%1|%2").arg(typeToken, value);
}

QString menuNavigatePayload(const QString &menuId)
{
    return menuPayload(QStringLiteral("n"), menuId);
}

QString menuEventPayload(const QString &event)
{
    return menuPayload(QStringLiteral("e"), event);
}

QString buildOccupyCommand(const QString &resourceName, int minutes)
{
    return QStringLiteral("/occupy \"%1\" %2").arg(resourceName, QString::number(minutes));
}

QString buildReleaseCommand(const QString &resourceName)
{
    return QStringLiteral("/release \"%1\"").arg(resourceName);
}

bool expectLastMessageContains(const TestBot &bot,
                               const MessengerUserKey &userKey,
                               const QString &fragment,
                               const QString &context,
                               QString *errorOut)
{
    const TestBot::SentMessage *message = lastMessageForChat(bot, userKey.userId());
    if (!message) {
        if (errorOut)
            *errorOut = QStringLiteral("Missing reply for %1.").arg(context);
        return false;
    }
    if (!message->text.contains(fragment)) {
        if (errorOut) {
            *errorOut =
                    QStringLiteral("Reply for %1 does not contain \"%2\".").arg(context, fragment);
        }
        return false;
    }
    return true;
}

bool expectCurrentOccupant(ServerDatabase *database,
                           const QString &resourceName,
                           const MessengerUserKey &expectedUser,
                           QString *errorOut)
{
    if (!database) {
        if (errorOut)
            *errorOut = QStringLiteral("Database is not available.");
        return false;
    }
    const ResourceQueueModel *queueModel = database->resourceQueueModel();
    if (!queueModel) {
        if (errorOut)
            *errorOut = QStringLiteral("Resource queue model is not available.");
        return false;
    }

    const ResourceQueueEntry occupant = queueModel->currentOccupantForResource(resourceName);
    if (!occupant.isValid()) {
        if (errorOut) {
            *errorOut =
                    QStringLiteral("No current occupant for resource \"%1\".").arg(resourceName);
        }
        return false;
    }
    if (occupant.user() != expectedUser) {
        if (errorOut) {
            *errorOut =
                    QStringLiteral("Unexpected occupant for resource \"%1\".").arg(resourceName);
        }
        return false;
    }
    return true;
}

bool updateUserSettings(ServerDatabase *database,
                        const MessengerUserKey &userKey,
                        const QString &locale,
                        User::OccupationNotifyMode notifyMode,
                        QString *errorOut)
{
    if (errorOut)
        errorOut->clear();
    if (!database) {
        if (errorOut)
            *errorOut = QStringLiteral("Database is not available.");
        return false;
    }
    const UserModel *model = database->userModel();
    if (!model) {
        if (errorOut)
            *errorOut = QStringLiteral("User model is not available.");
        return false;
    }

    User updated = model->userByKey(userKey);
    if (!updated.isValid()) {
        if (errorOut)
            *errorOut = QStringLiteral("Access is not activated.");
        return false;
    }

    updated.setLocale(locale);
    updated.setOccupationNotifyMode(notifyMode);
    const ServerDatabaseError error = database->updateUser(updated, kTestActor);
    if (error) {
        if (errorOut)
            *errorOut = error.debugString();
        return false;
    }
    return true;
}

bool expectMenuButton(const TestBot &bot,
                      const MessengerUserKey &userKey,
                      const QString &callbackData,
                      const QString &context,
                      QString *errorOut)
{
    const TestBot::SentMessage *message = lastMessageForChat(bot, userKey.userId());
    if (!message) {
        if (errorOut)
            *errorOut = QStringLiteral("Missing menu reply for %1.").arg(context);
        return false;
    }
    if (!messageHasCallbackData(*message, callbackData)) {
        if (errorOut)
            *errorOut = QStringLiteral("Menu button for %1 is missing.").arg(context);
        return false;
    }
    return true;
}

bool clickMenuButton(TestBot *backend,
                     const MessengerUserKey &userKey,
                     const QString &callbackData,
                     const QString &context,
                     QString *errorOut)
{
    if (!backend) {
        if (errorOut)
            *errorOut = QStringLiteral("Backend bot is not available.");
        return false;
    }
    if (!expectMenuButton(*backend, userKey, callbackData, context, errorOut))
        return false;

    backend->injectCallbackQuery(buildCallbackQuery(userKey, callbackData));
    return true;
}

bool clickMenuNavigate(TestBot *backend,
                       const MessengerUserKey &userKey,
                       const QString &menuId,
                       const QString &context,
                       QString *errorOut)
{
    return clickMenuButton(backend, userKey, menuNavigatePayload(menuId), context, errorOut);
}

bool clickMenuEvent(TestBot *backend,
                    const MessengerUserKey &userKey,
                    const QString &event,
                    const QString &context,
                    QString *errorOut)
{
    return clickMenuButton(backend, userKey, menuEventPayload(event), context, errorOut);
}

bool findMenuCallbackByText(const TestBot::SentMessage &message,
                            const QString &textFragment,
                            QString *callbackOut)
{
    const AbstractBot::MessageOptions::InlineKeyboard &keyboard = message.options.inlineKeyboard;
    for (const QVector<AbstractBot::MessageOptions::InlineKeyboardButton> &row : keyboard.rows) {
        for (const AbstractBot::MessageOptions::InlineKeyboardButton &button : row) {
            if (!button.isValid())
                continue;
            if (!button.text.contains(textFragment))
                continue;
            if (callbackOut)
                *callbackOut = button.callbackData;
            return true;
        }
    }
    return false;
}

bool clickMenuButtonByTextFragment(TestBot *backend,
                                   const MessengerUserKey &userKey,
                                   const QString &textFragment,
                                   const QString &context,
                                   QString *errorOut)
{
    if (!backend) {
        if (errorOut)
            *errorOut = QStringLiteral("Backend bot is not available.");
        return false;
    }

    const TestBot::SentMessage *message = lastMessageForChat(*backend, userKey.userId());
    if (!message) {
        if (errorOut)
            *errorOut = QStringLiteral("Missing menu reply for %1.").arg(context);
        return false;
    }

    QString callbackData;
    if (!findMenuCallbackByText(*message, textFragment, &callbackData)
        || callbackData.trimmed().isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Menu button for %1 with text \"%2\" is missing.")
                                .arg(context, textFragment);
        }
        return false;
    }

    backend->injectCallbackQuery(buildCallbackQuery(userKey, callbackData));
    return true;
}

void openMenuForUser(TestBot *backend, const MessengerUserKey &userKey)
{
    if (!backend)
        return;
    backend->injectMessage(buildMessage(userKey, QStringLiteral("/menu")));
}

bool openResourceCardMenuWithChecks(TestBot *backend,
                                    const MessengerUserKey &userKey,
                                    QString *errorOut)
{
    if (!backend)
        return false;
    if (errorOut)
        errorOut->clear();

    openMenuForUser(backend, userKey);

    if (!clickMenuNavigate(backend,
                           userKey,
                           QLatin1String(MessengerMenuIds::kMenuResourcesHome),
                           QStringLiteral("resources home navigation"),
                           errorOut)) {
        return false;
    }
    if (!clickMenuNavigate(backend,
                           userKey,
                           QLatin1String(MessengerMenuIds::kMenuResourceCardUser),
                           QStringLiteral("resource card navigation"),
                           errorOut)) {
        return false;
    }
    return clickMenuEvent(backend,
                          userKey,
                          QStringLiteral("sel:0"),
                          QStringLiteral("resource selection"),
                          errorOut);
}

bool openAdminNotificationMenuWithChecks(TestBot *backend,
                                         const MessengerUserKey &adminKey,
                                         QString *errorOut)
{
    if (!backend)
        return false;
    if (errorOut)
        errorOut->clear();

    openMenuForUser(backend, adminKey);

    if (!clickMenuNavigate(backend,
                           adminKey,
                           QLatin1String(MessengerMenuIds::kMenuAdminHome),
                           QStringLiteral("admin home navigation"),
                           errorOut)) {
        return false;
    }

    return clickMenuNavigate(backend,
                             adminKey,
                             QLatin1String(MessengerMenuIds::kMenuAdminNotification),
                             QStringLiteral("notification menu navigation"),
                             errorOut);
}

bool openAdminResourcesMenuWithChecks(TestBot *backend,
                                      const MessengerUserKey &adminKey,
                                      QString *errorOut)
{
    if (!backend)
        return false;
    if (errorOut)
        errorOut->clear();

    openMenuForUser(backend, adminKey);

    if (!clickMenuNavigate(backend,
                           adminKey,
                           QLatin1String(MessengerMenuIds::kMenuAdminHome),
                           QStringLiteral("admin home navigation"),
                           errorOut)) {
        return false;
    }

    return clickMenuNavigate(backend,
                             adminKey,
                             QLatin1String(MessengerMenuIds::kMenuAdminResources),
                             QStringLiteral("admin resources navigation"),
                             errorOut);
}

bool openAdminBlacklistMenuWithChecks(TestBot *backend,
                                      const MessengerUserKey &adminKey,
                                      QString *errorOut)
{
    if (!backend)
        return false;
    if (errorOut)
        errorOut->clear();

    openMenuForUser(backend, adminKey);

    if (!clickMenuNavigate(backend,
                           adminKey,
                           QLatin1String(MessengerMenuIds::kMenuAdminHome),
                           QStringLiteral("admin home navigation"),
                           errorOut)) {
        return false;
    }

    return clickMenuNavigate(backend,
                             adminKey,
                             QLatin1String(MessengerMenuIds::kMenuAdminBlacklist),
                             QStringLiteral("admin blacklist navigation"),
                             errorOut);
}

bool openAdminPendingAccessMenuWithChecks(TestBot *backend,
                                          const MessengerUserKey &adminKey,
                                          QString *errorOut)
{
    if (!backend)
        return false;
    if (errorOut)
        errorOut->clear();

    openMenuForUser(backend, adminKey);

    if (!clickMenuNavigate(backend,
                           adminKey,
                           QLatin1String(MessengerMenuIds::kMenuAdminHome),
                           QStringLiteral("admin home navigation"),
                           errorOut)) {
        return false;
    }

    return clickMenuNavigate(backend,
                             adminKey,
                             QLatin1String(MessengerMenuIds::kMenuAdminPendingAccess),
                             QStringLiteral("admin pending access navigation"),
                             errorOut);
}

bool openAdminResourceCardMenuWithChecks(TestBot *backend,
                                         const MessengerUserKey &adminKey,
                                         QString *errorOut)
{
    if (!backend)
        return false;
    if (!openAdminResourcesMenuWithChecks(backend, adminKey, errorOut))
        return false;

    return clickMenuNavigate(backend,
                             adminKey,
                             QLatin1String(MessengerMenuIds::kMenuAdminResourceCard),
                             QStringLiteral("admin resource card navigation"),
                             errorOut);
}

bool openAdminUsersMenuWithChecks(TestBot *backend,
                                  const MessengerUserKey &adminKey,
                                  QString *errorOut)
{
    if (!backend)
        return false;
    if (errorOut)
        errorOut->clear();

    openMenuForUser(backend, adminKey);

    if (!clickMenuNavigate(backend,
                           adminKey,
                           QLatin1String(MessengerMenuIds::kMenuAdminHome),
                           QStringLiteral("admin home navigation"),
                           errorOut)) {
        return false;
    }

    return clickMenuNavigate(backend,
                             adminKey,
                             QLatin1String(MessengerMenuIds::kMenuAdminUsers),
                             QStringLiteral("admin users navigation"),
                             errorOut);
}

bool openAdminUserCardMenuWithChecks(TestBot *backend,
                                     const MessengerUserKey &adminKey,
                                     QString *errorOut)
{
    if (!backend)
        return false;
    if (!openAdminUsersMenuWithChecks(backend, adminKey, errorOut))
        return false;

    return clickMenuNavigate(backend,
                             adminKey,
                             QLatin1String(MessengerMenuIds::kMenuAdminUserCard),
                             QStringLiteral("admin user card navigation"),
                             errorOut);
}

bool expectNoMenuButton(const TestBot &bot,
                        const MessengerUserKey &userKey,
                        const QString &callbackData,
                        const QString &context,
                        QString *errorOut)
{
    const TestBot::SentMessage *message = lastMessageForChat(bot, userKey.userId());
    if (!message) {
        if (errorOut)
            *errorOut = QStringLiteral("Missing menu reply for %1.").arg(context);
        return false;
    }
    if (messageHasCallbackData(*message, callbackData)) {
        if (errorOut) {
            *errorOut = QStringLiteral("Unexpected menu button for %1 is present.").arg(context);
        }
        return false;
    }
    return true;
}

bool captureLastMessageText(const TestBot &bot,
                            const MessengerUserKey &userKey,
                            const QString &context,
                            QString *textOut,
                            QString *errorOut)
{
    const TestBot::SentMessage *message = lastMessageForChat(bot, userKey.userId());
    if (!message) {
        if (errorOut)
            *errorOut = QStringLiteral("Missing reply for %1.").arg(context);
        return false;
    }
    if (textOut)
        *textOut = message->text;
    return true;
}

struct TestFixture
{
    QTemporaryDir tempDir;
    std::unique_ptr<ServerDatabase> database;
    std::unique_ptr<MessengerBotPoll> botPoll;
    std::unique_ptr<MessengerBot> bot;
    TestBot *backend = nullptr;

    bool initDatabase(const QString &fileName, QString *errorOut)
    {
        if (errorOut)
            errorOut->clear();
        if (!tempDir.isValid()) {
            if (errorOut)
                *errorOut = QStringLiteral("Failed to create temporary directory.");
            return false;
        }

        const QString dbPath = tempDir.filePath(fileName);
        database.reset(new ServerDatabase(dbPath));
        if (!database || !database->isReady()) {
            if (errorOut)
                *errorOut = QStringLiteral("Failed to initialize database.");
            return false;
        }

        return true;
    }

    bool addUser(const MessengerUserKey &userKey,
                 const QString &firstName,
                 const QString &lastName,
                 QString *errorOut)
    {
        if (errorOut)
            errorOut->clear();
        if (!database) {
            if (errorOut)
                *errorOut = QStringLiteral("Database is not available.");
            return false;
        }

        const User user(userKey, firstName, lastName);
        const ServerDatabaseError addUserError = database->addUser(user, kTestActor);
        if (addUserError) {
            if (errorOut)
                *errorOut = addUserError.debugString();
            return false;
        }

        return true;
    }

    bool addResource(const QString &resourceName,
                     const QString &description,
                     const QString &startMessage,
                     QString *errorOut)
    {
        if (errorOut)
            errorOut->clear();
        if (!database) {
            if (errorOut)
                *errorOut = QStringLiteral("Database is not available.");
            return false;
        }

        const Resource resource(resourceName, description, startMessage);
        const ServerDatabaseError addResourceError = database->addResource(resource, kTestActor);
        if (addResourceError) {
            if (errorOut)
                *errorOut = addResourceError.debugString();
            return false;
        }

        return true;
    }

    bool scheduleResource(const QString &resourceName,
                          const MessengerUserKey &userKey,
                          const QDateTime &startUtc,
                          const QDateTime &endUtc,
                          QString *errorOut)
    {
        if (errorOut)
            errorOut->clear();
        if (!database) {
            if (errorOut)
                *errorOut = QStringLiteral("Database is not available.");
            return false;
        }

        const ServerDatabaseError scheduleError =
                database->scheduleResourceUser(resourceName, userKey, startUtc, endUtc, kTestActor);
        if (scheduleError) {
            if (errorOut)
                *errorOut = scheduleError.debugString();
            return false;
        }

        return true;
    }

    bool releaseResource(const QString &resourceName,
                         const MessengerUserKey &userKey,
                         QString *errorOut)
    {
        if (errorOut)
            errorOut->clear();
        if (!database) {
            if (errorOut)
                *errorOut = QStringLiteral("Database is not available.");
            return false;
        }

        const ServerDatabaseError releaseError =
                database->releaseUserOccupationForResource(resourceName, userKey, userKey);
        if (releaseError) {
            if (errorOut)
                *errorOut = releaseError.debugString();
            return false;
        }

        return true;
    }

    bool setupBot(MessengerType type, const QString &token, QString *errorOut)
    {
        if (errorOut)
            errorOut->clear();
        if (!database) {
            if (errorOut)
                *errorOut = QStringLiteral("Database is not available.");
            return false;
        }

        bot.reset();
        backend = nullptr;
        botPoll.reset(new MessengerBotPoll);
        botPoll->setDatabase(database.get());
        bot.reset(botPoll->createBot(type, token));
        if (!bot) {
            if (errorOut)
                *errorOut = QStringLiteral("Failed to create messenger bot.");
            return false;
        }
        backend = new TestBot(type, kTestCapabilities, bot.get());
        MessengerBotTestHelper::replaceBackend(bot.get(), backend);
        return true;
    }

    void resetMessages()
    {
        if (backend)
            backend->clearMessages();
    }

    void sendCommand(const MessengerUserKey &userKey, const QString &command)
    {
        if (backend)
            backend->injectMessage(buildMessage(userKey, command));
    }
};

bool expectBanCommandWithDuration(TestFixture *fixture,
                                  const MessengerUserKey &adminKey,
                                  const MessengerUserKey &target,
                                  const QString &command,
                                  int expectedSeconds,
                                  const QString &expectedReason,
                                  QString *errorOut)
{
    if (errorOut)
        errorOut->clear();
    if (!fixture || !fixture->database || !fixture->backend) {
        if (errorOut)
            *errorOut = QStringLiteral("Test fixture is not available.");
        return false;
    }

    const QDateTime beforeUtc = QDateTime::currentDateTimeUtc();
    fixture->resetMessages();
    fixture->sendCommand(adminKey, command);
    const QDateTime afterUtc = QDateTime::currentDateTimeUtc();

    if (!expectLastMessageContains(*fixture->backend,
                                   adminKey,
                                   QStringLiteral("blocked"),
                                   command,
                                   errorOut)) {
        return false;
    }

    const BlacklistModel *blacklistModel = fixture->database->blacklistModel();
    if (!blacklistModel) {
        if (errorOut)
            *errorOut = QStringLiteral("Blacklist model is not available.");
        return false;
    }

    const BlacklistEntry entry = blacklistModel->entryByKey(target);
    if (!entry.isValid()) {
        if (errorOut)
            *errorOut = QStringLiteral("Blacklist entry was not created for %1.")
                                .arg(target.toString());
        return false;
    }
    if (entry.reason() != expectedReason) {
        if (errorOut) {
            *errorOut = QStringLiteral("Unexpected blacklist reason \"%1\", expected \"%2\".")
                                .arg(entry.reason(), expectedReason);
        }
        return false;
    }
    if (!entry.blockedUntilUtc().isValid()) {
        if (errorOut)
            *errorOut = QStringLiteral("Blacklist entry does not have an expiration.");
        return false;
    }

    const qint64 minObservedSeconds = afterUtc.secsTo(entry.blockedUntilUtc());
    const qint64 maxObservedSeconds = beforeUtc.secsTo(entry.blockedUntilUtc());
    if (minObservedSeconds < expectedSeconds - 5 || maxObservedSeconds > expectedSeconds + 5) {
        if (errorOut) {
            *errorOut = QStringLiteral("Unexpected blacklist duration range [%1, %2], expected %3.")
                                .arg(QString::number(minObservedSeconds),
                                     QString::number(maxObservedSeconds),
                                     QString::number(expectedSeconds));
        }
        return false;
    }

    return true;
}

bool spamUntilBlacklisted(TestFixture *fixture,
                          const MessengerUserKey &userKey,
                          int maxAttempts,
                          QString *errorOut)
{
    if (errorOut)
        errorOut->clear();
    if (!fixture || !fixture->database || !fixture->backend) {
        if (errorOut)
            *errorOut = QStringLiteral("Test fixture is not available.");
        return false;
    }

    fixture->resetMessages();
    fixture->sendCommand(userKey, QStringLiteral("ping 0"));
    if (!expectLastMessageContains(*fixture->backend,
                                   userKey,
                                   QStringLiteral("Access denied. Access is not activated."),
                                   QStringLiteral("pending access check"),
                                   errorOut)) {
        return false;
    }

    for (int i = 1; i < maxAttempts; ++i) {
        fixture->sendCommand(userKey, QStringLiteral("ping %1").arg(QString::number(i)));
        if (fixture->database->isBlacklisted(userKey))
            return true;
    }

    if (errorOut) {
        *errorOut = QStringLiteral("User %1 was not blacklisted after %2 attempts.")
                            .arg(userKey.toString(), QString::number(maxAttempts));
    }
    return false;
}
} // namespace

class MessengerBotTests : public QObject
{
    Q_OBJECT

private slots:
    void startsAndHandlesUnknownCommand();
    void handlesMultipleCommandsInSingleMessage();
    void continuesAfterCommandErrorInSingleMessage();
    void telegramUserMentionsUseProfileLinks();
    void banUserCommandParsesReadableDurations();
    void userSettingsCommandPersistsSettings();
    void userSettingsMenuUpdatesSettings();
    void serverDateTimeFormatCommandAndMenu();
    void serverSettingsHiddenFromGroupAdmins();
    void pendingAccessUserReplySetting();
    void statisticsVisibilityRespectsAdminsOnlySetting();
    void sendNotificationCommandSendsToAllUsers();
    void sendNotificationCommandSendsToSpecificUsers();
    void sendNotificationMenuSendsToAllUsers();
    void sendNotificationMenuSendsToSpecificUsers();
    void sendNotificationToAllUsersRespectsActorGroups();
    void occupationNotificationsFollowSettings();
    void occupationNotificationHandover();
    void scheduleExpiryNotifiesPreviousAndNext();
    void occupyQueuesAlreadyOccupiedResource();
    void indefiniteOccupationCanQueueIndefiniteWaiter();
    void releaseNotifiesNextInQueue();
    void adminResourceAddCommandAcceptsMultilineStartMessage();
    void adminResourceAddCommandAndMenu();
    void adminResourceDeleteCommandAndMenu();
    void adminResourceUpdateCommandAndMenu();
    void resourceStatusVisibilityAndCommandBehavior();
    void resourceGroupVisibilityAndCommandBehavior();
    void groupAdminCreationDefaultsToAdministeredGroups();
    void adminUserAddCommandAndMenu();
    void adminUserDeleteCommandAndMenu();
    void adminUserUpdateCommandAndMenu();
    void pendingAccessUserAutoBlockCommandAndMenu();
};

void MessengerBotTests::startsAndHandlesUnknownCommand()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot.sqlite"), &error),
             qPrintable(error));

    // Register user to bypass access checks for slash commands.
    const MessengerUserKey userKey(MessengerType::Telegram, QStringLiteral("user-alpha"));
    QVERIFY2(fixture.addUser(userKey, QStringLiteral("Alpha"), QStringLiteral("Tester"), &error),
             qPrintable(error));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    QVERIFY(!fixture.bot->isRunning());
    fixture.bot->start();
    QVERIFY(fixture.bot->isRunning());
    QVERIFY(fixture.backend->isRunning());

    // Send an unknown command and assert error response.
    fixture.resetMessages();
    fixture.sendCommand(userKey, QStringLiteral("/wrong"));

    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userKey,
                                       QStringLiteral("Unknown command"),
                                       QStringLiteral("unknown command reply"),
                                       &error),
             qPrintable(error));
}

void MessengerBotTests::handlesMultipleCommandsInSingleMessage()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_multi_command_message.sqlite"),
                                  &error),
             qPrintable(error));

    const MessengerUserKey userKey(MessengerType::Telegram, QStringLiteral("user-multi-command"));
    QVERIFY2(fixture.addUser(userKey, QStringLiteral("Multi"), QStringLiteral("Command"), &error),
             qPrintable(error));
    QVERIFY2(fixture.addResource(QStringLiteral("Room Multi"),
                                 QStringLiteral("Shared room"),
                                 QStringLiteral("Start note"),
                                 &error),
             qPrintable(error));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(userKey, QStringLiteral("/help\n/list_resources"));

    QCOMPARE(messageCountForChat(*fixture.backend, userKey.userId()), 2);
    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      userKey,
                                      QStringLiteral("/start"),
                                      QStringLiteral("multiline help command"),
                                      &error),
             qPrintable(error));
    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      userKey,
                                      QStringLiteral("Room Multi"),
                                      QStringLiteral("multiline list resources command"),
                                      &error),
             qPrintable(error));
}

void MessengerBotTests::continuesAfterCommandErrorInSingleMessage()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_multi_command_continue.sqlite"),
                                  &error),
             qPrintable(error));

    const MessengerUserKey userKey(MessengerType::Telegram, QStringLiteral("user-multi-continue"));
    QVERIFY2(fixture.addUser(userKey,
                             QStringLiteral("Continue"),
                             QStringLiteral("AfterError"),
                             &error),
             qPrintable(error));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(userKey, QStringLiteral("/wrong\n/help"));

    QCOMPARE(messageCountForChat(*fixture.backend, userKey.userId()), 2);
    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      userKey,
                                      QStringLiteral("Unknown command: /wrong"),
                                      QStringLiteral("first multiline command error"),
                                      &error),
             qPrintable(error));
    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      userKey,
                                      QStringLiteral("/start"),
                                      QStringLiteral("second multiline command success"),
                                      &error),
             qPrintable(error));
}

void MessengerBotTests::telegramUserMentionsUseProfileLinks()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_user_links.sqlite"), &error),
             qPrintable(error));

    const MessengerUserKey adminKey(MessengerType::Telegram, QStringLiteral("10001"));
    const User admin(adminKey,
                     QStringLiteral("Admin"),
                     QStringLiteral("Link"),
                     User::Role::AdminRole);
    const ServerDatabaseError addAdminError = fixture.database->addUser(admin, kTestActor);
    QVERIFY2(!addAdminError, qPrintable(addAdminError.debugString()));

    const MessengerUserKey targetKey(MessengerType::Telegram, QStringLiteral("10002"));
    QVERIFY2(fixture.addUser(targetKey, QStringLiteral("Target"), QStringLiteral("User"), &error),
             qPrintable(error));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(adminKey, QStringLiteral("/list_users"));

    const TestBot::SentMessage *listReply = lastMessageForChat(*fixture.backend, adminKey.userId());
    QVERIFY(listReply != nullptr);
    QCOMPARE(listReply->options.format, AbstractBot::MessageFormat::Html);
    QVERIFY(listReply->text.contains(QStringLiteral("tg://user?id=%1").arg(adminKey.userId())));
    QVERIFY(listReply->text.contains(QStringLiteral("tg://user?id=%1").arg(targetKey.userId())));

    fixture.resetMessages();
    fixture.sendCommand(adminKey, QStringLiteral("/ban_user %1 60 test").arg(targetKey.toString()));

    const TestBot::SentMessage *banReply = lastMessageForChat(*fixture.backend, adminKey.userId());
    QVERIFY(banReply != nullptr);
    QCOMPARE(banReply->options.format, AbstractBot::MessageFormat::Html);
    QVERIFY(banReply->text.contains(QStringLiteral("tg://user?id=%1").arg(targetKey.userId())));
}

void MessengerBotTests::banUserCommandParsesReadableDurations()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_ban_durations.sqlite"), &error),
             qPrintable(error));

    const MessengerUserKey adminKey(MessengerType::Telegram, QStringLiteral("ban-admin"));
    const User admin(adminKey,
                     QStringLiteral("Admin"),
                     QStringLiteral("Duration"),
                     User::Role::AdminRole);
    const ServerDatabaseError addAdminError = fixture.database->addUser(admin, kTestActor);
    QVERIFY2(!addAdminError, qPrintable(addAdminError.debugString()));

    const MessengerUserKey quotedTarget(MessengerType::Telegram, QStringLiteral("ban-quoted"));
    const MessengerUserKey unquotedTarget(MessengerType::Telegram, QStringLiteral("ban-unquoted"));
    const MessengerUserKey localizedTarget(MessengerType::Telegram,
                                           QStringLiteral("ban-localized"));
    QVERIFY2(fixture.addUser(quotedTarget,
                             QStringLiteral("Quoted"),
                             QStringLiteral("User"),
                             &error),
             qPrintable(error));
    QVERIFY2(fixture.addUser(unquotedTarget,
                             QStringLiteral("Unquoted"),
                             QStringLiteral("User"),
                             &error),
             qPrintable(error));
    QVERIFY2(fixture.addUser(localizedTarget,
                             QStringLiteral("Localized"),
                             QStringLiteral("User"),
                             &error),
             qPrintable(error));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    QVERIFY2(expectBanCommandWithDuration(&fixture,
                                          adminKey,
                                          quotedTarget,
                                          QStringLiteral("/ban_user %1 \"5 minutes\" spam")
                                                  .arg(quotedTarget.toString()),
                                          5 * 60,
                                          QStringLiteral("spam"),
                                          &error),
             qPrintable(error));
    QVERIFY2(expectBanCommandWithDuration(&fixture,
                                          adminKey,
                                          unquotedTarget,
                                          QStringLiteral("/ban_user %1 5 minutes spam")
                                                  .arg(unquotedTarget.toString()),
                                          5 * 60,
                                          QStringLiteral("spam"),
                                          &error),
             qPrintable(error));
    QVERIFY2(expectBanCommandWithDuration(&fixture,
                                          adminKey,
                                          localizedTarget,
                                          QStringLiteral("/ban_user %1 %2 %3")
                                                  .arg(localizedTarget.toString(),
                                                       QString::fromUtf8("2 часа"),
                                                       QString::fromUtf8("спам")),
                                          2 * 60 * 60,
                                          QString::fromUtf8("спам"),
                                          &error),
             qPrintable(error));
}

void MessengerBotTests::userSettingsCommandPersistsSettings()
{
    TestFixture fixture;
    QString error;
    const QString dbFile = QStringLiteral("messenger_bot_user_settings.sqlite");
    QVERIFY2(fixture.initDatabase(dbFile, &error), qPrintable(error));

    const MessengerUserKey userKey(MessengerType::Telegram, QStringLiteral("user-settings"));
    QVERIFY2(fixture.addUser(userKey, QStringLiteral("User"), QStringLiteral("Settings"), &error),
             qPrintable(error));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(userKey, QStringLiteral("/user_settings list"));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userKey,
                                       QStringLiteral("locale"),
                                       QStringLiteral("user settings list"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userKey,
                                       QStringLiteral("occupation.notify"),
                                       QStringLiteral("user settings list"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userKey,
                                       QStringLiteral("timezone"),
                                       QStringLiteral("user settings list"),
                                       &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(userKey, QStringLiteral("/user_settings set locale ru"));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userKey,
                                       QStringLiteral("Updated: locale = ru"),
                                       QStringLiteral("user settings locale update"),
                                       &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(userKey, QStringLiteral("/user_settings set occupation.notify all"));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userKey,
                                       QStringLiteral("Updated: occupation.notify = all"),
                                       QStringLiteral("user settings notify update"),
                                       &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(userKey, QStringLiteral("/user_settings set timezone UTC+3:00"));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userKey,
                                       QStringLiteral("180"),
                                       QStringLiteral("user settings timezone update"),
                                       &error),
             qPrintable(error));

    const UserModel *model = fixture.database->userModel();
    QVERIFY(model != nullptr);
    const User updated = model->userByKey(userKey);
    QVERIFY(updated.isValid());
    QCOMPARE(updated.locale(), QStringLiteral("ru"));
    QCOMPARE(updated.occupationNotifyMode(), User::OccupationNotifyMode::AllResources);
    QCOMPARE(updated.timezone().toStorageValue(), QStringLiteral("180"));
    QCOMPARE(fixture.bot->effectiveLocaleForUser(userKey, QStringLiteral("en")),
             QStringLiteral("ru"));
    QCOMPARE(fixture.bot->effectiveTimeZoneForUser(userKey).toStorageValue(),
             QStringLiteral("180"));

    const QString dbPath = fixture.tempDir.filePath(dbFile);
    fixture.bot.reset();
    fixture.backend = nullptr;
    fixture.database.reset();

    ServerDatabase reopened(dbPath);
    QVERIFY(reopened.isReady());
    const UserModel *reopenedModel = reopened.userModel();
    QVERIFY(reopenedModel != nullptr);
    const User reloaded = reopenedModel->userByKey(userKey);
    QVERIFY(reloaded.isValid());
    QCOMPARE(reloaded.locale(), QStringLiteral("ru"));
    QCOMPARE(reloaded.occupationNotifyMode(), User::OccupationNotifyMode::AllResources);
    QCOMPARE(reloaded.timezone().toStorageValue(), QStringLiteral("180"));
}

void MessengerBotTests::userSettingsMenuUpdatesSettings()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_user_settings_menu.sqlite"),
                                  &error),
             qPrintable(error));

    const MessengerUserKey userKey(MessengerType::Telegram, QStringLiteral("user-settings-menu"));
    QVERIFY2(fixture.addUser(userKey, QStringLiteral("User"), QStringLiteral("Menu"), &error),
             qPrintable(error));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    openMenuForUser(fixture.backend, userKey);
    QVERIFY2(expectMenuButton(*fixture.backend,
                              userKey,
                              menuNavigatePayload(
                                      QLatin1String(MessengerMenuIds::kMenuUserSettings)),
                              QStringLiteral("main menu user settings button"),
                              &error),
             qPrintable(error));

    QVERIFY2(clickMenuNavigate(fixture.backend,
                               userKey,
                               QLatin1String(MessengerMenuIds::kMenuUserSettings),
                               QStringLiteral("user settings navigation"),
                               &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              userKey,
                              menuEventPayload(QStringLiteral("us:select:locale")),
                              QStringLiteral("user settings locale select"),
                              &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              userKey,
                              menuEventPayload(QStringLiteral("us:select:timezone")),
                              QStringLiteral("user settings timezone select"),
                              &error),
             qPrintable(error));

    QVERIFY2(clickMenuEvent(fixture.backend,
                            userKey,
                            QStringLiteral("us:select:locale"),
                            QStringLiteral("user settings locale select"),
                            &error),
             qPrintable(error));
    QVERIFY2(clickMenuEvent(fixture.backend,
                            userKey,
                            QStringLiteral("us:manual:locale"),
                            QStringLiteral("user settings locale manual"),
                            &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(userKey, QStringLiteral("ru"));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userKey,
                                       QStringLiteral("Current value: ru"),
                                       QStringLiteral("user settings locale manual input"),
                                       &error),
             qPrintable(error));

    QVERIFY2(clickMenuEvent(fixture.backend,
                            userKey,
                            QStringLiteral("us:back"),
                            QStringLiteral("user settings back to list"),
                            &error),
             qPrintable(error));
    QVERIFY2(clickMenuEvent(fixture.backend,
                            userKey,
                            QStringLiteral("us:select:occupation.notify"),
                            QStringLiteral("user settings notify select"),
                            &error),
             qPrintable(error));
    QVERIFY2(clickMenuEvent(fixture.backend,
                            userKey,
                            QStringLiteral("us:presets:occupation.notify"),
                            QStringLiteral("user settings notify presets"),
                            &error),
             qPrintable(error));
    QVERIFY2(clickMenuEvent(fixture.backend,
                            userKey,
                            QStringLiteral("us:set:occupation.notify:off"),
                            QStringLiteral("user settings notify set"),
                            &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userKey,
                                       QStringLiteral("Current value: disabled"),
                                       QStringLiteral("user settings notify update"),
                                       &error),
             qPrintable(error));

    QVERIFY2(clickMenuEvent(fixture.backend,
                            userKey,
                            QStringLiteral("us:back"),
                            QStringLiteral("user settings back after notify"),
                            &error),
             qPrintable(error));
    QVERIFY2(clickMenuEvent(fixture.backend,
                            userKey,
                            QStringLiteral("us:select:timezone"),
                            QStringLiteral("user settings timezone select"),
                            &error),
             qPrintable(error));
    QVERIFY2(clickMenuEvent(fixture.backend,
                            userKey,
                            QStringLiteral("us:manual:timezone"),
                            QStringLiteral("user settings timezone manual"),
                            &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(userKey, QStringLiteral("03:00"));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userKey,
                                       QStringLiteral("180"),
                                       QStringLiteral("user settings timezone manual input"),
                                       &error),
             qPrintable(error));

    const UserModel *model = fixture.database->userModel();
    QVERIFY(model != nullptr);
    const User updated = model->userByKey(userKey);
    QVERIFY(updated.isValid());
    QCOMPARE(updated.locale(), QStringLiteral("ru"));
    QCOMPARE(updated.occupationNotifyMode(), User::OccupationNotifyMode::Disabled);
    QCOMPARE(updated.timezone().toStorageValue(), QStringLiteral("180"));
}

void MessengerBotTests::serverDateTimeFormatCommandAndMenu()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_server_datetime_format.sqlite"),
                                  &error),
             qPrintable(error));

    const MessengerUserKey adminKey(MessengerType::Telegram, QStringLiteral("admin-datetime"));
    const MessengerUserKey userKey(MessengerType::Telegram, QStringLiteral("user-datetime"));

    const User admin(adminKey, QStringLiteral("Admin"), QStringLiteral("DateTime"), true);
    const ServerDatabaseError addAdminError = fixture.database->addUser(admin, kTestActor);
    QVERIFY2(!addAdminError, qPrintable(addAdminError.debugString()));
    QVERIFY2(fixture.addUser(userKey, QStringLiteral("User"), QStringLiteral("DateTime"), &error),
             qPrintable(error));
    QVERIFY2(fixture.addResource(QStringLiteral("Printer"),
                                 QStringLiteral("Shared printer"),
                                 QStringLiteral("Use gently."),
                                 &error),
             qPrintable(error));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(userKey, QStringLiteral("/user_settings set timezone +03:00"));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userKey,
                                       QStringLiteral("180"),
                                       QStringLiteral("user timezone setup"),
                                       &error),
             qPrintable(error));

    const auto scheduleCommand = [](const QString &resource,
                                    const QString &startToken,
                                    const QString &endToken) -> QString {
        return QStringLiteral("/schedule %1 \"%2\" \"%3\"").arg(resource, startToken, endToken);
    };

    const QTimeZone userTimeZone(180 * 60);
    const QDate today = QDate::currentDate();
    const QDateTime firstStartLocal(today.addDays(3), QTime(10, 0), userTimeZone);
    const QDateTime firstEndLocal = firstStartLocal.addSecs(60 * 60);
    const QString firstStartToken = firstStartLocal.toString(QStringLiteral("dd.MM.yyyy HH:mm"));
    const QString firstEndToken = firstEndLocal.toString(QStringLiteral("dd.MM.yyyy HH:mm"));

    fixture.resetMessages();
    fixture.sendCommand(adminKey,
                        QStringLiteral(
                                "/server_settings set ui.date_time_format dd.MM.yyyy HH:mm"));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral(
                                               "Updated: ui.date_time_format = dd.MM.yyyy HH:mm"),
                                       QStringLiteral("server datetime format command update"),
                                       &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(userKey,
                        scheduleCommand(QStringLiteral("Printer"), firstStartToken, firstEndToken));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userKey,
                                       firstStartToken,
                                       QStringLiteral("schedule with custom command format"),
                                       &error),
             qPrintable(error));
    const QList<ResourceQueueEntry> firstSchedules =
            fixture.database->resourceQueueModel()->schedulesForResource(QStringLiteral("Printer"));
    bool firstScheduleFound = false;
    for (const ResourceQueueEntry &entry : firstSchedules) {
        if (entry.user() == userKey && entry.startAtUtc() == firstStartLocal.toUTC()
            && entry.endAtUtc() == firstEndLocal.toUTC()) {
            firstScheduleFound = true;
            break;
        }
    }
    QVERIFY(firstScheduleFound);

    openMenuForUser(fixture.backend, adminKey);
    QVERIFY2(clickMenuNavigate(fixture.backend,
                               adminKey,
                               QLatin1String(MessengerMenuIds::kMenuAdminHome),
                               QStringLiteral("admin home for datetime settings"),
                               &error),
             qPrintable(error));
    QVERIFY2(clickMenuNavigate(fixture.backend,
                               adminKey,
                               QLatin1String(MessengerMenuIds::kMenuAdminSettings),
                               QStringLiteral("admin settings navigation"),
                               &error),
             qPrintable(error));

    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("as:select:ui.date_time_format"),
                            QStringLiteral("select datetime setting"),
                            &error),
             qPrintable(error));
    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("as:manual:ui.date_time_format"),
                            QStringLiteral("manual datetime setting mode"),
                            &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(adminKey, QStringLiteral("dd.MM.yyyy HH:mm:ss"));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Current value: dd.MM.yyyy HH:mm:ss"),
                                       QStringLiteral("datetime setting manual input"),
                                       &error),
             qPrintable(error));

    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("as:presets:ui.date_time_format"),
                            QStringLiteral("datetime presets open"),
                            &error),
             qPrintable(error));
    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("as:set:ui.date_time_format:iso_sec"),
                            QStringLiteral("datetime preset set"),
                            &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Current value: yyyy-MM-dd HH:mm:ss"),
                                       QStringLiteral("datetime preset applied"),
                                       &error),
             qPrintable(error));

    const ServerSettingEntry formatEntry = fixture.database->serverSettingsModel()->entryByKey(
            QStringLiteral("ui.date_time_format"));
    QVERIFY(formatEntry.isValid());
    QCOMPARE(formatEntry.value().toString(), QStringLiteral("yyyy-MM-dd HH:mm:ss"));

    const QDateTime secondStartLocal(today.addDays(5), QTime(11, 0), userTimeZone);
    const QDateTime secondEndLocal = secondStartLocal.addSecs(60 * 60);
    const QString secondStartIso = secondStartLocal.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    const QString secondEndIso = secondEndLocal.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    const QString secondStartLegacy = secondStartLocal.toString(QStringLiteral("dd.MM.yyyy HH:mm"));
    const QString secondEndLegacy = secondEndLocal.toString(QStringLiteral("dd.MM.yyyy HH:mm"));

    fixture.resetMessages();
    fixture.sendCommand(userKey,
                        scheduleCommand(QStringLiteral("Printer"),
                                        secondStartLegacy,
                                        secondEndLegacy));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userKey,
                                       QStringLiteral("Invalid schedule start."),
                                       QStringLiteral("schedule rejects previous format"),
                                       &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(userKey,
                        scheduleCommand(QStringLiteral("Printer"), secondStartIso, secondEndIso));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userKey,
                                       secondStartIso,
                                       QStringLiteral("schedule with preset format"),
                                       &error),
             qPrintable(error));
}

void MessengerBotTests::serverSettingsHiddenFromGroupAdmins()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_server_settings_access.sqlite"),
                                  &error),
             qPrintable(error));

    const MessengerUserKey rootAdminKey(MessengerType::Telegram, QStringLiteral("root-settings"));
    const MessengerUserKey groupAdminKey(MessengerType::Telegram, QStringLiteral("group-settings"));

    const ServerDatabaseError addRootAdminError =
            fixture.database->addUser(User(rootAdminKey,
                                           QStringLiteral("Root"),
                                           QStringLiteral("Settings"),
                                           User::Role::AdminRole),
                                      kTestActor);
    QVERIFY2(!addRootAdminError, qPrintable(addRootAdminError.debugString()));

    User groupAdmin(groupAdminKey,
                    QStringLiteral("Group"),
                    QStringLiteral("Settings"),
                    User::Role::UserRole);
    QMap<Group, User::Role> groupRoles;
    groupRoles.insert(Group(QStringLiteral("alpha")), User::Role::AdminRole);
    groupAdmin.setGroupRoles(groupRoles);
    const ServerDatabaseError addGroupAdminError =
            fixture.database->addUser(groupAdmin, kTestActor);
    QVERIFY2(!addGroupAdminError, qPrintable(addGroupAdminError.debugString()));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(groupAdminKey, QStringLiteral("/help"));
    QVERIFY2(expectNoMessageContains(*fixture.backend,
                                     groupAdminKey,
                                     QStringLiteral("/server_settings"),
                                     QStringLiteral("group admin help without server settings"),
                                     &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(rootAdminKey, QStringLiteral("/help"));
    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      rootAdminKey,
                                      QStringLiteral("/server_settings"),
                                      QStringLiteral("root admin help with server settings"),
                                      &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(groupAdminKey, QStringLiteral("/server_settings list"));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       groupAdminKey,
                                       QStringLiteral("Access denied"),
                                       QStringLiteral("group admin server settings command denied"),
                                       &error),
             qPrintable(error));

    openMenuForUser(fixture.backend, groupAdminKey);
    QVERIFY2(clickMenuNavigate(fixture.backend,
                               groupAdminKey,
                               QLatin1String(MessengerMenuIds::kMenuAdminHome),
                               QStringLiteral("group admin home navigation"),
                               &error),
             qPrintable(error));
    QVERIFY2(expectNoMenuButton(*fixture.backend,
                                groupAdminKey,
                                menuNavigatePayload(
                                        QLatin1String(MessengerMenuIds::kMenuAdminSettings)),
                                QStringLiteral("group admin server settings menu"),
                                &error),
             qPrintable(error));

    openMenuForUser(fixture.backend, rootAdminKey);
    QVERIFY2(clickMenuNavigate(fixture.backend,
                               rootAdminKey,
                               QLatin1String(MessengerMenuIds::kMenuAdminHome),
                               QStringLiteral("root admin home navigation"),
                               &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              rootAdminKey,
                              menuNavigatePayload(
                                      QLatin1String(MessengerMenuIds::kMenuAdminSettings)),
                              QStringLiteral("root admin server settings menu"),
                              &error),
             qPrintable(error));
}

void MessengerBotTests::pendingAccessUserReplySetting()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_pending_access_reply.sqlite"),
                                  &error),
             qPrintable(error));

    const QString customReply =
            QStringLiteral("Contact support@example.com for access activation.");
    const ServerDatabaseError setError = fixture.database->setServerSetting(
            ServerRuntimeSettings::Setting::PendingAccessUserReplyMessage,
            QVariant::fromValue(customReply),
            kTestActor);
    QVERIFY2(!setError, qPrintable(setError.debugString()));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    const MessengerUserKey unknownKey(MessengerType::Telegram,
                                      QStringLiteral("pending-access-reply"));

    fixture.resetMessages();
    fixture.sendCommand(unknownKey, QStringLiteral("hello"));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       unknownKey,
                                       customReply,
                                       QStringLiteral("custom pending access reply"),
                                       &error),
             qPrintable(error));

    for (int i = 1; i < 9; ++i) {
        fixture.resetMessages();
        fixture.sendCommand(unknownKey, QStringLiteral("hello %1").arg(QString::number(i)));
        QVERIFY2(expectLastMessageContains(*fixture.backend,
                                           unknownKey,
                                           customReply,
                                           QStringLiteral(
                                                   "custom pending access reply before block"),
                                           &error),
                 qPrintable(error));
    }

    fixture.resetMessages();
    fixture.sendCommand(unknownKey, QStringLiteral("hello 9"));
    QVERIFY(fixture.backend->sentMessages().isEmpty());
    QVERIFY(fixture.database->isBlacklisted(unknownKey));
}

void MessengerBotTests::statisticsVisibilityRespectsAdminsOnlySetting()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_statistics_access.sqlite"), &error),
             qPrintable(error));

    const MessengerUserKey adminKey(MessengerType::Telegram, QStringLiteral("stats-admin"));
    const MessengerUserKey userKey(MessengerType::Telegram, QStringLiteral("stats-user"));

    const User admin(adminKey, QStringLiteral("Admin"), QStringLiteral("Stats"), true);
    const ServerDatabaseError addAdminError = fixture.database->addUser(admin, kTestActor);
    QVERIFY2(!addAdminError, qPrintable(addAdminError.debugString()));
    QVERIFY2(fixture.addUser(userKey, QStringLiteral("User"), QStringLiteral("Stats"), &error),
             qPrintable(error));

    const ServerDatabaseError enableStatsError =
            fixture.database->setServerSetting(ServerRuntimeSettings::Setting::StatsEnabled,
                                               true,
                                               kTestActor);
    QVERIFY2(!enableStatsError, qPrintable(enableStatsError.debugString()));

    const ServerSettingEntry adminsOnlyEntry =
            fixture.database->serverSettingsModel()->entryByKey(QLatin1String(
                    ServerRuntimeSettings::key(ServerRuntimeSettings::Setting::StatsAdminsOnly)));
    QVERIFY(adminsOnlyEntry.isValid());
    QVERIFY(adminsOnlyEntry.value().toBool());

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(userKey, QStringLiteral("/help"));
    QVERIFY2(expectNoMessageContains(*fixture.backend,
                                     userKey,
                                     QStringLiteral("/statistics"),
                                     QStringLiteral("user help without statistics"),
                                     &error),
             qPrintable(error));

    fixture.resetMessages();
    openMenuForUser(fixture.backend, userKey);
    QVERIFY2(expectNoMenuButton(*fixture.backend,
                                userKey,
                                menuNavigatePayload(
                                        QLatin1String(MessengerMenuIds::kMenuStatistics)),
                                QStringLiteral("user statistics button hidden"),
                                &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(adminKey, QStringLiteral("/help"));
    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      adminKey,
                                      QStringLiteral("/statistics"),
                                      QStringLiteral("admin help with statistics"),
                                      &error),
             qPrintable(error));

    fixture.resetMessages();
    openMenuForUser(fixture.backend, adminKey);
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuNavigatePayload(QLatin1String(MessengerMenuIds::kMenuStatistics)),
                              QStringLiteral("admin statistics button visible"),
                              &error),
             qPrintable(error));

    const ServerDatabaseError allowUsersError =
            fixture.database->setServerSetting(ServerRuntimeSettings::Setting::StatsAdminsOnly,
                                               false,
                                               kTestActor);
    QVERIFY2(!allowUsersError, qPrintable(allowUsersError.debugString()));

    fixture.resetMessages();
    fixture.sendCommand(userKey, QStringLiteral("/help"));
    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      userKey,
                                      QStringLiteral("/statistics"),
                                      QStringLiteral("user help with statistics"),
                                      &error),
             qPrintable(error));

    fixture.resetMessages();
    openMenuForUser(fixture.backend, userKey);
    QVERIFY2(expectMenuButton(*fixture.backend,
                              userKey,
                              menuNavigatePayload(QLatin1String(MessengerMenuIds::kMenuStatistics)),
                              QStringLiteral("user statistics button visible"),
                              &error),
             qPrintable(error));
}

void MessengerBotTests::sendNotificationCommandSendsToAllUsers()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_notify_command.sqlite"), &error),
             qPrintable(error));

    const MessengerUserKey adminKey(MessengerType::Telegram, QStringLiteral("notify-admin"));
    const MessengerUserKey userA(MessengerType::Telegram, QStringLiteral("notify-command-a"));
    const MessengerUserKey userB(MessengerType::Telegram, QStringLiteral("notify-command-b"));
    const MessengerUserKey other(MessengerType::LocalCli, QStringLiteral("notify-command-local"));

    const User admin(adminKey,
                     QStringLiteral("Admin"),
                     QStringLiteral("User"),
                     User::Role::AdminRole);
    const ServerDatabaseError addAdminError = fixture.database->addUser(admin, kTestActor);
    QVERIFY2(!addAdminError, qPrintable(addAdminError.debugString()));
    QVERIFY2(fixture.addUser(userA, QStringLiteral("User"), QStringLiteral("A"), &error),
             qPrintable(error));
    QVERIFY2(fixture.addUser(userB, QStringLiteral("User"), QStringLiteral("B"), &error),
             qPrintable(error));
    QVERIFY2(fixture.addUser(other, QStringLiteral("Local"), QStringLiteral("Cli"), &error),
             qPrintable(error));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    const QString text = QStringLiteral("Notify all users from command.");

    fixture.resetMessages();
    fixture.sendCommand(adminKey, QStringLiteral("/send_notification %1").arg(text));

    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      userA,
                                      text,
                                      QStringLiteral("notification command users A"),
                                      &error),
             qPrintable(error));
    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      userB,
                                      text,
                                      QStringLiteral("notification command users B"),
                                      &error),
             qPrintable(error));
    QVERIFY2(expectNoMessageEquals(*fixture.backend,
                                   adminKey,
                                   text,
                                   QStringLiteral("notification command users admin"),
                                   &error),
             qPrintable(error));
    QCOMPARE(messageCountForChat(*fixture.backend, other.userId()), 0);
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Alert sent to 2 users."),
                                       QStringLiteral("notification command summary"),
                                       &error),
             qPrintable(error));
}

void MessengerBotTests::sendNotificationCommandSendsToSpecificUsers()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_notify_command_specific.sqlite"),
                                  &error),
             qPrintable(error));

    const MessengerUserKey adminKey(MessengerType::Telegram,
                                    QStringLiteral("notify-specific-command-admin"));
    const MessengerUserKey userA(MessengerType::Telegram,
                                 QStringLiteral("notify-specific-command-a"));
    const MessengerUserKey userB(MessengerType::Telegram,
                                 QStringLiteral("notify-specific-command-b"));
    const MessengerUserKey userC(MessengerType::Telegram,
                                 QStringLiteral("notify-specific-command-c"));

    const User admin(adminKey,
                     QStringLiteral("Admin"),
                     QStringLiteral("Specific"),
                     User::Role::AdminRole);
    const ServerDatabaseError addAdminError = fixture.database->addUser(admin, kTestActor);
    QVERIFY2(!addAdminError, qPrintable(addAdminError.debugString()));
    QVERIFY2(fixture.addUser(userA, QStringLiteral("User"), QStringLiteral("A"), &error),
             qPrintable(error));
    QVERIFY2(fixture.addUser(userB, QStringLiteral("User"), QStringLiteral("B"), &error),
             qPrintable(error));
    QVERIFY2(fixture.addUser(userC, QStringLiteral("User"), QStringLiteral("C"), &error),
             qPrintable(error));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    const QString text = QStringLiteral("Notify selected users from command.");
    const QString recipients = QStringLiteral("%1,%2").arg(userA.toString(), userC.toString());

    fixture.resetMessages();
    fixture.sendCommand(adminKey,
                        QStringLiteral("/send_notification %1 \"%2\"").arg(recipients, text));

    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      userA,
                                      text,
                                      QStringLiteral("notification command specific users A"),
                                      &error),
             qPrintable(error));
    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      userC,
                                      text,
                                      QStringLiteral("notification command specific users C"),
                                      &error),
             qPrintable(error));
    QVERIFY2(expectNoMessageContains(*fixture.backend,
                                     userB,
                                     text,
                                     QStringLiteral("notification command specific users B"),
                                     &error),
             qPrintable(error));
    QVERIFY2(expectNoMessageEquals(*fixture.backend,
                                   adminKey,
                                   text,
                                   QStringLiteral("notification command specific users admin"),
                                   &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Alert sent to 2 users."),
                                       QStringLiteral("notification command specific summary"),
                                       &error),
             qPrintable(error));
}

void MessengerBotTests::sendNotificationMenuSendsToAllUsers()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_notify_all.sqlite"), &error),
             qPrintable(error));

    const MessengerUserKey adminKey(MessengerType::Telegram, QStringLiteral("notify-all-admin"));
    const MessengerUserKey userA(MessengerType::Telegram, QStringLiteral("notify-all-a"));
    const MessengerUserKey userB(MessengerType::Telegram, QStringLiteral("notify-all-b"));
    const MessengerUserKey other(MessengerType::LocalCli, QStringLiteral("notify-all-local"));

    const User admin(adminKey,
                     QStringLiteral("Admin"),
                     QStringLiteral("All"),
                     User::Role::AdminRole);
    const ServerDatabaseError addAdminError = fixture.database->addUser(admin, kTestActor);
    QVERIFY2(!addAdminError, qPrintable(addAdminError.debugString()));
    QVERIFY2(fixture.addUser(userA, QStringLiteral("User"), QStringLiteral("A"), &error),
             qPrintable(error));
    QVERIFY2(fixture.addUser(userB, QStringLiteral("User"), QStringLiteral("B"), &error),
             qPrintable(error));
    QVERIFY2(fixture.addUser(other, QStringLiteral("Local"), QStringLiteral("Cli"), &error),
             qPrintable(error));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    const QString text = QStringLiteral("Notify all users.");

    fixture.resetMessages();
    QVERIFY2(openAdminNotificationMenuWithChecks(fixture.backend, adminKey, &error),
             qPrintable(error));
    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("nt:all"),
                            QStringLiteral("select all users"),
                            &error),
             qPrintable(error));
    fixture.sendCommand(adminKey, text);
    fixture.sendCommand(adminKey, QStringLiteral("send"));

    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      userA,
                                      text,
                                      QStringLiteral("notification all users A"),
                                      &error),
             qPrintable(error));
    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      userB,
                                      text,
                                      QStringLiteral("notification all users B"),
                                      &error),
             qPrintable(error));
    QVERIFY2(expectNoMessageEquals(*fixture.backend,
                                   adminKey,
                                   text,
                                   QStringLiteral("notification all users admin"),
                                   &error),
             qPrintable(error));
    QCOMPARE(messageCountForChat(*fixture.backend, other.userId()), 0);
}

void MessengerBotTests::sendNotificationMenuSendsToSpecificUsers()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_notify_specific.sqlite"), &error),
             qPrintable(error));

    const MessengerUserKey adminKey(MessengerType::Telegram,
                                    QStringLiteral("notify-specific-admin"));
    const MessengerUserKey userA(MessengerType::Telegram, QStringLiteral("notify-specific-a"));
    const MessengerUserKey userB(MessengerType::Telegram, QStringLiteral("notify-specific-b"));
    const MessengerUserKey userC(MessengerType::Telegram, QStringLiteral("notify-specific-c"));

    const User admin(adminKey,
                     QStringLiteral("Admin"),
                     QStringLiteral("Specific"),
                     User::Role::AdminRole);
    const ServerDatabaseError addAdminError = fixture.database->addUser(admin, kTestActor);
    QVERIFY2(!addAdminError, qPrintable(addAdminError.debugString()));
    QVERIFY2(fixture.addUser(userA, QStringLiteral("User"), QStringLiteral("A"), &error),
             qPrintable(error));
    QVERIFY2(fixture.addUser(userB, QStringLiteral("User"), QStringLiteral("B"), &error),
             qPrintable(error));
    QVERIFY2(fixture.addUser(userC, QStringLiteral("User"), QStringLiteral("C"), &error),
             qPrintable(error));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    const QString text = QStringLiteral("Notify selected users.");

    fixture.resetMessages();
    QVERIFY2(openAdminNotificationMenuWithChecks(fixture.backend, adminKey, &error),
             qPrintable(error));
    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("nt:specific"),
                            QStringLiteral("select specific users"),
                            &error),
             qPrintable(error));
    QVERIFY2(clickMenuButtonByTextFragment(fixture.backend,
                                           adminKey,
                                           QStringLiteral("User A"),
                                           QStringLiteral("select user A"),
                                           &error),
             qPrintable(error));
    QVERIFY2(clickMenuButtonByTextFragment(fixture.backend,
                                           adminKey,
                                           QStringLiteral("User C"),
                                           QStringLiteral("select user C"),
                                           &error),
             qPrintable(error));
    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("nt:users:done"),
                            QStringLiteral("confirm recipients"),
                            &error),
             qPrintable(error));
    fixture.sendCommand(adminKey, text);
    fixture.sendCommand(adminKey, QStringLiteral("send"));

    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      userA,
                                      text,
                                      QStringLiteral("notification specific users A"),
                                      &error),
             qPrintable(error));
    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      userC,
                                      text,
                                      QStringLiteral("notification specific users C"),
                                      &error),
             qPrintable(error));
    QVERIFY2(expectNoMessageContains(*fixture.backend,
                                     userB,
                                     text,
                                     QStringLiteral("notification specific users B"),
                                     &error),
             qPrintable(error));
}

void MessengerBotTests::sendNotificationToAllUsersRespectsActorGroups()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_notify_groups.sqlite"), &error),
             qPrintable(error));

    const MessengerUserKey adminKey(MessengerType::Telegram, QStringLiteral("notify-group-admin"));
    const MessengerUserKey alphaUserKey(MessengerType::Telegram,
                                        QStringLiteral("notify-group-alpha"));
    const MessengerUserKey alphaChildUserKey(MessengerType::Telegram,
                                             QStringLiteral("notify-group-alpha-child"));
    const MessengerUserKey betaUserKey(MessengerType::Telegram,
                                       QStringLiteral("notify-group-beta"));
    const MessengerUserKey rootUserKey(MessengerType::Telegram,
                                       QStringLiteral("notify-group-root"));

    User admin(adminKey, QStringLiteral("Admin"), QStringLiteral("Alpha"));
    admin.setGroupRoles(QMap<Group, User::Role>({
            { Group(QStringLiteral("alpha")), User::Role::AdminRole },
    }));
    const ServerDatabaseError addAdminError = fixture.database->addUser(admin, kTestActor);
    QVERIFY2(!addAdminError, qPrintable(addAdminError.debugString()));

    User alphaUser(alphaUserKey, QStringLiteral("Alpha"), QStringLiteral("User"));
    alphaUser.setGroupRoles(QMap<Group, User::Role>({
            { Group(QStringLiteral("alpha")), User::Role::UserRole },
    }));
    const ServerDatabaseError addAlphaError = fixture.database->addUser(alphaUser, kTestActor);
    QVERIFY2(!addAlphaError, qPrintable(addAlphaError.debugString()));

    User alphaChildUser(alphaChildUserKey, QStringLiteral("Alpha"), QStringLiteral("Child"));
    alphaChildUser.setGroupRoles(QMap<Group, User::Role>({
            { Group(QStringLiteral("alpha/child")), User::Role::UserRole },
    }));
    const ServerDatabaseError addAlphaChildError =
            fixture.database->addUser(alphaChildUser, kTestActor);
    QVERIFY2(!addAlphaChildError, qPrintable(addAlphaChildError.debugString()));

    User betaUser(betaUserKey, QStringLiteral("Beta"), QStringLiteral("User"));
    betaUser.setGroupRoles(QMap<Group, User::Role>({
            { Group(QStringLiteral("beta")), User::Role::UserRole },
    }));
    const ServerDatabaseError addBetaError = fixture.database->addUser(betaUser, kTestActor);
    QVERIFY2(!addBetaError, qPrintable(addBetaError.debugString()));

    User rootUser(rootUserKey, QStringLiteral("Root"), QStringLiteral("User"));
    rootUser.setGroupRoles(QMap<Group, User::Role>({
            { Group::root(), User::Role::UserRole },
    }));
    const ServerDatabaseError addRootError = fixture.database->addUser(rootUser, kTestActor);
    QVERIFY2(!addRootError, qPrintable(addRootError.debugString()));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    const QString commandText = QStringLiteral("Notify alpha users from command.");

    fixture.resetMessages();
    fixture.sendCommand(adminKey, QStringLiteral("/send_notification %1").arg(commandText));

    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      alphaUserKey,
                                      commandText,
                                      QStringLiteral("group notification command alpha"),
                                      &error),
             qPrintable(error));
    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      alphaChildUserKey,
                                      commandText,
                                      QStringLiteral("group notification command child"),
                                      &error),
             qPrintable(error));
    QCOMPARE(messageCountForChat(*fixture.backend, betaUserKey.userId()), 0);
    QCOMPARE(messageCountForChat(*fixture.backend, rootUserKey.userId()), 0);
    QVERIFY2(expectNoMessageEquals(*fixture.backend,
                                   adminKey,
                                   commandText,
                                   QStringLiteral("group notification command admin"),
                                   &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Alert sent to 2 users."),
                                       QStringLiteral("group notification command summary"),
                                       &error),
             qPrintable(error));

    const QString menuText = QStringLiteral("Notify alpha users from menu.");

    fixture.resetMessages();
    QVERIFY2(openAdminNotificationMenuWithChecks(fixture.backend, adminKey, &error),
             qPrintable(error));
    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("nt:all"),
                            QStringLiteral("select all group users"),
                            &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("all users (2)"),
                                       QStringLiteral("group notification menu count"),
                                       &error),
             qPrintable(error));
    fixture.sendCommand(adminKey, menuText);
    fixture.sendCommand(adminKey, QStringLiteral("send"));

    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      alphaUserKey,
                                      menuText,
                                      QStringLiteral("group notification menu alpha"),
                                      &error),
             qPrintable(error));
    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      alphaChildUserKey,
                                      menuText,
                                      QStringLiteral("group notification menu child"),
                                      &error),
             qPrintable(error));
    QCOMPARE(messageCountForChat(*fixture.backend, betaUserKey.userId()), 0);
    QCOMPARE(messageCountForChat(*fixture.backend, rootUserKey.userId()), 0);
    QVERIFY2(expectNoMessageEquals(*fixture.backend,
                                   adminKey,
                                   menuText,
                                   QStringLiteral("group notification menu admin"),
                                   &error),
             qPrintable(error));
}

void MessengerBotTests::occupationNotificationsFollowSettings()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_notifications.sqlite"), &error),
             qPrintable(error));

    const QString resourceName = QStringLiteral("Desk 1");
    QVERIFY2(fixture.addResource(resourceName,
                                 QStringLiteral("Notification resource"),
                                 QStringLiteral("Start message"),
                                 &error),
             qPrintable(error));

    const QString alphaResourceName = QStringLiteral("Alpha Desk");
    Resource alphaResource(alphaResourceName,
                           QStringLiteral("Alpha notification resource"),
                           QStringLiteral("Start message"));
    alphaResource.setGroups(QSet<Group>({ Group(QStringLiteral("alpha")) }));
    const ServerDatabaseError addAlphaResourceError =
            fixture.database->addResource(alphaResource, kTestActor);
    QVERIFY2(!addAlphaResourceError, qPrintable(addAlphaResourceError.debugString()));

    const MessengerUserKey userA(MessengerType::Telegram, QStringLiteral("notify-a"));
    const MessengerUserKey userB(MessengerType::Telegram, QStringLiteral("notify-b"));
    const MessengerUserKey userC(MessengerType::Telegram, QStringLiteral("notify-c"));
    const MessengerUserKey userD(MessengerType::Telegram, QStringLiteral("notify-d"));
    const MessengerUserKey alphaOccupant(MessengerType::Telegram,
                                         QStringLiteral("notify-alpha-occupant"));
    const MessengerUserKey alphaWatcher(MessengerType::Telegram,
                                        QStringLiteral("notify-alpha-watcher"));
    const MessengerUserKey betaWatcher(MessengerType::Telegram,
                                       QStringLiteral("notify-beta-watcher"));

    QVERIFY2(fixture.addUser(userA, QStringLiteral("User"), QStringLiteral("A"), &error),
             qPrintable(error));
    QVERIFY2(fixture.addUser(userB, QStringLiteral("User"), QStringLiteral("B"), &error),
             qPrintable(error));
    QVERIFY2(fixture.addUser(userC, QStringLiteral("User"), QStringLiteral("C"), &error),
             qPrintable(error));
    QVERIFY2(fixture.addUser(userD, QStringLiteral("User"), QStringLiteral("D"), &error),
             qPrintable(error));

    User alphaOccupantUser(alphaOccupant, QStringLiteral("Alpha"), QStringLiteral("Occupant"));
    alphaOccupantUser.setGroupRoles(QMap<Group, User::Role>({
            { Group(QStringLiteral("alpha")), User::Role::UserRole },
    }));
    const ServerDatabaseError addAlphaOccupantError =
            fixture.database->addUser(alphaOccupantUser, kTestActor);
    QVERIFY2(!addAlphaOccupantError, qPrintable(addAlphaOccupantError.debugString()));

    User alphaWatcherUser(alphaWatcher, QStringLiteral("Alpha"), QStringLiteral("Watcher"));
    alphaWatcherUser.setGroupRoles(QMap<Group, User::Role>({
            { Group(QStringLiteral("alpha")), User::Role::UserRole },
    }));
    alphaWatcherUser.setOccupationNotifyMode(User::OccupationNotifyMode::AllResources);
    const ServerDatabaseError addAlphaWatcherError =
            fixture.database->addUser(alphaWatcherUser, kTestActor);
    QVERIFY2(!addAlphaWatcherError, qPrintable(addAlphaWatcherError.debugString()));

    User betaWatcherUser(betaWatcher, QStringLiteral("Beta"), QStringLiteral("Watcher"));
    betaWatcherUser.setGroupRoles(QMap<Group, User::Role>({
            { Group(QStringLiteral("beta")), User::Role::UserRole },
    }));
    betaWatcherUser.setOccupationNotifyMode(User::OccupationNotifyMode::AllResources);
    const ServerDatabaseError addBetaWatcherError =
            fixture.database->addUser(betaWatcherUser, kTestActor);
    QVERIFY2(!addBetaWatcherError, qPrintable(addBetaWatcherError.debugString()));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    QVERIFY2(updateUserSettings(fixture.database.get(),
                                userB,
                                QString(),
                                User::OccupationNotifyMode::AllResources,
                                &error),
             qPrintable(error));
    QVERIFY2(updateUserSettings(fixture.database.get(),
                                userD,
                                QString(),
                                User::OccupationNotifyMode::Disabled,
                                &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(userA, buildOccupyCommand(resourceName, 5));
    QVERIFY2(expectNoMessageContains(*fixture.backend,
                                     userA,
                                     QStringLiteral("occupied by User A"),
                                     QStringLiteral("occupied notification for A"),
                                     &error),
             qPrintable(error));
    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      userB,
                                      QStringLiteral("occupied by User A"),
                                      QStringLiteral("occupied notification for B"),
                                      &error),
             qPrintable(error));
    QCOMPARE(messageCountForChat(*fixture.backend, userC.userId()), 0);
    QCOMPARE(messageCountForChat(*fixture.backend, userD.userId()), 0);

    fixture.resetMessages();
    fixture.sendCommand(userA, buildReleaseCommand(resourceName));
    QVERIFY2(expectNoMessageContains(*fixture.backend,
                                     userA,
                                     QStringLiteral("released by User A"),
                                     QStringLiteral("released notification for A"),
                                     &error),
             qPrintable(error));
    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      userB,
                                      QStringLiteral("released by User A"),
                                      QStringLiteral("released notification for B"),
                                      &error),
             qPrintable(error));
    QCOMPARE(messageCountForChat(*fixture.backend, userC.userId()), 0);
    QCOMPARE(messageCountForChat(*fixture.backend, userD.userId()), 0);

    fixture.resetMessages();
    fixture.sendCommand(alphaOccupant, buildOccupyCommand(alphaResourceName, 5));
    QVERIFY2(expectNoMessageContains(*fixture.backend,
                                     alphaOccupant,
                                     QStringLiteral("occupied by Alpha Occupant"),
                                     QStringLiteral("grouped occupied notification for actor"),
                                     &error),
             qPrintable(error));
    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      alphaWatcher,
                                      QStringLiteral("occupied by Alpha Occupant"),
                                      QStringLiteral("grouped occupied notification for alpha"),
                                      &error),
             qPrintable(error));
    QVERIFY2(expectNoMessageContains(*fixture.backend,
                                     betaWatcher,
                                     QStringLiteral("occupied by Alpha Occupant"),
                                     QStringLiteral("grouped occupied notification for beta"),
                                     &error),
             qPrintable(error));
}

void MessengerBotTests::occupationNotificationHandover()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_handover.sqlite"), &error),
             qPrintable(error));

    const QString resourceName = QStringLiteral("Desk H");
    QVERIFY2(fixture.addResource(resourceName,
                                 QStringLiteral("Handover resource"),
                                 QStringLiteral("Start message"),
                                 &error),
             qPrintable(error));

    const MessengerUserKey userA(MessengerType::Telegram, QStringLiteral("handover-a"));
    const MessengerUserKey userB(MessengerType::Telegram, QStringLiteral("handover-b"));
    const MessengerUserKey userC(MessengerType::Telegram, QStringLiteral("handover-c"));

    QVERIFY2(fixture.addUser(userA, QStringLiteral("User"), QStringLiteral("A"), &error),
             qPrintable(error));
    QVERIFY2(fixture.addUser(userB, QStringLiteral("User"), QStringLiteral("B"), &error),
             qPrintable(error));
    QVERIFY2(fixture.addUser(userC, QStringLiteral("User"), QStringLiteral("C"), &error),
             qPrintable(error));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    fixture.sendCommand(userA, buildOccupyCommand(resourceName, 5));

    const QDateTime startUtc = QDateTime::currentDateTimeUtc().addSecs(600);
    const QDateTime endUtc = startUtc.addSecs(600);
    QVERIFY2(fixture.scheduleResource(resourceName, userB, startUtc, endUtc, &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(userA, buildReleaseCommand(resourceName));

    fixture.resetMessages();
    fixture.sendCommand(userC, buildOccupyCommand(resourceName, 5));

    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      userA,
                                      QStringLiteral("handed over"),
                                      QStringLiteral("handover notification for A"),
                                      &error),
             qPrintable(error));
    QVERIFY2(expectNoMessageContains(*fixture.backend,
                                     userC,
                                     QStringLiteral("handed over"),
                                     QStringLiteral("handover notification for C"),
                                     &error),
             qPrintable(error));
    QCOMPARE(messageCountForChat(*fixture.backend, userB.userId()), 0);
}

void MessengerBotTests::scheduleExpiryNotifiesPreviousAndNext()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_schedule_expiry.sqlite"), &error),
             qPrintable(error));

    const QString resourceName = QStringLiteral("Room E");
    QVERIFY2(fixture.addResource(resourceName,
                                 QStringLiteral("Expiry resource"),
                                 QStringLiteral("Expiry start note"),
                                 &error),
             qPrintable(error));

    const MessengerUserKey userA(MessengerType::Telegram, QStringLiteral("expiry-a"));
    const MessengerUserKey userB(MessengerType::Telegram, QStringLiteral("expiry-b"));
    QVERIFY2(fixture.addUser(userA, QStringLiteral("User"), QStringLiteral("A"), &error),
             qPrintable(error));
    QVERIFY2(fixture.addUser(userB, QStringLiteral("User"), QStringLiteral("B"), &error),
             qPrintable(error));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    const QDateTime baseUtc =
            QDateTime::fromSecsSinceEpoch(QDateTime::currentSecsSinceEpoch() + 2, QTimeZone::utc());
    const QDateTime aStartUtc = baseUtc;
    const QDateTime aEndUtc = baseUtc.addSecs(2);
    const QDateTime bStartUtc = aEndUtc;
    const QDateTime bEndUtc = bStartUtc.addSecs(2);

    QVERIFY2(fixture.scheduleResource(resourceName, userA, aStartUtc, aEndUtc, &error),
             qPrintable(error));
    QVERIFY2(fixture.scheduleResource(resourceName, userB, bStartUtc, bEndUtc, &error),
             qPrintable(error));

    QTRY_VERIFY_WITH_TIMEOUT(expectCurrentOccupant(fixture.database.get(),
                                                   resourceName,
                                                   userA,
                                                   &error),
                             4000);
    QTRY_VERIFY_WITH_TIMEOUT(
            expectAnyMessageContains(*fixture.backend,
                                     userA,
                                     QStringLiteral("Scheduled time for '%1' has started.")
                                             .arg(resourceName),
                                     QStringLiteral("initial schedule start notification"),
                                     &error),
            4000);
    fixture.resetMessages();

    QTRY_VERIFY_WITH_TIMEOUT(expectCurrentOccupant(fixture.database.get(),
                                                   resourceName,
                                                   userB,
                                                   &error),
                             5000);

    QCOMPARE(messageCountForChat(*fixture.backend, userA.userId()), 1);
    QCOMPARE(messageCountForChat(*fixture.backend, userB.userId()), 1);
    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      userA,
                                      QStringLiteral("Your occupation of '%1' has ended.")
                                              .arg(resourceName),
                                      QStringLiteral("schedule expiry ended notification"),
                                      &error),
             qPrintable(error));
    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      userB,
                                      QStringLiteral("Scheduled time for '%1' has started.")
                                              .arg(resourceName),
                                      QStringLiteral("schedule expiry started notification"),
                                      &error),
             qPrintable(error));
    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      userB,
                                      QStringLiteral("Expiry start note"),
                                      QStringLiteral("schedule expiry start message"),
                                      &error),
             qPrintable(error));
}

void MessengerBotTests::occupyQueuesAlreadyOccupiedResource()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_queue.sqlite"), &error),
             qPrintable(error));

    const QString resourceName = QStringLiteral("Room A");
    QVERIFY2(fixture.addResource(resourceName,
                                 QStringLiteral("Test resource"),
                                 QStringLiteral("Welcome message"),
                                 &error),
             qPrintable(error));

    const MessengerUserKey userA(MessengerType::Telegram, QStringLiteral("user-a"));
    const MessengerUserKey userB(MessengerType::Telegram, QStringLiteral("user-b"));

    QVERIFY2(fixture.addUser(userA, QStringLiteral("User"), QStringLiteral("A"), &error),
             qPrintable(error));
    QVERIFY2(fixture.addUser(userB, QStringLiteral("User"), QStringLiteral("B"), &error),
             qPrintable(error));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    // Occupy the resource with user A.
    fixture.resetMessages();
    fixture.sendCommand(userA, buildOccupyCommand(resourceName, 5));
    QVERIFY2(expectCurrentOccupant(fixture.database.get(), resourceName, userA, &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userA,
                                       QStringLiteral("Welcome message"),
                                       QStringLiteral("occupy reply start message"),
                                       &error),
             qPrintable(error));

    // User B waits for release instead of being rejected.
    fixture.sendCommand(userB, buildOccupyCommand(resourceName, 5));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userB,
                                       QStringLiteral("Queued"),
                                       QStringLiteral("command queued reply"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectCurrentOccupant(fixture.database.get(), resourceName, userA, &error),
             qPrintable(error));
    const QList<ResourceQueueEntry> commandQueue =
            fixture.database->resourceQueueModel()->queueForResource(resourceName);
    bool commandWaiterFound = false;
    for (const ResourceQueueEntry &entry : commandQueue) {
        if (entry.user() == userB && entry.waitsForRelease()) {
            commandWaiterFound = true;
            QCOMPARE(entry.requestedDurationMinutes(), 5);
            break;
        }
    }
    QVERIFY(commandWaiterFound);

    // Reset occupation so menu path starts from a clean state.
    QVERIFY2(fixture.releaseResource(resourceName, userA, &error), qPrintable(error));
    QVERIFY2(fixture.releaseResource(resourceName, userB, &error), qPrintable(error));

    fixture.resetMessages();

    // Repeat the same scenario through menu interactions.
    QVERIFY2(openResourceCardMenuWithChecks(fixture.backend, userA, &error), qPrintable(error));
    QVERIFY2(clickMenuEvent(fixture.backend,
                            userA,
                            QStringLiteral("ur:occupy"),
                            QStringLiteral("menu occupy"),
                            &error),
             qPrintable(error));
    QVERIFY2(clickMenuEvent(fixture.backend,
                            userA,
                            QStringLiteral("ur:dur:5"),
                            QStringLiteral("menu duration"),
                            &error),
             qPrintable(error));
    QVERIFY2(expectCurrentOccupant(fixture.database.get(), resourceName, userA, &error),
             qPrintable(error));

    QVERIFY2(openResourceCardMenuWithChecks(fixture.backend, userB, &error), qPrintable(error));
    QVERIFY2(clickMenuEvent(fixture.backend,
                            userB,
                            QStringLiteral("ur:occupy"),
                            QStringLiteral("menu occupy"),
                            &error),
             qPrintable(error));
    QVERIFY2(clickMenuEvent(fixture.backend,
                            userB,
                            QStringLiteral("ur:dur:5"),
                            QStringLiteral("menu duration"),
                            &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userB,
                                       QStringLiteral("when the resource is released"),
                                       QStringLiteral("menu queued reply"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectCurrentOccupant(fixture.database.get(), resourceName, userA, &error),
             qPrintable(error));
    const QList<ResourceQueueEntry> menuQueue =
            fixture.database->resourceQueueModel()->queueForResource(resourceName);
    bool menuWaiterFound = false;
    for (const ResourceQueueEntry &entry : menuQueue) {
        if (entry.user() == userB && entry.waitsForRelease()) {
            menuWaiterFound = true;
            QCOMPARE(entry.requestedDurationMinutes(), 5);
            break;
        }
    }
    QVERIFY(menuWaiterFound);
}

void MessengerBotTests::indefiniteOccupationCanQueueIndefiniteWaiter()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_indefinite_queue.sqlite"), &error),
             qPrintable(error));

    const QString resourceName = QStringLiteral("Room I");
    QVERIFY2(fixture.addResource(resourceName,
                                 QStringLiteral("Indefinite queue resource"),
                                 QStringLiteral("Indefinite start note"),
                                 &error),
             qPrintable(error));

    const MessengerUserKey userA(MessengerType::Telegram, QStringLiteral("indefinite-a"));
    const MessengerUserKey userB(MessengerType::Telegram, QStringLiteral("indefinite-b"));

    QVERIFY2(fixture.addUser(userA, QStringLiteral("User"), QStringLiteral("A"), &error),
             qPrintable(error));
    QVERIFY2(fixture.addUser(userB, QStringLiteral("User"), QStringLiteral("B"), &error),
             qPrintable(error));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    fixture.sendCommand(userA, buildOccupyCommand(resourceName, 0));
    QVERIFY2(expectCurrentOccupant(fixture.database.get(), resourceName, userA, &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(userB, QStringLiteral("/schedule \"%1\" 0 0").arg(resourceName));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userB,
                                       QStringLiteral("Queued"),
                                       QStringLiteral("indefinite queued reply"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userB,
                                       QStringLiteral("indefinite"),
                                       QStringLiteral("indefinite queued duration"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectCurrentOccupant(fixture.database.get(), resourceName, userA, &error),
             qPrintable(error));

    const QList<ResourceQueueEntry> waitingQueue =
            fixture.database->resourceQueueModel()->queueForResource(resourceName);
    bool waitingEntryFound = false;
    for (const ResourceQueueEntry &entry : waitingQueue) {
        if (entry.user() == userB) {
            waitingEntryFound = true;
            QVERIFY(entry.waitsForRelease());
            QVERIFY(entry.isIndefinite());
            QCOMPARE(entry.requestedDurationMinutes(), 0);
            QVERIFY(!entry.isActiveAtUtc(QDateTime::currentDateTimeUtc()));
            break;
        }
    }
    QVERIFY(waitingEntryFound);

    fixture.resetMessages();
    fixture.sendCommand(userA, buildReleaseCommand(resourceName));

    QVERIFY2(expectCurrentOccupant(fixture.database.get(), resourceName, userB, &error),
             qPrintable(error));
    const ResourceQueueEntry occupant =
            fixture.database->resourceQueueModel()->currentOccupantForResource(resourceName);
    QVERIFY(occupant.isValid());
    QVERIFY(!occupant.waitsForRelease());
    QVERIFY(occupant.isIndefinite());
    QCOMPARE(occupant.requestedDurationMinutes(), 0);
    QVERIFY(!occupant.endAtUtc().isValid());

    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      userB,
                                      QStringLiteral("Status for"),
                                      QStringLiteral("indefinite waiter queue status"),
                                      &error),
             qPrintable(error));
    QVERIFY2(expectAnyMessageContains(*fixture.backend,
                                      userB,
                                      QStringLiteral("Indefinite start note"),
                                      QStringLiteral("indefinite waiter start note"),
                                      &error),
             qPrintable(error));
}

void MessengerBotTests::releaseNotifiesNextInQueue()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_release.sqlite"), &error),
             qPrintable(error));

    const QString resourceName = QStringLiteral("Room Q");
    QVERIFY2(fixture.addResource(resourceName,
                                 QStringLiteral("Queue resource"),
                                 QStringLiteral("Queue start note"),
                                 &error),
             qPrintable(error));

    const MessengerUserKey userA(MessengerType::Telegram, QStringLiteral("user-release-a"));
    const MessengerUserKey userB(MessengerType::Telegram, QStringLiteral("user-release-b"));

    QVERIFY2(fixture.addUser(userA, QStringLiteral("User"), QStringLiteral("A"), &error),
             qPrintable(error));
    QVERIFY2(fixture.addUser(userB, QStringLiteral("User"), QStringLiteral("B"), &error),
             qPrintable(error));

    // Prepopulate schedule for user B so they are next in queue.
    const QDateTime scheduleStart = QDateTime::currentDateTimeUtc().addSecs(600);
    const QDateTime scheduleEnd = scheduleStart.addSecs(300);
    QVERIFY2(fixture.scheduleResource(resourceName, userB, scheduleStart, scheduleEnd, &error),
             qPrintable(error));
    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    // Occupy by user A, then release to trigger queue notification.
    fixture.sendCommand(userA, buildOccupyCommand(resourceName, 5));
    fixture.resetMessages();

    fixture.sendCommand(userA, buildReleaseCommand(resourceName));

    // User A gets release confirmation.
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userA,
                                       resourceName,
                                       QStringLiteral("release confirmation"),
                                       &error),
             qPrintable(error));

    // User B receives queue status for the released resource.
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userB,
                                       QStringLiteral("Status for"),
                                       QStringLiteral("queue status reply"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userB,
                                       resourceName,
                                       QStringLiteral("queue resource name in status reply"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userB,
                                       QStringLiteral("Queue start note"),
                                       QStringLiteral("queue start message reply"),
                                       &error),
             qPrintable(error));

    fixture.resetMessages();

    // Repeat the same scenario through menu interactions.
    QVERIFY2(openResourceCardMenuWithChecks(fixture.backend, userA, &error), qPrintable(error));
    QVERIFY2(clickMenuEvent(fixture.backend,
                            userA,
                            QStringLiteral("ur:occupy"),
                            QStringLiteral("menu occupy"),
                            &error),
             qPrintable(error));
    QVERIFY2(clickMenuEvent(fixture.backend,
                            userA,
                            QStringLiteral("ur:dur:5"),
                            QStringLiteral("menu duration"),
                            &error),
             qPrintable(error));
    QVERIFY2(clickMenuEvent(fixture.backend,
                            userA,
                            QStringLiteral("ur:release"),
                            QStringLiteral("menu release"),
                            &error),
             qPrintable(error));

    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userA,
                                       resourceName,
                                       QStringLiteral("menu release confirmation"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userB,
                                       QStringLiteral("Status for"),
                                       QStringLiteral("menu queue status reply"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userB,
                                       resourceName,
                                       QStringLiteral("menu queue resource name in status reply"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userB,
                                       QStringLiteral("Queue start note"),
                                       QStringLiteral("menu queue start message reply"),
                                       &error),
             qPrintable(error));
}

void MessengerBotTests::adminResourceAddCommandAcceptsMultilineStartMessage()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_add_resource_multiline.sqlite"),
                                  &error),
             qPrintable(error));

    const MessengerUserKey adminKey(MessengerType::Telegram,
                                    QStringLiteral("admin-add-resource-multiline"));
    const User admin(adminKey,
                     QStringLiteral("Admin"),
                     QStringLiteral("Multiline"),
                     User::Role::AdminRole);
    const ServerDatabaseError addAdminError = fixture.database->addUser(admin, kTestActor);
    QVERIFY2(!addAdminError, qPrintable(addAdminError.debugString()));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    const QString resourceName = QStringLiteral("Printer Multiline");
    const QString description = QStringLiteral("Color laser");
    const QString startMessage = QStringLiteral("Load paper.\nClose the tray.");

    fixture.resetMessages();
    fixture.sendCommand(adminKey,
                        QStringLiteral("/add_resource \"%1\" \"%2\" \"%3\"")
                                .arg(resourceName, description, startMessage));

    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Resource added"),
                                       QStringLiteral("add resource multiline command"),
                                       &error),
             qPrintable(error));

    const ResourceModel *resourceModel = fixture.database->resourceModel();
    QVERIFY(resourceModel != nullptr);
    const Resource added = resourceModel->resourceByName(resourceName);
    QVERIFY(added.isValid());
    QCOMPARE(added.description(), description);
    QCOMPARE(added.startMessage(), startMessage);
}

void MessengerBotTests::adminResourceAddCommandAndMenu()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_admin_add_resource.sqlite"),
                                  &error),
             qPrintable(error));

    const MessengerUserKey adminKey(MessengerType::Telegram, QStringLiteral("admin-add-resource"));
    const User admin(adminKey,
                     QStringLiteral("Admin"),
                     QStringLiteral("Resource"),
                     User::Role::AdminRole);
    const ServerDatabaseError addAdminError = fixture.database->addUser(admin, kTestActor);
    QVERIFY2(!addAdminError, qPrintable(addAdminError.debugString()));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    const QString resourceName = QStringLiteral("Printer A");
    const QString description = QStringLiteral("Color laser");
    const QString startMessage = QStringLiteral("Load paper.");

    fixture.resetMessages();
    fixture.sendCommand(adminKey,
                        QStringLiteral("/add_resource \"%1\" \"%2\" \"%3\"")
                                .arg(resourceName, description, startMessage));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Resource added"),
                                       QStringLiteral("add resource command"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("[On]"),
                                       QStringLiteral("add resource command"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       resourceName,
                                       QStringLiteral("add resource command"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       description,
                                       QStringLiteral("add resource command"),
                                       &error),
             qPrintable(error));

    const ResourceModel *resourceModel = fixture.database->resourceModel();
    QVERIFY(resourceModel != nullptr);
    const Resource added = resourceModel->resourceByName(resourceName);
    QVERIFY(added.isValid());
    QCOMPARE(added.description(), description);
    QCOMPARE(added.startMessage(), startMessage);

    const QString menuResourceName = QStringLiteral("Scanner B");
    const QString menuDescription = QStringLiteral("Desk scanner");
    const QString menuStartMessage = QStringLiteral("Handle with care.");

    fixture.resetMessages();
    QVERIFY2(openAdminResourcesMenuWithChecks(fixture.backend, adminKey, &error),
             qPrintable(error));
    QVERIFY2(clickMenuNavigate(fixture.backend,
                               adminKey,
                               QLatin1String(MessengerMenuIds::kMenuAdminResourceAdd),
                               QStringLiteral("admin resource add navigation"),
                               &error),
             qPrintable(error));

    QString previousText;
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Send resource name."),
                                       QStringLiteral("resource add name step"),
                                       &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("resource add name step"),
                                    &previousText,
                                    &error),
             qPrintable(error));
    QVERIFY2(expectNoMenuButton(*fixture.backend,
                                adminKey,
                                menuEventPayload(QStringLiteral("rw:skip")),
                                QStringLiteral("resource add name step"),
                                &error),
             qPrintable(error));

    fixture.sendCommand(adminKey, menuResourceName);

    QString currentText;
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Send description"),
                                       QStringLiteral("resource add description step"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("rw:skip")),
                              QStringLiteral("resource add description step"),
                              &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("resource add description step"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText, "Menu text did not change after the resource name step.");
    previousText = currentText;

    fixture.sendCommand(adminKey, menuDescription);

    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Send start message"),
                                       QStringLiteral("resource add start step"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("rw:skip")),
                              QStringLiteral("resource add start step"),
                              &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("resource add start step"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText,
             "Menu text did not change after the resource description step.");
    previousText = currentText;

    fixture.sendCommand(adminKey, menuStartMessage);

    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Send groups"),
                                       QStringLiteral("resource add groups step"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("rw:skip")),
                              QStringLiteral("resource add groups step"),
                              &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("resource add groups step"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText,
             "Menu text did not change after the resource start message step.");
    previousText = currentText;

    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("rw:skip"),
                            QStringLiteral("resource add groups step"),
                            &error),
             qPrintable(error));

    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Confirm adding resource"),
                                       QStringLiteral("resource add review step"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("rw:confirm")),
                              QStringLiteral("resource add review step"),
                              &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("rw:edit:name")),
                              QStringLiteral("resource add review step"),
                              &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("rw:edit:desc")),
                              QStringLiteral("resource add review step"),
                              &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("rw:edit:start")),
                              QStringLiteral("resource add review step"),
                              &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("rw:edit:groups")),
                              QStringLiteral("resource add review step"),
                              &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("resource add review step"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText,
             "Menu text did not change after the resource groups step.");
    previousText = currentText;

    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("rw:confirm"),
                            QStringLiteral("resource add confirm"),
                            &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Resource management"),
                                       QStringLiteral("resource add finish"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuNavigatePayload(
                                      QLatin1String(MessengerMenuIds::kMenuAdminResourceCard)),
                              QStringLiteral("resource add finish menu"),
                              &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuNavigatePayload(
                                      QLatin1String(MessengerMenuIds::kMenuAdminResourceAdd)),
                              QStringLiteral("resource add finish menu"),
                              &error),
             qPrintable(error));
    QString finalText;
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("resource add finish"),
                                    &finalText,
                                    &error),
             qPrintable(error));
    QVERIFY2(finalText != previousText, "Menu text did not change after confirming resource add.");

    const Resource menuAdded = resourceModel->resourceByName(menuResourceName);
    QVERIFY(menuAdded.isValid());
    QCOMPARE(menuAdded.description(), menuDescription);
    QCOMPARE(menuAdded.startMessage(), menuStartMessage);
}

void MessengerBotTests::adminResourceDeleteCommandAndMenu()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_admin_delete_resource.sqlite"),
                                  &error),
             qPrintable(error));

    const MessengerUserKey adminKey(MessengerType::Telegram,
                                    QStringLiteral("admin-delete-resource"));
    const User admin(adminKey,
                     QStringLiteral("Admin"),
                     QStringLiteral("Delete"),
                     User::Role::AdminRole);
    const ServerDatabaseError addAdminError = fixture.database->addUser(admin, kTestActor);
    QVERIFY2(!addAdminError, qPrintable(addAdminError.debugString()));

    const QString commandResource = QStringLiteral("Projector X");
    const QString menuResource = QStringLiteral("Whiteboard Y");
    QVERIFY2(fixture.addResource(commandResource,
                                 QStringLiteral("Command resource"),
                                 QStringLiteral("Start message"),
                                 &error),
             qPrintable(error));
    QVERIFY2(fixture.addResource(menuResource,
                                 QStringLiteral("Menu resource"),
                                 QStringLiteral("Start message"),
                                 &error),
             qPrintable(error));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(adminKey, QStringLiteral("/delete_resource \"%1\"").arg(commandResource));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Resource deleted"),
                                       QStringLiteral("delete resource command"),
                                       &error),
             qPrintable(error));

    const ResourceModel *resourceModel = fixture.database->resourceModel();
    QVERIFY(resourceModel != nullptr);
    QVERIFY(!resourceModel->resourceByName(commandResource).isValid());

    fixture.resetMessages();
    QVERIFY2(openAdminResourceCardMenuWithChecks(fixture.backend, adminKey, &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Select a resource"),
                                       QStringLiteral("resource card selector"),
                                       &error),
             qPrintable(error));
    QString previousText;
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("resource card selector"),
                                    &previousText,
                                    &error),
             qPrintable(error));

    QVERIFY2(clickMenuButtonByTextFragment(fixture.backend,
                                           adminKey,
                                           menuResource,
                                           QStringLiteral("select resource"),
                                           &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Resource: %1").arg(menuResource),
                                       QStringLiteral("resource card"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("rc:delete")),
                              QStringLiteral("resource card delete"),
                              &error),
             qPrintable(error));
    QString currentText;
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("resource card"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText, "Menu text did not change after selecting a resource.");
    previousText = currentText;

    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("rc:delete"),
                            QStringLiteral("resource delete"),
                            &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Confirm deleting resource"),
                                       QStringLiteral("resource delete confirm"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("delr:yes")),
                              QStringLiteral("resource delete confirm"),
                              &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("resource delete confirm"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText,
             "Menu text did not change after opening delete confirmation.");
    previousText = currentText;

    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("delr:yes"),
                            QStringLiteral("resource delete accept"),
                            &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Resource management"),
                                       QStringLiteral("resource delete finish"),
                                       &error),
             qPrintable(error));
    QString finalText;
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("resource delete finish"),
                                    &finalText,
                                    &error),
             qPrintable(error));
    QVERIFY2(finalText != previousText,
             "Menu text did not change after confirming resource delete.");

    QVERIFY(!resourceModel->resourceByName(menuResource).isValid());
}

void MessengerBotTests::adminResourceUpdateCommandAndMenu()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_admin_update_resource.sqlite"),
                                  &error),
             qPrintable(error));

    const MessengerUserKey adminKey(MessengerType::Telegram,
                                    QStringLiteral("admin-update-resource"));
    const User admin(adminKey,
                     QStringLiteral("Admin"),
                     QStringLiteral("Update"),
                     User::Role::AdminRole);
    const ServerDatabaseError addAdminError = fixture.database->addUser(admin, kTestActor);
    QVERIFY2(!addAdminError, qPrintable(addAdminError.debugString()));

    const QString commandResource = QStringLiteral("Projector Z");
    const QString menuResource = QStringLiteral("Screen W");
    QVERIFY2(fixture.addResource(commandResource,
                                 QStringLiteral("Old description"),
                                 QStringLiteral("Old start"),
                                 &error),
             qPrintable(error));
    QVERIFY2(fixture.addResource(menuResource,
                                 QStringLiteral("Initial description"),
                                 QStringLiteral("Initial start"),
                                 &error),
             qPrintable(error));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    const QString commandDescription = QStringLiteral("New description");
    const QString commandStart = QStringLiteral("New start");
    const Resource::Status commandStatus = Resource::Status::TemporarilyUnavailable;
    fixture.resetMessages();
    fixture.sendCommand(adminKey,
                        QStringLiteral("/update_resource \"%1\" \"%2\" \"%3\" \"%4\"")
                                .arg(commandResource,
                                     commandDescription,
                                     commandStart,
                                     QStringLiteral("TemporarilyUnavailable")));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Resource updated"),
                                       QStringLiteral("update resource command"),
                                       &error),
             qPrintable(error));

    const ResourceModel *resourceModel = fixture.database->resourceModel();
    QVERIFY(resourceModel != nullptr);
    const Resource updated = resourceModel->resourceByName(commandResource);
    QVERIFY(updated.isValid());
    QCOMPARE(updated.description(), commandDescription);
    QCOMPARE(updated.startMessage(), commandStart);
    QCOMPARE(updated.status(), commandStatus);

    const QString menuDescription = QStringLiteral("Updated description");
    const QString menuStart = QStringLiteral("Updated start");
    const Resource::Status menuStatus = Resource::Status::Off;

    fixture.resetMessages();
    QVERIFY2(openAdminResourceCardMenuWithChecks(fixture.backend, adminKey, &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Select a resource"),
                                       QStringLiteral("resource edit selector"),
                                       &error),
             qPrintable(error));
    QString previousText;
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("resource edit selector"),
                                    &previousText,
                                    &error),
             qPrintable(error));

    QVERIFY2(clickMenuButtonByTextFragment(fixture.backend,
                                           adminKey,
                                           menuResource,
                                           QStringLiteral("select resource for edit"),
                                           &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Resource: %1").arg(menuResource),
                                       QStringLiteral("resource card for edit"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("rc:edit")),
                              QStringLiteral("resource card edit"),
                              &error),
             qPrintable(error));
    QString currentText;
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("resource card for edit"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText,
             "Menu text did not change after selecting a resource for edit.");
    previousText = currentText;

    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("rc:edit"),
                            QStringLiteral("resource edit open"),
                            &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Resource details"),
                                       QStringLiteral("resource edit review"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("rw:confirm")),
                              QStringLiteral("resource edit review"),
                              &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("resource edit review"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText, "Menu text did not change after opening edit review.");
    previousText = currentText;

    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("rw:edit:desc"),
                            QStringLiteral("resource edit description"),
                            &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Send new description"),
                                       QStringLiteral("resource edit description step"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("rw:skip")),
                              QStringLiteral("resource edit description step"),
                              &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("resource edit description step"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText,
             "Menu text did not change after selecting description edit.");
    previousText = currentText;

    fixture.sendCommand(adminKey, menuDescription);
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Resource details"),
                                       QStringLiteral("resource edit review after desc"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       menuDescription,
                                       QStringLiteral("resource edit review after desc"),
                                       &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("resource edit review after desc"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText, "Menu text did not change after updating description.");
    previousText = currentText;

    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("rw:edit:start"),
                            QStringLiteral("resource edit start"),
                            &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Send new start message"),
                                       QStringLiteral("resource edit start step"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("rw:skip")),
                              QStringLiteral("resource edit start step"),
                              &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("resource edit start step"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText,
             "Menu text did not change after selecting start message edit.");
    previousText = currentText;

    fixture.sendCommand(adminKey, menuStart);
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Resource details"),
                                       QStringLiteral("resource edit review after start"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       menuStart,
                                       QStringLiteral("resource edit review after start"),
                                       &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("resource edit review after start"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText, "Menu text did not change after updating start message.");
    previousText = currentText;

    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("rw:edit:status"),
                            QStringLiteral("resource edit status"),
                            &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Current status"),
                                       QStringLiteral("resource edit status step"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("rw:setstatus:off")),
                              QStringLiteral("resource edit status step"),
                              &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("resource edit status step"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText, "Menu text did not change after opening status edit.");
    previousText = currentText;

    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("rw:setstatus:off"),
                            QStringLiteral("resource edit status choice"),
                            &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Status: Off"),
                                       QStringLiteral("resource edit review after status"),
                                       &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("resource edit review after status"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText, "Menu text did not change after updating status.");
    previousText = currentText;

    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("rw:confirm"),
                            QStringLiteral("resource edit confirm"),
                            &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Resource management"),
                                       QStringLiteral("resource edit finish"),
                                       &error),
             qPrintable(error));
    QString finalText;
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("resource edit finish"),
                                    &finalText,
                                    &error),
             qPrintable(error));
    QVERIFY2(finalText != previousText,
             "Menu text did not change after confirming resource update.");

    const Resource menuUpdated = resourceModel->resourceByName(menuResource);
    QVERIFY(menuUpdated.isValid());
    QCOMPARE(menuUpdated.description(), menuDescription);
    QCOMPARE(menuUpdated.startMessage(), menuStart);
    QCOMPARE(menuUpdated.status(), menuStatus);
}

void MessengerBotTests::resourceStatusVisibilityAndCommandBehavior()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_resource_status.sqlite"), &error),
             qPrintable(error));

    const MessengerUserKey adminKey(MessengerType::Telegram,
                                    QStringLiteral("admin-resource-status"));
    const MessengerUserKey userKey(MessengerType::Telegram, QStringLiteral("user-resource-status"));

    const User admin(adminKey,
                     QStringLiteral("Admin"),
                     QStringLiteral("Status"),
                     User::Role::AdminRole);
    const ServerDatabaseError addAdminError = fixture.database->addUser(admin, kTestActor);
    QVERIFY2(!addAdminError, qPrintable(addAdminError.debugString()));
    QVERIFY2(fixture.addUser(userKey, QStringLiteral("User"), QStringLiteral("Status"), &error),
             qPrintable(error));

    const QString onResource = QStringLiteral("Status On");
    const QString tempResource = QStringLiteral("Status Temp");
    const QString offResource = QStringLiteral("Status Off");

    QVERIFY2(fixture.addResource(onResource,
                                 QStringLiteral("Visible resource"),
                                 QStringLiteral("On start"),
                                 &error),
             qPrintable(error));
    QVERIFY2(fixture.addResource(tempResource,
                                 QStringLiteral("Temporary resource"),
                                 QStringLiteral("Temp start"),
                                 &error),
             qPrintable(error));
    QVERIFY2(fixture.addResource(offResource,
                                 QStringLiteral("Hidden resource"),
                                 QStringLiteral("Off start"),
                                 &error),
             qPrintable(error));

    const ServerDatabaseError tempStatusError =
            fixture.database->setResourceStatus(tempResource,
                                                Resource::Status::TemporarilyUnavailable,
                                                adminKey);
    QVERIFY2(!tempStatusError, qPrintable(tempStatusError.debugString()));
    const ServerDatabaseError offStatusError =
            fixture.database->setResourceStatus(offResource, Resource::Status::Off, adminKey);
    QVERIFY2(!offStatusError, qPrintable(offStatusError.debugString()));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(userKey, QStringLiteral("/list_resources"));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userKey,
                                       QStringLiteral("%1 [On]").arg(onResource),
                                       QStringLiteral("list resources shows on"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userKey,
                                       QStringLiteral("%1 [TemporarilyUnavailable]")
                                               .arg(tempResource),
                                       QStringLiteral("list resources shows temp"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectNoMessageContains(*fixture.backend,
                                     userKey,
                                     offResource,
                                     QStringLiteral("list resources hides off"),
                                     &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(userKey, buildOccupyCommand(offResource, 5));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userKey,
                                       QStringLiteral("Resource '%1' was not found.")
                                               .arg(offResource),
                                       QStringLiteral("occupy off resource"),
                                       &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(userKey, buildOccupyCommand(tempResource, 5));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       userKey,
                                       QStringLiteral("temporarily unavailable"),
                                       QStringLiteral("occupy temp unavailable resource"),
                                       &error),
             qPrintable(error));

    fixture.resetMessages();
    openMenuForUser(fixture.backend, userKey);
    QVERIFY2(clickMenuNavigate(fixture.backend,
                               userKey,
                               QLatin1String(MessengerMenuIds::kMenuResourcesHome),
                               QStringLiteral("resources home for hidden resource"),
                               &error),
             qPrintable(error));
    QVERIFY2(clickMenuNavigate(fixture.backend,
                               userKey,
                               QLatin1String(MessengerMenuIds::kMenuResourceCardUser),
                               QStringLiteral("resource card selector for hidden resource"),
                               &error),
             qPrintable(error));
    QString hiddenButtonError;
    const bool hiddenButtonClicked =
            clickMenuButtonByTextFragment(fixture.backend,
                                          userKey,
                                          offResource,
                                          QStringLiteral("user should not see off resource"),
                                          &hiddenButtonError);
    QVERIFY2(!hiddenButtonClicked, qPrintable(hiddenButtonError));

    fixture.resetMessages();
    QVERIFY2(openAdminResourceCardMenuWithChecks(fixture.backend, adminKey, &error),
             qPrintable(error));
    QVERIFY2(clickMenuButtonByTextFragment(fixture.backend,
                                           adminKey,
                                           offResource,
                                           QStringLiteral("admin selects off resource"),
                                           &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("%1 [Off]").arg(offResource),
                                       QStringLiteral("admin sees off status"),
                                       &error),
             qPrintable(error));
}

void MessengerBotTests::resourceGroupVisibilityAndCommandBehavior()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_resource_groups.sqlite"), &error),
             qPrintable(error));

    const MessengerUserKey noGroupUser(MessengerType::Telegram, QStringLiteral("user-no-group"));
    const MessengerUserKey alphaUser(MessengerType::Telegram, QStringLiteral("user-alpha-group"));
    const MessengerUserKey alphaChildUser(MessengerType::Telegram,
                                          QStringLiteral("user-alpha-child-group"));
    const MessengerUserKey rootUser(MessengerType::Telegram, QStringLiteral("user-root-group"));

    QVERIFY2(fixture.addUser(noGroupUser, QStringLiteral("No"), QStringLiteral("Group"), &error),
             qPrintable(error));

    User alpha(alphaUser, QStringLiteral("Alpha"), QStringLiteral("User"));
    alpha.setGroupRoles(QMap<Group, User::Role>({
            { Group(QStringLiteral("alpha")), User::Role::UserRole },
    }));
    const ServerDatabaseError addAlphaError = fixture.database->addUser(alpha, kTestActor);
    QVERIFY2(!addAlphaError, qPrintable(addAlphaError.debugString()));

    User alphaChild(alphaChildUser, QStringLiteral("Alpha"), QStringLiteral("Child"));
    alphaChild.setGroupRoles(QMap<Group, User::Role>({
            { Group(QStringLiteral("alpha/child")), User::Role::UserRole },
    }));
    const ServerDatabaseError addAlphaChildError =
            fixture.database->addUser(alphaChild, kTestActor);
    QVERIFY2(!addAlphaChildError, qPrintable(addAlphaChildError.debugString()));

    User root(rootUser, QStringLiteral("Root"), QStringLiteral("User"));
    root.setGroupRoles(QMap<Group, User::Role>({
            { Group::root(), User::Role::UserRole },
    }));
    const ServerDatabaseError addRootError = fixture.database->addUser(root, kTestActor);
    QVERIFY2(!addRootError, qPrintable(addRootError.debugString()));

    const QString noGroupResource = QStringLiteral("No Group Resource");
    const QString alphaResource = QStringLiteral("Alpha Resource");
    const QString alphaChildResource = QStringLiteral("Alpha Child Resource");
    const QString betaResource = QStringLiteral("Beta Resource");

    Resource rootResource(noGroupResource, QStringLiteral("Root only"), QStringLiteral("Start"));
    rootResource.setGroups(QSet<Group>({ Group::root() }));
    const ServerDatabaseError addRootResourceError =
            fixture.database->addResource(rootResource, kTestActor);
    QVERIFY2(!addRootResourceError, qPrintable(addRootResourceError.debugString()));

    Resource alphaOnly(alphaResource, QStringLiteral("Alpha"), QStringLiteral("Start"));
    alphaOnly.setGroups(QSet<Group>({ Group(QStringLiteral("alpha")) }));
    const ServerDatabaseError addAlphaResourceError =
            fixture.database->addResource(alphaOnly, kTestActor);
    QVERIFY2(!addAlphaResourceError, qPrintable(addAlphaResourceError.debugString()));

    Resource alphaChildOnly(alphaChildResource,
                            QStringLiteral("Alpha child"),
                            QStringLiteral("Start"));
    alphaChildOnly.setGroups(QSet<Group>({ Group(QStringLiteral("alpha/child")) }));
    const ServerDatabaseError addAlphaChildResourceError =
            fixture.database->addResource(alphaChildOnly, kTestActor);
    QVERIFY2(!addAlphaChildResourceError, qPrintable(addAlphaChildResourceError.debugString()));

    Resource betaOnly(betaResource, QStringLiteral("Beta"), QStringLiteral("Start"));
    betaOnly.setGroups(QSet<Group>({ Group(QStringLiteral("beta")) }));
    const ServerDatabaseError addBetaResourceError =
            fixture.database->addResource(betaOnly, kTestActor);
    QVERIFY2(!addBetaResourceError, qPrintable(addBetaResourceError.debugString()));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(noGroupUser, QStringLiteral("/list_resources"));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       noGroupUser,
                                       noGroupResource,
                                       QStringLiteral("no-group user sees no-group resource"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       noGroupUser,
                                       alphaResource,
                                       QStringLiteral("no-group user sees alpha resource"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       noGroupUser,
                                       betaResource,
                                       QStringLiteral("no-group user sees beta resource"),
                                       &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(alphaUser, QStringLiteral("/list_resources"));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       alphaUser,
                                       alphaResource,
                                       QStringLiteral("alpha user sees alpha resource"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       alphaUser,
                                       alphaChildResource,
                                       QStringLiteral("alpha user sees child resource"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectNoMessageContains(*fixture.backend,
                                     alphaUser,
                                     noGroupResource,
                                     QStringLiteral("alpha user hides no-group resource"),
                                     &error),
             qPrintable(error));
    QVERIFY2(expectNoMessageContains(*fixture.backend,
                                     alphaUser,
                                     betaResource,
                                     QStringLiteral("alpha user hides beta resource"),
                                     &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(alphaChildUser, QStringLiteral("/list_resources"));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       alphaChildUser,
                                       alphaChildResource,
                                       QStringLiteral("child user sees child resource"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectNoMessageContains(*fixture.backend,
                                     alphaChildUser,
                                     alphaResource,
                                     QStringLiteral("child user hides parent resource"),
                                     &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(rootUser, QStringLiteral("/list_resources"));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       rootUser,
                                       noGroupResource,
                                       QStringLiteral("root group user sees no-group resource"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       rootUser,
                                       betaResource,
                                       QStringLiteral("root group user sees beta resource"),
                                       &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(alphaUser, buildOccupyCommand(betaResource, 5));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       alphaUser,
                                       QStringLiteral("Resource '%1' was not found.")
                                               .arg(betaResource),
                                       QStringLiteral("alpha user cannot occupy beta resource"),
                                       &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(alphaUser, buildOccupyCommand(alphaChildResource, 5));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       alphaUser,
                                       QStringLiteral("You are now occupying '%1'.")
                                               .arg(alphaChildResource),
                                       QStringLiteral("alpha user can occupy child resource"),
                                       &error),
             qPrintable(error));
}

void MessengerBotTests::groupAdminCreationDefaultsToAdministeredGroups()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_group_admin_defaults.sqlite"),
                                  &error),
             qPrintable(error));

    const MessengerUserKey adminKey(MessengerType::Telegram, QStringLiteral("scoped-admin"));
    User admin(adminKey, QStringLiteral("Scoped"), QStringLiteral("Admin"), User::Role::UserRole);
    const QMap<Group, User::Role> adminRoles{
        { Group(QStringLiteral("alpha")), User::Role::AdminRole },
        { Group(QStringLiteral("beta")), User::Role::AdminRole },
    };
    admin.setGroupRoles(adminRoles);
    const ServerDatabaseError addAdminError = fixture.database->addUser(admin, kTestActor);
    QVERIFY2(!addAdminError, qPrintable(addAdminError.debugString()));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    fixture.sendCommand(adminKey, QStringLiteral("/add_resource \"Scoped Resource\" \"\" \"\""));
    const Resource scopedResource =
            fixture.database->resourceModel()->resourceByName(QStringLiteral("Scoped Resource"));
    QVERIFY(scopedResource.isValid());
    QCOMPARE(scopedResource.groups(),
             QSet<Group>({ Group(QStringLiteral("alpha")), Group(QStringLiteral("beta")) }));

    const MessengerUserKey scopedUserKey(MessengerType::Telegram, QStringLiteral("scoped-user"));
    fixture.sendCommand(adminKey,
                        QStringLiteral("/add_user %1 Scoped User false")
                                .arg(scopedUserKey.toString()));
    const User scopedUser = fixture.database->userModel()->userByKey(scopedUserKey);
    QVERIFY(scopedUser.isValid());
    QCOMPARE(scopedUser.groups(),
             QSet<Group>({ Group(QStringLiteral("alpha")), Group(QStringLiteral("beta")) }));
    QCOMPARE(scopedUser.roleForGroup(Group(QStringLiteral("alpha"))), User::Role::UserRole);
    QCOMPARE(scopedUser.roleForGroup(Group(QStringLiteral("beta"))), User::Role::UserRole);

    fixture.sendCommand(adminKey,
                        QStringLiteral(
                                "/add_resource \"Nested Resource\" \"\" \"\" \"alpha/child\""));
    const Resource nested =
            fixture.database->resourceModel()->resourceByName(QStringLiteral("Nested Resource"));
    QVERIFY(nested.isValid());
    QCOMPARE(nested.groups(), QSet<Group>({ Group(QStringLiteral("alpha/child")) }));

    fixture.sendCommand(adminKey,
                        QStringLiteral("/add_resource \"Forbidden Root\" \"\" \"\" \"\""));
    QVERIFY(!fixture.database->resourceModel()->existsByName(QStringLiteral("Forbidden Root")));

    const MessengerUserKey forbiddenRootKey(MessengerType::Telegram,
                                            QStringLiteral("forbidden-root"));
    fixture.sendCommand(adminKey,
                        QStringLiteral("/add_user %1 Root User false -")
                                .arg(forbiddenRootKey.toString()));
    QVERIFY(!fixture.database->userModel()->exists(forbiddenRootKey));

    fixture.resetMessages();
    QVERIFY2(openAdminResourcesMenuWithChecks(fixture.backend, adminKey, &error),
             qPrintable(error));
    QVERIFY2(clickMenuNavigate(fixture.backend,
                               adminKey,
                               QLatin1String(MessengerMenuIds::kMenuAdminResourceAdd),
                               QStringLiteral("scoped resource add navigation"),
                               &error),
             qPrintable(error));
    fixture.sendCommand(adminKey, QStringLiteral("Scoped Menu Resource"));
    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("rw:skip"),
                            QStringLiteral("scoped resource description"),
                            &error),
             qPrintable(error));
    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("rw:skip"),
                            QStringLiteral("scoped resource start message"),
                            &error),
             qPrintable(error));
    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("rw:skip"),
                            QStringLiteral("scoped resource default groups"),
                            &error),
             qPrintable(error));
    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("rw:confirm"),
                            QStringLiteral("scoped resource confirm"),
                            &error),
             qPrintable(error));
    const Resource menuResource = fixture.database->resourceModel()->resourceByName(
            QStringLiteral("Scoped Menu Resource"));
    QVERIFY(menuResource.isValid());
    QCOMPARE(menuResource.groups(),
             QSet<Group>({ Group(QStringLiteral("alpha")), Group(QStringLiteral("beta")) }));

    const MessengerUserKey menuUserKey(MessengerType::Telegram, QStringLiteral("scoped-menu-user"));
    fixture.resetMessages();
    QVERIFY2(openAdminUsersMenuWithChecks(fixture.backend, adminKey, &error), qPrintable(error));
    QVERIFY2(clickMenuNavigate(fixture.backend,
                               adminKey,
                               QLatin1String(MessengerMenuIds::kMenuAdminUserAdd),
                               QStringLiteral("scoped user add navigation"),
                               &error),
             qPrintable(error));
    fixture.sendCommand(adminKey, menuUserKey.toString());
    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("uw:role:user"),
                            QStringLiteral("scoped user role"),
                            &error),
             qPrintable(error));
    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("uw:groups:default"),
                            QStringLiteral("scoped user default groups"),
                            &error),
             qPrintable(error));
    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("uw:confirm"),
                            QStringLiteral("scoped user confirm"),
                            &error),
             qPrintable(error));
    const User menuUser = fixture.database->userModel()->userByKey(menuUserKey);
    QVERIFY(menuUser.isValid());
    QCOMPARE(menuUser.groups(),
             QSet<Group>({ Group(QStringLiteral("alpha")), Group(QStringLiteral("beta")) }));
}

void MessengerBotTests::adminUserAddCommandAndMenu()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_admin_add_user.sqlite"), &error),
             qPrintable(error));

    const MessengerUserKey adminKey(MessengerType::Telegram, QStringLiteral("admin-add-user"));
    const User admin(adminKey,
                     QStringLiteral("Admin"),
                     QStringLiteral("User"),
                     User::Role::AdminRole);
    const ServerDatabaseError addAdminError = fixture.database->addUser(admin, kTestActor);
    QVERIFY2(!addAdminError, qPrintable(addAdminError.debugString()));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    const MessengerUserKey commandUser(MessengerType::Telegram, QStringLiteral("cmd-user-add"));
    fixture.resetMessages();
    fixture.sendCommand(adminKey,
                        QStringLiteral("/add_user %1 %2 %3 admin")
                                .arg(commandUser.toString(),
                                     QStringLiteral("Command"),
                                     QStringLiteral("User")));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("User added"),
                                       QStringLiteral("add user command"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Command User"),
                                       QStringLiteral("add user command"),
                                       &error),
             qPrintable(error));

    const UserModel *userModel = fixture.database->userModel();
    QVERIFY(userModel != nullptr);
    const User added = userModel->userByKey(commandUser);
    QVERIFY(added.isValid());
    QCOMPARE(added.firstName(), QStringLiteral("Command"));
    QCOMPARE(added.lastName(), QStringLiteral("User"));
    QVERIFY(added.isAdmin());

    const MessengerUserKey menuUser(MessengerType::Telegram, QStringLiteral("menu-user-add"));
    fixture.backend->addEmptyProfileUserId(menuUser.userId());
    const QString menuFirst = QStringLiteral("Menu");
    const QString menuLast = QStringLiteral("Created");

    fixture.resetMessages();
    QVERIFY2(openAdminUsersMenuWithChecks(fixture.backend, adminKey, &error), qPrintable(error));
    QVERIFY2(clickMenuNavigate(fixture.backend,
                               adminKey,
                               QLatin1String(MessengerMenuIds::kMenuAdminUserAdd),
                               QStringLiteral("admin user add navigation"),
                               &error),
             qPrintable(error));

    QString previousText;
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Send user identifier"),
                                       QStringLiteral("user add id step"),
                                       &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("user add id step"),
                                    &previousText,
                                    &error),
             qPrintable(error));

    fixture.sendCommand(adminKey, menuUser.toString());
    QString currentText;
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Send first name"),
                                       QStringLiteral("user add first step"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("uw:skip")),
                              QStringLiteral("user add first step"),
                              &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("user add first step"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText, "Menu text did not change after user identifier step.");
    previousText = currentText;

    fixture.sendCommand(adminKey, menuFirst);
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Send last name"),
                                       QStringLiteral("user add last step"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("uw:skip")),
                              QStringLiteral("user add last step"),
                              &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("user add last step"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText, "Menu text did not change after first name step.");
    previousText = currentText;

    fixture.sendCommand(adminKey, menuLast);
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Choose a role"),
                                       QStringLiteral("user add role step"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("uw:role:user")),
                              QStringLiteral("user add role step"),
                              &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("uw:role:admin")),
                              QStringLiteral("user add role step"),
                              &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("user add role step"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText, "Menu text did not change after last name step.");
    previousText = currentText;

    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("uw:role:admin"),
                            QStringLiteral("user add role select"),
                            &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Send group roles"),
                                       QStringLiteral("user add groups"),
                                       &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("user add groups"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText, "Menu text did not change after role selection.");
    previousText = currentText;

    fixture.sendCommand(adminKey, QStringLiteral("-"));

    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Confirm adding user"),
                                       QStringLiteral("user add review"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Role: admin"),
                                       QStringLiteral("user add review"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("uw:confirm")),
                              QStringLiteral("user add review"),
                              &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("user add review"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText, "Menu text did not change after groups input.");
    previousText = currentText;

    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("uw:confirm"),
                            QStringLiteral("user add confirm"),
                            &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("User management"),
                                       QStringLiteral("user add finish"),
                                       &error),
             qPrintable(error));
    QString finalText;
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("user add finish"),
                                    &finalText,
                                    &error),
             qPrintable(error));
    QVERIFY2(finalText != previousText, "Menu text did not change after confirming user add.");

    const User menuAdded = userModel->userByKey(menuUser);
    QVERIFY(menuAdded.isValid());
    QCOMPARE(menuAdded.firstName(), menuFirst);
    QCOMPARE(menuAdded.lastName(), menuLast);
    QVERIFY(menuAdded.isAdmin());
}

void MessengerBotTests::adminUserDeleteCommandAndMenu()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_admin_delete_user.sqlite"), &error),
             qPrintable(error));

    const MessengerUserKey adminKey(MessengerType::Telegram, QStringLiteral("admin-delete-user"));
    const User admin(adminKey,
                     QStringLiteral("Admin"),
                     QStringLiteral("User"),
                     User::Role::AdminRole);
    const ServerDatabaseError addAdminError = fixture.database->addUser(admin, kTestActor);
    QVERIFY2(!addAdminError, qPrintable(addAdminError.debugString()));

    const MessengerUserKey commandUser(MessengerType::Telegram, QStringLiteral("cmd-user-delete"));
    const MessengerUserKey menuUser(MessengerType::Telegram, QStringLiteral("menu-user-delete"));
    QVERIFY2(fixture.addUser(commandUser,
                             QStringLiteral("Command"),
                             QStringLiteral("Delete"),
                             &error),
             qPrintable(error));
    QVERIFY2(fixture.addUser(menuUser, QStringLiteral("Menu"), QStringLiteral("Delete"), &error),
             qPrintable(error));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(adminKey, QStringLiteral("/delete_user %1").arg(commandUser.toString()));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("User deleted"),
                                       QStringLiteral("delete user command"),
                                       &error),
             qPrintable(error));

    const UserModel *userModel = fixture.database->userModel();
    QVERIFY(userModel != nullptr);
    QVERIFY(!userModel->userByKey(commandUser).isValid());

    fixture.resetMessages();
    QVERIFY2(openAdminUserCardMenuWithChecks(fixture.backend, adminKey, &error), qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Select a user"),
                                       QStringLiteral("user card selector"),
                                       &error),
             qPrintable(error));
    QString previousText;
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("user card selector"),
                                    &previousText,
                                    &error),
             qPrintable(error));

    QVERIFY2(clickMenuButtonByTextFragment(fixture.backend,
                                           adminKey,
                                           QStringLiteral("Menu Delete"),
                                           QStringLiteral("select user"),
                                           &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("User:"),
                                       QStringLiteral("user card"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("uc:delete")),
                              QStringLiteral("user card delete"),
                              &error),
             qPrintable(error));
    QString currentText;
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("user card"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText, "Menu text did not change after selecting a user.");
    previousText = currentText;

    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("uc:delete"),
                            QStringLiteral("user delete"),
                            &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Confirm deleting user"),
                                       QStringLiteral("user delete confirm"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("delu:yes")),
                              QStringLiteral("user delete confirm"),
                              &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("user delete confirm"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText,
             "Menu text did not change after opening user delete confirmation.");
    previousText = currentText;

    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("delu:yes"),
                            QStringLiteral("user delete accept"),
                            &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("User management"),
                                       QStringLiteral("user delete finish"),
                                       &error),
             qPrintable(error));
    QString finalText;
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("user delete finish"),
                                    &finalText,
                                    &error),
             qPrintable(error));
    QVERIFY2(finalText != previousText, "Menu text did not change after confirming user delete.");

    QVERIFY(!userModel->userByKey(menuUser).isValid());
}

void MessengerBotTests::adminUserUpdateCommandAndMenu()
{
    TestFixture fixture;
    QString error;
    QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_admin_update_user.sqlite"), &error),
             qPrintable(error));

    const MessengerUserKey adminKey(MessengerType::Telegram, QStringLiteral("admin-update-user"));
    const User admin(adminKey,
                     QStringLiteral("Admin"),
                     QStringLiteral("User"),
                     User::Role::AdminRole);
    const ServerDatabaseError addAdminError = fixture.database->addUser(admin, kTestActor);
    QVERIFY2(!addAdminError, qPrintable(addAdminError.debugString()));

    const MessengerUserKey commandUser(MessengerType::Telegram, QStringLiteral("cmd-user-update"));
    const MessengerUserKey menuUser(MessengerType::Telegram, QStringLiteral("menu-user-update"));
    QVERIFY2(fixture.addUser(commandUser, QStringLiteral("Old"), QStringLiteral("Name"), &error),
             qPrintable(error));
    QVERIFY2(fixture.addUser(menuUser, QStringLiteral("Menu"), QStringLiteral("User"), &error),
             qPrintable(error));

    QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
             qPrintable(error));

    fixture.resetMessages();
    fixture.sendCommand(adminKey,
                        QStringLiteral("/update_user %1 %2 %3 admin")
                                .arg(commandUser.toString(),
                                     QStringLiteral("Command"),
                                     QStringLiteral("Updated")));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("User updated"),
                                       QStringLiteral("update user command"),
                                       &error),
             qPrintable(error));

    const UserModel *userModel = fixture.database->userModel();
    QVERIFY(userModel != nullptr);
    const User updated = userModel->userByKey(commandUser);
    QVERIFY(updated.isValid());
    QCOMPARE(updated.firstName(), QStringLiteral("Command"));
    QCOMPARE(updated.lastName(), QStringLiteral("Updated"));
    QVERIFY(updated.isAdmin());

    const QString menuFirst = QStringLiteral("MenuUpdated");
    const QString menuLast = QStringLiteral("UserUpdated");

    fixture.resetMessages();
    QVERIFY2(openAdminUserCardMenuWithChecks(fixture.backend, adminKey, &error), qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Select a user"),
                                       QStringLiteral("user edit selector"),
                                       &error),
             qPrintable(error));
    QString previousText;
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("user edit selector"),
                                    &previousText,
                                    &error),
             qPrintable(error));

    QVERIFY2(clickMenuButtonByTextFragment(fixture.backend,
                                           adminKey,
                                           QStringLiteral("Menu User"),
                                           QStringLiteral("select user for edit"),
                                           &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("User:"),
                                       QStringLiteral("user card for edit"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("uc:edit")),
                              QStringLiteral("user card edit"),
                              &error),
             qPrintable(error));
    QString currentText;
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("user card for edit"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText,
             "Menu text did not change after selecting a user for edit.");
    previousText = currentText;

    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("uc:edit"),
                            QStringLiteral("user edit open"),
                            &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("User details"),
                                       QStringLiteral("user edit review"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("uw:edit:first")),
                              QStringLiteral("user edit review"),
                              &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("user edit review"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText,
             "Menu text did not change after opening user edit review.");
    previousText = currentText;

    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("uw:edit:first"),
                            QStringLiteral("user edit first"),
                            &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Send new first name"),
                                       QStringLiteral("user edit first step"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("uw:skip")),
                              QStringLiteral("user edit first step"),
                              &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("user edit first step"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText,
             "Menu text did not change after selecting first name edit.");
    previousText = currentText;

    fixture.sendCommand(adminKey, menuFirst);
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("User details"),
                                       QStringLiteral("user edit review"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       menuFirst,
                                       QStringLiteral("user edit review"),
                                       &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("user edit review"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText, "Menu text did not change after updating first name.");
    previousText = currentText;

    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("uw:edit:last"),
                            QStringLiteral("user edit last"),
                            &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Send new last name"),
                                       QStringLiteral("user edit last step"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("uw:skip")),
                              QStringLiteral("user edit last step"),
                              &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("user edit last step"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText,
             "Menu text did not change after selecting last name edit.");
    previousText = currentText;

    fixture.sendCommand(adminKey, menuLast);
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("User details"),
                                       QStringLiteral("user edit review after last"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       menuLast,
                                       QStringLiteral("user edit review after last"),
                                       &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("user edit review after last"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText, "Menu text did not change after updating last name.");
    previousText = currentText;

    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("uw:edit:role"),
                            QStringLiteral("user edit role"),
                            &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Choose a role"),
                                       QStringLiteral("user edit role step"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectMenuButton(*fixture.backend,
                              adminKey,
                              menuEventPayload(QStringLiteral("uw:role:admin")),
                              QStringLiteral("user edit role step"),
                              &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("user edit role step"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText, "Menu text did not change after selecting role edit.");
    previousText = currentText;

    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("uw:role:admin"),
                            QStringLiteral("user edit role select"),
                            &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("User details"),
                                       QStringLiteral("user edit review after role"),
                                       &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("Role: admin"),
                                       QStringLiteral("user edit review after role"),
                                       &error),
             qPrintable(error));
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("user edit review after role"),
                                    &currentText,
                                    &error),
             qPrintable(error));
    QVERIFY2(currentText != previousText, "Menu text did not change after updating role.");
    previousText = currentText;

    QVERIFY2(clickMenuEvent(fixture.backend,
                            adminKey,
                            QStringLiteral("uw:confirm"),
                            QStringLiteral("user edit confirm"),
                            &error),
             qPrintable(error));
    QVERIFY2(expectLastMessageContains(*fixture.backend,
                                       adminKey,
                                       QStringLiteral("User management"),
                                       QStringLiteral("user edit finish"),
                                       &error),
             qPrintable(error));
    QString finalText;
    QVERIFY2(captureLastMessageText(*fixture.backend,
                                    adminKey,
                                    QStringLiteral("user edit finish"),
                                    &finalText,
                                    &error),
             qPrintable(error));
    QVERIFY2(finalText != previousText, "Menu text did not change after confirming user update.");

    const User menuUpdated = userModel->userByKey(menuUser);
    QVERIFY(menuUpdated.isValid());
    QCOMPARE(menuUpdated.firstName(), menuFirst);
    QCOMPARE(menuUpdated.lastName(), menuLast);
    QVERIFY(menuUpdated.isAdmin());
}

void MessengerBotTests::pendingAccessUserAutoBlockCommandAndMenu()
{
    {
        TestFixture fixture;
        QString error;
        QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_pending_access_command.sqlite"),
                                      &error),
                 qPrintable(error));

        const MessengerUserKey adminKey(MessengerType::Telegram,
                                        QStringLiteral("messenger_limiter"));
        const User admin(adminKey,
                         QStringLiteral("Admin"),
                         QStringLiteral("Only"),
                         User::Role::AdminRole);
        const ServerDatabaseError addAdminError = fixture.database->addUser(admin, kTestActor);
        QVERIFY2(!addAdminError, qPrintable(addAdminError.debugString()));

        QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
                 qPrintable(error));

        const MessengerUserKey unknownKey(MessengerType::Telegram, QStringLiteral("spam-command"));
        QVERIFY2(spamUntilBlacklisted(&fixture, unknownKey, 20, &error), qPrintable(error));

        const BlacklistModel *blacklistModel = fixture.database->blacklistModel();
        QVERIFY(blacklistModel != nullptr);
        QVERIFY(blacklistModel->exists(unknownKey));

        const PendingAccessUserModel *pendingAccessModel =
                fixture.database->pendingAccessUserModel();
        QVERIFY(pendingAccessModel != nullptr);
        QVERIFY(pendingAccessModel->exists(unknownKey));

        const int countBefore = messageCountForChat(*fixture.backend, unknownKey.userId());
        fixture.sendCommand(unknownKey, QStringLiteral("after-block"));
        const int countAfter = messageCountForChat(*fixture.backend, unknownKey.userId());
        QCOMPARE(countAfter, countBefore);

        fixture.resetMessages();
        fixture.sendCommand(adminKey, QStringLiteral("/pending_access"));
        QVERIFY2(expectLastMessageContains(*fixture.backend,
                                           adminKey,
                                           unknownKey.toString(),
                                           QStringLiteral("pending access list"),
                                           &error),
                 qPrintable(error));
        QVERIFY2(expectLastMessageContains(*fixture.backend,
                                           adminKey,
                                           QStringLiteral("blacklisted"),
                                           QStringLiteral("pending access list"),
                                           &error),
                 qPrintable(error));

        fixture.resetMessages();
        fixture.sendCommand(adminKey, QStringLiteral("/blacklist"));
        QVERIFY2(expectLastMessageContains(*fixture.backend,
                                           adminKey,
                                           unknownKey.toString(),
                                           QStringLiteral("blacklist list"),
                                           &error),
                 qPrintable(error));

        fixture.resetMessages();
        fixture.sendCommand(adminKey, QStringLiteral("/unban_user %1").arg(unknownKey.toString()));
        QVERIFY2(expectLastMessageContains(*fixture.backend,
                                           adminKey,
                                           QStringLiteral("unblocked"),
                                           QStringLiteral("unban command"),
                                           &error),
                 qPrintable(error));
        QVERIFY(!blacklistModel->exists(unknownKey));

        fixture.resetMessages();
        fixture.sendCommand(adminKey,
                            QStringLiteral("/add_user %1 %2 %3")
                                    .arg(unknownKey.toString(),
                                         QStringLiteral("Recovered"),
                                         QStringLiteral("User")));
        QVERIFY2(expectLastMessageContains(*fixture.backend,
                                           adminKey,
                                           QStringLiteral("User added"),
                                           QStringLiteral("add user command"),
                                           &error),
                 qPrintable(error));

        const UserModel *userModel = fixture.database->userModel();
        QVERIFY(userModel != nullptr);
        const User added = userModel->userByKey(unknownKey);
        QVERIFY(added.isValid());
        QCOMPARE(added.firstName(), QStringLiteral("Recovered"));
        QCOMPARE(added.lastName(), QStringLiteral("User"));
        QVERIFY(!blacklistModel->exists(unknownKey));
        QVERIFY(!pendingAccessModel->exists(unknownKey));
    }

    {
        TestFixture fixture;
        QString error;
        QVERIFY2(fixture.initDatabase(QStringLiteral("messenger_bot_pending_access_menu.sqlite"),
                                      &error),
                 qPrintable(error));

        const MessengerUserKey adminKey(MessengerType::Telegram,
                                        QStringLiteral("messenger_limiter"));
        const User admin(adminKey,
                         QStringLiteral("Admin"),
                         QStringLiteral("Only"),
                         User::Role::AdminRole);
        const ServerDatabaseError addAdminError = fixture.database->addUser(admin, kTestActor);
        QVERIFY2(!addAdminError, qPrintable(addAdminError.debugString()));

        QVERIFY2(fixture.setupBot(MessengerType::Telegram, QStringLiteral("token"), &error),
                 qPrintable(error));

        const MessengerUserKey unknownKey(MessengerType::Telegram, QStringLiteral("spam-menu"));
        fixture.backend->addEmptyProfileUserId(unknownKey.userId());
        QVERIFY2(spamUntilBlacklisted(&fixture, unknownKey, 20, &error), qPrintable(error));

        const BlacklistModel *blacklistModel = fixture.database->blacklistModel();
        QVERIFY(blacklistModel != nullptr);
        QVERIFY(blacklistModel->exists(unknownKey));

        const PendingAccessUserModel *pendingAccessModel =
                fixture.database->pendingAccessUserModel();
        QVERIFY(pendingAccessModel != nullptr);
        QVERIFY(pendingAccessModel->exists(unknownKey));

        fixture.resetMessages();
        QVERIFY2(openAdminBlacklistMenuWithChecks(fixture.backend, adminKey, &error),
                 qPrintable(error));
        QVERIFY2(expectLastMessageContains(*fixture.backend,
                                           adminKey,
                                           QStringLiteral("Blacklist"),
                                           QStringLiteral("blacklist menu"),
                                           &error),
                 qPrintable(error));
        QVERIFY2(expectLastMessageContains(*fixture.backend,
                                           adminKey,
                                           unknownKey.toString(),
                                           QStringLiteral("blacklist menu"),
                                           &error),
                 qPrintable(error));
        QString previousText;
        QVERIFY2(captureLastMessageText(*fixture.backend,
                                        adminKey,
                                        QStringLiteral("blacklist menu"),
                                        &previousText,
                                        &error),
                 qPrintable(error));

        QVERIFY2(clickMenuNavigate(fixture.backend,
                                   adminKey,
                                   QLatin1String(MessengerMenuIds::kMenuAdminHome),
                                   QStringLiteral("blacklist back to admin home"),
                                   &error),
                 qPrintable(error));
        QString currentText;
        QVERIFY2(captureLastMessageText(*fixture.backend,
                                        adminKey,
                                        QStringLiteral("admin home after blacklist"),
                                        &currentText,
                                        &error),
                 qPrintable(error));
        QVERIFY2(currentText != previousText,
                 "Menu text did not change after leaving blacklist menu.");
        previousText = currentText;

        QVERIFY2(clickMenuNavigate(fixture.backend,
                                   adminKey,
                                   QLatin1String(MessengerMenuIds::kMenuAdminPendingAccess),
                                   QStringLiteral("pending access menu navigation"),
                                   &error),
                 qPrintable(error));
        QVERIFY2(expectLastMessageContains(*fixture.backend,
                                           adminKey,
                                           QStringLiteral("Users awaiting access activation"),
                                           QStringLiteral("pending access menu"),
                                           &error),
                 qPrintable(error));
        QVERIFY2(expectLastMessageContains(*fixture.backend,
                                           adminKey,
                                           unknownKey.toString(),
                                           QStringLiteral("pending access menu"),
                                           &error),
                 qPrintable(error));
        QVERIFY2(expectLastMessageContains(*fixture.backend,
                                           adminKey,
                                           QStringLiteral("blacklisted"),
                                           QStringLiteral("pending access menu"),
                                           &error),
                 qPrintable(error));
        QVERIFY2(expectMenuButton(*fixture.backend,
                                  adminKey,
                                  menuNavigatePayload(
                                          QLatin1String(MessengerMenuIds::kMenuAdminUserAdd)),
                                  QStringLiteral("pending access add button"),
                                  &error),
                 qPrintable(error));
        QVERIFY2(captureLastMessageText(*fixture.backend,
                                        adminKey,
                                        QStringLiteral("pending access menu"),
                                        &currentText,
                                        &error),
                 qPrintable(error));
        QVERIFY2(currentText != previousText,
                 "Menu text did not change after opening pending access menu.");
        previousText = currentText;

        QVERIFY2(clickMenuNavigate(fixture.backend,
                                   adminKey,
                                   QLatin1String(MessengerMenuIds::kMenuAdminUserAdd),
                                   QStringLiteral("register user navigation"),
                                   &error),
                 qPrintable(error));
        QVERIFY2(expectLastMessageContains(*fixture.backend,
                                           adminKey,
                                           QStringLiteral("Send user identifier"),
                                           QStringLiteral("user add id step"),
                                           &error),
                 qPrintable(error));
        QVERIFY2(expectNoMenuButton(*fixture.backend,
                                    adminKey,
                                    menuEventPayload(QStringLiteral("uw:skip")),
                                    QStringLiteral("user add id step"),
                                    &error),
                 qPrintable(error));
        QVERIFY2(captureLastMessageText(*fixture.backend,
                                        adminKey,
                                        QStringLiteral("user add id step"),
                                        &currentText,
                                        &error),
                 qPrintable(error));
        QVERIFY2(currentText != previousText,
                 "Menu text did not change after entering user add flow.");
        previousText = currentText;

        fixture.sendCommand(adminKey, unknownKey.toString());
        QVERIFY2(expectLastMessageContains(*fixture.backend,
                                           adminKey,
                                           QStringLiteral("Send first name"),
                                           QStringLiteral("user add first step"),
                                           &error),
                 qPrintable(error));
        QVERIFY2(expectMenuButton(*fixture.backend,
                                  adminKey,
                                  menuEventPayload(QStringLiteral("uw:skip")),
                                  QStringLiteral("user add first step"),
                                  &error),
                 qPrintable(error));
        QVERIFY2(captureLastMessageText(*fixture.backend,
                                        adminKey,
                                        QStringLiteral("user add first step"),
                                        &currentText,
                                        &error),
                 qPrintable(error));
        QVERIFY2(currentText != previousText,
                 "Menu text did not change after providing user identifier.");
        previousText = currentText;

        const QString firstName = QStringLiteral("Recovered");
        const QString lastName = QStringLiteral("User");

        fixture.sendCommand(adminKey, firstName);
        QVERIFY2(expectLastMessageContains(*fixture.backend,
                                           adminKey,
                                           QStringLiteral("Send last name"),
                                           QStringLiteral("user add last step"),
                                           &error),
                 qPrintable(error));
        QVERIFY2(expectMenuButton(*fixture.backend,
                                  adminKey,
                                  menuEventPayload(QStringLiteral("uw:skip")),
                                  QStringLiteral("user add last step"),
                                  &error),
                 qPrintable(error));
        QVERIFY2(captureLastMessageText(*fixture.backend,
                                        adminKey,
                                        QStringLiteral("user add last step"),
                                        &currentText,
                                        &error),
                 qPrintable(error));
        QVERIFY2(currentText != previousText,
                 "Menu text did not change after providing first name.");
        previousText = currentText;

        fixture.sendCommand(adminKey, lastName);
        QVERIFY2(expectLastMessageContains(*fixture.backend,
                                           adminKey,
                                           QStringLiteral("Choose a role"),
                                           QStringLiteral("user add role step"),
                                           &error),
                 qPrintable(error));
        QVERIFY2(expectMenuButton(*fixture.backend,
                                  adminKey,
                                  menuEventPayload(QStringLiteral("uw:role:user")),
                                  QStringLiteral("user add role step"),
                                  &error),
                 qPrintable(error));
        QVERIFY2(expectMenuButton(*fixture.backend,
                                  adminKey,
                                  menuEventPayload(QStringLiteral("uw:role:admin")),
                                  QStringLiteral("user add role step"),
                                  &error),
                 qPrintable(error));
        QVERIFY2(captureLastMessageText(*fixture.backend,
                                        adminKey,
                                        QStringLiteral("user add role step"),
                                        &currentText,
                                        &error),
                 qPrintable(error));
        QVERIFY2(currentText != previousText,
                 "Menu text did not change after providing last name.");
        previousText = currentText;

        QVERIFY2(clickMenuEvent(fixture.backend,
                                adminKey,
                                QStringLiteral("uw:role:user"),
                                QStringLiteral("user add role select"),
                                &error),
                 qPrintable(error));
        QVERIFY2(expectLastMessageContains(*fixture.backend,
                                           adminKey,
                                           QStringLiteral("Send group roles"),
                                           QStringLiteral("user add groups"),
                                           &error),
                 qPrintable(error));
        QVERIFY2(captureLastMessageText(*fixture.backend,
                                        adminKey,
                                        QStringLiteral("user add groups"),
                                        &currentText,
                                        &error),
                 qPrintable(error));
        QVERIFY2(currentText != previousText, "Menu text did not change after selecting role.");
        previousText = currentText;

        fixture.sendCommand(adminKey, QStringLiteral("-"));

        QVERIFY2(expectLastMessageContains(*fixture.backend,
                                           adminKey,
                                           QStringLiteral("Confirm adding user"),
                                           QStringLiteral("user add review"),
                                           &error),
                 qPrintable(error));
        QVERIFY2(expectMenuButton(*fixture.backend,
                                  adminKey,
                                  menuEventPayload(QStringLiteral("uw:confirm")),
                                  QStringLiteral("user add review"),
                                  &error),
                 qPrintable(error));
        QVERIFY2(captureLastMessageText(*fixture.backend,
                                        adminKey,
                                        QStringLiteral("user add review"),
                                        &currentText,
                                        &error),
                 qPrintable(error));
        QVERIFY2(currentText != previousText, "Menu text did not change after groups input.");
        previousText = currentText;

        QVERIFY2(clickMenuEvent(fixture.backend,
                                adminKey,
                                QStringLiteral("uw:confirm"),
                                QStringLiteral("user add confirm"),
                                &error),
                 qPrintable(error));
        QVERIFY2(expectLastMessageContains(*fixture.backend,
                                           adminKey,
                                           QStringLiteral("User management"),
                                           QStringLiteral("user add finish"),
                                           &error),
                 qPrintable(error));
        QString finalText;
        QVERIFY2(captureLastMessageText(*fixture.backend,
                                        adminKey,
                                        QStringLiteral("user add finish"),
                                        &finalText,
                                        &error),
                 qPrintable(error));
        QVERIFY2(finalText != previousText, "Menu text did not change after confirming user add.");

        const UserModel *userModel = fixture.database->userModel();
        QVERIFY(userModel != nullptr);
        const User added = userModel->userByKey(unknownKey);
        QVERIFY(added.isValid());
        QCOMPARE(added.firstName(), firstName);
        QCOMPARE(added.lastName(), lastName);
        QVERIFY(!blacklistModel->exists(unknownKey));
        QVERIFY(!pendingAccessModel->exists(unknownKey));
    }
}

BOOKING_RESOURCES_REGISTER_TEST(MessengerBotTests)

#include "MessengerBotTests.moc"
