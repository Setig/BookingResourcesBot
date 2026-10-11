#ifndef USERTIMEZONE_H
#define USERTIMEZONE_H

#include <QTimeZone>

#include <QString>

/**
 * @brief User-configured time zone represented as fixed UTC offset in minutes.
 *
 * The value stores either:
 * - explicit custom offset in minutes (e.g. +180, -180), or
 * - "default" mode (no custom value) that falls back to server system time zone.
 */
class UserTimeZone final
{
public:
    UserTimeZone();
    explicit UserTimeZone(short utcOffsetMinutes);

    UserTimeZone(const UserTimeZone &other);
    UserTimeZone(UserTimeZone &&other) noexcept;
    UserTimeZone &operator=(const UserTimeZone &other);
    UserTimeZone &operator=(UserTimeZone &&other) noexcept;
    ~UserTimeZone();

    [[nodiscard]] bool hasCustomOffset() const;
    [[nodiscard]] short utcOffsetMinutes() const;
    void clear();
    bool setUtcOffsetMinutes(short offsetMinutes);

    [[nodiscard]] QString toStorageValue() const;
    [[nodiscard]] QString toDisplayString() const;
    [[nodiscard]] QTimeZone toQTimeZone() const;

    bool operator==(const UserTimeZone &other) const;
    bool operator!=(const UserTimeZone &other) const;

    [[nodiscard]] static short minUtcOffsetMinutes();
    [[nodiscard]] static short maxUtcOffsetMinutes();
    [[nodiscard]] static bool isOffsetInRange(int offsetMinutes);
    [[nodiscard]] static bool parseOffsetToken(const QString &token, short *offsetMinutesOut);
    [[nodiscard]] static bool parseUserToken(const QString &token, UserTimeZone *timeZoneOut);
    [[nodiscard]] static UserTimeZone fromStorageValue(const QString &value);
    [[nodiscard]] static QString formatOffset(short offsetMinutes);
    [[nodiscard]] static QTimeZone fallbackTimeZone();

private:
    short m_utcOffsetMinutes;
};

#endif // USERTIMEZONE_H
