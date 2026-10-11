#ifndef WEBCLIENT_H
#define WEBCLIENT_H

#include <QMutex>
#include <QQueue>
#include <QTimer>
#include <QObject>
#include <QJsonObject>

#include "SharedMemoryClient.h"

class WebClient final : public QObject
{
    Q_OBJECT

public:
    using UserProfileProvider = std::function<QJsonObject(const QString &)>;

    explicit WebClient(const QString &sharedMemoryKey = QString(), QObject *parent = nullptr);
    ~WebClient() override;

    [[nodiscard]] QString sharedMemoryKey() const;
    void setSharedMemoryKey(const QString &key);
    void setSharedMemorySecret(const QString &secret);
    void setUserProfileProvider(UserProfileProvider provider);

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] bool isConnected() const;

public slots:
    bool start();
    void stop();
    void startAutoReconnect();

    bool sendMessage(const QJsonObject &payload);
    bool sendCallback(const QJsonObject &payload);
    QJsonObject authorizeUser(const QJsonObject &payload, int timeoutMs = 5000);
    QJsonObject userSettings(const QJsonObject &payload, int timeoutMs = 5000);
    QJsonObject updateUserSettings(const QJsonObject &payload, int timeoutMs = 5000);
    QJsonObject commandList(const QJsonObject &payload, int timeoutMs = 5000);
    QJsonObject ping(int timeoutMs = 5000);

signals:
    void botEventReceived(const QJsonObject &event);
    void errorOccurred(const QString &description);
    void connected();
    void disconnected();

private slots:
    void handleResponse(const SharedMemoryProtocol::Frame &frame);
    void flushPendingFrames();
    void startFlushTimer();
    void stopFlushTimer();
    void tryReconnect();
    void handleHandshakeTimeout();

private:
    bool queueFrame(const SharedMemoryProtocol::Frame &frame);
    void flushPendingFramesLocked();
    void scheduleFlushLocked();
    void scheduleStopFlushLocked();
    QJsonObject request(const QString &type, const QJsonObject &payload, int timeoutMs);
    QJsonObject userProfileResponse(const QString &userId) const;
    void beginHandshake();
    void detachFromServer();
    void setConnected(bool connected);
    void noteReconnectFailure();

    mutable QMutex m_sendMutex;
    SharedMemoryClient m_client;
    QQueue<SharedMemoryProtocol::Frame> m_pendingFrames;
    QTimer m_flushTimer;
    QTimer m_reconnectTimer;
    QTimer m_handshakeTimer;
    QString m_handshakeRequestId;
    UserProfileProvider m_userProfileProvider;
    quint64 m_droppedFrameCount;
    bool m_connected;
    bool m_handshakePending;
    bool m_autoReconnect;
    int m_reconnectAttemptCount;
};

#endif // WEBCLIENT_H
