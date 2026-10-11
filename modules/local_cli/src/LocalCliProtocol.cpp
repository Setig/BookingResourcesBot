#include "LocalCliProtocol.h"

#include <QtEndian>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcLocalCliProtocol, "booking.modules.localcli.protocol")

namespace {
constexpr qsizetype kFrameHeaderSize = static_cast<qsizetype>(sizeof(quint32));
}

LocalCliProtocol::Frame LocalCliProtocol::makeCompleteResult(const CompletionResult &result)
{
    Frame frame;
    frame.type = QStringLiteral("complete-result");
    QJsonObject payload;
    payload.insert(QStringLiteral("replace_from"), result.replaceFrom);
    payload.insert(QStringLiteral("replace_to"), result.replaceTo);
    QJsonArray values;
    for (const QString &value : result.values)
        values.append(value);
    payload.insert(QStringLiteral("values"), values);
    payload.insert(QStringLiteral("append_space"), result.appendSpace);
    frame.payload = payload;
    return frame;
}

QByteArray LocalCliProtocol::serialize(const Frame &frame)
{
    QJsonObject envelope;
    envelope.insert(QStringLiteral("type"), frame.type);
    envelope.insert(QStringLiteral("payload"), frame.payload);

    const QByteArray body = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    if (body.size() > LocalCliProtocol::kMaxPayloadSize) {
        qCWarning(lcLocalCliProtocol).nospace()
                << "LocalCliProtocol: payload size exceeds limit: " << body.size();
        return QByteArray();
    }

    QByteArray buffer;
    buffer.reserve(kFrameHeaderSize + body.size());

    const quint32 length = static_cast<quint32>(body.size());
    char header[sizeof(quint32)];
    qToLittleEndian(length, reinterpret_cast<uchar *>(header));
    buffer.append(header, sizeof(header));
    buffer.append(body);
    return buffer;
}

bool LocalCliProtocol::takeFrame(QByteArray *buffer, Frame *frame)
{
    if (!buffer || !frame)
        return false;

    if (buffer->size() < kFrameHeaderSize)
        return false;

    const quint32 payloadSize =
            qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(buffer->constData()));
    if (payloadSize == 0) {
        qCWarning(lcLocalCliProtocol) << "LocalCliProtocol: empty payload size in frame header";
        buffer->remove(0, kFrameHeaderSize);
        return false;
    }
    if (static_cast<qsizetype>(payloadSize) > LocalCliProtocol::kMaxPayloadSize) {
        qCWarning(lcLocalCliProtocol).nospace()
                << "LocalCliProtocol: payload size exceeds limit: " << payloadSize;
        buffer->clear();
        return false;
    }
    const qsizetype totalSize = kFrameHeaderSize + static_cast<qsizetype>(payloadSize);
    if (buffer->size() < totalSize)
        return false;

    const QByteArray payload = buffer->mid(kFrameHeaderSize, payloadSize);
    buffer->remove(0, totalSize);

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcLocalCliProtocol).nospace()
                << "LocalCliProtocol: failed to parse frame payload: " << error.errorString();
        return false;
    }

    const QJsonObject envelope = doc.object();
    frame->type = envelope.value(QStringLiteral("type")).toString();
    frame->payload = envelope.value(QStringLiteral("payload")).toObject();
    return frame->isValid();
}
