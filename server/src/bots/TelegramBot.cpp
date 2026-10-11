#include "TelegramBot.h"

#include <QUrl>
#include <QSet>
#include <QFile>
#include <QHash>
#include <QTimer>
#include <QVector>
#include <QVariant>
#include <QPointer>
#include <QFileInfo>
#include <QUrlQuery>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QNetworkRequest>
#include <QLoggingCategory>
#include <QNetworkAccessManager>

#include "MessengerAbstractCommand.h"
#include "MessengerCommandManager.h"

Q_LOGGING_CATEGORY(lcTelegramBot, "booking.server.telegram")

namespace {

constexpr int kTelegramCallbackDataLimit = 64;

struct TelegramApiResponseInfo
{
    bool hasPayload = false;
    bool ok = false;
    int errorCode = 0;
    QString description;
    QString parseError;
};

TelegramApiResponseInfo parseTelegramApiResponse(const QByteArray &payload)
{
    TelegramApiResponseInfo info;

    if (payload.isEmpty()) {
        info.parseError = QStringLiteral("Empty response body");
        return info;
    }

    info.hasPayload = true;

    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError) {
        info.parseError = error.errorString();
        return info;
    }

    if (!doc.isObject()) {
        info.parseError = QStringLiteral("Unexpected JSON root type");
        return info;
    }

    const QJsonObject root = doc.object();
    info.ok = root.value(QStringLiteral("ok")).toBool();
    info.errorCode = root.value(QStringLiteral("error_code")).toInt();
    info.description = root.value(QStringLiteral("description")).toString();

    return info;
}

QString describeTelegramResponseIssue(const TelegramApiResponseInfo &info,
                                      const QByteArray &payload)
{
    if (!info.parseError.isEmpty()) {
        return QStringLiteral("Telegram response parse error: %1").arg(info.parseError);
    }

    if (info.hasPayload && info.ok)
        return QString();

    if (info.hasPayload && !info.ok) {
        if (info.errorCode != 0 && !info.description.isEmpty()) {
            return QStringLiteral("Telegram error %1: %2")
                    .arg(info.errorCode)
                    .arg(info.description);
        }
        if (!info.description.isEmpty())
            return QStringLiteral("Telegram API error: %1").arg(info.description);
        if (info.errorCode != 0)
            return QStringLiteral("Telegram error %1").arg(info.errorCode);
        return QStringLiteral("Telegram API returned ok=false");
    }

    if (payload.isEmpty())
        return QStringLiteral("Empty Telegram response body");

    QString snippet = QString::fromUtf8(payload.left(256)).trimmed();
    if (payload.size() > 256)
        snippet.append(QStringLiteral("…"));

    if (!snippet.isEmpty())
        return QStringLiteral("Telegram response: %1").arg(snippet);

    return QStringLiteral("Unexpected Telegram response.");
}

QString buildMethodErrorMessage(const QString &method, QNetworkReply *reply, const QString &detail)
{
    QString message = QStringLiteral("%1 error").arg(method);
    const QVariant statusAttr = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (statusAttr.isValid()) {
        const int statusCode = statusAttr.toInt();
        if (statusCode > 0)
            message += QStringLiteral(" (HTTP %1)").arg(statusCode);
    }

    if (!detail.isEmpty())
        message += QStringLiteral(": %1").arg(detail);

    return message;
}

QByteArray buildFormUrlEncodedBody(const QUrlQuery &query)
{
    const QList<QPair<QString, QString>> items = query.queryItems(QUrl::FullyDecoded);
    QByteArray body;
    bool first = true;
    for (const QPair<QString, QString> &item : items) {
        if (!first)
            body.append('&');
        first = false;

        body.append(QUrl::toPercentEncoding(item.first));
        body.append('=');
        body.append(QUrl::toPercentEncoding(item.second));
    }
    return body;
}

} // namespace

/**
 * @brief Internal data holder for TelegramBot (pimpl idiom).
 *
 * This structure stores networking state and timers, while all behavior
 * is implemented in TelegramBot methods.
 */
class TelegramBotPrivate
{
public:
    TelegramBotPrivate(const QString &token);

    inline static constexpr char kTelegramApiBaseUrl[] = "https://api.telegram.org/bot";

    const QString token;

    QNetworkAccessManager network;
    QTimer timer;
    qint64 lastUpdateId = 0;
    bool running = false;
    bool requestInProgress = false;
    bool startupSpamFilterPending = true;
    int errorBackoffMs = 1000;
    QPointer<QNetworkReply> activeReply;
    QSet<QNetworkReply *> pendingRequests;
};

TelegramBotPrivate::TelegramBotPrivate(const QString &token)
    : token(token)
{
    // do nothing
}

