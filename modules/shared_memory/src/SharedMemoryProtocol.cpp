#include "SharedMemoryProtocol.h"

#include <QUuid>
#include <QJsonValue>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>

Q_LOGGING_CATEGORY(lcSharedMemoryProtocol, "booking.modules.sharedmemory.protocol")

SharedMemoryProtocol::Frame::Frame()
    : version(SharedMemoryProtocol::kProtocolVersion)
{
    // do nothing
}

SharedMemoryProtocol::Frame::Frame(const Frame &other)
    : version(other.version)
    , type(other.type)
    , requestId(other.requestId)
    , authentication(other.authentication)
    , payload(other.payload)
{
    // do nothing
}

SharedMemoryProtocol::Frame::Frame(Frame &&other) noexcept
    : version(other.version)
    , type(std::move(other.type))
    , requestId(std::move(other.requestId))
    , authentication(std::move(other.authentication))
    , payload(std::move(other.payload))
{
    // do nothing
}

SharedMemoryProtocol::Frame &SharedMemoryProtocol::Frame::operator=(const Frame &other)
{
    if (this == &other)
        return *this;

    version = other.version;
    type = other.type;
    requestId = other.requestId;
    authentication = other.authentication;
    payload = other.payload;
    return *this;
}

SharedMemoryProtocol::Frame &SharedMemoryProtocol::Frame::operator=(Frame &&other) noexcept
{
    if (this == &other)
        return *this;

    version = other.version;
    type = std::move(other.type);
    requestId = std::move(other.requestId);
    authentication = std::move(other.authentication);
    payload = std::move(other.payload);
    return *this;
}

SharedMemoryProtocol::Frame::~Frame()
{
    // do nothing
}

namespace {

QJsonObject authenticationEnvelope(const SharedMemoryProtocol::Frame &frame)
{
    QJsonObject envelope;
    envelope.insert(QStringLiteral("version"), frame.version);
    envelope.insert(QStringLiteral("type"), frame.type.trimmed());
    envelope.insert(QStringLiteral("request_id"), frame.requestId.trimmed());
    envelope.insert(QStringLiteral("payload"), frame.payload);
    return envelope;
}

QByteArray authenticationPayload(const SharedMemoryProtocol::Frame &frame)
{
    return QJsonDocument(authenticationEnvelope(frame)).toJson(QJsonDocument::Compact);
}

QString authenticationCode(const SharedMemoryProtocol::Frame &frame, const QByteArray &secret)
{
    if (secret.isEmpty())
        return QString();

    return QString::fromLatin1(QMessageAuthenticationCode::hash(authenticationPayload(frame),
                                                                secret,
                                                                QCryptographicHash::Sha256)
                                       .toHex());
}

bool constantTimeEquals(const QByteArray &left, const QByteArray &right)
{
    if (left.size() != right.size())
        return false;

    uchar diff = 0;
    for (int i = 0; i < left.size(); ++i)
        diff |= static_cast<uchar>(left.at(i) ^ right.at(i));
    return diff == 0;
}

} // namespace

bool SharedMemoryProtocol::Frame::isValid() const
{
    return version == SharedMemoryProtocol::kProtocolVersion && !type.trimmed().isEmpty()
           && !requestId.trimmed().isEmpty();
}

QByteArray SharedMemoryProtocol::serialize(const SharedMemoryProtocol::Frame &frame,
                                           const QByteArray &authenticationSecret)
{
    if (!frame.isValid())
        return QByteArray();

    QJsonObject envelope = authenticationEnvelope(frame);
    if (!authenticationSecret.isEmpty())
        envelope.insert(QStringLiteral("auth"), authenticationCode(frame, authenticationSecret));

    const QByteArray data = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    if (data.size() > SharedMemoryProtocol::kMaxPayloadSize) {
        qCWarning(lcSharedMemoryProtocol).nospace()
                << "Shared memory frame exceeds payload limit: " << data.size();
        return QByteArray();
    }
    return data;
}

bool SharedMemoryProtocol::parse(const QByteArray &data,
                                 SharedMemoryProtocol::Frame *frame,
                                 QString *errorOut,
                                 const QByteArray &authenticationSecret)
{
    if (errorOut)
        errorOut->clear();

    if (!frame) {
        if (errorOut)
            *errorOut = QStringLiteral("Frame output pointer is null.");
        return false;
    }
    if (data.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("Frame payload is empty.");
        return false;
    }
    if (data.size() > SharedMemoryProtocol::kMaxPayloadSize) {
        if (errorOut)
            *errorOut = QStringLiteral("Frame payload exceeds size limit.");
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut)
            *errorOut = parseError.errorString();
        return false;
    }

    const QJsonObject envelope = doc.object();
    SharedMemoryProtocol::Frame parsed;
    parsed.version = envelope.value(QStringLiteral("version")).toInt();
    parsed.type = envelope.value(QStringLiteral("type")).toString().trimmed();
    parsed.requestId = envelope.value(QStringLiteral("request_id")).toString().trimmed();
    parsed.authentication = envelope.value(QStringLiteral("auth")).toString().trimmed();
    parsed.payload = envelope.value(QStringLiteral("payload")).toObject();

    if (!parsed.isValid()) {
        if (errorOut)
            *errorOut = QStringLiteral("Frame envelope is invalid.");
        return false;
    }

    if (!authenticationSecret.isEmpty()) {
        if (parsed.authentication.isEmpty()) {
            if (errorOut)
                *errorOut = QStringLiteral("Frame authentication is missing.");
            return false;
        }

        const QByteArray expected = authenticationCode(parsed, authenticationSecret).toLatin1();
        const QByteArray actual = parsed.authentication.toLatin1();
        if (!constantTimeEquals(expected, actual)) {
            if (errorOut)
                *errorOut = QStringLiteral("Frame authentication is invalid.");
            return false;
        }
    }

    *frame = parsed;
    return true;
}

QString SharedMemoryProtocol::createRequestId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
