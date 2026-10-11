#include "WebClient.h"

#include <QTimer>
#include <QThread>
#include <QEventLoop>
#include <QJsonObject>
#include <QMutexLocker>
#include <QMetaObject>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcWebClient, "booking.web.client")

namespace {
constexpr int kFlushIntervalMs = 50;
constexpr int kReconnectIntervalMs = 3000;
constexpr int kHandshakeTimeoutMs = 1000;
constexpr int kReconnectReminderAttempts = 10;
constexpr int kMaxPendingFrames = 256;
} // namespace

WebClient::WebClient(const QString &sharedMemoryKey, QObject *parent)
    : QObject(parent)
    , m_client(sharedMemoryKey, this)
    , m_droppedFrameCount(0)
    , m_connected(false)
    , m_handshakePending(false)
    , m_autoReconnect(false)
    , m_reconnectAttemptCount(0)
{
    m_flushTimer.setInterval(kFlushIntervalMs);
    m_reconnectTimer.setInterval(kReconnectIntervalMs);
    m_handshakeTimer.setInterval(kHandshakeTimeoutMs);
    m_handshakeTimer.setSingleShot(true);
    QObject::connect(&m_client,
                     &SharedMemoryClient::responseReceived,
                     this,
                     &WebClient::handleResponse);
    QObject::connect(&m_client,
                     &SharedMemoryClient::errorOccurred,
                     this,
                     &WebClient::errorOccurred);
    QObject::connect(&m_flushTimer, &QTimer::timeout, this, &WebClient::flushPendingFrames);
    QObject::connect(&m_reconnectTimer, &QTimer::timeout, this, &WebClient::tryReconnect);
    QObject::connect(&m_handshakeTimer, &QTimer::timeout, this, &WebClient::handleHandshakeTimeout);
}

WebClient::~WebClient()
{
    stop();
}

QString WebClient::sharedMemoryKey() const
{
    return m_client.baseKey();
}

void WebClient::setSharedMemoryKey(const QString &key)
{
    m_client.setBaseKey(key);
}

void WebClient::setSharedMemorySecret(const QString &secret)
{
    m_client.setAuthenticationSecret(secret.toUtf8());
}

void WebClient::setUserProfileProvider(WebClient::UserProfileProvider provider)
{
    m_userProfileProvider = provider;
}

bool WebClient::isRunning() const
{
    return m_client.isRunning();
}

bool WebClient::isConnected() const
{
    QMutexLocker locker(&m_sendMutex);
    return m_connected;
}

bool WebClient::start()
{
    return m_client.start();
}

void WebClient::stop()
{
    if (thread() != QThread::currentThread()) {
        QMetaObject::invokeMethod(this, [this]() { stop(); }, Qt::BlockingQueuedConnection);
        return;
    }

    m_autoReconnect = false;
    m_reconnectTimer.stop();
    m_handshakeTimer.stop();
    m_flushTimer.stop();
    QMutexLocker locker(&m_sendMutex);
    m_handshakePending = false;
    m_handshakeRequestId.clear();
    m_pendingFrames.clear();
    m_client.stop();
    m_connected = false;
}

void WebClient::startAutoReconnect()
{
    if (thread() != QThread::currentThread()) {
        QMetaObject::invokeMethod(this, [this]() { startAutoReconnect(); }, Qt::QueuedConnection);
        return;
    }

    m_autoReconnect = true;
    if (!m_reconnectTimer.isActive())
        m_reconnectTimer.start();
    tryReconnect();
}

bool WebClient::sendMessage(const QJsonObject &payload)
{
    SharedMemoryProtocol::Frame frame;
    frame.type = QStringLiteral("message");
    frame.requestId = SharedMemoryProtocol::createRequestId();
    frame.payload = payload;
    return queueFrame(frame);
}

bool WebClient::sendCallback(const QJsonObject &payload)
{
    SharedMemoryProtocol::Frame frame;
    frame.type = QStringLiteral("callback");
    frame.requestId = SharedMemoryProtocol::createRequestId();
    frame.payload = payload;
    return queueFrame(frame);
}

QJsonObject WebClient::authorizeUser(const QJsonObject &payload, int timeoutMs)
{
    return request(QStringLiteral("authorize_user"), payload, timeoutMs);
}

QJsonObject WebClient::userSettings(const QJsonObject &payload, int timeoutMs)
{
    return request(QStringLiteral("user_settings_get"), payload, timeoutMs);
}

QJsonObject WebClient::updateUserSettings(const QJsonObject &payload, int timeoutMs)
{
    return request(QStringLiteral("user_settings_set"), payload, timeoutMs);
}

QJsonObject WebClient::commandList(const QJsonObject &payload, int timeoutMs)
{
    return request(QStringLiteral("commands"), payload, timeoutMs);
}

QJsonObject WebClient::ping(int timeoutMs)
{
    return request(QStringLiteral("ping"), QJsonObject(), timeoutMs);
}

