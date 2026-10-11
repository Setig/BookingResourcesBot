#include "LocalCliBot.h"

#include "LocalCliHelper.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QCoreApplication>

Q_LOGGING_CATEGORY(lcServerLocalCli, "booking.server.localcli")

namespace {

QString normalizedCommand(const QString &command)
{
    return command.trimmed();
}

} // namespace

LocalCliBot::LocalCliBot(QString endpointName, QObject *parent)
    : AbstractBot(parent)
    , m_endpointName(endpointName.trimmed())
    , m_completionHandler()
    , m_processPid(QCoreApplication::applicationPid())
    , m_sessionCounter(0)
{
    if (m_endpointName.isEmpty())
        m_endpointName = LocalCliRegistry::defaultServerName(m_processPid);

    m_server.reset(new QLocalServer(this));
    QObject::connect(m_server.data(),
                     &QLocalServer::newConnection,
                     this,
                     &LocalCliBot::handleNewConnection);
}

LocalCliBot::~LocalCliBot()
{
    dropAllSessions(QString());
}

MessengerType LocalCliBot::messenger() const
{
    return MessengerType::LocalCli;
}

AbstractBot::Capabilities LocalCliBot::capabilities() const
{
    return AbstractBot::Capabilities();
}

bool LocalCliBot::isRunning() const
{
    return m_server && m_server->isListening();
}

bool LocalCliBot::fetchUserProfile(const QString &userId,
                                   AbstractBot::UserProfile *profileOut,
                                   QString *errorDescription)
{
    Q_UNUSED(errorDescription);

    if (!profileOut)
        return true;

    profileOut->userId = userId;
    profileOut->username = QStringLiteral("local-cli:%1").arg(userId);
    profileOut->firstName.clear();
    profileOut->lastName.clear();
    profileOut->languageCode.clear();
    profileOut->isBot = false;
    return true;
}

void LocalCliBot::start()
{
    if (!m_server)
        m_server.reset(new QLocalServer(this));

    if (m_server->isListening())
        return;

    QLocalServer::removeServer(m_endpointName);
    if (!m_server->listen(m_endpointName)) {
        emit errorOccurred(QStringLiteral("Unable to start local CLI server on '%1': %2.")
                                   .arg(m_endpointName, m_server->errorString()));
        return;
    }

    registerInstance();
    qCInfo(lcServerLocalCli).nospace()
            << "Local CLI endpoint listening on \"" << qPrintable(m_endpointName) << "\".";
}

void LocalCliBot::stop()
{
    shutdown(tr("booking-resources-server process is shutting down."));
}

void LocalCliBot::sendMessage(const AbstractBot::ChatAddress &chat,
                              const QString &text,
                              const AbstractBot::MessageOptions &options)
{
    Q_UNUSED(options);

    if (chat.messenger != MessengerType::LocalCli)
        return;

    Session *session = m_sessionsByChatId.value(chat.chatId, nullptr);
    if (!session)
        return;

    sendResponse(session, text);
}

void LocalCliBot::sendFile(const AbstractBot::ChatAddress &chat,
                           const QString &filePath,
                           const QString &caption,
                           AbstractBot::MessageFormat captionFormat)
{
    Q_UNUSED(captionFormat);

    if (chat.messenger != MessengerType::LocalCli)
        return;
    if (!chat.isValid())
        return;

    QString text = caption.trimmed();
    if (!text.isEmpty())
        text.append(QStringLiteral("\n"));
    text.append(tr("File saved at: %1").arg(filePath));
    sendMessage(chat, text, AbstractBot::MessageOptions());
}

void LocalCliBot::handleNewConnection()
{
    if (!m_server)
        return;

    while (m_server->hasPendingConnections()) {
        QLocalSocket *socket = m_server->nextPendingConnection();
        if (!socket)
            continue;

        socket->setParent(this);
        Session *session = new Session;
        session->socket = socket;

        m_sessions.insert(socket, session);

        QObject::connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
            handleSocketReadyRead(socket);
        });
        QObject::connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
            handleSocketDisconnected(socket);
        });
        QObject::connect(socket,
                         qOverload<QLocalSocket::LocalSocketError>(&QLocalSocket::errorOccurred),
                         this,
                         [this, socket](QLocalSocket::LocalSocketError error) {
                             handleSocketError(socket, error);
                         });
    }
}

