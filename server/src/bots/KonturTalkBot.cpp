#include "KonturTalkBot.h"

#include <QSet>
#include <QTimer>
#include <QVector>
#include <QVariant>
#include <QPointer>
#include <QFileInfo>
#include <QJsonValue>
#include <QUrlQuery>
#include <QEventLoop>
#include <QStringList>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QLoggingCategory>
#include <QNetworkAccessManager>

Q_LOGGING_CATEGORY(lcKonturTalkBot, "booking.server.kontur_talk")

namespace {

constexpr int kKonturTalkPollingTimeoutSeconds = 30;
constexpr int kSeenEventIdsLimit = 512;

struct KonturTalkApiResponseInfo
{
    bool hasPayload = false;
    bool isObject = false;
    QJsonObject root;
    QString parseError;
};

KonturTalkApiResponseInfo parseKonturTalkApiResponse(const QByteArray &payload)
{
    KonturTalkApiResponseInfo info;

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

    info.isObject = true;
    info.root = doc.object();
    return info;
}

QString describeDetailObject(const QJsonObject &detail)
{
    const QString errcode = detail.value(QStringLiteral("errcode")).toString().trimmed();
    const QString error = detail.value(QStringLiteral("error")).toString().trimmed();
    if (!errcode.isEmpty() && !error.isEmpty())
        return QStringLiteral("%1: %2").arg(errcode, error);
    if (!error.isEmpty())
        return error;
    if (!errcode.isEmpty())
        return errcode;
    return QString();
}

QString describeValidationDetails(const QJsonArray &details)
{
    QStringList messages;
    for (const QJsonValue &itemValue : details) {
        if (!itemValue.isObject())
            continue;

        const QJsonObject item = itemValue.toObject();
        QString location;
        const QJsonArray loc = item.value(QStringLiteral("loc")).toArray();
        for (const QJsonValue &locPart : loc) {
            if (!location.isEmpty())
                location.append(QLatin1Char('.'));
            if (locPart.isString()) {
                location.append(locPart.toString());
            } else if (locPart.isDouble()) {
                location.append(QString::number(static_cast<int>(locPart.toDouble())));
            }
        }

        const QString message = item.value(QStringLiteral("msg")).toString().trimmed();
        if (!location.isEmpty() && !message.isEmpty()) {
            messages.append(QStringLiteral("%1: %2").arg(location, message));
        } else if (!message.isEmpty()) {
            messages.append(message);
        }
    }

    return messages.join(QStringLiteral("; "));
}

QString describeKonturTalkResponseIssue(const KonturTalkApiResponseInfo &info,
                                        const QByteArray &payload)
{
    if (!info.parseError.isEmpty())
        return QStringLiteral("Kontur.Talk response parse error: %1").arg(info.parseError);

    if (info.isObject) {
        const QJsonValue detailValue = info.root.value(QStringLiteral("detail"));
        if (detailValue.isObject()) {
            const QString detail = describeDetailObject(detailValue.toObject());
            if (!detail.isEmpty())
                return detail;
        } else if (detailValue.isArray()) {
            const QString detail = describeValidationDetails(detailValue.toArray());
            if (!detail.isEmpty())
                return detail;
        } else if (detailValue.isString()) {
            const QString detail = detailValue.toString().trimmed();
            if (!detail.isEmpty())
                return detail;
        }

        const QString error = info.root.value(QStringLiteral("error")).toString().trimmed();
        if (!error.isEmpty())
            return error;

        const QString errorMessage =
                info.root.value(QStringLiteral("errorMessage")).toString().trimmed();
        if (!errorMessage.isEmpty())
            return errorMessage;

        const QString message = info.root.value(QStringLiteral("message")).toString().trimmed();
        if (!message.isEmpty())
            return message;
    }

    if (payload.isEmpty())
        return QStringLiteral("Empty Kontur.Talk response body");

    QString snippet = QString::fromUtf8(payload.left(256)).trimmed();
    if (payload.size() > 256)
        snippet.append(QStringLiteral("..."));

    if (!snippet.isEmpty())
        return QStringLiteral("Kontur.Talk response: %1").arg(snippet);

    return QStringLiteral("Unexpected Kontur.Talk response.");
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

bool isHttpSuccess(QNetworkReply *reply)
{
    const QVariant statusAttr = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (!statusAttr.isValid())
        return true;

    const int statusCode = statusAttr.toInt();
    return statusCode >= 200 && statusCode < 300;
}

QString formatToKonturTalk(AbstractBot::MessageFormat format)
{
    switch (format) {
    case AbstractBot::MessageFormat::Markdown:
        return QStringLiteral("markdown");
    case AbstractBot::MessageFormat::Html:
        return QStringLiteral("html");
    case AbstractBot::MessageFormat::PlainText:
    default:
        return QStringLiteral("plain");
    }
}

QString nullableString(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    if (value.isString())
        return value.toString().trimmed();
    return QString();
}

} // namespace

class KonturTalkBotPrivate
{
public:
    explicit KonturTalkBotPrivate(const QString &token);

