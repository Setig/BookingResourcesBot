#include <QtTest>
#include "TestRegistry.h"

#include "LocalCliProtocol.h"

#include <QtEndian>
#include <QJsonObject>
#include <QJsonDocument>

namespace {

QByteArray encodePayload(const QByteArray &payload)
{
    char header[sizeof(quint32)] = {};
    qToLittleEndian(static_cast<quint32>(payload.size()), reinterpret_cast<uchar *>(header));

    QByteArray buffer;
    buffer.append(header, static_cast<int>(sizeof(quint32)));
    buffer.append(payload);
    return buffer;
}

QByteArray encodeEnvelope(const QJsonObject &envelope)
{
    return encodePayload(QJsonDocument(envelope).toJson(QJsonDocument::Compact));
}

} // namespace

class LocalCliProtocolTests : public QObject
{
    Q_OBJECT

private slots:
    void roundTripFrame();
    void keepsIncompletePayloadBuffered();
    void takesOneFrameAndLeavesNextFrameBuffered();
    void rejectsMalformedJsonPayload();
    void rejectsEnvelopeWithoutFrameType();
    void rejectsEmptyPayloadHeader();
    void rejectsOversizedPayloadHeader();
    void serializeRejectsOversizedPayload();
};

void LocalCliProtocolTests::roundTripFrame()
{
    LocalCliProtocol::Frame frame;
    frame.type = QStringLiteral("ping");
    QJsonObject payload;
    payload.insert(QStringLiteral("text"), QStringLiteral("hello"));
    frame.payload = payload;

    const QByteArray encoded = LocalCliProtocol::serialize(frame);
    QVERIFY(!encoded.isEmpty());

    QByteArray buffer = encoded;
    LocalCliProtocol::Frame decoded;
    QVERIFY(LocalCliProtocol::takeFrame(&buffer, &decoded));
    QCOMPARE(decoded.type, frame.type);
    QCOMPARE(decoded.payload, frame.payload);
    QVERIFY(buffer.isEmpty());
}

void LocalCliProtocolTests::keepsIncompletePayloadBuffered()
{
    LocalCliProtocol::Frame frame;
    frame.type = QStringLiteral("hello");
    frame.payload.insert(QStringLiteral("pid"), 1234);

    const QByteArray encoded = LocalCliProtocol::serialize(frame);
    QVERIFY(encoded.size() > static_cast<int>(sizeof(quint32)) + 2);

    const int splitAt = static_cast<int>(sizeof(quint32)) + 2;
    QByteArray buffer = encoded.left(splitAt);

    LocalCliProtocol::Frame decoded;
    QVERIFY(!LocalCliProtocol::takeFrame(&buffer, &decoded));
    QCOMPARE(buffer, encoded.left(splitAt));

    buffer.append(encoded.mid(splitAt));
    QVERIFY(LocalCliProtocol::takeFrame(&buffer, &decoded));
    QCOMPARE(decoded.type, frame.type);
    QCOMPARE(decoded.payload, frame.payload);
    QVERIFY(buffer.isEmpty());
}

void LocalCliProtocolTests::takesOneFrameAndLeavesNextFrameBuffered()
{
    LocalCliProtocol::Frame first;
    first.type = QStringLiteral("hello");
    first.payload.insert(QStringLiteral("pid"), 1234);

    LocalCliProtocol::Frame second;
    second.type = QStringLiteral("command");
    second.payload.insert(QStringLiteral("text"), QStringLiteral("/help"));

    const QByteArray firstEncoded = LocalCliProtocol::serialize(first);
    const QByteArray secondEncoded = LocalCliProtocol::serialize(second);
    QByteArray buffer = firstEncoded + secondEncoded;

    LocalCliProtocol::Frame decoded;
    QVERIFY(LocalCliProtocol::takeFrame(&buffer, &decoded));
    QCOMPARE(decoded.type, first.type);
    QCOMPARE(decoded.payload, first.payload);
    QCOMPARE(buffer, secondEncoded);

    QVERIFY(LocalCliProtocol::takeFrame(&buffer, &decoded));
    QCOMPARE(decoded.type, second.type);
    QCOMPARE(decoded.payload, second.payload);
    QVERIFY(buffer.isEmpty());
}

void LocalCliProtocolTests::rejectsMalformedJsonPayload()
{
    QByteArray buffer = encodePayload(QByteArrayLiteral("{not-json"));

    LocalCliProtocol::Frame decoded;
    QVERIFY(!LocalCliProtocol::takeFrame(&buffer, &decoded));
    QVERIFY(buffer.isEmpty());
}

void LocalCliProtocolTests::rejectsEnvelopeWithoutFrameType()
{
    QJsonObject payload;
    payload.insert(QStringLiteral("text"), QStringLiteral("hello"));

    QJsonObject envelope;
    envelope.insert(QStringLiteral("payload"), payload);
    QByteArray buffer = encodeEnvelope(envelope);

    LocalCliProtocol::Frame decoded;
    QVERIFY(!LocalCliProtocol::takeFrame(&buffer, &decoded));
    QVERIFY(buffer.isEmpty());
}

void LocalCliProtocolTests::rejectsEmptyPayloadHeader()
{
    QByteArray buffer;
    buffer.resize(static_cast<int>(sizeof(quint32)));
    char header[sizeof(quint32)] = {};
    qToLittleEndian(static_cast<quint32>(0), reinterpret_cast<uchar *>(header));
    buffer.clear();
    buffer.append(header, static_cast<int>(sizeof(quint32)));

    LocalCliProtocol::Frame decoded;
    QVERIFY(!LocalCliProtocol::takeFrame(&buffer, &decoded));
    QVERIFY(buffer.isEmpty());
}

void LocalCliProtocolTests::rejectsOversizedPayloadHeader()
{
    QByteArray buffer;
    buffer.resize(static_cast<int>(sizeof(quint32)));

    const quint32 oversized = static_cast<quint32>(LocalCliProtocol::kMaxPayloadSize + 1);
    char header[sizeof(quint32)] = {};
    qToLittleEndian(oversized, reinterpret_cast<uchar *>(header));
    buffer.clear();
    buffer.append(header, static_cast<int>(sizeof(quint32)));

    LocalCliProtocol::Frame decoded;
    QVERIFY(!LocalCliProtocol::takeFrame(&buffer, &decoded));
    QVERIFY(buffer.isEmpty());
}

void LocalCliProtocolTests::serializeRejectsOversizedPayload()
{
    LocalCliProtocol::Frame frame;
    frame.type = QStringLiteral("payload");
    frame.payload.insert(QStringLiteral("text"),
                         QString(static_cast<int>(LocalCliProtocol::kMaxPayloadSize),
                                 QLatin1Char('x')));

    const QByteArray encoded = LocalCliProtocol::serialize(frame);
    QVERIFY(encoded.isEmpty());
}

BOOKING_RESOURCES_REGISTER_TEST(LocalCliProtocolTests)

#include "LocalCliProtocolTests.moc"