void LocalCliBot::handleSocketReadyRead(QLocalSocket *socket)
{
    Session *session = m_sessions.value(socket, nullptr);
    if (!session || session->closing || !session->socket)
        return;

    session->buffer.append(socket->readAll());

    while (true) {
        session = m_sessions.value(socket, nullptr);
        if (!session || session->closing || !session->socket)
            break;

        LocalCliProtocol::Frame frame;
        if (!LocalCliProtocol::takeFrame(&session->buffer, &frame))
            break;

        processFrame(session, frame);
    }
}

void LocalCliBot::handleSocketDisconnected(QLocalSocket *socket)
{
    Session *session = m_sessions.value(socket, nullptr);
    if (!session || session->closing)
        return;

    // Close on the event loop so a peer-close during sendFrame/flush cannot
    // delete the Session while the caller still holds it.
    QMetaObject::invokeMethod(
            this,
            [this, socket]() {
                Session *current = m_sessions.value(socket, nullptr);
                if (current)
                    closeSession(current);
            },
            Qt::QueuedConnection);
}

void LocalCliBot::handleSocketError(QLocalSocket *socket, QLocalSocket::LocalSocketError error)
{
    Q_UNUSED(socket);
    if (error == QLocalSocket::PeerClosedError)
        return;

    qCWarning(lcServerLocalCli).nospace()
            << "Local CLI socket error (" << static_cast<int>(error) << ").";
}