    inline static constexpr char kKonturTalkApiBaseUrl[] =
            "https://chat.ktalk.ru/_matrix/client/strangler/api/v1/bot/";

    const QString token;

    QNetworkAccessManager network;
    QTimer timer;
    bool running = false;
    bool requestInProgress = false;
    int errorBackoffMs = 1000;
    QPointer<QNetworkReply> activeReply;
    QSet<QNetworkReply *> pendingRequests;
    QSet<QString> seenEventIds;
    QVector<QString> seenEventOrder;
};

KonturTalkBotPrivate::KonturTalkBotPrivate(const QString &token)
    : token(token.trimmed())
{
    // do nothing
}

KonturTalkBot::KonturTalkBot(const QString &token, QObject *parent)
    : AbstractBot(parent)
    , d(new KonturTalkBotPrivate(token))
{
    if (d->token.isEmpty()) {
        QMetaObject::invokeMethod(
                this,
                [this]() {
                    qCCritical(lcKonturTalkBot).nospace()
                            << KonturTalkBot::staticMetaObject.className()
                            << " create: token is empty!";
                    emit errorOccurred(QStringLiteral("Kontur.Talk bot token is empty!"));
                },
                Qt::QueuedConnection);
    }

    d->timer.setSingleShot(true);
    connect(&d->timer, &QTimer::timeout, this, [this]() { onTimeout(); });
}

KonturTalkBot::~KonturTalkBot()
{
    d->running = false;
    d->timer.stop();
    if (QNetworkReply *reply = d->activeReply.data()) {
        d->activeReply.clear();
        d->requestInProgress = false;
        reply->abort();
    }

    KonturTalkBotPrivate *state = d;
    d = nullptr;
    delete state;
}

MessengerType KonturTalkBot::messenger() const
{
    return MessengerType::KonturTalk;
}

AbstractBot::Capabilities KonturTalkBot::capabilities() const
{
    return CapabilityFlag::MarkdownFormatting | CapabilityFlag::HtmlFormatting;
}

bool KonturTalkBot::isRunning() const
{
    return d->running;
}

void KonturTalkBot::start()
{
    if (d->token.isEmpty())
        return;

    if (d->running)
        return;

    d->running = true;
    d->errorBackoffMs = 1000;

    if (!d->requestInProgress)
        fetchUpdates();
}

void KonturTalkBot::stop()
{
    d->running = false;
    d->timer.stop();

    if (QNetworkReply *reply = d->activeReply.data()) {
        d->activeReply.clear();
        d->requestInProgress = false;
        reply->abort();
    }
}

void KonturTalkBot::startShutdown()
{
    stop();
}

bool KonturTalkBot::waitShutdown(int msecs)
{
    return waitForPendingRequests(msecs);
}

void KonturTalkBot::sendMessage(const ChatAddress &chat,
                                const QString &text,
                                const MessageOptions &options)
{
    if (d->token.isEmpty())
        return;

    if (!chat.isValid()) {
        emit errorOccurred(QStringLiteral("Cannot send Kontur.Talk message: room id is empty."));
        return;
    }

    if (text.isEmpty()) {
        emit errorOccurred(QStringLiteral("Cannot send Kontur.Talk message: text is empty."));
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("room_id"), chat.chatId);
    payload.insert(QStringLiteral("thread_id"),
                   options.threadId.isEmpty() ? QJsonValue(QJsonValue::Null)
                                              : QJsonValue(options.threadId));
    payload.insert(QStringLiteral("format"), formatToKonturTalk(options.format));
    payload.insert(QStringLiteral("message"), text);
    payload.insert(QStringLiteral("mentions"), QJsonArray());

    const QJsonDocument doc(payload);
    const QByteArray body = doc.toJson(QJsonDocument::Compact);

    QNetworkRequest request(buildMethodUrl(QStringLiteral("send_message")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");

    const ChatAddress sentChat = chat;
    QNetworkReply *reply = d->network.post(request, body);
    trackPendingRequest(reply);
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, sentChat]() {
        if (!reply)
            return;
        untrackPendingRequest(reply);

        const QByteArray payload = reply->readAll();
        const KonturTalkApiResponseInfo apiInfo = parseKonturTalkApiResponse(payload);

        const auto emitDetailedError = [this, reply](const QString &detail) {
            if (detail.isEmpty())
                return;
            emit errorOccurred(
                    buildMethodErrorMessage(QStringLiteral("send_message"), reply, detail));
        };

        if (reply->error() != QNetworkReply::NoError || !isHttpSuccess(reply)) {
            QString detail = reply->errorString();
            const QString apiDetail = describeKonturTalkResponseIssue(apiInfo, payload);
            if (!apiDetail.isEmpty()) {
                if (!detail.isEmpty())
                    detail += QStringLiteral(" - ");
                detail += apiDetail;
            }
            emitDetailedError(detail);
            reply->deleteLater();
            return;
        }

        if (!apiInfo.parseError.isEmpty() || !apiInfo.isObject) {
            emitDetailedError(describeKonturTalkResponseIssue(apiInfo, payload));
            reply->deleteLater();
            return;
        }

        const QString sentMessageId =
                apiInfo.root.value(QStringLiteral("event_id")).toString().trimmed();
        if (!sentMessageId.isEmpty())
            emit messageSent(sentChat, sentMessageId);

        reply->deleteLater();
    });
}

void KonturTalkBot::sendFile(const ChatAddress &chat,
                             const QString &filePath,
                             const QString &caption,
                             AbstractBot::MessageFormat captionFormat)
{
    Q_UNUSED(captionFormat);

    if (d->token.isEmpty())
        return;

    if (!chat.isValid()) {
        emit errorOccurred(QStringLiteral("Cannot send Kontur.Talk file: room id is empty."));
        return;
    }

    emit errorOccurred(QStringLiteral("Cannot send Kontur.Talk file: file messages are not "
                                      "supported by the documented Bot API."));

    const QFileInfo info(filePath);
    const QString fileLabel = info.fileName().isEmpty() ? filePath : info.fileName();

    QString text = caption.trimmed();
    if (!text.isEmpty())
        text.append(QStringLiteral("\n"));
    text.append(
            tr("File attachment is not supported by Kontur.Talk Bot API yet: %1").arg(fileLabel));

    MessageOptions options;
    options.threadId = chat.threadId;
    sendMessage(chat, text, options);
}

int KonturTalkBot::pendingRequestCount() const
{
    return d ? d->pendingRequests.size() : 0;
}

bool KonturTalkBot::waitForPendingRequests(int timeoutMs)
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
    QObject::connect(this, &KonturTalkBot::pendingRequestCountChanged, &loop, [&loop](int count) {
        if (count == 0)
            loop.quit();
    });

