#include "LocalCliEndpoint.h"

#include "LocalCliHelper.h"
#include "LocalCliProtocol.h"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QPointer>
#include <QMetaObject>
#include <QCoreApplication>

Q_LOGGING_CATEGORY(lcLocalCliEndpoint, "booking.modules.localcli.endpoint")

namespace {

QString normalizedCommand(const QString &command)
{
    return command.trimmed();
}

} // namespace

class LocalCliEndpointPrivate
{
public:
    struct Session
    {
        QPointer<QLocalSocket> socket;
        QByteArray buffer;
        QString chatId;
        QString label;
        qint64 clientPid = -1;
        quint64 nextMessageId = 1;
        bool handshaken = false;
        bool sessionNotified = false;
        bool closing = false;
    };

    explicit LocalCliEndpointPrivate(LocalCliEndpoint *q);

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
    void dropAllSessions(const QString &reason);
    void notifySessionStarted(Session *session);
    void notifySessionFinished(Session *session);
    void registerInstance();
    void unregisterInstance();

    LocalCliEndpoint *q;
    QString endpointName;
    LocalCliRegistry::Kind kind;
    QString sharedMemoryKey;
    QString welcomeInfo;
    LocalCliEndpoint::CommandHandler commandHandler;
    LocalCliEndpoint::CompletionHandler completionHandler;
    QLocalServer *server;
    QHash<QLocalSocket *, Session *> sessions;
    LocalCliRegistry::Entry registryEntry;
    qint64 processPid;
    quint64 sessionCounter;
};

LocalCliEndpointPrivate::LocalCliEndpointPrivate(LocalCliEndpoint *q)
    : q(q)
    , endpointName()
    , kind(LocalCliRegistry::Kind::Server)
    , sharedMemoryKey()
    , welcomeInfo()
    , commandHandler()
    , completionHandler()
    , server(nullptr)
    , sessions()
    , registryEntry()
    , processPid(QCoreApplication::applicationPid())
    , sessionCounter(0)
{
    // do nothing
}

void LocalCliEndpointPrivate::handleNewConnection()
{
    if (!server)
        return;

    while (server->hasPendingConnections()) {
        QLocalSocket *socket = server->nextPendingConnection();
        if (!socket)
            continue;

        socket->setParent(q);
        Session *session = new Session;
        session->socket = socket;
        sessions.insert(socket, session);

        QObject::connect(socket, &QLocalSocket::readyRead, q, [this, socket]() {
            handleSocketReadyRead(socket);
        });
        QObject::connect(socket, &QLocalSocket::disconnected, q, [this, socket]() {
            handleSocketDisconnected(socket);
        });
        QObject::connect(socket,
                         qOverload<QLocalSocket::LocalSocketError>(&QLocalSocket::errorOccurred),
                         q,
                         [this, socket](QLocalSocket::LocalSocketError error) {
                             handleSocketError(socket, error);
                         });
    }
}

void LocalCliEndpointPrivate::handleSocketReadyRead(QLocalSocket *socket)
{
    Session *session = sessions.value(socket, nullptr);
    if (!session || session->closing || !session->socket)
        return;

    session->buffer.append(socket->readAll());

    while (true) {
        session = sessions.value(socket, nullptr);
        if (!session || session->closing || !session->socket)
            break;

        LocalCliProtocol::Frame frame;
        if (!LocalCliProtocol::takeFrame(&session->buffer, &frame))
            break;

        processFrame(session, frame);
    }
}

void LocalCliEndpointPrivate::handleSocketDisconnected(QLocalSocket *socket)
{
    Session *session = sessions.value(socket, nullptr);
    if (!session || session->closing)
        return;

    // Close on the event loop so a peer-close during sendFrame/flush cannot
    // delete the Session while the caller still holds it.
    QMetaObject::invokeMethod(
            q,
            [this, socket]() {
                Session *current = sessions.value(socket, nullptr);
                if (current)
                    closeSession(current);
            },
            Qt::QueuedConnection);
}

