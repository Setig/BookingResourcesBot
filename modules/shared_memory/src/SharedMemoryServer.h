#ifndef SHAREDMEMORYSERVER_H
#define SHAREDMEMORYSERVER_H

#include <QObject>
#include <QJsonObject>

#include "SharedMemoryEndpoint.h"
#include "SharedMemoryProtocol.h"

class SharedMemoryServer final : public QObject
{
    Q_OBJECT

public:
    explicit SharedMemoryServer(const QString &baseKey = QString(), QObject *parent = nullptr);
    ~SharedMemoryServer() override;

    [[nodiscard]] QString baseKey() const;
    void setBaseKey(const QString &baseKey);
    void setAuthenticationSecret(const QByteArray &secret);

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] quint64 receivedFrameCount() const;
    [[nodiscard]] quint64 sentFrameCount() const;
    [[nodiscard]] quint64 busyWriteCount() const;
    [[nodiscard]] quint64 receivedByteCount() const;
    [[nodiscard]] quint64 sentByteCount() const;
    [[nodiscard]] quint64 notificationErrorCount() const;
    [[nodiscard]] quint64 coalescedWakeupCount() const;
    [[nodiscard]] quint64 pollBatchCount() const;

public slots:
    bool start();
    void stop();
    bool sendFrame(const SharedMemoryProtocol::Frame &frame);
    SharedMemoryEndpoint::SendStatus sendFrameStatus(const SharedMemoryProtocol::Frame &frame);
    bool sendResponse(const QString &requestId,
                      const QString &type,
                      const QJsonObject &payload = QJsonObject());

signals:
    void requestReceived(const SharedMemoryProtocol::Frame &frame);
    void errorOccurred(const QString &description);

private:
    SharedMemoryEndpoint *m_endpoint;
};

#endif // SHAREDMEMORYSERVER_H
