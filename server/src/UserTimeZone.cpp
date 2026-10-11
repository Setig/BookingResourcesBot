#include "UserTimeZone.h"

#include <QtGlobal>
#include <QRegularExpression>

#include <limits>

namespace {
constexpr short kMinUtcOffsetMinutes = static_cast<short>(-(23 * 60 + 59));
constexpr short kMaxUtcOffsetMinutes = static_cast<short>(23 * 60 + 59);
constexpr short kNoCustomOffsetSentinel = std::numeric_limits<short>::min();

static_assert(kNoCustomOffsetSentinel < kMinUtcOffsetMinutes,
              "Sentinel must not overlap valid UTC offset range");

inline bool isCustomOffsetStored(short value)
{
    return value != kNoCustomOffsetSentinel;
}
} // namespace

UserTimeZone::UserTimeZone()
    : m_utcOffsetMinutes(kNoCustomOffsetSentinel)
{
    // do nothing
}

UserTimeZone::UserTimeZone(short utcOffsetMinutes)
    : m_utcOffsetMinutes(kNoCustomOffsetSentinel)
{
    if (!setUtcOffsetMinutes(utcOffsetMinutes))
        clear();
}

UserTimeZone::UserTimeZone(const UserTimeZone &other)
    : m_utcOffsetMinutes(other.m_utcOffsetMinutes)
{
    // do nothing
}

UserTimeZone::UserTimeZone(UserTimeZone &&other) noexcept
    : m_utcOffsetMinutes(other.m_utcOffsetMinutes)
{
    // do nothing
}

UserTimeZone &UserTimeZone::operator=(const UserTimeZone &other)
{
    if (this != &other) {
        m_utcOffsetMinutes = other.m_utcOffsetMinutes;
    }
    return *this;
}

UserTimeZone &UserTimeZone::operator=(UserTimeZone &&other) noexcept
{
    if (this != &other) {
        m_utcOffsetMinutes = other.m_utcOffsetMinutes;
    }
    return *this;
}

UserTimeZone::~UserTimeZone()
{
    // do nothing
}

bool UserTimeZone::hasCustomOffset() const
{
    return isCustomOffsetStored(m_utcOffsetMinutes);
}

short UserTimeZone::utcOffsetMinutes() const
{
    if (!hasCustomOffset())
        return 0;
    return m_utcOffsetMinutes;
}

void UserTimeZone::clear()
{
    m_utcOffsetMinutes = kNoCustomOffsetSentinel;
}

bool UserTimeZone::setUtcOffsetMinutes(short offsetMinutes)
{
    if (!isOffsetInRange(offsetMinutes))
        return false;

    m_utcOffsetMinutes = offsetMinutes;
    return true;
}

QString UserTimeZone::toStorageValue() const
{
    if (!hasCustomOffset())
        return QString();
    return QString::number(m_utcOffsetMinutes);
}

QString UserTimeZone::toDisplayString() const
{
    if (!hasCustomOffset())
        return QString();
    return formatOffset(m_utcOffsetMinutes);
}

QTimeZone UserTimeZone::toQTimeZone() const
{
    if (!hasCustomOffset())
        return fallbackTimeZone();

    const QTimeZone zone(static_cast<int>(m_utcOffsetMinutes) * 60);
    if (zone.isValid())
        return zone;
    return fallbackTimeZone();
}

bool UserTimeZone::operator==(const UserTimeZone &other) const
{
    return m_utcOffsetMinutes == other.m_utcOffsetMinutes;
}

bool UserTimeZone::operator!=(const UserTimeZone &other) const
{
    return !(*this == other);
}

short UserTimeZone::minUtcOffsetMinutes()
{
    return kMinUtcOffsetMinutes;
}

short UserTimeZone::maxUtcOffsetMinutes()
{
    return kMaxUtcOffsetMinutes;
}

bool UserTimeZone::isOffsetInRange(int offsetMinutes)
{
    return offsetMinutes >= static_cast<int>(kMinUtcOffsetMinutes)
           && offsetMinutes <= static_cast<int>(kMaxUtcOffsetMinutes);
}

