#include "SharedMemoryClient.h"

#include "SharedMemoryEndpoint.h"

SharedMemoryClient::SharedMemoryClient(const QString &baseKey, QObject *parent)
    : QObject(parent)
    , m_endpoint(new SharedMemoryEndpoint(SharedMemoryEndpoint::Role::Client, baseKey, this))
{
    QObject::connect(m_endpoint,
                     &SharedMemoryEndpoint::frameReceived,
                     this,
                     &SharedMemoryClient::responseReceived);
    QObject::connect(m_endpoint,
                     &SharedMemoryEndpoint::errorOccurred,
                     this,
                     &SharedMemoryClient::errorOccurred);
}

SharedMemoryClient::~SharedMemoryClient()
{
    // do nothing
}

QString SharedMemoryClient::baseKey() const
{
    return m_endpoint ? m_endpoint->baseKey() : QString();
}

void SharedMemoryClient::setBaseKey(const QString &baseKey)
{
    if (m_endpoint)
        m_endpoint->setBaseKey(baseKey);
}

void SharedMemoryClient::setAuthenticationSecret(const QByteArray &secret)
{
    if (m_endpoint)
        m_endpoint->setAuthenticationSecret(secret);
}

bool SharedMemoryClient::isRunning() const
{
    return m_endpoint && m_endpoint->isRunning();
}

QString SharedMemoryClient::lastError() const
{
    return m_endpoint ? m_endpoint->lastError() : QString();
}

quint64 SharedMemoryClient::receivedFrameCount() const
{
    return m_endpoint ? m_endpoint->receivedFrameCount() : 0;
}

quint64 SharedMemoryClient::sentFrameCount() const
{
    return m_endpoint ? m_endpoint->sentFrameCount() : 0;
}

quint64 SharedMemoryClient::busyWriteCount() const
{
    return m_endpoint ? m_endpoint->busyWriteCount() : 0;
}

quint64 SharedMemoryClient::receivedByteCount() const
{
    return m_endpoint ? m_endpoint->receivedByteCount() : 0;
}

quint64 SharedMemoryClient::sentByteCount() const
{
    return m_endpoint ? m_endpoint->sentByteCount() : 0;
}

quint64 SharedMemoryClient::notificationErrorCount() const
{
    return m_endpoint ? m_endpoint->notificationErrorCount() : 0;
}

quint64 SharedMemoryClient::coalescedWakeupCount() const
{
    return m_endpoint ? m_endpoint->coalescedWakeupCount() : 0;
}

quint64 SharedMemoryClient::pollBatchCount() const
{
    return m_endpoint ? m_endpoint->pollBatchCount() : 0;
}

bool SharedMemoryClient::start()
{
    return m_endpoint && m_endpoint->start();
}

void SharedMemoryClient::stop()
{
    if (m_endpoint)
        m_endpoint->stop();
}

bool SharedMemoryClient::sendFrame(const SharedMemoryProtocol::Frame &frame)
{
    return m_endpoint && m_endpoint->sendFrame(frame);
}

SharedMemoryEndpoint::SendStatus SharedMemoryClient::sendFrameStatus(
        const SharedMemoryProtocol::Frame &frame)
{
    return m_endpoint ? m_endpoint->sendFrameStatus(frame)
                      : SharedMemoryEndpoint::SendStatus::Failed;
}

bool SharedMemoryClient::sendResponse(const QString &requestId,
                                      const QString &type,
                                      const QJsonObject &payload)
{
    SharedMemoryProtocol::Frame frame;
    frame.type = type.trimmed();
    frame.requestId = requestId.trimmed().isEmpty() ? SharedMemoryProtocol::createRequestId()
                                                    : requestId.trimmed();
    frame.payload = payload;
    return sendFrame(frame);
}

QString SharedMemoryClient::sendRequest(const QString &type, const QJsonObject &payload)
{
    if (!m_endpoint)
        return QString();

    SharedMemoryProtocol::Frame frame;
    frame.type = type.trimmed();
    frame.requestId = SharedMemoryProtocol::createRequestId();
    frame.payload = payload;

    if (!m_endpoint->sendFrame(frame))
        return QString();
    return frame.requestId;
}
