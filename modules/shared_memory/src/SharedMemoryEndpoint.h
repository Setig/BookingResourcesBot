#ifndef SHAREDMEMORYENDPOINT_H
#define SHAREDMEMORYENDPOINT_H

#include <QObject>
#include <QString>
#include <QAtomicInteger>

#include "SharedMemoryProtocol.h"

class QSharedMemory;
class QSystemSemaphore;
class QTimer;

class SharedMemoryEndpoint final : public QObject
{
    Q_OBJECT

public:
    enum class SendStatus {
        Success = 0,
        Busy,
        Failed
    };

    enum class Role {
        Server = 0,
        Client
    };

    explicit SharedMemoryEndpoint(Role role,
                                  const QString &baseKey = QString(),
                                  QObject *parent = nullptr);
    ~SharedMemoryEndpoint() override;

    [[nodiscard]] Role role() const;
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
    SendStatus sendFrameStatus(const SharedMemoryProtocol::Frame &frame);
    void poll();

signals:
    void frameReceived(const SharedMemoryProtocol::Frame &frame);
    void errorOccurred(const QString &description);

private:
    class Channel;
    class NotificationWorker;

    enum class ReadStatus {
        Empty = 0,
        FrameRead,
        FrameDiscarded,
        Failed
    };

    [[nodiscard]] QString normalizeBaseKey(const QString &key) const;
    [[nodiscard]] QString channelKey(const QString &name) const;
    bool openChannel(Channel *channel, bool createSegment);
    bool ensureQueue(Channel *channel, bool reset);
    bool ensureStartedForSend();
    bool lockChannel(Channel *channel);
    bool unlockChannel(Channel *channel);
    ReadStatus readFrame(Channel *channel, SharedMemoryProtocol::Frame *frame);
    SendStatus writeFrame(Channel *channel, const SharedMemoryProtocol::Frame &frame);
    void schedulePoll();
    void handleNotificationError(const QString &key, const QString &description);
    void setLastError(const QString &description);

    Role m_role;
    QString m_baseKey;
    QByteArray m_authenticationSecret;
    Channel *m_incoming;
    Channel *m_outgoing;
    NotificationWorker *m_notificationWorker;
    QTimer *m_fallbackPollTimer;
    QString m_lastError;
    quint64 m_receivedFrameCount;
    quint64 m_sentFrameCount;
    quint64 m_busyWriteCount;
    quint64 m_receivedByteCount;
    quint64 m_sentByteCount;
    QAtomicInteger<int> m_pollScheduled;
    QAtomicInteger<quint64> m_notificationErrorCount;
    QAtomicInteger<quint64> m_coalescedWakeupCount;
    QAtomicInteger<quint64> m_pollBatchCount;
    bool m_running;
};

#endif // SHAREDMEMORYENDPOINT_H