bool UserTimeZone::parseOffsetToken(const QString &token, short *offsetMinutesOut)
{
    if (!offsetMinutesOut)
        return false;

    QString normalized = token.trimmed();
    if (normalized.isEmpty())
        return false;

    normalized.remove(QLatin1Char(' '));
    normalized.remove(QLatin1Char('\t'));

    if (normalized.compare(QStringLiteral("UTC"), Qt::CaseInsensitive) == 0
        || normalized.compare(QStringLiteral("GMT"), Qt::CaseInsensitive) == 0) {
        *offsetMinutesOut = 0;
        return true;
    }

    static const QRegularExpression plainMinutesRegex(QStringLiteral("^[+-]?\\d+$"));
    const QRegularExpressionMatch plainMinutesMatch = plainMinutesRegex.match(normalized);
    if (plainMinutesMatch.hasMatch()) {
        bool ok = false;
        const int minutes = normalized.toInt(&ok);
        if (!ok || !isOffsetInRange(minutes))
            return false;
        *offsetMinutesOut = static_cast<short>(minutes);
        return true;
    }

    if (normalized.startsWith(QStringLiteral("UTC"), Qt::CaseInsensitive)) {
        normalized = normalized.mid(3);
    } else if (normalized.startsWith(QStringLiteral("GMT"), Qt::CaseInsensitive)) {
        normalized = normalized.mid(3);
    }

    int offsetMinutes = 0;

    static const QRegularExpression signedOffsetRegex(
            QStringLiteral("^([+-])(\\d{1,2})(?::?(\\d{2}))?$"));
    const QRegularExpressionMatch signedOffsetMatch = signedOffsetRegex.match(normalized);
    if (signedOffsetMatch.hasMatch()) {
        bool hoursOk = false;
        const int hours = signedOffsetMatch.captured(2).toInt(&hoursOk);
        if (!hoursOk || hours < 0 || hours > 23)
            return false;

        int minutesPart = 0;
        if (!signedOffsetMatch.captured(3).isEmpty()) {
            bool minutesOk = false;
            minutesPart = signedOffsetMatch.captured(3).toInt(&minutesOk);
            if (!minutesOk || minutesPart < 0 || minutesPart > 59)
                return false;
        }

        offsetMinutes = hours * 60 + minutesPart;
        if (signedOffsetMatch.captured(1) == QStringLiteral("-"))
            offsetMinutes = -offsetMinutes;
    } else {
        // Treat unsigned clock-like value as positive offset, e.g. "03:00" => UTC+03:00.
        static const QRegularExpression unsignedClockRegex(QStringLiteral("^(\\d{1,2}):(\\d{2})$"));
        const QRegularExpressionMatch unsignedClockMatch = unsignedClockRegex.match(normalized);
        if (!unsignedClockMatch.hasMatch())
            return false;

        bool hoursOk = false;
        bool minutesOk = false;
        const int hours = unsignedClockMatch.captured(1).toInt(&hoursOk);
        const int minutesPart = unsignedClockMatch.captured(2).toInt(&minutesOk);
        if (!hoursOk || !minutesOk || hours < 0 || hours > 23 || minutesPart < 0
            || minutesPart > 59) {
            return false;
        }

        offsetMinutes = hours * 60 + minutesPart;
    }

    if (!isOffsetInRange(offsetMinutes))
        return false;

    *offsetMinutesOut = static_cast<short>(offsetMinutes);
    return true;
}

bool UserTimeZone::parseUserToken(const QString &token, UserTimeZone *timeZoneOut)
{
    if (!timeZoneOut)
        return false;

    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty()) {
        timeZoneOut->clear();
        return true;
    }

    const QString normalized = trimmed.toLower();
    if (normalized == QStringLiteral("default") || normalized == QStringLiteral("auto")
        || normalized == QStringLiteral("system")) {
        timeZoneOut->clear();
        return true;
    }

    short offsetMinutes = 0;
    if (!parseOffsetToken(trimmed, &offsetMinutes))
        return false;

    *timeZoneOut = UserTimeZone(offsetMinutes);
    return true;
}

UserTimeZone UserTimeZone::fromStorageValue(const QString &value)
{
    short offsetMinutes = 0;
    if (!parseOffsetToken(value, &offsetMinutes))
        return UserTimeZone();
    return UserTimeZone(offsetMinutes);
}

QString UserTimeZone::formatOffset(short offsetMinutes)
{
    const int bounded = qBound(static_cast<int>(kMinUtcOffsetMinutes),
                               static_cast<int>(offsetMinutes),
                               static_cast<int>(kMaxUtcOffsetMinutes));
    if (bounded == 0)
        return QStringLiteral("UTC");

    const int absMinutes = qAbs(bounded);
    const int hours = absMinutes / 60;
    const int minutes = absMinutes % 60;
    if (bounded > 0) {
        return QStringLiteral("UTC+%1:%2")
                .arg(hours, 2, 10, QLatin1Char('0'))
                .arg(minutes, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("UTC-%1:%2")
            .arg(hours, 2, 10, QLatin1Char('0'))
            .arg(minutes, 2, 10, QLatin1Char('0'));
}

QTimeZone UserTimeZone::fallbackTimeZone()
{
    const QTimeZone system = QTimeZone::systemTimeZone();
    if (system.isValid())
        return system;
    return QTimeZone::utc();
}
