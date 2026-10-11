#ifndef LOCALCLIBOT_H
#define LOCALCLIBOT_H

#include "AbstractBot.h"

#include <functional>

#include <QHash>
#include <QPointer>
#include <QByteArray>
#include <QScopedPointer>
#include <QLocalSocket>

#include "LocalCliProtocol.h"
#include "LocalCliRegistry.h"

class QLocalServer;

/**
 * @brief AbstractBot implementation that exposes a QLocalServer endpoint for the CLI.
 */
class LocalCliBot : public AbstractBot
{
    Q_OBJECT

public:
    using CompletionHandler = std::function<LocalCliProtocol::CompletionResult(const QString &text,
                                                                               int cursor,
                                                                               const QString &userId)>;

    /**
     * @brief Constructs a LocalCliBot instance.
     * @param endpointName Local server endpoint name. If empty, a process-specific default is used.
     * @param parent Optional QObject parent.
     */
    explicit LocalCliBot(QString endpointName = QString(), QObject *parent = nullptr);

    /**
     * @brief Destroys the LocalCliBot instance.
     */
    ~LocalCliBot() override;

    [[nodiscard]] MessengerType messenger() const override;
    [[nodiscard]] Capabilities capabilities() const override;
    [[nodiscard]] bool isRunning() const override;
    bool fetchUserProfile(const QString &userId,
                          AbstractBot::UserProfile *profileOut,
                          QString *errorDescription) override;
    void setCompletionHandler(CompletionHandler handler);

public slots:
    /**
     * @brief Starts listening for Local CLI client connections.
     */
    void start() override;

    /**
     * @brief Stops listening and disconnects active Local CLI sessions.
     */
    void stop() override;

    void sendMessage(const AbstractBot::ChatAddress &chat,
                     const QString &text,
                     const AbstractBot::MessageOptions &options) override;
    void sendFile(const AbstractBot::ChatAddress &chat,
                  const QString &filePath,
                  const QString &caption = QString(),
                  AbstractBot::MessageFormat captionFormat =
                          AbstractBot::MessageFormat::PlainText) override;

signals:
    void clientSessionStarted();
    void clientSessionFinished();

private:
    /**
     * @brief Per-client connection state for a LocalCliBot session.
     */
    struct Session
    {
        QPointer<QLocalSocket> socket;
        QByteArray buffer;
        QString chatId;
        QString userId;
        QString label;
        qint64 clientPid = -1;
        quint64 nextMessageId = 1;
        bool handshaken = false;
        bool sessionNotified = false;
        bool closing = false;
    };

    void handleNewConnection();
    void handleSocketReadyRead(QLocalSocket *socket);
    void handleSocketDisconnected(QLocalSocket *socket);
    void handleSocketError(QLocalSocket *socket, QLocalSocket::LocalSocketError error);
    void processFrame(Session *session, const LocalCliProtocol::Frame &frame);
    void handleHandshake(Session *session, const LocalCliProtocol::Frame &frame);
    void handleCommand(Session *session, const QString &command);
    void handleComplete(Session *session, const LocalCliProtocol::Frame &frame);
    void handleDisconnectRequest(Session *session);
    void sendFrame(Session *session, const LocalCliProtocol::Frame &frame);
    void sendResponse(Session *session, const QString &text);
    void sendError(Session *session, const QString &text);
    void sendResult(Session *session, const QString &status, quint64 messageSequence);
    void sendInfo(Session *session, const QString &text);
    void closeSession(Session *session, const QString &reason = QString());
    void shutdown(const QString &reason);
    void registerInstance();
    void unregisterInstance();
    void dropAllSessions(const QString &reason);
    void notifySessionStarted(Session *session);
    void notifySessionFinished(Session *session);

    QString m_endpointName;
    CompletionHandler m_completionHandler;
    QScopedPointer<QLocalServer> m_server;
    QHash<QLocalSocket *, Session *> m_sessions;
    QHash<QString, Session *> m_sessionsByChatId;
    LocalCliRegistry::Entry m_registryEntry;
    qint64 m_processPid;
    quint64 m_sessionCounter;
};

#endif // LOCALCLIBOT_H
