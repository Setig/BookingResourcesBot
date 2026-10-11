#include <QtTest>
#include "TestRegistry.h"

#include "LocalCliEndpoint.h"
#include "LocalCliProtocol.h"
#include "LocalCliRegistry.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QUuid>

namespace {

QString uniqueEndpointName()
{
    return QStringLiteral("booking-resources-test-%1-%2")
            .arg(QCoreApplication::applicationPid())
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

void pumpIpc()
{
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QTest::qWait(1);
}

bool connectToEndpoint(QLocalSocket *socket, const QString &endpointName)
{
    socket->connectToServer(endpointName);
    if (!socket->waitForConnected(2000))
        return false;

    // Let QLocalServer accept the connection before the first write. On Windows the
    // client can report ConnectedState before newConnection is delivered.
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1000) {
        pumpIpc();
        if (socket->state() == QLocalSocket::ConnectedState)
            return true;
    }
    return socket->state() == QLocalSocket::ConnectedState;
}

bool writeFrame(QLocalSocket *socket, const LocalCliProtocol::Frame &frame)
{
    const QByteArray data = LocalCliProtocol::serialize(frame);
    if (data.isEmpty() || !socket || socket->state() != QLocalSocket::ConnectedState)
        return false;

    qint64 totalWritten = 0;
    QElapsedTimer timer;
    timer.start();
    while (totalWritten < data.size() && timer.elapsed() < 2000) {
        const qint64 written =
                socket->write(data.constData() + totalWritten, data.size() - totalWritten);
        if (written < 0)
            return false;
        if (written == 0) {
            pumpIpc();
            if (socket->bytesToWrite() > 0)
                socket->waitForBytesWritten(50);
            continue;
        }
        totalWritten += written;
    }

    socket->flush();
    pumpIpc();
    return totalWritten == data.size();
}

bool readFrame(QLocalSocket *socket,
               QByteArray *buffer,
               LocalCliProtocol::Frame *frame,
               int timeoutMs = 2000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (LocalCliProtocol::takeFrame(buffer, frame))
            return true;
        if (socket->bytesAvailable() > 0) {
            buffer->append(socket->readAll());
            continue;
        }
        socket->waitForReadyRead(20);
        pumpIpc();
        if (socket->bytesAvailable() > 0)
            buffer->append(socket->readAll());
    }
    return LocalCliProtocol::takeFrame(buffer, frame);
}

bool waitUntilDisconnected(QLocalSocket *socket, int timeoutMs = 1000)
{
    QElapsedTimer timer;
    timer.start();
    while (socket && socket->state() != QLocalSocket::UnconnectedState
           && timer.elapsed() < timeoutMs) {
        pumpIpc();
        if (socket->state() == QLocalSocket::UnconnectedState)
            return true;
        socket->waitForDisconnected(20);
    }
    return socket && socket->state() == QLocalSocket::UnconnectedState;
}

bool connectAndHandshake(QLocalSocket *socket,
                         const QString &endpointName,
                         QByteArray *buffer,
                         LocalCliProtocol::Frame *ack)
{
    if (!connectToEndpoint(socket, endpointName))
        return false;

    LocalCliProtocol::Frame hello;
    hello.type = QStringLiteral("hello");
    hello.payload.insert(QStringLiteral("pid"),
                         static_cast<double>(QCoreApplication::applicationPid()));
    hello.payload.insert(QStringLiteral("label"), QStringLiteral("test-cli"));
    if (!writeFrame(socket, hello))
        return false;
    return readFrame(socket, buffer, ack);
}

} // namespace

class LocalCliEndpointTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void listensAndRegisters();
    void handshakeHasNoSecret();
    void commandBeforeHandshakeIsRejected();
    void commandReturnsResponseAndResult();
    void byeClosesSession();
    void handshakeNotifiesClientSession();
    void clientAbortAfterHandshakeKeepsEndpointRunning();
    void registersWebEntryWithKey();
    void completeReturnsValues();
    void completeWithoutHandlerReturnsEmptyValues();
    void helloAckAdvertisesCompleteFeature();
};

void LocalCliEndpointTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void LocalCliEndpointTests::listensAndRegisters()
{
    LocalCliEndpoint endpoint;
    const QString name = uniqueEndpointName();
    endpoint.setEndpointName(name);
    QVERIFY(endpoint.start());
    QVERIFY(endpoint.isRunning());
    QCOMPARE(endpoint.endpointName(), name);

    LocalCliRegistry::Entry entry;
    QVERIFY(LocalCliRegistry::entryForPid(QCoreApplication::applicationPid(), &entry));
    QCOMPARE(entry.serverName, name);

    endpoint.stop();
    QVERIFY(!endpoint.isRunning());
}