void WebClient::handleResponse(const SharedMemoryProtocol::Frame &frame)
{
    const QString type = frame.type.trimmed().toLower();
    bool handshakeMatched = false;
    bool handshakeOk = false;
    {
        QMutexLocker locker(&m_sendMutex);
        if (m_handshakePending && frame.requestId == m_handshakeRequestId) {
            handshakeMatched = true;
            handshakeOk = type == QLatin1String("pong")
                          && frame.payload.value(QStringLiteral("ok")).toBool();
            m_handshakePending = false;
            m_handshakeRequestId.clear();
        }
    }
    if (handshakeMatched) {
        m_handshakeTimer.stop();
        if (handshakeOk)
            setConnected(true);
        else
            detachFromServer();
        return;
    }

    if (type == QLatin1String("user_profile_request")) {
        const QString userId = frame.payload.value(QStringLiteral("user_id")).toString();
        SharedMemoryProtocol::Frame response;
        response.type = QStringLiteral("user_profile_result");
        response.requestId = frame.requestId;
        response.payload = userProfileResponse(userId);
        queueFrame(response);
        return;
    }

    if (type == QLatin1String("bot_message") || type == QLatin1String("bot_edit")
        || type == QLatin1String("bot_file") || type == QLatin1String("command_list")
        || type == QLatin1String("command_cache_invalidated")) {
        QJsonObject event = frame.payload;
        event.insert(QStringLiteral("type"), type);
        event.insert(QStringLiteral("request_id"), frame.requestId);
        emit botEventReceived(event);
    }
}

bool WebClient::queueFrame(const SharedMemoryProtocol::Frame &frame)
{
    QMutexLocker locker(&m_sendMutex);
    if (!m_connected)
        return false;

    if (m_pendingFrames.isEmpty()) {
        const SharedMemoryEndpoint::SendStatus status = m_client.sendFrameStatus(frame);
        if (status == SharedMemoryEndpoint::SendStatus::Success)
            return true;
        if (status == SharedMemoryEndpoint::SendStatus::Failed)
            return false;
    }

    if (m_pendingFrames.size() >= kMaxPendingFrames) {
        ++m_droppedFrameCount;
        qCWarning(lcWebClient).nospace()
                << "Dropping web IPC frame because the outgoing queue is full (type=" << frame.type
                << ", dropped=" << m_droppedFrameCount << ").";
        return false;
    }

    m_pendingFrames.enqueue(frame);
    scheduleFlushLocked();
    return true;
}

void WebClient::flushPendingFrames()
{
    QMutexLocker locker(&m_sendMutex);
    flushPendingFramesLocked();
}

void WebClient::flushPendingFramesLocked()
{
    if (!m_connected)
        return;

    while (!m_pendingFrames.isEmpty()) {
        const SharedMemoryProtocol::Frame frame = m_pendingFrames.head();
        const SharedMemoryEndpoint::SendStatus status = m_client.sendFrameStatus(frame);
        if (status == SharedMemoryEndpoint::SendStatus::Busy)
            break;
        if (status == SharedMemoryEndpoint::SendStatus::Failed) {
            ++m_droppedFrameCount;
            qCWarning(lcWebClient).nospace()
                    << "Dropping web IPC frame after permanent send failure (type=" << frame.type
                    << ", dropped=" << m_droppedFrameCount << ").";
        }
        m_pendingFrames.dequeue();
    }

    if (m_pendingFrames.isEmpty()) {
        scheduleStopFlushLocked();
    } else {
        scheduleFlushLocked();
    }
}

void WebClient::scheduleFlushLocked()
{
    QMetaObject::invokeMethod(this, "startFlushTimer", Qt::QueuedConnection);
}

void WebClient::scheduleStopFlushLocked()
{
    QMetaObject::invokeMethod(this, "stopFlushTimer", Qt::QueuedConnection);
}

void WebClient::startFlushTimer()
{
    QMutexLocker locker(&m_sendMutex);
    if (m_pendingFrames.isEmpty())
        return;
    if (!m_flushTimer.isActive())
        m_flushTimer.start();
}

void WebClient::stopFlushTimer()
{
    QMutexLocker locker(&m_sendMutex);
    if (!m_pendingFrames.isEmpty())
        return;
    if (m_flushTimer.isActive())
        m_flushTimer.stop();
}

void WebClient::tryReconnect()
{
    if (thread() != QThread::currentThread()) {
        QMetaObject::invokeMethod(this, [this]() { tryReconnect(); }, Qt::QueuedConnection);
        return;
    }

    if (!m_autoReconnect)
        return;

    {
        QMutexLocker locker(&m_sendMutex);
        if (m_handshakePending)
            return;
    }

    if (!m_client.isRunning() && !m_client.start()) {
        setConnected(false);
        noteReconnectFailure();
        return;
    }

    beginHandshake();
}