TelegramBot::TelegramBot(const QString &token, QObject *parent)
    : AbstractBot(parent)
    , d(new TelegramBotPrivate(token))
{
    if (d->token.isEmpty()) {
        QMetaObject::invokeMethod(
                this,
                [this]() {
                    qCCritical(lcTelegramBot).nospace() << TelegramBot::staticMetaObject.className()
                                                        << " create: token is empty!";
                    emit errorOccurred(QStringLiteral("Telegram bot token is empty!"));
                },
                Qt::QueuedConnection);
    }

    // Timer is used only for delayed retries after errors.
    d->timer.setSingleShot(true);

    // Route QTimer timeout signal through the TelegramBot (QObject).
    connect(&d->timer, &QTimer::timeout, this, [this]() { onTimeout(); });
}

TelegramBot::~TelegramBot()
{
    TelegramBotPrivate *state = d;
    d = nullptr;
    delete state;
}

MessengerType TelegramBot::messenger() const
{
    return MessengerType::Telegram;
}

AbstractBot::Capabilities TelegramBot::capabilities() const
{
    return CapabilityFlag::MarkdownFormatting | CapabilityFlag::HtmlFormatting
           | CapabilityFlag::InlineKeyboards;
}

bool TelegramBot::isRunning() const
{
    return d->running;
}

bool TelegramBot::fetchUserProfile(const QString &userId,
                                   AbstractBot::UserProfile *profileOut,
                                   QString *errorDescription)
{
    if (d->token.isEmpty())
        return false;

    const QString trimmedId = userId.trimmed();
    if (trimmedId.isEmpty()) {
        if (errorDescription)
            *errorDescription = QStringLiteral("Telegram user identifier is empty");
        return false;
    }

    QUrl url = buildMethodUrl(QStringLiteral("getChat"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("chat_id"), trimmedId);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    bool timedOut = false;
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        loop.quit();
    });
    QNetworkReply *reply = d->network.get(request);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    timeout.start(10000);
    loop.exec();

    if (timedOut) {
        reply->abort();
        reply->deleteLater();
        if (errorDescription)
            *errorDescription = QStringLiteral("Telegram user lookup request timed out");
        return false;
    }

    timeout.stop();

    const QByteArray payload = reply->readAll();
    const TelegramApiResponseInfo apiInfo = parseTelegramApiResponse(payload);

    if (reply->error() != QNetworkReply::NoError) {
        QString detail = reply->errorString();
        const QString apiDetail = describeTelegramResponseIssue(apiInfo, payload);
        if (!apiDetail.isEmpty())
            detail += QStringLiteral(" - %1").arg(apiDetail);
        if (errorDescription)
            *errorDescription = detail;
        reply->deleteLater();
        return false;
    }

    reply->deleteLater();

    if (!apiInfo.parseError.isEmpty()) {
        if (errorDescription)
            *errorDescription = apiInfo.parseError;
        return false;
    }

    if (!apiInfo.ok) {
        if (errorDescription)
            *errorDescription = describeTelegramResponseIssue(apiInfo, payload);
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (errorDescription)
            *errorDescription = QStringLiteral("Telegram profile parse error: %1")
                                        .arg(parseError.errorString());
        return false;
    }

    if (!doc.isObject()) {
        if (errorDescription)
            *errorDescription = QStringLiteral("Unexpected Telegram profile response shape");
        return false;
    }

    const QJsonObject root = doc.object();
    const QJsonObject resultObj = root.value(QStringLiteral("result")).toObject();
    if (resultObj.isEmpty()) {
        if (errorDescription)
            *errorDescription = QStringLiteral("Telegram profile response did not include result");
        return false;
    }

    const QString chatType = resultObj.value(QStringLiteral("type")).toString();
    if (!chatType.isEmpty() && chatType != QStringLiteral("private")) {
        if (errorDescription)
            *errorDescription = QStringLiteral("Telegram chat type %1 is not a user").arg(chatType);
        return false;
    }

    const qint64 resolvedId = static_cast<qint64>(resultObj.value(QStringLiteral("id")).toDouble());
    if (resolvedId <= 0) {
        if (errorDescription)
            *errorDescription = QStringLiteral("Telegram profile response contained invalid id");
        return false;
    }

    AbstractBot::UserProfile profile;
    profile.userId = QString::number(resolvedId);
    profile.username = resultObj.value(QStringLiteral("username")).toString();
    profile.firstName = resultObj.value(QStringLiteral("first_name")).toString();
    profile.lastName = resultObj.value(QStringLiteral("last_name")).toString();
    profile.languageCode = resultObj.value(QStringLiteral("language_code")).toString();
    profile.isBot = resultObj.value(QStringLiteral("is_bot")).toBool();

    if (profileOut)
        *profileOut = profile;

    return profile.isValid();
}

void TelegramBot::start()
{
    if (d->token.isEmpty())
        return;

    if (d->running)
        return;

    d->running = true;
    d->errorBackoffMs = 1000;
    if (d->lastUpdateId == 0)
        d->startupSpamFilterPending = true;

    // Start first long-poll request immediately.
    if (!d->requestInProgress)
        fetchUpdates();
}

