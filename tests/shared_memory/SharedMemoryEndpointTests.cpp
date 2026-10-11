#include <cstring>
#include "TestRegistry.h"

#include <QtEndian>
#include <QtTest/QtTest>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QSharedMemory>
#include <QScopedPointer>

#include "SharedMemoryEndpoint.h"

namespace {

constexpr quint32 kMagic = 0x42525742U; // BRWB
constexpr qsizetype kTestSegmentSize = SharedMemoryProtocol::kMaxPayloadSize + 32 + 4;

QString uniqueKey(const QString &name)
{
    return QStringLiteral("booking-resources-shm-test-%1-%2")
            .arg(name, SharedMemoryProtocol::createRequestId());
}

void writeU32(char *target, qsizetype offset, quint32 value)
{
    qToLittleEndian(value, reinterpret_cast<uchar *>(target + offset));
}

SharedMemoryProtocol::Frame makeFrame(int sequence, int payloadSize = 0)
{
    SharedMemoryProtocol::Frame frame;
    frame.type = QStringLiteral("message");
    frame.requestId = SharedMemoryProtocol::createRequestId();
    frame.payload.insert(QStringLiteral("sequence"), sequence);
    if (payloadSize > 0)
        frame.payload.insert(QStringLiteral("text"), QString(payloadSize, QLatin1Char('x')));
    return frame;
}

bool sendFrameWithRetry(SharedMemoryEndpoint *endpoint,
                        const SharedMemoryProtocol::Frame &frame,
                        int timeoutMs)
{
    if (!endpoint)
        return false;

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        const SharedMemoryEndpoint::SendStatus status = endpoint->sendFrameStatus(frame);
        if (status == SharedMemoryEndpoint::SendStatus::Success)
            return true;
        if (status == SharedMemoryEndpoint::SendStatus::Failed)
            return false;

        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(1);
    }
    return false;
}

QSharedMemory *createLegacySegment(const QString &key)
{
    QSharedMemory *memory = new QSharedMemory(key);
    if (!memory->create(static_cast<int>(kTestSegmentSize))) {
        delete memory;
        return nullptr;
    }
    if (!memory->lock()) {
        delete memory;
        return nullptr;
    }
    char *data = static_cast<char *>(memory->data());
    std::memset(data, 0, static_cast<size_t>(kTestSegmentSize));
    writeU32(data, 0, kMagic);
    writeU32(data, 4, SharedMemoryProtocol::kProtocolVersion);
    writeU32(data, 8, 1);
    writeU32(data, 12, 0);
    if (!memory->unlock()) {
        delete memory;
        return nullptr;
    }
    return memory;
}

} // namespace

class SharedMemoryEndpointTests : public QObject
{
    Q_OBJECT

private slots:
    void ringBufferWrapAround();
    void fullQueueReportsBusy();
    void invalidFrameReportsFailed();
    void legacyLayoutIsReset();
    void burstNotificationsAreCoalesced();
    void clientCanRestart();
    void invalidFrameDoesNotBlockFollowingFrame();
    void sustainedTrafficSoak();
};

void SharedMemoryEndpointTests::ringBufferWrapAround()
{
    const QString key = uniqueKey(QStringLiteral("wrap"));
    SharedMemoryEndpoint server(SharedMemoryEndpoint::Role::Server, key);
    SharedMemoryEndpoint client(SharedMemoryEndpoint::Role::Client, key);
    QVector<int> receivedSequences;

    QObject::connect(&server,
                     &SharedMemoryEndpoint::frameReceived,
                     &server,
                     [&receivedSequences](const SharedMemoryProtocol::Frame &frame) {
                         receivedSequences.append(
                                 frame.payload.value(QStringLiteral("sequence")).toInt());
                     });

    QVERIFY(server.start());
    QVERIFY(client.start());

    constexpr int kFrameCount = 180;
    constexpr int kPayloadSize = 32 * 1024;
    for (int i = 0; i < kFrameCount; ++i) {
        QVERIFY(sendFrameWithRetry(&client, makeFrame(i, kPayloadSize), 5000));
    }

    QTRY_COMPARE_WITH_TIMEOUT(receivedSequences.size(), kFrameCount, 5000);
    for (int i = 0; i < kFrameCount; ++i)
        QCOMPARE(receivedSequences.at(i), i);
}

