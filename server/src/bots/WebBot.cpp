#include "WebBot.h"

#include "MessengerBot.h"
#include "MessengerCommandCompletions.h"
#include "MessengerCommandAudience.h"
#include "MessengerCommandManager.h"
#include "MessengerCommandParser.h"
#include "MessengerAbstractCommand.h"
#include "BookingResourcesTranslator.h"

#include <QFile>
#include <QTimer>
#include <QFileInfo>
#include <QDateTime>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QVector>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcServerWeb, "booking.server.web")

namespace {
constexpr int kFlushIntervalMs = 50;
constexpr int kMaxPendingFrames = 512;
constexpr qint64 kMaxInlineFileBytes = 2 * 1024 * 1024;

QJsonArray parserArgumentsToJson(const MessengerCommandParser &parser)
{
    QJsonArray result;
    const QVector<MessengerCommandParser::ArgumentInfo> arguments = parser.arguments();
    for (const MessengerCommandParser::ArgumentInfo &argument : arguments) {
        QJsonObject item;
        item.insert(QStringLiteral("name"), argument.name);
        item.insert(QStringLiteral("optional"), argument.optional);
        item.insert(QStringLiteral("greedy"), argument.greedy);
        item.insert(QStringLiteral("allow_empty"), argument.allowEmpty);
        result.append(item);
    }
    return result;
}

} // namespace

WebBot::WebBot(const QString &sharedMemoryKey, QObject *parent)
    : AbstractBot(parent)
    , m_server(sharedMemoryKey, this)
    , m_translator(nullptr)
    , m_droppedFrameCount(0)
{
    m_flushTimer.setInterval(kFlushIntervalMs);
    QObject::connect(&m_flushTimer, &QTimer::timeout, this, &WebBot::flushPendingFrames);
    QObject::connect(&m_server, &SharedMemoryServer::requestReceived, this, &WebBot::handleRequest);
    QObject::connect(&m_server, &SharedMemoryServer::errorOccurred, this, &WebBot::errorOccurred);
}

WebBot::~WebBot()
{
    stop();
}

MessengerType WebBot::messenger() const
{
    return MessengerType::Web;
}

AbstractBot::Capabilities WebBot::capabilities() const
{
    return AbstractBot::CapabilityFlag::MarkdownFormatting
           | AbstractBot::CapabilityFlag::HtmlFormatting
           | AbstractBot::CapabilityFlag::InlineKeyboards;
}

bool WebBot::isRunning() const
{
    return m_server.isRunning();
}

bool WebBot::fetchUserProfile(const QString &userId,
                              AbstractBot::UserProfile *profileOut,
                              QString *errorDescription)
{
    const QString trimmedUserId = userId.trimmed();
    if (trimmedUserId.isEmpty()) {
        if (errorDescription)
            *errorDescription = tr("User identifier is empty.");
        return false;
    }

    if (!isRunning()) {
        if (errorDescription)
            *errorDescription = tr("Web IPC server is not running.");
        return false;
    }

    const QString requestId = SharedMemoryProtocol::createRequestId();
    QJsonObject payload;
    payload.insert(QStringLiteral("user_id"), trimmedUserId);

    SharedMemoryProtocol::Frame request;
    request.type = QStringLiteral("user_profile_request");
    request.requestId = requestId;
    request.payload = payload;

    QJsonObject response;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    const QMetaObject::Connection connection =
            QObject::connect(this,
                             &WebBot::userProfileResponseReceived,
                             &loop,
                             [&](const QString &receivedRequestId,
                                 const QJsonObject &receivedPayload) {
                                 if (receivedRequestId != requestId)
                                     return;
                                 response = receivedPayload;
                                 loop.quit();
                             });

    queueFrame(request);
    timeout.start(5000);
    loop.exec();
    QObject::disconnect(connection);

    if (response.isEmpty()) {
        if (errorDescription)
            *errorDescription = tr("Timed out waiting for web user profile.");
        return false;
    }

    if (!response.value(QStringLiteral("ok")).toBool()) {
        if (errorDescription) {
            *errorDescription =
                    response.value(QStringLiteral("error")).toString(tr("Web user was not found."));
        }
        return false;
    }

    if (!profileOut)
        return true;

    profileOut->userId =
            response.value(QStringLiteral("user_id")).toString(trimmedUserId).trimmed();
    profileOut->username = response.value(QStringLiteral("username")).toString().trimmed();
    profileOut->firstName = response.value(QStringLiteral("first_name")).toString().trimmed();
    profileOut->lastName = response.value(QStringLiteral("last_name")).toString().trimmed();
    profileOut->languageCode = response.value(QStringLiteral("language_code")).toString().trimmed();
    profileOut->isBot = response.value(QStringLiteral("is_bot")).toBool(false);
    if (profileOut->username.isEmpty())
        profileOut->username = QStringLiteral("web:%1").arg(profileOut->userId);
    return !profileOut->userId.isEmpty();
}