    loop.exec();

    const bool drained = (pendingRequestCount() == 0);
    if (!drained && timeoutMs > 0) {
        qCWarning(lcKonturTalkBot).nospace()
                << "Pending Kontur.Talk requests did not finish within " << timeoutMs
                << " ms (pending=" << pendingRequestCount() << ").";
    }
    return drained;
}

QUrl KonturTalkBot::buildMethodUrl(const QString &method) const
{
    QString urlStr = QString::fromLatin1(KonturTalkBotPrivate::kKonturTalkApiBaseUrl);
    urlStr += d->token;
    urlStr += QLatin1Char('/');
    urlStr += method;
    return QUrl(urlStr);
}

void KonturTalkBot::fetchUpdates()
{
    if (!d->running || d->requestInProgress)
        return;

    QUrl url = buildMethodUrl(QStringLiteral("get_updates"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("timeout"),
                       QString::number(kKonturTalkPollingTimeoutSeconds));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = d->network.get(request);
    d->requestInProgress = true;
    d->activeReply = reply;
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onUpdatesFinished(reply);
    });
}

void KonturTalkBot::onUpdatesFinished(QNetworkReply *reply)
{
    if (!reply)
        return;

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
        d->errorBackoffMs = 1000;
        fetchUpdates();
    } else if (!d->timer.isActive()) {
        d->timer.start(d->errorBackoffMs);
        d->errorBackoffMs = qMin(d->errorBackoffMs * 2, 300000);
    }
}