void TelegramBot::stop()
{
    d->running = false;
    d->timer.stop();

    // Abort active long-poll request so shutdown doesn't wait on it.
    if (QNetworkReply *reply = d->activeReply.data()) {
        d->activeReply.clear();
        d->requestInProgress = false;
        reply->abort();
    }
}

void TelegramBot::startShutdown()
{
    stop();
}

bool TelegramBot::waitShutdown(int msecs)
{
    return waitForPendingRequests(msecs);
}

void TelegramBot::sendMessage(const ChatAddress &chat,
                              const QString &text,
                              const MessageOptions &options)
{
    if (d->token.isEmpty())
        return;

    if (!chat.isValid()) {
        emit errorOccurred(QStringLiteral("Cannot send Telegram message: chat id is empty."));
        return;
    }

    QUrl url = buildMethodUrl(QStringLiteral("sendMessage"));

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("chat_id"), chat.chatId);
    query.addQueryItem(QStringLiteral("text"), text);

    QString modeStr;
    switch (options.format) {
    case MessageFormat::Markdown:
        modeStr = QStringLiteral("Markdown");
        break;
    case MessageFormat::Html:
        modeStr = QStringLiteral("HTML");
        break;
    case MessageFormat::PlainText:
    default:
        break;
    }

    if (!modeStr.isEmpty())
        query.addQueryItem(QStringLiteral("parse_mode"), modeStr);

    if (!options.replyToMessageId.isEmpty()) {
        query.addQueryItem(QStringLiteral("reply_to_message_id"), options.replyToMessageId);
    }

    if (!options.threadId.isEmpty()) {
        query.addQueryItem(QStringLiteral("message_thread_id"), options.threadId);
    }

    if (!options.inlineKeyboard.isEmpty()) {
        QJsonArray keyboardArray;
        for (const QVector<MessageOptions::InlineKeyboardButton> &row :
             options.inlineKeyboard.rows) {
            QJsonArray rowArray;
            for (const MessageOptions::InlineKeyboardButton &button : row) {
                if (!button.isValid())
                    continue;
                if (button.callbackData.size() > kTelegramCallbackDataLimit) {
                    qCWarning(lcTelegramBot).nospace()
                            << "Skipping Telegram inline button \"" << button.text
                            << "\" – payload exceeds " << kTelegramCallbackDataLimit << " bytes.";
                    continue;
                }
                QJsonObject buttonObject;
                buttonObject.insert(QStringLiteral("text"), button.text);
                buttonObject.insert(QStringLiteral("callback_data"), button.callbackData);
                rowArray.append(buttonObject);
            }
            if (!rowArray.isEmpty())
                keyboardArray.append(rowArray);
        }

        if (!keyboardArray.isEmpty()) {
            QJsonObject replyMarkup;
            replyMarkup.insert(QStringLiteral("inline_keyboard"), keyboardArray);
            const QJsonDocument markupDoc(replyMarkup);
            query.addQueryItem(QStringLiteral("reply_markup"),
                               QString::fromUtf8(markupDoc.toJson(QJsonDocument::Compact)));
        }
    }

    const QByteArray body = buildFormUrlEncodedBody(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));

    const ChatAddress sentChat = chat;
    const bool trackSentId = !options.inlineKeyboard.isEmpty();
    QNetworkReply *reply = d->network.post(request, body);
    trackPendingRequest(reply);
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, sentChat, trackSentId]() {
        if (!reply)
            return;
        untrackPendingRequest(reply);

        const QByteArray payload = reply->readAll();
        const TelegramApiResponseInfo apiInfo = parseTelegramApiResponse(payload);

        const auto emitDetailedError = [this, reply](const QString &detail) {
            if (detail.isEmpty())
                return;
            emit errorOccurred(
                    buildMethodErrorMessage(QStringLiteral("sendMessage"), reply, detail));
        };

        if (reply->error() != QNetworkReply::NoError) {
            QString detail = reply->errorString();
            const QString apiDetail = describeTelegramResponseIssue(apiInfo, payload);
            if (!apiDetail.isEmpty())
                detail += QStringLiteral(" - %1").arg(apiDetail);
            emitDetailedError(detail);
            reply->deleteLater();
            return;
        }

        if (!apiInfo.parseError.isEmpty() || !apiInfo.ok) {
            emitDetailedError(describeTelegramResponseIssue(apiInfo, payload));
            reply->deleteLater();
            return;
        }

        {
            QString sentMessageId;
            QJsonParseError error{};
            const QJsonDocument doc = QJsonDocument::fromJson(payload, &error);
            if (error.error == QJsonParseError::NoError && doc.isObject()) {
                const QJsonObject root = doc.object();
                const QJsonObject resultObj = root.value(QStringLiteral("result")).toObject();
                const QJsonValue idValue = resultObj.value(QStringLiteral("message_id"));
                qint64 messageId = 0;
                if (idValue.isDouble()) {
                    messageId = static_cast<qint64>(idValue.toDouble());
                } else if (idValue.isString()) {
                    messageId = idValue.toString().trimmed().toLongLong();
                }
                if (messageId > 0) {
                    sentMessageId = QString::number(messageId);
                }
            }

            if (!sentMessageId.isEmpty()) {
                if (trackSentId)
                    emit messageSent(sentChat, sentMessageId);
            }
        }

        reply->deleteLater();
    });
}