void WebBot::setTranslator(BookingResourcesTranslator *translator)
{
    m_translator = translator;
}

void WebBot::setSharedMemorySecret(const QString &secret)
{
    m_server.setAuthenticationSecret(secret.toUtf8());
}

void WebBot::start()
{
    if (!m_server.start()) {
        emit errorOccurred(m_server.lastError());
        return;
    }
    flushPendingFrames();
}

void WebBot::stop()
{
    m_flushTimer.stop();
    m_pendingFrames.clear();
    m_server.stop();
}

void WebBot::startShutdown()
{
    flushPendingFrames();
    if (!m_pendingFrames.isEmpty() && !m_flushTimer.isActive())
        m_flushTimer.start();
}

bool WebBot::waitShutdown(int msecs)
{
    const bool drained = waitForPendingFrames(msecs);
    if (!drained && msecs > 0) {
        qCWarning(lcServerWeb).nospace() << "Pending web IPC frames did not flush within " << msecs
                                         << " ms (pending=" << m_pendingFrames.size() << ").";
    }

    stop();
    return drained;
}

void WebBot::sendMessage(const AbstractBot::ChatAddress &chat,
                         const QString &text,
                         const AbstractBot::MessageOptions &options)
{
    sendOutgoingFrame(QStringLiteral("bot_message"),
                      chat,
                      SharedMemoryProtocol::createRequestId(),
                      text,
                      options);
}

void WebBot::sendFile(const AbstractBot::ChatAddress &chat,
                      const QString &filePath,
                      const QString &caption,
                      AbstractBot::MessageFormat captionFormat)
{
    AbstractBot::MessageOptions options;
    options.format = captionFormat;

    QJsonObject extra;
    extra.insert(QStringLiteral("file_path"), filePath);
    const QFileInfo fileInfo(filePath);
    if (fileInfo.exists() && fileInfo.isFile()) {
        extra.insert(QStringLiteral("file_name"), fileInfo.fileName());
        extra.insert(QStringLiteral("file_size"), static_cast<double>(fileInfo.size()));
        if (fileInfo.size() <= kMaxInlineFileBytes) {
            QFile file(fileInfo.absoluteFilePath());
            if (file.open(QIODevice::ReadOnly)) {
                extra.insert(QStringLiteral("file_content_base64"),
                             QString::fromLatin1(file.readAll().toBase64()));
            }
        }
    }
    sendOutgoingFrame(QStringLiteral("bot_file"),
                      chat,
                      SharedMemoryProtocol::createRequestId(),
                      caption,
                      options,
                      extra);
}

void WebBot::editMessage(const AbstractBot::ChatAddress &chat,
                         const QString &messageId,
                         const QString &text,
                         const AbstractBot::MessageOptions &options)
{
    sendOutgoingFrame(QStringLiteral("bot_edit"), chat, messageId, text, options);
}

void WebBot::sendAuthorizationResponse(const QString &requestId, const QJsonObject &payload)
{
    m_server.sendResponse(requestId, QStringLiteral("authorize_user_result"), payload);
}