void LocalCliEndpointTests::handshakeHasNoSecret()
{
    LocalCliEndpoint endpoint;
    const QString name = uniqueEndpointName();
    endpoint.setEndpointName(name);
    QVERIFY(endpoint.start());

    QLocalSocket socket;
    QByteArray buffer;
    LocalCliProtocol::Frame ack;
    QVERIFY(connectAndHandshake(&socket, name, &buffer, &ack));
    QCOMPARE(ack.type, QStringLiteral("hello-ack"));
    QVERIFY(!ack.payload.contains(QStringLiteral("secret")));
    QVERIFY(!ack.payload.contains(QStringLiteral("hmac")));
    QVERIFY(ack.payload.value(QStringLiteral("message"))
                    .toString()
                    .contains(QStringLiteral("booking-resources-server")));

    endpoint.stop();
}

void LocalCliEndpointTests::commandBeforeHandshakeIsRejected()
{
    LocalCliEndpoint endpoint;
    const QString name = uniqueEndpointName();
    endpoint.setEndpointName(name);
    endpoint.setCommandHandler([](const QString &) {
        LocalCliEndpoint::CommandResult result;
        result.ok = true;
        result.text = QStringLiteral("should-not-run");
        return result;
    });
    QVERIFY(endpoint.start());

    QLocalSocket socket;
    QVERIFY(connectToEndpoint(&socket, name));

    LocalCliProtocol::Frame command;
    command.type = QStringLiteral("command");
    command.payload.insert(QStringLiteral("text"), QStringLiteral("/help"));
    QVERIFY(writeFrame(&socket, command));

    QByteArray buffer;
    LocalCliProtocol::Frame error;
    QVERIFY(readFrame(&socket, &buffer, &error));
    QCOMPARE(error.type, QStringLiteral("error"));
    QVERIFY(!error.payload.value(QStringLiteral("text"))
                     .toString()
                     .contains(QStringLiteral("should-not-run")));

    endpoint.stop();
}

void LocalCliEndpointTests::commandReturnsResponseAndResult()
{
    LocalCliEndpoint endpoint;
    const QString name = uniqueEndpointName();
    endpoint.setEndpointName(name);
    endpoint.setCommandHandler([](const QString &command) {
        LocalCliEndpoint::CommandResult result;
        result.ok = command.contains(QLatin1String("/help"));
        result.text = result.ok ? QStringLiteral("ok-help") : QStringLiteral("bad");
        return result;
    });
    QVERIFY(endpoint.start());

    QLocalSocket socket;
    QByteArray buffer;
    LocalCliProtocol::Frame ack;
    QVERIFY(connectAndHandshake(&socket, name, &buffer, &ack));

    LocalCliProtocol::Frame command;
    command.type = QStringLiteral("command");
    command.payload.insert(QStringLiteral("text"), QStringLiteral("/help"));
    QVERIFY(writeFrame(&socket, command));

    LocalCliProtocol::Frame response;
    QVERIFY(readFrame(&socket, &buffer, &response));
    QCOMPARE(response.type, QStringLiteral("response"));
    QCOMPARE(response.payload.value(QStringLiteral("text")).toString(), QStringLiteral("ok-help"));

    LocalCliProtocol::Frame result;
    QVERIFY(readFrame(&socket, &buffer, &result));
    QCOMPARE(result.type, QStringLiteral("result"));
    QCOMPARE(result.payload.value(QStringLiteral("status")).toString(), QStringLiteral("ok"));

    endpoint.stop();
}

void LocalCliEndpointTests::byeClosesSession()
{
    LocalCliEndpoint endpoint;
    const QString name = uniqueEndpointName();
    endpoint.setEndpointName(name);
    QVERIFY(endpoint.start());

    QLocalSocket socket;
    QByteArray buffer;
    LocalCliProtocol::Frame ack;
    QVERIFY(connectAndHandshake(&socket, name, &buffer, &ack));

    LocalCliProtocol::Frame bye;
    bye.type = QStringLiteral("bye");
    QVERIFY(writeFrame(&socket, bye));
    QVERIFY(waitUntilDisconnected(&socket));

    endpoint.stop();
}

void LocalCliEndpointTests::handshakeNotifiesClientSession()
{
    LocalCliEndpoint endpoint;
    const QString name = uniqueEndpointName();
    endpoint.setEndpointName(name);
    QSignalSpy started(&endpoint, &LocalCliEndpoint::clientSessionStarted);
    QSignalSpy finished(&endpoint, &LocalCliEndpoint::clientSessionFinished);
    QVERIFY(endpoint.start());

    QLocalSocket socket;
    QByteArray buffer;
    LocalCliProtocol::Frame ack;
    QVERIFY(connectAndHandshake(&socket, name, &buffer, &ack));
    QTRY_COMPARE(started.count(), 1);
    QCOMPARE(finished.count(), 0);

    LocalCliProtocol::Frame bye;
    bye.type = QStringLiteral("bye");
    QVERIFY(writeFrame(&socket, bye));
    QVERIFY(waitUntilDisconnected(&socket));
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(started.count(), 1);

    endpoint.stop();
}