void LocalCliEndpointPrivate::handleSocketError(QLocalSocket *socket,
                                                QLocalSocket::LocalSocketError error)
{
    Q_UNUSED(socket);
    if (error == QLocalSocket::PeerClosedError)
        return;

    qCWarning(lcLocalCliEndpoint).nospace()
            << "Local CLI socket error (" << static_cast<int>(error) << ").";
}

void LocalCliEndpointPrivate::processFrame(Session *session, const LocalCliProtocol::Frame &frame)
{
    if (!session)
        return;

    const QString type = frame.type.trimmed().toLower();
    if (!session->handshaken) {
        if (type == QLatin1String("hello")) {
            handleHandshake(session, frame);
        } else {
            sendError(session,
                      LocalCliEndpoint::tr("Handshake is required before sending commands."));
            closeSession(session, QStringLiteral("Handshake missing."));
        }
        return;
    }

    if (type == QLatin1String("command")) {
        const QString command = frame.payload.value(QStringLiteral("text")).toString();
        handleCommand(session, command);
    } else if (type == QLatin1String("complete")) {
        handleComplete(session, frame);
    } else if (type == QLatin1String("bye")) {
        handleDisconnectRequest(session);
    } else if (type == QLatin1String("ping")) {
        sendInfo(session, LocalCliEndpoint::tr("pong"));
    } else {
        sendError(session, LocalCliEndpoint::tr("Unsupported frame type: %1").arg(type));
    }
}

void LocalCliEndpointPrivate::handleHandshake(Session *session,
                                              const LocalCliProtocol::Frame &frame)
{
    if (!session)
        return;
    if (session->handshaken) {
        sendError(session, LocalCliEndpoint::tr("Handshake already completed."));
        return;
    }

    const qint64 clientPid =
            static_cast<qint64>(frame.payload.value(QStringLiteral("pid")).toDouble(-1));
    if (clientPid <= 0) {
        sendError(session, LocalCliEndpoint::tr("Client PID is missing in handshake."));
        closeSession(session, QStringLiteral("Invalid handshake."));
        return;
    }

    session->clientPid = clientPid;
    session->label = frame.payload.value(QStringLiteral("label")).toString().trimmed();
    if (session->label.isEmpty())
        session->label = LocalCliHelper::localHostLabel();

    session->chatId = QStringLiteral("%1-%2").arg(session->clientPid).arg(++sessionCounter);
    session->handshaken = true;

    qCInfo(lcLocalCliEndpoint).nospace()
            << "Local CLI connected (client_pid=" << session->clientPid
            << ", chat=" << session->chatId << ", label=" << session->label << ')';

    const QString processName = kind == LocalCliRegistry::Kind::Web
                                        ? QStringLiteral("booking-resources-web")
                                        : QStringLiteral("booking-resources-server");

    LocalCliProtocol::Frame ack;
    ack.type = QStringLiteral("hello-ack");
    QJsonObject payload;
    payload.insert(QStringLiteral("pid"), static_cast<double>(processPid));
    payload.insert(QStringLiteral("server"), endpointName);
    payload.insert(QStringLiteral("kind"), LocalCliRegistry::kindKey(kind));
    QJsonArray features;
    features.append(QStringLiteral("complete"));
    payload.insert(QStringLiteral("features"), features);
    payload.insert(
            QStringLiteral("message"),
            LocalCliEndpoint::tr("Connected to %1 process %2.").arg(processName).arg(processPid));
    ack.payload = payload;
    sendFrame(session, ack);

    if (!welcomeInfo.trimmed().isEmpty())
        sendInfo(session, welcomeInfo);

    notifySessionStarted(session);
}

void LocalCliEndpointPrivate::handleCommand(Session *session, const QString &command)
{
    if (!session || !session->handshaken) {
        sendError(session, LocalCliEndpoint::tr("Handshake is required before commands."));
        return;
    }

    const QString normalized = normalizedCommand(command);
    const quint64 messageSequence = session->nextMessageId;
    if (normalized.isEmpty()) {
        sendError(session, LocalCliEndpoint::tr("Command payload is empty."));
        sendResult(session, QStringLiteral("error"), messageSequence);
        return;
    }

    session->nextMessageId++;

    if (!commandHandler) {
        sendError(session, LocalCliEndpoint::tr("No command handler is configured."));
        sendResult(session, QStringLiteral("error"), messageSequence);
        return;
    }

    const LocalCliEndpoint::CommandResult result = commandHandler(command);
    if (!result.text.trimmed().isEmpty()) {
        if (result.ok)
            sendResponse(session, result.text);
        else
            sendError(session, result.text);
    }
    sendResult(session,
               result.ok ? QStringLiteral("ok") : QStringLiteral("error"),
               messageSequence);
}