void WebBot::sendUserSettingsResponse(const QString &requestId, const QJsonObject &payload)
{
    m_server.sendResponse(requestId, QStringLiteral("user_settings_result"), payload);
}

void WebBot::sendCommandCacheInvalidated(const MessengerUserKey &user)
{
    if (user.messenger() != MessengerType::Web || user.userId().trimmed().isEmpty())
        return;

    QJsonObject payload;
    payload.insert(QStringLiteral("chat_id"), user.userId().trimmed());
    payload.insert(QStringLiteral("timestamp"),
                   QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));

    SharedMemoryProtocol::Frame frame;
    frame.type = QStringLiteral("command_cache_invalidated");
    frame.requestId = SharedMemoryProtocol::createRequestId();
    frame.payload = payload;
    queueFrame(frame);
}

void WebBot::sendCommandCacheInvalidated()
{
    QJsonObject payload;
    payload.insert(QStringLiteral("global"), true);
    payload.insert(QStringLiteral("timestamp"),
                   QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));

    SharedMemoryProtocol::Frame frame;
    frame.type = QStringLiteral("command_cache_invalidated");
    frame.requestId = SharedMemoryProtocol::createRequestId();
    frame.payload = payload;
    queueFrame(frame);
}

void WebBot::handleRequest(const SharedMemoryProtocol::Frame &frame)
{
    const QString type = frame.type.trimmed().toLower();
    if (type == QLatin1String("message")) {
        AbstractBot::IncomingMessage message;
        message.chat = chatFromPayload(frame.payload);
        message.sender = userFromPayload(frame.payload);
        message.languageCode = frame.payload.value(QStringLiteral("language_code")).toString();
        message.text = frame.payload.value(QStringLiteral("text")).toString();
        message.messageId = frame.payload.value(QStringLiteral("message_id")).toString();
        if (message.messageId.isEmpty())
            message.messageId = frame.requestId;
        emit messageReceived(message);
        return;
    }

    if (type == QLatin1String("callback")) {
        AbstractBot::CallbackQuery query;
        query.id = frame.requestId;
        query.chat = chatFromPayload(frame.payload);
        query.sender = userFromPayload(frame.payload);
        query.languageCode = frame.payload.value(QStringLiteral("language_code")).toString();
        query.data = frame.payload.value(QStringLiteral("data")).toString();
        query.originalMessageId =
                frame.payload.value(QStringLiteral("original_message_id")).toString();
        emit callbackQueryReceived(query);
        return;
    }

    if (type == QLatin1String("user_profile_result")) {
        emit userProfileResponseReceived(frame.requestId, frame.payload);
        return;
    }

    if (type == QLatin1String("authorize_user")) {
        if (receivers(SIGNAL(userAuthorizationRequested(QString, QJsonObject))) > 0) {
            emit userAuthorizationRequested(frame.requestId, frame.payload);
        } else {
            QJsonObject response;
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"),
                            tr("Authorization handler is not configured."));
            sendAuthorizationResponse(frame.requestId, response);
        }
        return;
    }

    if (type == QLatin1String("user_settings_get") || type == QLatin1String("user_settings_set")) {
        if (receivers(SIGNAL(userSettingsRequested(QString, QString, QJsonObject))) > 0) {
            emit userSettingsRequested(frame.requestId, type, frame.payload);
        } else {
            QJsonObject response;
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"),
                            tr("User settings handler is not configured."));
            sendUserSettingsResponse(frame.requestId, response);
        }
        return;
    }

    if (type == QLatin1String("commands")) {
        const MessengerUserKey
                user(MessengerType::Web,
                     frame.payload.value(QStringLiteral("user_id")).toString().trimmed());
        QJsonObject response;
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("locale"),
                        frame.payload.value(QStringLiteral("locale")).toString());
        response.insert(QStringLiteral("commands"),
                        commandListToJson(frame.payload.value(QStringLiteral("locale")).toString(),
                                          user));
        m_server.sendResponse(frame.requestId, QStringLiteral("commands_result"), response);
        return;
    }

    if (type == QLatin1String("ping")) {
        QJsonObject response;
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("message"), QStringLiteral("pong"));
        m_server.sendResponse(frame.requestId, QStringLiteral("pong"), response);
        return;
    }

    sendResult(frame.requestId,
               QStringLiteral("error"),
               tr("Unsupported web IPC request type: %1").arg(type));
}

