#ifndef WEBPASSWORDHASHER_H
#define WEBPASSWORDHASHER_H

#include <QtGlobal>
#include <QString>
#include <QByteArray>
#include <QStringList>
#include <QRandomGenerator>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>

namespace WebPasswordHasher {

inline QString pendingPasswordHash()
{
    return QStringLiteral("pending$");
}

inline bool isPendingPasswordHash(const QString &encodedHash)
{
    return encodedHash.trimmed() == pendingPasswordHash();
}

inline constexpr int kSaltBytes = 16;
inline constexpr int kHashBytes = 32;
inline constexpr int kMinimumIterations = 100000;
inline constexpr int kDefaultIterations = 210000;

inline QByteArray randomBytes(int size)
{
    QByteArray bytes;
    bytes.resize(size);
    for (int offset = 0; offset < size;) {
        const quint32 value = QRandomGenerator::system()->generate();
        for (int i = 0; i < static_cast<int>(sizeof(value)) && offset < size; ++i, ++offset)
            bytes[offset] = static_cast<char>((value >> (i * 8)) & 0xff);
    }
    return bytes;
}

inline QByteArray int32Be(quint32 value)
{
    QByteArray result(4, '\0');
    result[0] = static_cast<char>((value >> 24) & 0xff);
    result[1] = static_cast<char>((value >> 16) & 0xff);
    result[2] = static_cast<char>((value >> 8) & 0xff);
    result[3] = static_cast<char>(value & 0xff);
    return result;
}

inline QByteArray hmacSha256(const QByteArray &key, const QByteArray &message)
{
    return QMessageAuthenticationCode::hash(message, key, QCryptographicHash::Sha256);
}

inline QByteArray pbkdf2Sha256(const QByteArray &password,
                               const QByteArray &salt,
                               int iterations,
                               int outputBytes)
{
    const int effectiveIterations = qMax(kMinimumIterations, iterations);
    QByteArray result;
    result.reserve(outputBytes);

    for (quint32 block = 1; result.size() < outputBytes; ++block) {
        QByteArray u = hmacSha256(password, salt + int32Be(block));
        QByteArray t = u;
        for (int iteration = 1; iteration < effectiveIterations; ++iteration) {
            u = hmacSha256(password, u);
            for (int i = 0; i < t.size(); ++i)
                t[i] = static_cast<char>(static_cast<uchar>(t.at(i)) ^ static_cast<uchar>(u.at(i)));
        }
        result.append(t);
    }

    return result.left(outputBytes);
}

inline bool constantTimeEquals(const QByteArray &left, const QByteArray &right)
{
    if (left.size() != right.size())
        return false;

    uchar diff = 0;
    for (int i = 0; i < left.size(); ++i)
        diff |= static_cast<uchar>(left.at(i) ^ right.at(i));
    return diff == 0;
}

inline QString hashPassword(const QString &password, int iterations = kDefaultIterations)
{
    const QByteArray salt = randomBytes(kSaltBytes);
    const QByteArray hash =
            pbkdf2Sha256(password.toUtf8(), salt, qMax(kMinimumIterations, iterations), kHashBytes);
    return QStringLiteral("pbkdf2-sha256$%1$%2$%3")
            .arg(qMax(kMinimumIterations, iterations))
            .arg(QString::fromLatin1(salt.toBase64()))
            .arg(QString::fromLatin1(hash.toBase64()));
}

inline bool verifyPassword(const QString &password, const QString &encodedHash)
{
    const QStringList parts = encodedHash.split(QLatin1Char('$'));
    if (parts.size() != 4 || parts.at(0) != QLatin1String("pbkdf2-sha256"))
        return false;

    bool iterationsOk = false;
    const int iterations = parts.at(1).toInt(&iterationsOk);
    if (!iterationsOk || iterations < kMinimumIterations)
        return false;

    const QByteArray salt = QByteArray::fromBase64(parts.at(2).toLatin1());
    const QByteArray expected = QByteArray::fromBase64(parts.at(3).toLatin1());
    if (salt.size() < kSaltBytes || expected.size() != kHashBytes)
        return false;

    const QByteArray actual = pbkdf2Sha256(password.toUtf8(), salt, iterations, expected.size());
    return constantTimeEquals(actual, expected);
}

} // namespace WebPasswordHasher

#endif // WEBPASSWORDHASHER_H
