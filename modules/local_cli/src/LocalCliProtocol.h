#ifndef LOCALCLIPROTOCOL_H
#define LOCALCLIPROTOCOL_H

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QJsonObject>

/**
 * @brief Simple length-prefixed JSON protocol used between LocalCliClient and LocalCliBot.
 */
namespace LocalCliProtocol {

/**
 * @brief Maximum payload size for a single frame.
 */
constexpr qsizetype kMaxPayloadSize = 8 * 1024 * 1024;

/**
 * @brief Single protocol frame.
 *
 * Frames are serialized as a 32-bit little-endian length prefix followed by a compact JSON object
 * with "type" and "payload" fields.
 */
struct Frame
{
    QString type;
    QJsonObject payload;

    [[nodiscard]] bool isValid() const { return !type.trimmed().isEmpty(); }
};

/**
 * @brief Result of a complete request used by Local CLI hosts.
 */
struct CompletionResult
{
    int replaceFrom = 0;
    int replaceTo = 0;
    QStringList values;
    bool appendSpace = true;
};

/**
 * @brief Builds a complete-result frame from a completion answer.
 */
[[nodiscard]] Frame makeCompleteResult(const CompletionResult &result);

/**
 * @brief Serializes a frame to a wire buffer suitable for writing to a QLocalSocket.
 */
[[nodiscard]] QByteArray serialize(const Frame &frame);

/**
 * @brief Attempts to parse and remove a single frame from a receive buffer.
 *
 * @return true if a full valid frame was parsed and written to @p frame.
 */
[[nodiscard]] bool takeFrame(QByteArray *buffer, Frame *frame);

} // namespace LocalCliProtocol

#endif // LOCALCLIPROTOCOL_H