AbstractBot::ChatAddress WebBot::chatFromPayload(const QJsonObject &payload) const
{
    AbstractBot::ChatAddress chat;
    chat.messenger = MessengerType::Web;
    chat.chatId = payload.value(QStringLiteral("chat_id")).toString().trimmed();
    if (chat.chatId.isEmpty())
        chat.chatId = payload.value(QStringLiteral("user_id")).toString().trimmed();
    chat.threadId = payload.value(QStringLiteral("thread_id")).toString().trimmed();
    return chat;
}

AbstractBot::UserAddress WebBot::userFromPayload(const QJsonObject &payload) const
{
    AbstractBot::UserAddress user;
    user.userId = payload.value(QStringLiteral("user_id")).toString().trimmed();
    user.username = payload.value(QStringLiteral("username")).toString().trimmed();
    user.firstName = payload.value(QStringLiteral("first_name")).toString().trimmed();
    user.lastName = payload.value(QStringLiteral("last_name")).toString().trimmed();
    if (user.username.isEmpty())
        user.username = QStringLiteral("web:%1").arg(user.userId);
    return user;
}

QJsonObject WebBot::messageOptionsToJson(const AbstractBot::MessageOptions &options) const
{
    QJsonObject result;
    result.insert(QStringLiteral("format"), static_cast<int>(options.format));
    result.insert(QStringLiteral("reply_to_message_id"), options.replyToMessageId);
    result.insert(QStringLiteral("thread_id"), options.threadId);

    QJsonArray rows;
    for (const QVector<AbstractBot::MessageOptions::InlineKeyboardButton> &row :
         options.inlineKeyboard.rows) {
        QJsonArray renderedRow;
        for (const AbstractBot::MessageOptions::InlineKeyboardButton &button : row) {
            if (!button.isValid())
                continue;
            QJsonObject item;
            item.insert(QStringLiteral("text"), button.text);
            item.insert(QStringLiteral("callback_data"), button.callbackData);
            renderedRow.append(item);
        }
        if (!renderedRow.isEmpty())
            rows.append(renderedRow);
    }
    result.insert(QStringLiteral("inline_keyboard"), rows);
    return result;
}

QString WebBot::commandHelpText(const MessengerAbstractCommand *command) const
{
    if (!command)
        return QString();

    QStringList lines;
    const QString description = command->parser().description().trimmed();
    if (!description.isEmpty())
        lines << description;

    const QString usage = command->parser().usageSignature().trimmed();
    if (!usage.isEmpty())
        lines << MessengerAbstractCommand::tr("Usage: %1").arg(usage);

    const QStringList details = command->details();
    for (const QString &detail : details) {
        if (!detail.trimmed().isEmpty())
            lines << detail;
    }

    QStringList cleanExamples;
    if (command->parser().argumentCount() > 1) {
        const QStringList examples = command->examples();
        for (const QString &example : examples) {
            if (!example.trimmed().isEmpty())
                cleanExamples << example;
        }
    }

    if (!cleanExamples.isEmpty()) {
        lines << MessengerAbstractCommand::tr("Examples:");
        lines.append(cleanExamples);
    }

    if (lines.isEmpty())
        lines << command->parser().helpLine();

    return lines.join(QStringLiteral("\n")).trimmed();
}