void WebClient::beginHandshake()
{
    SharedMemoryProtocol::Frame frame;
    frame.type = QStringLiteral("ping");
    frame.requestId = SharedMemoryProtocol::createRequestId();

    SharedMemoryEndpoint::SendStatus status = SharedMemoryEndpoint::SendStatus::Failed;
    {
        QMutexLocker locker(&m_sendMutex);
        if (m_handshakePending)
            return;
        status = m_client.sendFrameStatus(frame);
        if (status == SharedMemoryEndpoint::SendStatus::Busy)
            return;
        if (status == SharedMemoryEndpoint::SendStatus::Success) {
            m_handshakeRequestId = frame.requestId;
            m_handshakePending = true;
        }
    }

    if (status == SharedMemoryEndpoint::SendStatus::Failed) {
        detachFromServer();
        return;
    }

    m_handshakeTimer.start();
}

void WebClient::handleHandshakeTimeout()
{
    bool pending = false;
    {
        QMutexLocker locker(&m_sendMutex);
        pending = m_handshakePending;
    }
    if (!pending)
        return;
    detachFromServer();
}

void WebClient::detachFromServer()
{
    if (thread() != QThread::currentThread()) {
        QMetaObject::invokeMethod(this, [this]() { detachFromServer(); }, Qt::QueuedConnection);
        return;
    }

    m_handshakeTimer.stop();
    m_flushTimer.stop();
    bool wasConnected = false;
    {
        QMutexLocker locker(&m_sendMutex);
        m_handshakePending = false;
        m_handshakeRequestId.clear();
        m_pendingFrames.clear();
        m_client.stop();
        wasConnected = m_connected;
        m_connected = false;
    }
    if (wasConnected)
        emit disconnected();
    noteReconnectFailure();
}

void WebClient::setConnected(bool connected)
{
    {
        QMutexLocker locker(&m_sendMutex);
        if (m_connected == connected)
            return;
        m_connected = connected;
        if (connected)
            m_reconnectAttemptCount = 0;
    }
    if (connected)
        emit this->connected();
    else
        emit disconnected();
}

void WebClient::noteReconnectFailure()
{
    int attemptCount = 0;
    {
        QMutexLocker locker(&m_sendMutex);
        ++m_reconnectAttemptCount;
        attemptCount = m_reconnectAttemptCount;
    }
    const QString lastError = m_client.lastError();
    if (attemptCount == 1) {
        if (lastError.isEmpty()) {
            qCInfo(lcWebClient) << "Web IPC is not available yet; retrying periodically.";
        } else {
            qCInfo(lcWebClient).noquote()
                    << "Web IPC is not available yet; retrying periodically:" << lastError;
        }
        return;
    }

    if (attemptCount % kReconnectReminderAttempts != 0)
        return;

    if (lastError.isEmpty()) {
        qCInfo(lcWebClient) << "Web IPC is still unavailable after" << attemptCount << "attempts.";
    } else {
        qCInfo(lcWebClient).noquote()
                << "Web IPC is still unavailable after" << attemptCount << "attempts:" << lastError;
    }
}

QJsonObject WebClient::request(const QString &type, const QJsonObject &payload, int timeoutMs)
{
    QJsonObject result;
    QString requestId;
    {
        QMutexLocker locker(&m_sendMutex);
        if (!m_connected) {
            result.insert(QStringLiteral("ok"), false);
            result.insert(QStringLiteral("error"), tr("Server is not available."));
            result.insert(QStringLiteral("error_code"), QStringLiteral("server_unavailable"));
            return result;
        }

        flushPendingFramesLocked();
        if (!m_pendingFrames.isEmpty()) {
            result.insert(QStringLiteral("ok"), false);
            result.insert(QStringLiteral("error"), tr("Server is not available."));
            result.insert(QStringLiteral("error_code"), QStringLiteral("server_unavailable"));
            scheduleFlushLocked();
            return result;
        }

        requestId = m_client.sendRequest(type, payload);
        if (requestId.isEmpty()) {
            result.insert(QStringLiteral("ok"), false);
            result.insert(QStringLiteral("error"), tr("Server is not available."));
            result.insert(QStringLiteral("error_code"), QStringLiteral("server_unavailable"));
            scheduleFlushLocked();
            return result;
        }
    }

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    const QMetaObject::Connection connection =
            QObject::connect(&m_client,
                             &SharedMemoryClient::responseReceived,
                             &loop,
                             [&](const SharedMemoryProtocol::Frame &frame) {
                                 if (frame.requestId != requestId)
                                     return;
                                 result = frame.payload;
                                 result.insert(QStringLiteral("response_type"), frame.type);
                                 loop.quit();
                             });

    timeout.start(qMax(1, timeoutMs));
    loop.exec();
    QObject::disconnect(connection);

    if (result.isEmpty()) {
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("error"), tr("Timed out waiting for server response."));
    }
    return result;
}

QJsonObject WebClient::userProfileResponse(const QString &userId) const
{
    QJsonObject response;
    if (!m_userProfileProvider) {
        response.insert(QStringLiteral("ok"), false);
        response.insert(QStringLiteral("error"),
                        tr("Web user profile provider is not configured."));
        return response;
    }

    response = m_userProfileProvider(userId.trimmed());
    if (!response.value(QStringLiteral("ok")).toBool()
        && !response.contains(QStringLiteral("error")))
        response.insert(QStringLiteral("error"), tr("Web user was not found."));
    return response;
}
