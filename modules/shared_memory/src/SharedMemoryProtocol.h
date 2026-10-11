#ifndef SHAREDMEMORYPROTOCOL_H
#define SHAREDMEMORYPROTOCOL_H

#include <QString>
#include <QByteArray>
#include <QJsonObject>

namespace SharedMemoryProtocol {

constexpr int kProtocolVersion = 1;
constexpr qsizetype kMaxPayloadSize = 4 * 1024 * 1024;

struct Frame
{
    int version;
    QString type;
    QString requestId;
    QString authentication;
    QJsonObject payload;

    Frame();
    Frame(const Frame &other);
    Frame(Frame &&other) noexcept;
    Frame &operator=(const Frame &other);
    Frame &operator=(Frame &&other) noexcept;
    ~Frame();

    [[nodiscard]] bool isValid() const;
};

[[nodiscard]] QByteArray serialize(const Frame &frame,
                                   const QByteArray &authenticationSecret = QByteArray());
[[nodiscard]] bool parse(const QByteArray &data,
                         Frame *frame,
                         QString *errorOut = nullptr,
                         const QByteArray &authenticationSecret = QByteArray());
[[nodiscard]] QString createRequestId();

} // namespace SharedMemoryProtocol

#endif // SHAREDMEMORYPROTOCOL_H