QJsonArray WebBot::commandListToJson(const QString &locale, const MessengerUserKey &user) const
{
    BookingResourcesTranslator::ScopedLocaleOverride localeGuard =
            m_translator ? m_translator->useLocale(locale)
                         : BookingResourcesTranslator::ScopedLocaleOverride();
    Q_UNUSED(localeGuard);

    QJsonArray result;
    const MessengerCommandManager *manager = commandManager();
    if (!manager)
        return result;
    manager->refreshCommandTranslations();

    const QVector<MessengerAbstractCommand *> commands = manager->commands();
    QVector<const MessengerAbstractCommand *> availableCommands;
    QStringList availableCommandNames;
    const MessengerBot *ownerBot = manager->bot();
    const bool canCompleteArguments = ownerBot && ownerBot->hasActiveAccess(user);
    AbstractBot::IncomingMessage availabilityMessage;
    const AbstractBot::IncomingMessage *availabilityContext = nullptr;
    if (user.isValid()) {
        availabilityMessage.chat.messenger = user.messenger();
        availabilityMessage.chat.chatId = user.userId();
        availabilityMessage.sender.userId = user.userId();
        availabilityContext = &availabilityMessage;
    }

    for (const MessengerAbstractCommand *command : commands) {
        if (!command || !command->isEnabled(availabilityContext))
            continue;
        const MessengerCommandAudience audience = command->audience();
        if (audience == MessengerCommandAudience::AdminsOnly) {
            if (!ownerBot || !ownerBot->isAdminUser(user))
                continue;
        }
        if (audience == MessengerCommandAudience::ServerAdminsOnly) {
            if (!ownerBot || !ownerBot->canManageServerSettings(user))
                continue;
        }
        availableCommands.append(command);
        availableCommandNames.append(command->command());
    }

    for (const MessengerAbstractCommand *command : availableCommands) {
        const MessengerCommandParser &parser = command->parser();
        QJsonObject item;
        item.insert(QStringLiteral("name"), command->command());
        item.insert(QStringLiteral("description"), parser.description());
        item.insert(QStringLiteral("usage"), parser.usageSignature());
        item.insert(QStringLiteral("help"), commandHelpText(command));
        item.insert(QStringLiteral("has_arguments"), parser.argumentCount() > 0);
        item.insert(QStringLiteral("arguments"), parserArgumentsToJson(parser));
        item.insert(QStringLiteral("argument_completions"),
                    canCompleteArguments
                            ? MessengerCommandCompletions::argumentCompletionsToJson(
                                      command->command(),
                                      manager,
                                      user,
                                      availableCommandNames)
                            : QJsonArray());
        result.append(item);
    }
    return result;
}

void WebBot::sendResult(const QString &requestId, const QString &status, const QString &message)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("status"), status);
    if (!message.trimmed().isEmpty())
        payload.insert(QStringLiteral("message"), message);
    payload.insert(QStringLiteral("timestamp"),
                   QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    m_server.sendResponse(requestId, QStringLiteral("result"), payload);
}

void WebBot::queueFrame(const SharedMemoryProtocol::Frame &frame)
{
    if (m_pendingFrames.isEmpty() && m_server.isRunning()) {
        const SharedMemoryEndpoint::SendStatus status = m_server.sendFrameStatus(frame);
        if (status == SharedMemoryEndpoint::SendStatus::Success) {
            emitMessageSentForFrame(frame);
            return;
        }
        if (status == SharedMemoryEndpoint::SendStatus::Failed) {
            ++m_droppedFrameCount;
            qCWarning(lcServerWeb).nospace()
                    << "Dropping web IPC frame after permanent send failure (type=" << frame.type
                    << ", dropped=" << m_droppedFrameCount << ").";
            return;
        }
    }

    if (m_pendingFrames.size() >= kMaxPendingFrames) {
        ++m_droppedFrameCount;
        qCWarning(lcServerWeb).nospace()
                << "Dropping web IPC frame because the outgoing queue is full (type=" << frame.type
                << ", dropped=" << m_droppedFrameCount << ").";
        return;
    }

    m_pendingFrames.enqueue(frame);
    if (m_server.isRunning() && !m_flushTimer.isActive())
        m_flushTimer.start();
}