void LocalCliEndpointTests::clientAbortAfterHandshakeKeepsEndpointRunning()
{
    LocalCliEndpoint endpoint;
    const QString name = uniqueEndpointName();
    endpoint.setEndpointName(name);
    QSignalSpy started(&endpoint, &LocalCliEndpoint::clientSessionStarted);
    QSignalSpy finished(&endpoint, &LocalCliEndpoint::clientSessionFinished);
    QVERIFY(endpoint.start());

    {
        QLocalSocket socket;
        QByteArray buffer;
        LocalCliProtocol::Frame ack;
        QVERIFY(connectAndHandshake(&socket, name, &buffer, &ack));
        QTRY_COMPARE(started.count(), 1);
        socket.abort();
    }

    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(endpoint.isRunning());

    QLocalSocket socket;
    QByteArray buffer;
    LocalCliProtocol::Frame ack;
    QVERIFY(connectAndHandshake(&socket, name, &buffer, &ack));
    QTRY_COMPARE(started.count(), 2);

    endpoint.stop();
    QTRY_COMPARE(finished.count(), 2);
}

void LocalCliEndpointTests::registersWebEntryWithKey()
{
    LocalCliEndpoint endpoint;
    const QString name = uniqueEndpointName();
    const QString key = QStringLiteral("booking-resources-web-test");
    endpoint.setEndpointName(name);
    endpoint.setKind(LocalCliRegistry::Kind::Web);
    endpoint.setSharedMemoryKey(key);
    QVERIFY(endpoint.start());

    LocalCliRegistry::Entry entry;
    QVERIFY(LocalCliRegistry::entryForSharedMemoryKey(key, &entry));
    QVERIFY(entry.isWeb());
    QCOMPARE(entry.sharedMemoryKey, key);
    QCOMPARE(entry.serverName, name);

    endpoint.stop();
}

void LocalCliEndpointTests::completeReturnsValues()
{
    LocalCliEndpoint endpoint;
    const QString name = uniqueEndpointName();
    endpoint.setEndpointName(name);
    endpoint.setCompletionHandler([](const QString &text, int cursor) {
        LocalCliProtocol::CompletionResult result;
        result.replaceFrom = 0;
        result.replaceTo = text.size();
        result.values << QStringLiteral("/help") << QStringLiteral("/list_users");
        result.appendSpace = cursor >= 0;
        return result;
    });
    QVERIFY(endpoint.start());

    QLocalSocket socket;
    QByteArray buffer;
    LocalCliProtocol::Frame ack;
    QVERIFY(connectAndHandshake(&socket, name, &buffer, &ack));

    LocalCliProtocol::Frame request;
    request.type = QStringLiteral("complete");
    request.payload.insert(QStringLiteral("text"), QStringLiteral("/h"));
    request.payload.insert(QStringLiteral("cursor"), 2);
    QVERIFY(writeFrame(&socket, request));

    LocalCliProtocol::Frame reply;
    QVERIFY(readFrame(&socket, &buffer, &reply));
    QCOMPARE(reply.type, QStringLiteral("complete-result"));
    QCOMPARE(reply.payload.value(QStringLiteral("replace_from")).toInt(), 0);
    QCOMPARE(reply.payload.value(QStringLiteral("replace_to")).toInt(), 2);
    QCOMPARE(reply.payload.value(QStringLiteral("values")).toArray().size(), 2);
    QVERIFY(reply.payload.value(QStringLiteral("append_space")).toBool());

    endpoint.stop();
}

void LocalCliEndpointTests::completeWithoutHandlerReturnsEmptyValues()
{
    LocalCliEndpoint endpoint;
    const QString name = uniqueEndpointName();
    endpoint.setEndpointName(name);
    QVERIFY(endpoint.start());

    QLocalSocket socket;
    QByteArray buffer;
    LocalCliProtocol::Frame ack;
    QVERIFY(connectAndHandshake(&socket, name, &buffer, &ack));

    LocalCliProtocol::Frame request;
    request.type = QStringLiteral("complete");
    request.payload.insert(QStringLiteral("text"), QStringLiteral("/h"));
    request.payload.insert(QStringLiteral("cursor"), 2);
    QVERIFY(writeFrame(&socket, request));

    LocalCliProtocol::Frame reply;
    QVERIFY(readFrame(&socket, &buffer, &reply));
    QCOMPARE(reply.type, QStringLiteral("complete-result"));
    QVERIFY(reply.payload.value(QStringLiteral("values")).toArray().isEmpty());

    endpoint.stop();
}

void LocalCliEndpointTests::helloAckAdvertisesCompleteFeature()
{
    LocalCliEndpoint endpoint;
    const QString name = uniqueEndpointName();
    endpoint.setEndpointName(name);
    QVERIFY(endpoint.start());

    QLocalSocket socket;
    QByteArray buffer;
    LocalCliProtocol::Frame ack;
    QVERIFY(connectAndHandshake(&socket, name, &buffer, &ack));
    QCOMPARE(ack.type, QStringLiteral("hello-ack"));
    const QJsonArray features = ack.payload.value(QStringLiteral("features")).toArray();
    QVERIFY(features.contains(QStringLiteral("complete")));

    endpoint.stop();
}

BOOKING_RESOURCES_REGISTER_TEST(LocalCliEndpointTests)

#include "LocalCliEndpointTests.moc"