void TelegramBot::sendFile(const ChatAddress &chat,
                           const QString &filePath,
                           const QString &caption,
                           AbstractBot::MessageFormat captionFormat)
{
    if (d->token.isEmpty())
        return;

    if (!chat.isValid()) {
        emit errorOccurred(QStringLiteral("Cannot send Telegram file: chat id is empty."));
        return;
    }

    QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        emit errorOccurred(
                QStringLiteral("Cannot send Telegram file: file not found: %1").arg(filePath));
        return;
    }

    QFile *file = new QFile(filePath);
    if (!file->open(QIODevice::ReadOnly)) {
        emit errorOccurred(
                QStringLiteral("Cannot send Telegram file: cannot open file: %1").arg(filePath));
        delete file;
        return;
    }

    QUrl url = buildMethodUrl(QStringLiteral("sendDocument"));

    QHttpMultiPart *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    auto addTextPart = [&](const QString &name, const QString &value) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"%1\"").arg(name)));
        part.setBody(value.toUtf8());
        multi->append(part);
    };

    addTextPart(QStringLiteral("chat_id"), chat.chatId);
    if (!chat.threadId.trimmed().isEmpty()) {
        addTextPart(QStringLiteral("message_thread_id"), chat.threadId);
    }
    const QString trimmedCaption = caption.trimmed();
    if (!trimmedCaption.isEmpty()) {
        QString modeStr;
        switch (captionFormat) {
        case MessageFormat::Markdown:
            modeStr = QStringLiteral("Markdown");
            break;
        case MessageFormat::Html:
            modeStr = QStringLiteral("HTML");
            break;
        case MessageFormat::PlainText:
        default:
            break;
        }
        if (!modeStr.isEmpty())
            addTextPart(QStringLiteral("parse_mode"), modeStr);

        addTextPart(QStringLiteral("caption"), caption);
    }

    QHttpPart filePart;
    const QString fileName =
            info.fileName().isEmpty() ? QStringLiteral("report.csv") : info.fileName();
    const bool removeAfterSend =
            fileName.startsWith(QStringLiteral("stats_report_"))
            && info.suffix().compare(QStringLiteral("csv"), Qt::CaseInsensitive) == 0;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"document\"; filename=\"%1\"")
                                        .arg(fileName)));
    filePart.setBodyDevice(file);
    file->setParent(multi);
    multi->append(filePart);

    QNetworkRequest request(url);
    QNetworkReply *reply = d->network.post(request, multi);
    multi->setParent(reply);
    trackPendingRequest(reply);
    QObject::connect(reply,
                     &QNetworkReply::finished,
                     this,
                     [this, reply, file, filePath, removeAfterSend]() {
                         if (!reply)
                             return;
                         untrackPendingRequest(reply);

                         const QByteArray payload = reply->readAll();
                         const TelegramApiResponseInfo apiInfo = parseTelegramApiResponse(payload);
                         const auto cleanupSourceFile = [file, filePath, removeAfterSend]() {
                             if (!removeAfterSend)
                                 return;
                             if (file && file->isOpen())
                                 file->close();
                             if (QFile::exists(filePath) && !QFile::remove(filePath)) {
                                 qCWarning(lcTelegramBot).nospace()
                                         << "Failed to remove temporary report file: " << filePath;
                             }
                         };

                         const auto emitDetailedError = [this, reply](const QString &detail) {
                             if (detail.isEmpty())
                                 return;
                             emit errorOccurred(
                                     buildMethodErrorMessage(QStringLiteral("sendDocument"),
                                                             reply,
                                                             detail));
                         };

                         if (reply->error() != QNetworkReply::NoError) {
                             QString detail = reply->errorString();
                             const QString apiDetail =
                                     describeTelegramResponseIssue(apiInfo, payload);
                             if (!apiDetail.isEmpty())
                                 detail += QStringLiteral(" - %1").arg(apiDetail);
                             emitDetailedError(detail);
                             cleanupSourceFile();
                             reply->deleteLater();
                             return;
                         }

                         if (!apiInfo.parseError.isEmpty() || !apiInfo.ok) {
                             emitDetailedError(describeTelegramResponseIssue(apiInfo, payload));
                             cleanupSourceFile();
                             reply->deleteLater();
                             return;
                         }

                         cleanupSourceFile();
                         reply->deleteLater();
                     });
}