void WebBot::emitMessageSentForFrame(const SharedMemoryProtocol::Frame &frame)
{
    if (frame.type != QLatin1String("bot_message"))
        return;

    const QString messageId =
            frame.payload.value(QStringLiteral("message_id")).toString().trimmed();
    if (messageId.isEmpty())
        return;

    AbstractBot::ChatAddress chat;
    chat.messenger = MessengerType::Web;
    chat.chatId = frame.payload.value(QStringLiteral("chat_id")).toString().trimmed();
    chat.threadId = frame.payload.value(QStringLiteral("thread_id")).toString().trimmed();
    if (chat.isValid())
        emit messageSent(chat, messageId);
}

bool WebBot::waitForPendingFrames(int timeoutMs)
{
    flushPendingFrames();
    if (m_pendingFrames.isEmpty())
        return true;
    if (timeoutMs == 0)
        return false;

    QEventLoop loop;
    QTimer retryTimer;
    QTimer timeoutTimer;
    bool timedOut = false;

    retryTimer.setInterval(m_flushTimer.interval());
    QObject::connect(&retryTimer, &QTimer::timeout, &loop, [this, &loop]() {
        flushPendingFrames();
        if (m_pendingFrames.isEmpty())
            loop.quit();
    });

    if (timeoutMs > 0) {
        timeoutTimer.setSingleShot(true);
        QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, [&loop, &timedOut]() {
            timedOut = true;
            loop.quit();
        });
        timeoutTimer.start(timeoutMs);
    }

    retryTimer.start();
    loop.exec();

    return !timedOut && m_pendingFrames.isEmpty();
}

void WebBot::flushPendingFrames()
{
    while (!m_pendingFrames.isEmpty()) {
        const SharedMemoryProtocol::Frame frame = m_pendingFrames.head();
        const SharedMemoryEndpoint::SendStatus status = m_server.sendFrameStatus(frame);
        if (status == SharedMemoryEndpoint::SendStatus::Busy)
            break;
        if (status == SharedMemoryEndpoint::SendStatus::Failed) {
            ++m_droppedFrameCount;
            qCWarning(lcServerWeb).nospace()
                    << "Dropping web IPC frame after permanent send failure (type=" << frame.type
                    << ", dropped=" << m_droppedFrameCount << ").";
            m_pendingFrames.dequeue();
            continue;
        }

        emitMessageSentForFrame(frame);

        m_pendingFrames.dequeue();
    }

    if (m_pendingFrames.isEmpty())
        m_flushTimer.stop();
}

void WebBot::sendOutgoingFrame(const QString &type,
                               const AbstractBot::ChatAddress &chat,
                               const QString &messageId,
                               const QString &text,
                               const AbstractBot::MessageOptions &options,
                               const QJsonObject &extraPayload)
{
    if (chat.messenger != MessengerType::Web || !chat.isValid())
        return;

    QJsonObject payload = extraPayload;
    payload.insert(QStringLiteral("chat_id"), chat.chatId);
    payload.insert(QStringLiteral("thread_id"), chat.threadId);
    payload.insert(QStringLiteral("message_id"), messageId);
    payload.insert(QStringLiteral("text"), text);
    payload.insert(QStringLiteral("options"), messageOptionsToJson(options));
    payload.insert(QStringLiteral("timestamp"),
                   QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));

    SharedMemoryProtocol::Frame frame;
    frame.type = type;
    frame.requestId = SharedMemoryProtocol::createRequestId();
    frame.payload = payload;
    queueFrame(frame);
}

void WebBot::syncCommandsWithPlatform()
{
    SharedMemoryProtocol::Frame frame;
    frame.type = QStringLiteral("command_list");
    frame.requestId = SharedMemoryProtocol::createRequestId();
    QJsonObject payload;
    payload.insert(QStringLiteral("commands"), commandListToJson());
    frame.payload = payload;
    queueFrame(frame);
}
