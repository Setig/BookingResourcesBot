#include "SharedMemoryServer.h"

#include "SharedMemoryEndpoint.h"

SharedMemoryServer::SharedMemoryServer(const QString &baseKey, QObject *parent)
    : QObject(parent)
    , m_endpoint(new SharedMemoryEndpoint(SharedMemoryEndpoint::Role::Server, baseKey, this))
{
    QObject::connect(m_endpoint,
                     &SharedMemoryEndpoint::frameReceived,
                     this,
                     &SharedMemoryServer::requestReceived);
    QObject::connect(m_endpoint,
                     &SharedMemoryEndpoint::errorOccurred,
                     this,
                     &SharedMemoryServer::errorOccurred);
}

SharedMemoryServer::~SharedMemoryServer()
{
    // do nothing
}

QString SharedMemoryServer::baseKey() const
{
    return m_endpoint ? m_endpoint->baseKey() : QString();
}

void SharedMemoryServer::setBaseKey(const QString &baseKey)
{
    if (m_endpoint)
        m_endpoint->setBaseKey(baseKey);
}

void SharedMemoryServer::setAuthenticationSecret(const QByteArray &secret)
{
    if (m_endpoint)
        m_endpoint->setAuthenticationSecret(secret);
}

bool SharedMemoryServer::isRunning() const
{
    return m_endpoint && m_endpoint->isRunning();
}

QString SharedMemoryServer::lastError() const
{
    return m_endpoint ? m_endpoint->lastError() : QString();
}

quint64 SharedMemoryServer::receivedFrameCount() const
{
    return m_endpoint ? m_endpoint->receivedFrameCount() : 0;
}

quint64 SharedMemoryServer::sentFrameCount() const
{
    return m_endpoint ? m_endpoint->sentFrameCount() : 0;
}

quint64 SharedMemoryServer::busyWriteCount() const
{
    return m_endpoint ? m_endpoint->busyWriteCount() : 0;
}

quint64 SharedMemoryServer::receivedByteCount() const
{
    return m_endpoint ? m_endpoint->receivedByteCount() : 0;
}

quint64 SharedMemoryServer::sentByteCount() const
{
    return m_endpoint ? m_endpoint->sentByteCount() : 0;
}

quint64 SharedMemoryServer::notificationErrorCount() const
{
    return m_endpoint ? m_endpoint->notificationErrorCount() : 0;
}

quint64 SharedMemoryServer::coalescedWakeupCount() const
{
    return m_endpoint ? m_endpoint->coalescedWakeupCount() : 0;
}

quint64 SharedMemoryServer::pollBatchCount() const
{
    return m_endpoint ? m_endpoint->pollBatchCount() : 0;
}

bool SharedMemoryServer::start()
{
    return m_endpoint && m_endpoint->start();
}

void SharedMemoryServer::stop()
{
    if (m_endpoint)
        m_endpoint->stop();
}

bool SharedMemoryServer::sendFrame(const SharedMemoryProtocol::Frame &frame)
{
    return m_endpoint && m_endpoint->sendFrame(frame);
}

SharedMemoryEndpoint::SendStatus SharedMemoryServer::sendFrameStatus(
        const SharedMemoryProtocol::Frame &frame)
{
    return m_endpoint ? m_endpoint->sendFrameStatus(frame)
                      : SharedMemoryEndpoint::SendStatus::Failed;
}

bool SharedMemoryServer::sendResponse(const QString &requestId,
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