void TelegramBot::editMessage(const ChatAddress &chat,
                              const QString &messageId,
                              const QString &text,
                              const MessageOptions &options)
{
    if (d->token.isEmpty())
        return;

    if (!chat.isValid()) {
        emit errorOccurred(QStringLiteral("Cannot edit Telegram message: chat id is empty."));
        return;
    }

    const QString trimmedMessageId = messageId.trimmed();
    if (trimmedMessageId.isEmpty()) {
        emit errorOccurred(QStringLiteral("Cannot edit Telegram message: message id is empty."));
        return;
    }

    QUrl url = buildMethodUrl(QStringLiteral("editMessageText"));

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("chat_id"), chat.chatId);
    query.addQueryItem(QStringLiteral("message_id"), trimmedMessageId);
    query.addQueryItem(QStringLiteral("text"), text);

    QString modeStr;
    switch (options.format) {
    case MessageFormat::Markdown:
        modeStr = QStringLiteral("Markdown");
        break;
    case MessageFormat::Html:
        modeStr = QStringLiteral("HTML");
        break;
    case MessageFormat::PlainText:
    default:
        break;
    }

    if (!modeStr.isEmpty())
        query.addQueryItem(QStringLiteral("parse_mode"), modeStr);

    // Telegram keeps the previous inline keyboard if reply_markup is omitted.
    // For menu flows, we often need to explicitly clear the keyboard (e.g. when switching to
    // input mode or after confirming an action).
    QJsonArray keyboardArray;
    for (const QVector<MessageOptions::InlineKeyboardButton> &row : options.inlineKeyboard.rows) {
        QJsonArray rowArray;
        for (const MessageOptions::InlineKeyboardButton &button : row) {
            if (!button.isValid())
                continue;
            if (button.callbackData.size() > kTelegramCallbackDataLimit) {
                qCWarning(lcTelegramBot).nospace()
                        << "Skipping Telegram inline button \"" << button.text
                        << "\" – payload exceeds " << kTelegramCallbackDataLimit << " bytes.";
                continue;
            }
            QJsonObject buttonObject;
            buttonObject.insert(QStringLiteral("text"), button.text);
            buttonObject.insert(QStringLiteral("callback_data"), button.callbackData);
            rowArray.append(buttonObject);
        }
        if (!rowArray.isEmpty())
            keyboardArray.append(rowArray);
    }

    // Telegram expects reply_markup to be a JSON-serialized object. To remove an existing inline
    // keyboard we must send an empty InlineKeyboardMarkup object (inline_keyboard: []).
    {
        QJsonObject replyMarkup;
        replyMarkup.insert(QStringLiteral("inline_keyboard"), keyboardArray);
        const QJsonDocument markupDoc(replyMarkup);
        query.addQueryItem(QStringLiteral("reply_markup"),
                           QString::fromUtf8(markupDoc.toJson(QJsonDocument::Compact)));
    }

    const QByteArray body = buildFormUrlEncodedBody(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));

    QNetworkReply *reply = d->network.post(request, body);
    trackPendingRequest(reply);
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (!reply)
            return;
        untrackPendingRequest(reply);

        const QByteArray payload = reply->readAll();
        const TelegramApiResponseInfo apiInfo = parseTelegramApiResponse(payload);
        const QString apiDetail = describeTelegramResponseIssue(apiInfo, payload);
        const bool isNotModified =
                apiDetail.contains(QStringLiteral("message is not modified"), Qt::CaseInsensitive);

        const auto emitDetailedError = [this, reply](const QString &detail) {
            if (detail.isEmpty())
                return;
            emit errorOccurred(
                    buildMethodErrorMessage(QStringLiteral("editMessageText"), reply, detail));
        };

        if (reply->error() != QNetworkReply::NoError) {
            if (isNotModified) {
                reply->deleteLater();
                return;
            }
            QString detail = reply->errorString();
            if (!apiDetail.isEmpty())
                detail += QStringLiteral(" - %1").arg(apiDetail);
            emitDetailedError(detail);
            reply->deleteLater();
            return;
        }

        if (!apiInfo.parseError.isEmpty() || !apiInfo.ok) {
            if (!isNotModified) {
                emitDetailedError(apiDetail);
            }
        }

        reply->deleteLater();
    });
}

void TelegramBot::answerCallbackQuery(const QString &callbackId, const QString &text)
{
    if (d->token.isEmpty())
        return;

    const QString trimmedId = callbackId.trimmed();
    if (trimmedId.isEmpty())
        return;

    QUrl url = buildMethodUrl(QStringLiteral("answerCallbackQuery"));
    QJsonObject payload;
    payload.insert(QStringLiteral("callback_query_id"), trimmedId);

    const QString trimmedText = text.trimmed();
    if (!trimmedText.isEmpty())
        payload.insert(QStringLiteral("text"), trimmedText);

    const QJsonDocument doc(payload);
    const QByteArray body = doc.toJson(QJsonDocument::Compact);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply *reply = d->network.post(request, body);
    trackPendingRequest(reply);
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (!reply)
            return;
        untrackPendingRequest(reply);

        const QByteArray payload = reply->readAll();
        const TelegramApiResponseInfo apiInfo = parseTelegramApiResponse(payload);
        const auto emitDetailedError = [this, reply](const QString &detail) {
            if (detail.isEmpty())
                return;
            emit errorOccurred(
                    buildMethodErrorMessage(QStringLiteral("answerCallbackQuery"), reply, detail));
        };

        if (reply->error() != QNetworkReply::NoError) {
            QString detail = reply->errorString();
            const QString apiDetail = describeTelegramResponseIssue(apiInfo, payload);
            if (!apiDetail.isEmpty())
                detail += QStringLiteral(" - %1").arg(apiDetail);
            emitDetailedError(detail);
            reply->deleteLater();
            return;
        }

        if (!apiInfo.parseError.isEmpty() || !apiInfo.ok)
            emitDetailedError(describeTelegramResponseIssue(apiInfo, payload));

        reply->deleteLater();
    });
}