bool KonturTalkBot::handleUpdatesReply(QNetworkReply *reply)
{
    const QByteArray data = reply->readAll();
    const KonturTalkApiResponseInfo apiInfo = parseKonturTalkApiResponse(data);

    if (reply->error() != QNetworkReply::NoError || !isHttpSuccess(reply)) {
        QString detail = reply->errorString();
        const QString apiDetail = describeKonturTalkResponseIssue(apiInfo, data);
        if (!apiDetail.isEmpty()) {
            if (!detail.isEmpty())
                detail += QStringLiteral(" - ");
            detail += apiDetail;
        }
        emit errorOccurred(buildMethodErrorMessage(QStringLiteral("get_updates"), reply, detail));
        return false;
    }

    if (!apiInfo.parseError.isEmpty() || !apiInfo.isObject) {
        emit errorOccurred(buildMethodErrorMessage(QStringLiteral("get_updates"),
                                                   reply,
                                                   describeKonturTalkResponseIssue(apiInfo, data)));
        return false;
    }

    const QJsonArray updates = apiInfo.root.value(QStringLiteral("updates")).toArray();
    for (const QJsonValue &updateValue : updates) {
        if (!updateValue.isObject())
            continue;

        const QJsonObject update = updateValue.toObject();
        const QString eventId = nullableString(update, QStringLiteral("event_id"));
        if (!rememberEventId(eventId))
            continue;

        const QString messageType = nullableString(update, QStringLiteral("message_type"));
        if (messageType != QStringLiteral("m.text")) {
            qCDebug(lcKonturTalkBot).nospace()
                    << "Skipping unsupported Kontur.Talk message type: " << qPrintable(messageType);
            continue;
        }

        const QString text = update.value(QStringLiteral("body")).toString();
        if (text.isEmpty())
            continue;

        AbstractBot::IncomingMessage message;
        message.chat.messenger = MessengerType::KonturTalk;
        message.chat.chatId = nullableString(update, QStringLiteral("room_id"));
        message.chat.threadId = nullableString(update, QStringLiteral("thread_id"));
        message.sender.userId = nullableString(update, QStringLiteral("user_id"));
        message.text = text;
        message.messageId = eventId;

        if (!message.chat.isValid() || !message.sender.isValid()) {
            qCWarning(lcKonturTalkBot).nospace()
                    << "Skipping Kontur.Talk message with incomplete addresses"
                    << " event_id=" << qPrintable(eventId);
            continue;
        }

        emit messageReceived(message);
    }

    return true;
}

void KonturTalkBot::onTimeout()
{
    fetchUpdates();
}

bool KonturTalkBot::rememberEventId(const QString &eventId)
{
    const QString trimmed = eventId.trimmed();
    if (trimmed.isEmpty())
        return true;

    if (d->seenEventIds.contains(trimmed))
        return false;

    d->seenEventIds.insert(trimmed);
    d->seenEventOrder.append(trimmed);

    while (d->seenEventOrder.size() > kSeenEventIdsLimit) {
        const QString oldest = d->seenEventOrder.takeFirst();
        d->seenEventIds.remove(oldest);
    }

    return true;
}

void KonturTalkBot::trackPendingRequest(QNetworkReply *reply)
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

void KonturTalkBot::untrackPendingRequest(QNetworkReply *reply)
{
    if (!d || !reply)
        return;
    if (d->pendingRequests.remove(reply) > 0)
        emit pendingRequestCountChanged(d->pendingRequests.size());
}
