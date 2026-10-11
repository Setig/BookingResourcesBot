#include <QtTest/QtTest>
#include "TestRegistry.h"

#include "SharedMemoryProtocol.h"

class SharedMemoryProtocolTests : public QObject
{
    Q_OBJECT

private slots:
    void roundTripFrame();
    void roundTripAuthenticatedFrame();
    void rejectInvalidAuthentication();
    void allowLegacyFrameWithoutSecret();
    void rejectInvalidFrame();
};

void SharedMemoryProtocolTests::roundTripFrame()
{
    SharedMemoryProtocol::Frame frame;
    frame.type = QStringLiteral("message");
    frame.requestId = SharedMemoryProtocol::createRequestId();
    frame.payload.insert(QStringLiteral("text"), QStringLiteral("hello"));

    SharedMemoryProtocol::Frame parsed;
    QString error;
    QVERIFY(SharedMemoryProtocol::parse(SharedMemoryProtocol::serialize(frame), &parsed, &error));
    QCOMPARE(parsed.version, SharedMemoryProtocol::kProtocolVersion);
    QCOMPARE(parsed.type, frame.type);
    QCOMPARE(parsed.requestId, frame.requestId);
    QCOMPARE(parsed.payload.value(QStringLiteral("text")).toString(), QStringLiteral("hello"));
}

void SharedMemoryProtocolTests::roundTripAuthenticatedFrame()
{
    SharedMemoryProtocol::Frame frame;
    frame.type = QStringLiteral("message");
    frame.requestId = SharedMemoryProtocol::createRequestId();
    frame.payload.insert(QStringLiteral("text"), QStringLiteral("hello"));

    SharedMemoryProtocol::Frame parsed;
    QString error;
    const QByteArray data =
            SharedMemoryProtocol::serialize(frame, QByteArrayLiteral("shared-secret"));
    QVERIFY(SharedMemoryProtocol::parse(data, &parsed, &error, QByteArrayLiteral("shared-secret")));
    QCOMPARE(parsed.type, frame.type);
    QVERIFY(!parsed.authentication.isEmpty());
}

void SharedMemoryProtocolTests::rejectInvalidAuthentication()
{
    SharedMemoryProtocol::Frame frame;
    frame.type = QStringLiteral("message");
    frame.requestId = SharedMemoryProtocol::createRequestId();

    SharedMemoryProtocol::Frame parsed;
    QString error;
    const QByteArray data =
            SharedMemoryProtocol::serialize(frame, QByteArrayLiteral("shared-secret"));
    QVERIFY(!SharedMemoryProtocol::parse(data, &parsed, &error, QByteArrayLiteral("other-secret")));
    QVERIFY(!error.isEmpty());
}

void SharedMemoryProtocolTests::allowLegacyFrameWithoutSecret()
{
    SharedMemoryProtocol::Frame frame;
    frame.type = QStringLiteral("message");
    frame.requestId = SharedMemoryProtocol::createRequestId();

    SharedMemoryProtocol::Frame parsed;
    QString error;
    const QByteArray data = SharedMemoryProtocol::serialize(frame);
    QVERIFY(SharedMemoryProtocol::parse(data, &parsed, &error));
    QVERIFY(parsed.authentication.isEmpty());
}

void SharedMemoryProtocolTests::rejectInvalidFrame()
{
    SharedMemoryProtocol::Frame parsed;
    QString error;
    QVERIFY(!SharedMemoryProtocol::parse(QByteArrayLiteral("{}"), &parsed, &error));
    QVERIFY(!error.isEmpty());
}

BOOKING_RESOURCES_REGISTER_TEST(SharedMemoryProtocolTests)

#include "SharedMemoryProtocolTests.moc"