int TelegramBot::pendingRequestCount() const
{
    return d ? d->pendingRequests.size() : 0;
}

bool TelegramBot::waitForPendingRequests(int timeoutMs)
{
    if (pendingRequestCount() == 0)
        return true;

    if (timeoutMs == 0)
        return false;

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    if (timeoutMs > 0) {
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(timeoutMs);
    }
    QObject::connect(this, &TelegramBot::pendingRequestCountChanged, &loop, [&loop](int count) {
        if (count == 0)
            loop.quit();
    });

    loop.exec();

    const bool drained = (pendingRequestCount() == 0);
    if (!drained && timeoutMs > 0) {
        qCWarning(lcTelegramBot).nospace()
                << "Pending Telegram requests did not finish within " << timeoutMs
                << " ms (pending=" << pendingRequestCount() << ").";
    }
    return drained;
}

void TelegramBot::trackPendingRequest(QNetworkReply *reply)
{
    if (!d || !reply)
        return;
    if (d->pendingRequests.contains(reply))
        return;

    d->pendingRequests.insert(reply);
    emit pendingRequestCountChanged(d->pendingRequests.size());

    QObject::connect(reply, &QObject::destroyed, this, [this, reply]() {
        if (!d)
            return;
        if (d->pendingRequests.remove(reply) > 0)
            emit pendingRequestCountChanged(d->pendingRequests.size());
    });
}

void TelegramBot::untrackPendingRequest(QNetworkReply *reply)
{
    if (!d || !reply)
        return;
    if (d->pendingRequests.remove(reply) > 0)
        emit pendingRequestCountChanged(d->pendingRequests.size());
}

void TelegramBot::syncCommandsWithPlatform()
{
    if (d->token.isEmpty())
        return;

    const MessengerCommandManager *commandManager = this->commandManager();
    if (!commandManager)
        return;

    // Build JSON payload for setMyCommands.
    QJsonArray commandArray;

    for (const MessengerAbstractCommand *commandImpl : commandManager->commands()) {
        if (!commandImpl || !commandImpl->isEnabled(nullptr))
            continue;
        if (commandImpl->audience() == MessengerCommandAudience::ServerAdminsOnly)
            continue;

        QString command = commandImpl->command().trimmed();
        const QString description = commandImpl->parser().description().trimmed();

        if (command.isEmpty())
            continue;

        // Telegram Bot API expects command names without leading '/'.
        if (command.startsWith(QLatin1Char('/')))
            command.remove(0, 1);

        if (command.isEmpty())
            continue;

        QJsonObject cmdObj;
        cmdObj.insert(QStringLiteral("command"), command);
        if (!description.isEmpty())
            cmdObj.insert(QStringLiteral("description"), description);

        commandArray.append(cmdObj);
    }

    QJsonObject root;
    root.insert(QStringLiteral("commands"), commandArray);

    const QJsonDocument doc(root);
    const QByteArray body = doc.toJson(QJsonDocument::Compact);

    QUrl url = buildMethodUrl(QStringLiteral("setMyCommands"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply *reply = d->network.post(request, body);
    trackPendingRequest(reply);
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (!reply)
            return;
        untrackPendingRequest(reply);

        const QByteArray payload = reply->readAll();
        const TelegramApiResponseInfo apiInfo = parseTelegramApiResponse(payload);
        const auto emitDetailedError = [this, reply](const QString &detail) {
            if (detail.isEmpty())
                return;
            emit errorOccurred(
                    buildMethodErrorMessage(QStringLiteral("setMyCommands"), reply, detail));
        };

        if (reply->error() != QNetworkReply::NoError) {
            QString detail = reply->errorString();
            const QString apiDetail = describeTelegramResponseIssue(apiInfo, payload);
            if (!apiDetail.isEmpty())
                detail += QStringLiteral(" - %1").arg(apiDetail);
            emitDetailedError(detail);
            reply->deleteLater();
            return;
        }

        if (!apiInfo.parseError.isEmpty() || !apiInfo.ok)
            emitDetailedError(describeTelegramResponseIssue(apiInfo, payload));

        reply->deleteLater();
    });
}

void TelegramBot::onTimeout()
{
    // Called only after errors, with a delay.
    if (!d->running || d->requestInProgress)
        return;

    fetchUpdates();
}