void SharedMemoryEndpointTests::fullQueueReportsBusy()
{
    const QString key = uniqueKey(QStringLiteral("busy"));
    SharedMemoryEndpoint server(SharedMemoryEndpoint::Role::Server, key);
    SharedMemoryEndpoint client(SharedMemoryEndpoint::Role::Client, key);

    QVERIFY(server.start());
    QVERIFY(client.start());
    server.stop();

    SharedMemoryEndpoint::SendStatus status = SharedMemoryEndpoint::SendStatus::Success;
    for (int i = 0; i < 1000; ++i) {
        status = client.sendFrameStatus(makeFrame(i, 128 * 1024));
        if (status != SharedMemoryEndpoint::SendStatus::Success)
            break;
    }

    QVERIFY(status == SharedMemoryEndpoint::SendStatus::Busy);
}

void SharedMemoryEndpointTests::invalidFrameReportsFailed()
{
    const QString key = uniqueKey(QStringLiteral("failed"));
    SharedMemoryEndpoint server(SharedMemoryEndpoint::Role::Server, key);
    SharedMemoryEndpoint client(SharedMemoryEndpoint::Role::Client, key);
    SharedMemoryProtocol::Frame invalidFrame;

    QVERIFY(server.start());
    QVERIFY(client.start());
    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(
                                 QStringLiteral(".*Cannot serialize shared memory frame.*")));
    QVERIFY(client.sendFrameStatus(invalidFrame) == SharedMemoryEndpoint::SendStatus::Failed);
}

void SharedMemoryEndpointTests::legacyLayoutIsReset()
{
    const QString key = uniqueKey(QStringLiteral("legacy"));
    QScopedPointer<QSharedMemory> requests(createLegacySegment(key + QStringLiteral(".requests")));
    QScopedPointer<QSharedMemory> responses(
            createLegacySegment(key + QStringLiteral(".responses")));
    QVERIFY(!requests.isNull());
    QVERIFY(!responses.isNull());

    SharedMemoryEndpoint server(SharedMemoryEndpoint::Role::Server, key);
    SharedMemoryEndpoint client(SharedMemoryEndpoint::Role::Client, key);
    QVector<int> receivedSequences;

    QObject::connect(&server,
                     &SharedMemoryEndpoint::frameReceived,
                     &server,
                     [&receivedSequences](const SharedMemoryProtocol::Frame &frame) {
                         receivedSequences.append(
                                 frame.payload.value(QStringLiteral("sequence")).toInt());
                     });

    QVERIFY(server.start());
    QVERIFY(client.start());
    QVERIFY(client.sendFrameStatus(makeFrame(7)) == SharedMemoryEndpoint::SendStatus::Success);

    QTRY_COMPARE_WITH_TIMEOUT(receivedSequences.size(), 1, 5000);
    QCOMPARE(receivedSequences.constFirst(), 7);
}

void SharedMemoryEndpointTests::burstNotificationsAreCoalesced()
{
    const QString key = uniqueKey(QStringLiteral("burst"));
    SharedMemoryEndpoint server(SharedMemoryEndpoint::Role::Server, key);
    SharedMemoryEndpoint client(SharedMemoryEndpoint::Role::Client, key);
    QVector<int> receivedSequences;

    QObject::connect(&server,
                     &SharedMemoryEndpoint::frameReceived,
                     &server,
                     [&receivedSequences](const SharedMemoryProtocol::Frame &frame) {
                         receivedSequences.append(
                                 frame.payload.value(QStringLiteral("sequence")).toInt());
                     });

    QVERIFY(server.start());
    QVERIFY(client.start());

    constexpr int kFrameCount = 2000;
    for (int i = 0; i < kFrameCount; ++i)
        QVERIFY(client.sendFrameStatus(makeFrame(i)) == SharedMemoryEndpoint::SendStatus::Success);

    // Let the notification thread consume semaphore tokens while the main event loop is paused.
    QTest::qSleep(20);
    QTRY_COMPARE_WITH_TIMEOUT(receivedSequences.size(), kFrameCount, 5000);
    QVERIFY(server.coalescedWakeupCount() > 0);
    QVERIFY(server.pollBatchCount() < static_cast<quint64>(kFrameCount));

    for (int i = 0; i < kFrameCount; ++i)
        QCOMPARE(receivedSequences.at(i), i);
}