void LocalCliEndpointPrivate::handleComplete(Session *session, const LocalCliProtocol::Frame &frame)
{
    if (!session || !session->handshaken) {
        sendError(session, LocalCliEndpoint::tr("Handshake is required before commands."));
        return;
    }

    const QString text = frame.payload.value(QStringLiteral("text")).toString();
    const int cursor = frame.payload.value(QStringLiteral("cursor")).toInt(text.size());
    LocalCliProtocol::CompletionResult result;
    if (completionHandler)
        result = completionHandler(text, cursor);
    sendFrame(session, LocalCliProtocol::makeCompleteResult(result));
}

void LocalCliEndpointPrivate::handleDisconnectRequest(Session *session)
{
    closeSession(session, LocalCliEndpoint::tr("Client requested disconnection."));
}

void LocalCliEndpointPrivate::sendFrame(Session *session, const LocalCliProtocol::Frame &frame)
{
    if (!session || !session->socket)
        return;
    if (session->socket->state() != QLocalSocket::ConnectedState)
        return;

    session->socket->write(LocalCliProtocol::serialize(frame));
    session->socket->flush();
}

void LocalCliEndpointPrivate::sendResponse(Session *session, const QString &text)
{
    if (!session)
        return;
    LocalCliProtocol::Frame frame;
    frame.type = QStringLiteral("response");
    QJsonObject payload;
    payload.insert(QStringLiteral("text"), text);
    payload.insert(QStringLiteral("timestamp"),
                   QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    frame.payload = payload;
    sendFrame(session, frame);
}

void LocalCliEndpointPrivate::sendError(Session *session, const QString &text)
{
    if (!session)
        return;
    LocalCliProtocol::Frame frame;
    frame.type = QStringLiteral("error");
    QJsonObject payload;
    payload.insert(QStringLiteral("text"), text);
    frame.payload = payload;
    sendFrame(session, frame);
}

void LocalCliEndpointPrivate::sendResult(Session *session,
                                         const QString &status,
                                         quint64 messageSequence)
{
    if (!session)
        return;
    LocalCliProtocol::Frame frame;
    frame.type = QStringLiteral("result");
    QJsonObject payload;
    payload.insert(QStringLiteral("status"), status);
    payload.insert(QStringLiteral("message_id"), QString::number(messageSequence));
    payload.insert(QStringLiteral("timestamp"),
                   QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    frame.payload = payload;
    sendFrame(session, frame);
}

void LocalCliEndpointPrivate::sendInfo(Session *session, const QString &text)
{
    if (!session)
        return;
    LocalCliProtocol::Frame frame;
    frame.type = QStringLiteral("info");
    QJsonObject payload;
    payload.insert(QStringLiteral("text"), text);
    frame.payload = payload;
    sendFrame(session, frame);
}

void LocalCliEndpointPrivate::closeSession(Session *session, const QString &reason)
{
    if (!session || session->closing)
        return;

    session->closing = true;

    if (session->handshaken) {
        qCInfo(lcLocalCliEndpoint).nospace()
                << "Local CLI disconnected (client_pid=" << session->clientPid
                << ", chat=" << session->chatId << ", label=" << session->label
                << ", reason=" << (reason.isEmpty() ? QStringLiteral("n/a") : reason) << ')';
    }

    QLocalSocket *socket = session->socket.data();
    if (socket)
        sessions.remove(socket);

    if (socket) {
        socket->disconnect(q);
        if (!reason.isEmpty() && session->handshaken
            && socket->state() == QLocalSocket::ConnectedState) {
            sendInfo(session, reason);
        }
        socket->disconnectFromServer();
        socket->deleteLater();
        session->socket = nullptr;
    }

    notifySessionFinished(session);
    delete session;
}

void LocalCliEndpointPrivate::notifySessionStarted(Session *session)
{
    if (!session || session->sessionNotified)
        return;

    session->sessionNotified = true;
    emit q->clientSessionStarted();
}

void LocalCliEndpointPrivate::notifySessionFinished(Session *session)
{
    if (!session || !session->sessionNotified)
        return;

    session->sessionNotified = false;
    emit q->clientSessionFinished();
}

void LocalCliEndpointPrivate::dropAllSessions(const QString &reason)
{
    const QList<Session *> snapshot = sessions.values();
    for (Session *session : snapshot)
        closeSession(session, reason);
    sessions.clear();
}

void LocalCliEndpointPrivate::registerInstance()
{
    LocalCliRegistry::Entry entry;
    entry.pid = processPid;
    entry.kind = kind;
    entry.serverName = endpointName;
    entry.executablePath = QCoreApplication::applicationFilePath();
    entry.sharedMemoryKey = sharedMemoryKey.trimmed();
    entry.startedAtUtc = QDateTime::currentDateTimeUtc();

    if (LocalCliRegistry::registerInstance(entry))
        registryEntry = entry;
}

void LocalCliEndpointPrivate::unregisterInstance()
{
    if (registryEntry.pid <= 0)
        return;
    LocalCliRegistry::unregisterInstance(registryEntry.pid);
    registryEntry = LocalCliRegistry::Entry();
}

LocalCliEndpoint::LocalCliEndpoint(QObject *parent)
    : QObject(parent)
    , d(new LocalCliEndpointPrivate(this))
{
    d->endpointName = LocalCliRegistry::defaultServerName(d->processPid);
    d->server = new QLocalServer(this);
    QObject::connect(d->server, &QLocalServer::newConnection, this, [this]() {
        d->handleNewConnection();
    });
}

LocalCliEndpoint::~LocalCliEndpoint()
{
    stop();
    delete d;
}

void LocalCliEndpoint::setEndpointName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        d->endpointName = LocalCliRegistry::defaultServerName(d->processPid);
    else
        d->endpointName = trimmed;
}

void LocalCliEndpoint::setKind(LocalCliRegistry::Kind kind)
{
    d->kind = kind;
}

void LocalCliEndpoint::setSharedMemoryKey(const QString &key)
{
    d->sharedMemoryKey = key.trimmed();
}

void LocalCliEndpoint::setWelcomeInfo(const QString &text)
{
    d->welcomeInfo = text;
}

void LocalCliEndpoint::setCommandHandler(CommandHandler handler)
{
    d->commandHandler = std::move(handler);
}

void LocalCliEndpoint::setCompletionHandler(CompletionHandler handler)
{
    d->completionHandler = std::move(handler);
}

QString LocalCliEndpoint::endpointName() const
{
    return d->endpointName;
}

bool LocalCliEndpoint::isRunning() const
{
    return d->server && d->server->isListening();
}

bool LocalCliEndpoint::start()
{
    if (!d->server)
        d->server = new QLocalServer(this);

    if (d->server->isListening())
        return true;

    QLocalServer::removeServer(d->endpointName);
    if (!d->server->listen(d->endpointName)) {
        qCWarning(lcLocalCliEndpoint).nospace()
                << "Unable to start local CLI server on \"" << qPrintable(d->endpointName)
                << "\": " << qPrintable(d->server->errorString()) << ".";
        return false;
    }

    d->registerInstance();
    qCInfo(lcLocalCliEndpoint).nospace()
            << "Local CLI endpoint listening on \"" << qPrintable(d->endpointName) << "\".";
    return true;
}

void LocalCliEndpoint::stop()
{
    const QString reason = d->kind == LocalCliRegistry::Kind::Web
                                   ? tr("booking-resources-web process is shutting down.")
                                   : tr("booking-resources-server process is shutting down.");
    d->dropAllSessions(reason);

    if (d->server && d->server->isListening())
        d->server->close();

    d->unregisterInstance();
    QLocalServer::removeServer(d->endpointName);
}