QUrl TelegramBot::buildMethodUrl(const QString &method) const
{
    QString urlStr = QString::fromLatin1(TelegramBotPrivate::kTelegramApiBaseUrl);
    urlStr += d->token;
    urlStr += QLatin1Char('/');
    urlStr += method;
    return QUrl(urlStr);
}

void TelegramBot::fetchUpdates()
{
    if (!d->running || d->requestInProgress)
        return;

    QUrl url = buildMethodUrl(QStringLiteral("getUpdates"));

    QUrlQuery query;
    if (d->lastUpdateId > 0) {
        // Ask Telegram to send only updates with higher ID
        query.addQueryItem(QStringLiteral("offset"), QString::number(d->lastUpdateId + 1));
    }
    // Use long polling to avoid aggressive short-interval polling.
    // Telegram allows long timeouts; 25 seconds is a typical value.
    query.addQueryItem(QStringLiteral("timeout"), QString::number(25));

    if (!query.isEmpty())
        url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply *reply = d->network.get(request);
    d->requestInProgress = true;
    d->activeReply = reply;
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onUpdatesFinished(reply);
    });
}

void TelegramBot::onUpdatesFinished(QNetworkReply *reply)
{
    if (!reply)
        return;

    // Ignore stale replies that are no longer tracked.
    if (reply != d->activeReply) {
        reply->deleteLater();
        return;
    }

    d->requestInProgress = false;
    d->activeReply.clear();

    const bool ok = handleUpdatesReply(reply);
    reply->deleteLater();

    if (!d->running)
        return;

    if (ok) {
        // Successful poll: reset backoff and immediately start next long poll.
        d->errorBackoffMs = 1000;
        fetchUpdates();
    } else {
        // Error case: schedule next attempt with exponential backoff
        if (!d->timer.isActive()) {
            d->timer.start(d->errorBackoffMs);
            d->errorBackoffMs = qMin(d->errorBackoffMs * 2, 300000);
        }
    }
}