void SharedMemoryEndpointTests::clientCanRestart()
{
    const QString key = uniqueKey(QStringLiteral("restart"));
    SharedMemoryEndpoint server(SharedMemoryEndpoint::Role::Server, key);
    QVector<int> receivedSequences;

    QObject::connect(&server,
                     &SharedMemoryEndpoint::frameReceived,
                     &server,
                     [&receivedSequences](const SharedMemoryProtocol::Frame &frame) {
                         receivedSequences.append(
                                 frame.payload.value(QStringLiteral("sequence")).toInt());
                     });

    QVERIFY(server.start());
    {
        SharedMemoryEndpoint client(SharedMemoryEndpoint::Role::Client, key);
        QVERIFY(client.start());
        QVERIFY(sendFrameWithRetry(&client, makeFrame(1), 1000));
        QTRY_COMPARE_WITH_TIMEOUT(receivedSequences.size(), 1, 1000);
    }

    {
        SharedMemoryEndpoint client(SharedMemoryEndpoint::Role::Client, key);
        QVERIFY(client.start());
        QVERIFY(sendFrameWithRetry(&client, makeFrame(2), 1000));
        QTRY_COMPARE_WITH_TIMEOUT(receivedSequences.size(), 2, 1000);
    }

    QCOMPARE(receivedSequences, QVector<int>({ 1, 2 }));
}

void SharedMemoryEndpointTests::invalidFrameDoesNotBlockFollowingFrame()
{
    const QString key = uniqueKey(QStringLiteral("invalid-followed-by-valid"));
    SharedMemoryEndpoint server(SharedMemoryEndpoint::Role::Server, key);
    QVector<int> receivedSequences;
    server.setAuthenticationSecret(QByteArrayLiteral("correct-secret"));

    QObject::connect(&server,
                     &SharedMemoryEndpoint::frameReceived,
                     &server,
                     [&receivedSequences](const SharedMemoryProtocol::Frame &frame) {
                         receivedSequences.append(
                                 frame.payload.value(QStringLiteral("sequence")).toInt());
                     });
    QVERIFY(server.start());

    {
        SharedMemoryEndpoint client(SharedMemoryEndpoint::Role::Client, key);
        client.setAuthenticationSecret(QByteArrayLiteral("wrong-secret"));
        QVERIFY(client.start());
        QVERIFY(client.sendFrameStatus(makeFrame(1)) == SharedMemoryEndpoint::SendStatus::Success);
    }
    {
        SharedMemoryEndpoint client(SharedMemoryEndpoint::Role::Client, key);
        client.setAuthenticationSecret(QByteArrayLiteral("correct-secret"));
        QVERIFY(client.start());
        QVERIFY(client.sendFrameStatus(makeFrame(2)) == SharedMemoryEndpoint::SendStatus::Success);
    }

    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(
                                 QStringLiteral(".*Failed to parse shared memory frame.*")));
    QTRY_COMPARE_WITH_TIMEOUT(receivedSequences.size(), 1, 1000);
    QCOMPARE(receivedSequences.constFirst(), 2);
}

void SharedMemoryEndpointTests::sustainedTrafficSoak()
{
    bool durationOk = false;
    const int durationSeconds =
            qEnvironmentVariableIntValue("BOOKING_RESOURCES_IPC_SOAK_SECONDS", &durationOk);
    if (!durationOk || durationSeconds <= 0) {
        QSKIP("Set BOOKING_RESOURCES_IPC_SOAK_SECONDS to enable the IPC soak scenario.");
    }

    const QString key = uniqueKey(QStringLiteral("soak"));
    SharedMemoryEndpoint server(SharedMemoryEndpoint::Role::Server, key);
    SharedMemoryEndpoint client(SharedMemoryEndpoint::Role::Client, key);
    int receivedCount = 0;
    QObject::connect(&server,
                     &SharedMemoryEndpoint::frameReceived,
                     &server,
                     [&receivedCount](const SharedMemoryProtocol::Frame &) { ++receivedCount; });

    QVERIFY(server.start());
    QVERIFY(client.start());

    QElapsedTimer timer;
    timer.start();
    int sentCount = 0;
    while (timer.elapsed() < static_cast<qint64>(durationSeconds) * 1000) {
        for (int i = 0; i < 16; ++i) {
            const SharedMemoryEndpoint::SendStatus status =
                    client.sendFrameStatus(makeFrame(sentCount));
            if (status == SharedMemoryEndpoint::SendStatus::Busy)
                break;
            QVERIFY(status == SharedMemoryEndpoint::SendStatus::Success);
            ++sentCount;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(1);
    }

    QTRY_COMPARE_WITH_TIMEOUT(receivedCount, sentCount, 5000);
    QCOMPARE(server.notificationErrorCount(), quint64(0));
    QVERIFY(server.pollBatchCount() < static_cast<quint64>(sentCount));
}

BOOKING_RESOURCES_REGISTER_TEST(SharedMemoryEndpointTests)

#include "SharedMemoryEndpointTests.moc"
