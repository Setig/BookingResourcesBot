#include "ServerDateTimeFormat.h"

#include "ServerDatabase.h"
#include "ServerRuntimeSettings.h"

#include <QStringList>
#include <QCoreApplication>
#include <QRegularExpression>

namespace {

using Setting = ServerRuntimeSettings::Setting;

QString fallbackFormatLiteral()
{
    return QStringLiteral("yyyy-MM-dd HH:mm:ss");
}

QTimeZone resolveEffectiveTimeZone(const UserTimeZone &timeZone)
{
    return timeZone.toQTimeZone();
}

void appendUniqueFormat(QStringList *formats, const QString &candidate)
{
    if (!formats)
        return;
    const QString trimmed = candidate.trimmed();
    if (trimmed.isEmpty())
        return;
    if (formats->contains(trimmed))
        return;
    formats->append(trimmed);
}

QDateTime normalizeToUtc(const QDateTime &parsed,
                         ServerDateTimeFormat::ParseMode mode,
                         const UserTimeZone &timeZone)
{
    if (!parsed.isValid())
        return QDateTime();

    if (mode == ServerDateTimeFormat::ParseMode::LocalToUtc) {
        const Qt::TimeSpec timeSpec = parsed.timeSpec();
        // Trust only an explicit UTC or numeric offset from the token. Qt 6
        // often attaches a valid system QTimeZone to LocalTime values, which
        // must not override the user or server timezone.
        if (timeSpec == Qt::UTC || timeSpec == Qt::OffsetFromUTC)
            return parsed.toUTC();
        const QTimeZone zone = resolveEffectiveTimeZone(timeZone);
        const QDateTime zoned(parsed.date(), parsed.time(), zone);
        return zoned.toUTC();
    }

    if (parsed.timeSpec() == Qt::UTC || parsed.timeSpec() == Qt::OffsetFromUTC
        || parsed.timeSpec() == Qt::TimeZone) {
        return parsed.toUTC();
    }

    if (parsed.timeSpec() == Qt::LocalTime) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        return QDateTime(parsed.date(), parsed.time(), QTimeZone::utc());
#else
        QDateTime utc(parsed);
        utc.setTimeSpec(Qt::UTC);
        return utc;
#endif
    }
    return parsed.toUTC();
}

} // namespace

namespace ServerDateTimeFormat {

QString defaultLocalizedFormat()
{
    // Keep translation context compatible with the existing fallback translation entry.
    const QString translated =
            QCoreApplication::translate("MessengerTextRender", "yyyy-MM-dd HH:mm:ss").trimmed();
    return translated.isEmpty() ? fallbackFormatLiteral() : translated;
}

QString effectiveFormat(const ServerDatabase *database)
{
    const QString fallback = defaultLocalizedFormat();
    if (!database)
        return fallback;

    const QString configured =
            database->serverSettingValue(Setting::DateTimeFormat).toString().trimmed();
    if (configured.isEmpty())
        return fallback;
    return isValidQtFormat(configured) ? configured : fallback;
}

bool isResetToken(const QString &token)
{
    const QString normalized = token.trimmed().toLower();
    return normalized == QStringLiteral("default") || normalized == QStringLiteral("auto")
           || normalized == QStringLiteral("locale");
}

bool isValidQtFormat(const QString &format)
{
    const QString trimmed = format.trimmed();
    if (trimmed.isEmpty())
        return false;
    if (trimmed.contains(QLatin1Char('\n')) || trimmed.contains(QLatin1Char('\r')))
        return false;

    static const QRegularExpression tokenRegex(QStringLiteral("[dMyHhmsz]"));
    if (!tokenRegex.match(trimmed).hasMatch())
        return false;

    const QString rendered = QDateTime::currentDateTime().toString(trimmed).trimmed();
    return !rendered.isEmpty();
}

bool parseSettingValue(const QString &token, QString *normalizedFormatOut)
{
    if (!normalizedFormatOut)
        return false;

    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty() || isResetToken(trimmed)) {
        normalizedFormatOut->clear();
        return true;
    }
    if (!isValidQtFormat(trimmed))
        return false;

    *normalizedFormatOut = trimmed;
    return true;
}

bool isValidTimeZoneId(const QString &timeZoneId)
{
    short offsetMinutes = 0;
    return UserTimeZone::parseOffsetToken(timeZoneId, &offsetMinutes);
}

QString normalizeTimeZoneId(const QString &timeZoneId)
{
    short offsetMinutes = 0;
    if (!UserTimeZone::parseOffsetToken(timeZoneId, &offsetMinutes))
        return QString();

    return QString::number(offsetMinutes);
}

bool parseTimeZoneOffsetMinutes(const QString &token, short *offsetMinutesOut)
{
    return UserTimeZone::parseOffsetToken(token, offsetMinutesOut);
}

QString formatUtcOffset(short offsetMinutes)
{
    return UserTimeZone::formatOffset(offsetMinutes);
}

QTimeZone effectiveTimeZone(const UserTimeZone &timeZone)
{
    return resolveEffectiveTimeZone(timeZone);
}

QDateTime toLocalDateTime(const QDateTime &utcDateTime, const UserTimeZone &timeZone)
{
    if (!utcDateTime.isValid())
        return QDateTime();

    return utcDateTime.toUTC().toTimeZone(resolveEffectiveTimeZone(timeZone));
}

bool parseTimeZoneValue(const QString &token, UserTimeZone *timeZoneOut)
{
    return UserTimeZone::parseUserToken(token, timeZoneOut);
}

QString formatLocalDateTime(const QDateTime &utcDateTime,
                            const QString &format,
                            const QString &invalidText,
                            const UserTimeZone &timeZone)
{
    if (!utcDateTime.isValid())
        return invalidText;

    QString effective = format.trimmed();
    if (effective.isEmpty() || !isValidQtFormat(effective))
        effective = defaultLocalizedFormat();

    return toLocalDateTime(utcDateTime, timeZone).toString(effective);
}

bool parseUserDateTime(const QString &token,
                       const QString &format,
                       ParseMode mode,
                       QDateTime *utcOut,
                       const UserTimeZone &timeZone)
{
    if (!utcOut)
        return false;

    const QString normalizedToken = token.trimmed();
    if (normalizedToken.isEmpty())
        return false;

    QDateTime parsed = QDateTime::fromString(normalizedToken, Qt::ISODate);
    if (parsed.isValid()) {
        *utcOut = normalizeToUtc(parsed, mode, timeZone);
        return utcOut->isValid();
    }

    QStringList candidates;
    appendUniqueFormat(&candidates, format);
    appendUniqueFormat(&candidates, defaultLocalizedFormat());
    appendUniqueFormat(&candidates, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    appendUniqueFormat(&candidates, QStringLiteral("yyyy-MM-dd HH:mm"));

    for (const QString &candidate : std::as_const(candidates)) {
        parsed = QDateTime::fromString(normalizedToken, candidate);
        if (!parsed.isValid())
            continue;
        *utcOut = normalizeToUtc(parsed, mode, timeZone);
        if (utcOut->isValid())
            return true;
    }

    return false;
}

} // namespace ServerDateTimeFormat