void LocalCliBot::processFrame(Session *session, const LocalCliProtocol::Frame &frame)
{
    if (!session)
        return;

    const QString type = frame.type.trimmed().toLower();
    if (!session->handshaken) {
        if (type == QLatin1String("hello")) {
            handleHandshake(session, frame);
        } else {
            sendError(session, tr("Handshake is required before sending commands."));
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
        sendInfo(session, tr("pong"));
    } else {
        sendError(session, tr("Unsupported frame type: %1").arg(type));
    }
}

void LocalCliBot::handleHandshake(Session *session, const LocalCliProtocol::Frame &frame)
{
    if (!session)
        return;
    if (session->handshaken) {
        sendError(session, tr("Handshake already completed."));
        return;
    }

    const qint64 clientPid =
            static_cast<qint64>(frame.payload.value(QStringLiteral("pid")).toDouble(-1));
    if (clientPid <= 0) {
        sendError(session, tr("Client PID is missing in handshake."));
        closeSession(session, QStringLiteral("Invalid handshake."));
        return;
    }

    session->clientPid = clientPid;
    session->userId = QString::number(clientPid);
    session->label = frame.payload.value(QStringLiteral("label")).toString().trimmed();
    if (session->label.isEmpty())
        session->label = LocalCliHelper::localHostLabel();

    session->chatId =
            QStringLiteral("%1-%2").arg(session->userId, QString::number(++m_sessionCounter));
    session->handshaken = true;
    m_sessionsByChatId.insert(session->chatId, session);

    qCInfo(lcServerLocalCli).nospace()
            << "Local CLI connected (client_pid=" << session->clientPid
            << ", chat=" << session->chatId << ", label=" << session->label << ')';

    LocalCliProtocol::Frame ack;
    ack.type = QStringLiteral("hello-ack");
    QJsonObject payload;
    payload.insert(QStringLiteral("pid"), static_cast<double>(m_processPid));
    payload.insert(QStringLiteral("server"), m_endpointName);
    QJsonArray features;
    features.append(QStringLiteral("complete"));
    payload.insert(QStringLiteral("features"), features);
    payload.insert(QStringLiteral("message"),
                   tr("Connected to booking-resources-server process %1.").arg(m_processPid));
    ack.payload = payload;
    sendFrame(session, ack);

    sendInfo(session,
             tr("Local CLI is restricted to admin slash-commands. "
                "Type '\\help' inside the CLI for usage details."));

    notifySessionStarted(session);
}

void LocalCliBot::handleCommand(Session *session, const QString &command)
{
    if (!session || !session->handshaken) {
        sendError(session, tr("Handshake is required before commands."));
        return;
    }

    const QString normalized = normalizedCommand(command);
    const quint64 messageSequence = session->nextMessageId;
    if (normalized.isEmpty()) {
        sendError(session, tr("Command payload is empty."));
        sendResult(session, QStringLiteral("error"), messageSequence);
        return;
    }

    session->nextMessageId++;

    AbstractBot::IncomingMessage message;
    message.chat.messenger = MessengerType::LocalCli;
    message.chat.chatId = session->chatId;
    message.sender.userId = session->userId;
    message.sender.username = session->label;
    message.text = command;
    message.messageId = QString::number(messageSequence);

    emit messageReceived(message);

    sendResult(session, QStringLiteral("ok"), messageSequence);
}

void LocalCliBot::setCompletionHandler(CompletionHandler handler)
{
    m_completionHandler = std::move(handler);
}

void LocalCliBot::handleComplete(Session *session, const LocalCliProtocol::Frame &frame)
{
    if (!session || !session->handshaken) {
        sendError(session, tr("Handshake is required before commands."));
        return;
    }

    const QString text = frame.payload.value(QStringLiteral("text")).toString();
    const int cursor = frame.payload.value(QStringLiteral("cursor")).toInt(text.size());
    LocalCliProtocol::CompletionResult result;
    if (m_completionHandler)
        result = m_completionHandler(text, cursor, session->userId);
    sendFrame(session, LocalCliProtocol::makeCompleteResult(result));
}

void LocalCliBot::handleDisconnectRequest(Session *session)
{
    closeSession(session, tr("Client requested disconnection."));
}

void LocalCliBot::sendFrame(Session *session, const LocalCliProtocol::Frame &frame)
{
    if (!session || !session->socket)
        return;
    if (session->socket->state() != QLocalSocket::ConnectedState)
        return;

    session->socket->write(LocalCliProtocol::serialize(frame));
    session->socket->flush();
}

void LocalCliBot::sendResponse(Session *session, const QString &text)
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

void LocalCliBot::sendError(Session *session, const QString &text)
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

void LocalCliBot::sendResult(Session *session, const QString &status, quint64 messageSequence)
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

void LocalCliBot::sendInfo(Session *session, const QString &text)
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

void LocalCliBot::closeSession(Session *session, const QString &reason)
{
    if (!session || session->closing)
        return;

    session->closing = true;

    if (session->handshaken) {
        qCInfo(lcServerLocalCli).nospace()
                << "Local CLI disconnected (client_pid=" << session->clientPid
                << ", chat=" << session->chatId << ", label=" << session->label
                << ", reason=" << (reason.isEmpty() ? QStringLiteral("n/a") : reason) << ')';
    }

    QLocalSocket *socket = session->socket.data();
    if (socket)
        m_sessions.remove(socket);
    if (!session->chatId.isEmpty())
        m_sessionsByChatId.remove(session->chatId);

    if (socket) {
        socket->disconnect(this);
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

void LocalCliBot::registerInstance()
{
    LocalCliRegistry::Entry entry;
    entry.pid = m_processPid;
    entry.serverName = m_endpointName;
    entry.executablePath = QCoreApplication::applicationFilePath();
    entry.startedAtUtc = QDateTime::currentDateTimeUtc();

    if (LocalCliRegistry::registerInstance(entry))
        m_registryEntry = entry;
}

void LocalCliBot::unregisterInstance()
{
    if (m_registryEntry.pid <= 0)
        return;
    LocalCliRegistry::unregisterInstance(m_registryEntry.pid);
    m_registryEntry = LocalCliRegistry::Entry();
}

void LocalCliBot::notifySessionStarted(Session *session)
{
    if (!session || session->sessionNotified)
        return;

    session->sessionNotified = true;
    emit clientSessionStarted();
}

void LocalCliBot::notifySessionFinished(Session *session)
{
    if (!session || !session->sessionNotified)
        return;

    session->sessionNotified = false;
    emit clientSessionFinished();
}

void LocalCliBot::dropAllSessions(const QString &reason)
{
    const QList<Session *> snapshot = m_sessions.values();
    for (Session *session : snapshot) {
        closeSession(session, reason);
    }
    m_sessions.clear();
    m_sessionsByChatId.clear();
}

void LocalCliBot::shutdown(const QString &reason)
{
    dropAllSessions(reason);

    if (m_server && m_server->isListening())
        m_server->close();

    unregisterInstance();
    QLocalServer::removeServer(m_endpointName);
}
