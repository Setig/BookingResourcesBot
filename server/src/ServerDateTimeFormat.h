#ifndef SERVERDATETIMEFORMAT_H
#define SERVERDATETIMEFORMAT_H

#include <QDateTime>
#include <QTimeZone>

#include <QString>

#include "UserTimeZone.h"

class ServerDatabase;

namespace ServerDateTimeFormat {

enum class ParseMode {
    LocalToUtc = 0,
    UtcInput
};

QString defaultLocalizedFormat();
QString effectiveFormat(const ServerDatabase *database);

bool isResetToken(const QString &token);
bool isValidQtFormat(const QString &format);
bool parseSettingValue(const QString &token, QString *normalizedFormatOut);
bool isValidTimeZoneId(const QString &timeZoneId);
QString normalizeTimeZoneId(const QString &timeZoneId);
bool parseTimeZoneOffsetMinutes(const QString &token, short *offsetMinutesOut);
QString formatUtcOffset(short offsetMinutes);
QTimeZone effectiveTimeZone(const UserTimeZone &timeZone = UserTimeZone());
QDateTime toLocalDateTime(const QDateTime &utcDateTime,
                          const UserTimeZone &timeZone = UserTimeZone());
bool parseTimeZoneValue(const QString &token, UserTimeZone *timeZoneOut);

QString formatLocalDateTime(const QDateTime &utcDateTime,
                            const QString &format,
                            const QString &invalidText = QString(),
                            const UserTimeZone &timeZone = UserTimeZone());

bool parseUserDateTime(const QString &token,
                       const QString &format,
                       ParseMode mode,
                       QDateTime *utcOut,
                       const UserTimeZone &timeZone = UserTimeZone());

} // namespace ServerDateTimeFormat

#endif // SERVERDATETIMEFORMAT_H