bool TelegramBot::handleUpdatesReply(QNetworkReply *reply)
{
    const QByteArray data = reply->readAll();
    const TelegramApiResponseInfo apiInfo = parseTelegramApiResponse(data);

    if (reply->error() != QNetworkReply::NoError) {
        QString detail = reply->errorString();
        const QString apiDetail = describeTelegramResponseIssue(apiInfo, data);
        if (!apiDetail.isEmpty())
            detail += QStringLiteral(" - %1").arg(apiDetail);
        emit errorOccurred(buildMethodErrorMessage(QStringLiteral("getUpdates"), reply, detail));
        return false;
    }

    if (!apiInfo.parseError.isEmpty() || !apiInfo.ok) {
        emit errorOccurred(buildMethodErrorMessage(QStringLiteral("getUpdates"),
                                                   reply,
                                                   describeTelegramResponseIssue(apiInfo, data)));
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        emit errorOccurred(buildMethodErrorMessage(
                QStringLiteral("getUpdates"),
                reply,
                QStringLiteral("Telegram response parse error: %1").arg(parseError.errorString())));
        return false;
    }

    if (!doc.isObject()) {
        emit errorOccurred(buildMethodErrorMessage(QStringLiteral("getUpdates"),
                                                   reply,
                                                   QStringLiteral("Unexpected JSON root type")));
        return false;
    }

    const QJsonObject root = doc.object();

    const QJsonValue resultVal = root.value(QStringLiteral("result"));
    const bool dropStartupSpam = d->startupSpamFilterPending;
    if (!resultVal.isArray()) {
        if (dropStartupSpam)
            d->startupSpamFilterPending = false;
        return true;
    }

    const QJsonArray resultArray = resultVal.toArray();

    struct PendingMessage
    {
        qint64 updateId = 0;
        AbstractBot::IncomingMessage message;
    };

    QHash<QString, PendingMessage> perUserLatest;
    if (dropStartupSpam)
        perUserLatest.reserve(resultArray.size());

    for (const QJsonValue &itemVal : resultArray) {
        if (!itemVal.isObject())
            continue;

        const QJsonObject updateObj = itemVal.toObject();
        const qint64 updateId =
                static_cast<qint64>(updateObj.value(QStringLiteral("update_id")).toDouble());

        if (updateId > d->lastUpdateId)
            d->lastUpdateId = updateId;

        const QJsonObject callbackObj =
                updateObj.value(QStringLiteral("callback_query")).toObject();
        if (!callbackObj.isEmpty()) {
            AbstractBot::CallbackQuery callbackQuery;
            callbackQuery.chat.messenger = MessengerType::Telegram;
            callbackQuery.id = callbackObj.value(QStringLiteral("id")).toString();

            const QJsonObject fromObj = callbackObj.value(QStringLiteral("from")).toObject();
            const qint64 userId =
                    static_cast<qint64>(fromObj.value(QStringLiteral("id")).toDouble());
            if (userId > 0)
                callbackQuery.sender.userId = QString::number(userId);
            else
                callbackQuery.sender.userId = fromObj.value(QStringLiteral("id")).toString();
            callbackQuery.sender.username = fromObj.value(QStringLiteral("username")).toString();
            callbackQuery.sender.firstName = fromObj.value(QStringLiteral("first_name")).toString();
            callbackQuery.sender.lastName = fromObj.value(QStringLiteral("last_name")).toString();
            callbackQuery.languageCode =
                    fromObj.value(QStringLiteral("language_code")).toString().trimmed();

            const QJsonObject messageObj = callbackObj.value(QStringLiteral("message")).toObject();
            if (!messageObj.isEmpty()) {
                const QJsonObject chatObj = messageObj.value(QStringLiteral("chat")).toObject();
                const qint64 chatId =
                        static_cast<qint64>(chatObj.value(QStringLiteral("id")).toDouble());
                if (chatId != 0)
                    callbackQuery.chat.chatId = QString::number(chatId);

                const QJsonValue threadValue =
                        messageObj.value(QStringLiteral("message_thread_id"));
                if (threadValue.isDouble()) {
                    const qint64 threadId = static_cast<qint64>(threadValue.toDouble());
                    if (threadId > 0)
                        callbackQuery.chat.threadId = QString::number(threadId);
                }

                const qint64 messageId = static_cast<qint64>(
                        messageObj.value(QStringLiteral("message_id")).toDouble());
                if (messageId > 0)
                    callbackQuery.originalMessageId = QString::number(messageId);
            }

            callbackQuery.data = callbackObj.value(QStringLiteral("data")).toString();

            const bool callbackValid = callbackQuery.isValid();
            if (callbackValid)
                emit callbackQueryReceived(callbackQuery);
            answerCallbackQuery(callbackQuery.id);

            continue;
        }

        const QJsonObject messageObj = updateObj.value(QStringLiteral("message")).toObject();
        if (messageObj.isEmpty())
            continue;

        const QJsonObject chatObj = messageObj.value(QStringLiteral("chat")).toObject();

        const qint64 chatId = static_cast<qint64>(chatObj.value(QStringLiteral("id")).toDouble());
        const QJsonObject fromObj = messageObj.value(QStringLiteral("from")).toObject();
        const qint64 userId = static_cast<qint64>(fromObj.value(QStringLiteral("id")).toDouble());

        const QString text = messageObj.value(QStringLiteral("text")).toString();

        if (text.isEmpty())
            continue;

        AbstractBot::IncomingMessage message;
        message.chat.messenger = MessengerType::Telegram;
        message.chat.chatId = QString::number(chatId);

        const QJsonValue threadValue = messageObj.value(QStringLiteral("message_thread_id"));
        if (threadValue.isDouble()) {
            const qint64 threadId = static_cast<qint64>(threadValue.toDouble());
            if (threadId > 0)
                message.chat.threadId = QString::number(threadId);
        }

        message.sender.userId = QString::number(userId);
        message.sender.username = fromObj.value(QStringLiteral("username")).toString();

        const QString firstName = fromObj.value(QStringLiteral("first_name")).toString();
        const QString lastName = fromObj.value(QStringLiteral("last_name")).toString();
        message.sender.firstName = firstName;
        message.sender.lastName = lastName;

        const QString languageCode =
                fromObj.value(QStringLiteral("language_code")).toString().trimmed();
        if (!languageCode.isEmpty())
            message.languageCode = languageCode;

        message.text = text;

        const qint64 messageId =
                static_cast<qint64>(messageObj.value(QStringLiteral("message_id")).toDouble());
        if (messageId > 0)
            message.messageId = QString::number(messageId);

        if (dropStartupSpam) {
            // Keep only the most recent command per user when clearing backlog.
            PendingMessage pending{ updateId, message };
            QString dedupKey = message.sender.userId;
            if (dedupKey.isEmpty())
                dedupKey = message.chat.chatId;
            if (dedupKey.isEmpty())
                dedupKey = QStringLiteral("update:%1").arg(updateId);

            QHash<QString, PendingMessage>::iterator existingIt = perUserLatest.find(dedupKey);
            if (existingIt == perUserLatest.end() || existingIt->updateId <= updateId) {
                perUserLatest.insert(dedupKey, pending);
            }
            continue;
        }

        emit messageReceived(message);
    }

    if (dropStartupSpam) {
        QVector<PendingMessage> orderedMessages;
        orderedMessages.reserve(perUserLatest.size());

        for (QHash<QString, PendingMessage>::const_iterator it = perUserLatest.cbegin();
             it != perUserLatest.cend();
             ++it)
            orderedMessages.append(it.value());

        std::sort(orderedMessages.begin(),
                  orderedMessages.end(),
                  [](const PendingMessage &lhs, const PendingMessage &rhs) {
                      return lhs.updateId < rhs.updateId;
                  });

        for (const PendingMessage &pending : orderedMessages)
            emit messageReceived(pending.message);

        d->startupSpamFilterPending = false;
    }

    return true;
}
