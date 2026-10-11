#include "MessengerAbstractCommand.h"

#include "User.h"
#include "Resource.h"
#include "MessengerBot.h"
#include "MessengerBotPoll.h"
#include "MessengerTextRender.h"
#include "ServerDateTimeFormat.h"
#include "MessengerCommandManager.h"
#include "MessengerUserFacingErrors.h"

#include <QDateTime>
#include <QHash>
#include <QStringList>
#include <QRegularExpression>
#include <limits>

namespace {

struct DurationToken
{
    bool ok = false;
    qint64 minutes = 0;
    bool hasExplicitUnit = false;
};

QStringList splitDurationUnitTokens(const QString &tokens)
{
    QStringList result;
    const QStringList parts = tokens.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString normalized = part.trimmed().toLower();
        if (!normalized.isEmpty() && !result.contains(normalized))
            result.append(normalized);
    }
    return result;
}

void addDurationUnitTokens(QHash<QString, qint64> *unitMinutes,
                           qint64 minutes,
                           const QStringList &tokens)
{
    if (!unitMinutes)
        return;

    for (const QString &token : tokens) {
        const QString normalized = token.trimmed().toLower();
        if (!normalized.isEmpty())
            unitMinutes->insert(normalized, minutes);
    }
}

QHash<QString, qint64> durationUnitMinutes(const QString &localizedMinuteTokens,
                                           const QString &localizedHourTokens,
                                           const QString &localizedDayTokens,
                                           const QString &localizedWeekTokens,
                                           const QString &localizedMonthTokens)
{
    static constexpr qint64 kHourMinutes = 60;
    static constexpr qint64 kDayMinutes = 60 * 24;
    static constexpr qint64 kWeekMinutes = kDayMinutes * 7;
    static constexpr qint64 kMonthMinutes = kDayMinutes * 30;

    QHash<QString, qint64> units;
    addDurationUnitTokens(&units,
                          1,
                          QStringList() << QStringLiteral("m") << QStringLiteral("min")
                                        << QStringLiteral("minute") << QStringLiteral("minutes"));
    addDurationUnitTokens(&units,
                          kHourMinutes,
                          QStringList() << QStringLiteral("h") << QStringLiteral("hr")
                                        << QStringLiteral("hrs") << QStringLiteral("hour")
                                        << QStringLiteral("hours"));
    addDurationUnitTokens(&units,
                          kDayMinutes,
                          QStringList() << QStringLiteral("d") << QStringLiteral("day")
                                        << QStringLiteral("days"));
    addDurationUnitTokens(&units,
                          kWeekMinutes,
                          QStringList() << QStringLiteral("w") << QStringLiteral("week")
                                        << QStringLiteral("weeks"));
    addDurationUnitTokens(&units,
                          kMonthMinutes,
                          QStringList() << QStringLiteral("mo") << QStringLiteral("mon")
                                        << QStringLiteral("month") << QStringLiteral("months"));

    addDurationUnitTokens(&units, 1, splitDurationUnitTokens(localizedMinuteTokens));
    addDurationUnitTokens(&units, kHourMinutes, splitDurationUnitTokens(localizedHourTokens));
    addDurationUnitTokens(&units, kDayMinutes, splitDurationUnitTokens(localizedDayTokens));
    addDurationUnitTokens(&units, kWeekMinutes, splitDurationUnitTokens(localizedWeekTokens));
    addDurationUnitTokens(&units, kMonthMinutes, splitDurationUnitTokens(localizedMonthTokens));

    addDurationUnitTokens(&units,
                          1,
                          QStringList()
                                  << QString::fromUtf8("мин") << QString::fromUtf8("минута")
                                  << QString::fromUtf8("минуты") << QString::fromUtf8("минут"));
    addDurationUnitTokens(&units,
                          kHourMinutes,
                          QStringList() << QString::fromUtf8("ч") << QString::fromUtf8("час")
                                        << QString::fromUtf8("часа") << QString::fromUtf8("часов"));
    addDurationUnitTokens(&units,
                          kDayMinutes,
                          QStringList() << QString::fromUtf8("д") << QString::fromUtf8("день")
                                        << QString::fromUtf8("дня") << QString::fromUtf8("дней"));
    addDurationUnitTokens(&units,
                          kWeekMinutes,
                          QStringList()
                                  << QString::fromUtf8("нед") << QString::fromUtf8("неделя")
                                  << QString::fromUtf8("недели") << QString::fromUtf8("недель"));
    addDurationUnitTokens(&units,
                          kMonthMinutes,
                          QStringList()
                                  << QString::fromUtf8("мес") << QString::fromUtf8("месяц")
                                  << QString::fromUtf8("месяца") << QString::fromUtf8("месяцев"));
    return units;
}

DurationToken parseDurationToken(const QString &token, const QHash<QString, qint64> &unitMinutes)
{
    DurationToken result;
    const QString normalized = token.trimmed().toLower();
    if (normalized.isEmpty())
        return result;

    static const QRegularExpression durationRegex(
            QStringLiteral("^\\s*(\\d+)\\s*([^\\d\\s]+)?\\s*$"));
    const QRegularExpressionMatch match = durationRegex.match(normalized);
    if (!match.hasMatch())
        return result;

    bool ok = false;
    const qint64 value = match.captured(1).toLongLong(&ok);
    if (!ok || value < 0)
        return result;

    const QString unitToken = match.captured(2).trimmed();
    const bool hasExplicitUnit = !unitToken.isEmpty();
    const qint64 multiplier = hasExplicitUnit ? unitMinutes.value(unitToken, -1) : 1;
    if (multiplier <= 0)
        return result;

    if (value > std::numeric_limits<qint64>::max() / multiplier)
        return result;

    result.ok = true;
    result.minutes = value * multiplier;
    result.hasExplicitUnit = hasExplicitUnit;
    return result;
}

} // namespace

MessengerAbstractCommand::MessengerAbstractCommand(MessengerCommandManager *manager,
                                                   MessengerCommandAudience audience,
                                                   uint priority)
    : m_manager(manager)
    , m_audience(audience)
    , m_priority(priority)
{
    // do nothing
}

MessengerAbstractCommand::~MessengerAbstractCommand()
{
    // do nothing
}

MessengerCommandManager *MessengerAbstractCommand::manager() const
{
    return m_manager;
}

MessengerBot *MessengerAbstractCommand::bot() const
{
    return m_manager ? m_manager->bot() : nullptr;
}

const MessengerCommandParser &MessengerAbstractCommand::parser() const
{
    return m_parser;
}

MessengerCommandAudience MessengerAbstractCommand::audience() const
{
    return m_audience;
}

QString MessengerAbstractCommand::command() const
{
    return m_parser.command();
}

uint MessengerAbstractCommand::priority() const
{
    return m_priority;
}

bool MessengerAbstractCommand::isEnabled(const AbstractBot::IncomingMessage *message) const
{
    Q_UNUSED(message);
    return true;
}

QString MessengerAbstractCommand::disabledUserFacingReason(
        const AbstractBot::IncomingMessage &message) const
{
    Q_UNUSED(message);
    return QString();
}

MessengerCommandParser::ParseResult MessengerAbstractCommand::parseArguments(
        const QString &args) const
{
    return m_parser.parse(args);
}

void MessengerAbstractCommand::initParser(QString command, QString description)
{
    m_parser = MessengerCommandParser(std::move(command), std::move(description));
    configureParser(m_parser);
}

void MessengerAbstractCommand::configureParser(MessengerCommandParser &) const
{
    // default: no extra parser configuration
}

bool MessengerAbstractCommand::fetchMessengerUserProfile(MessengerType messenger,
                                                         const QString &userId,
                                                         AbstractBot::UserProfile *profileOut,
                                                         QString *errorOut) const
{
    MessengerBot *b = bot();
    if (!b || !b->backendBot()) {
        if (errorOut)
            *errorOut = tr("Messenger backend is not available.");
        return false;
    }

    MessengerBotPoll *poll = b->poll();
    AbstractBot *backend = poll ? poll->backendBot(messenger) : nullptr;
    if (!backend) {
        if (errorOut)
            *errorOut = tr("Cannot verify users from %1 with the current bot.")
                                .arg(messengerTypeDisplayName(messenger));
        return false;
    }

    const QString trimmedId = userId.trimmed();
    if (trimmedId.isEmpty()) {
        if (errorOut)
            *errorOut = MessengerUserFacingErrors::userIdentifierEmpty();
        return false;
    }

    QString lookupError;
    const bool resolved = backend->fetchUserProfile(trimmedId, profileOut, &lookupError);
    if (!resolved) {
        if (errorOut)
            *errorOut = lookupError;
        return false;
    }

    return true;
}

bool MessengerAbstractCommand::parseBoolToken(const QString &token, bool defaultValue)
{
    bool value = defaultValue;
    if (parseBoolTokenValue(token, &value))
        return value;
    return defaultValue;
}

bool MessengerAbstractCommand::parseBoolTokenValue(const QString &token, bool *valueOut)
{
    if (!valueOut)
        return false;

    if (token.isEmpty())
        return false;

    const QString lower = token.trimmed().toLower();
    if (lower.isEmpty())
        return false;

    const QStringList trueTokens = {
        QStringLiteral("1"),           QStringLiteral("true"),
        QStringLiteral("yes"),         QStringLiteral("y"),
        QStringLiteral("on"),          QStringLiteral("admin"),
        tr("1").trimmed().toLower(),   tr("true").trimmed().toLower(),
        tr("yes").trimmed().toLower(), tr("y").trimmed().toLower(),
        tr("on").trimmed().toLower(),  tr("admin").trimmed().toLower(),
    };
    if (trueTokens.contains(lower)) {
        *valueOut = true;
        return true;
    }

    const QStringList falseTokens = {
        QStringLiteral("0"),           QStringLiteral("false"),
        QStringLiteral("no"),          QStringLiteral("n"),
        QStringLiteral("off"),         QStringLiteral("user"),
        tr("0").trimmed().toLower(),   tr("false").trimmed().toLower(),
        tr("no").trimmed().toLower(),  tr("n").trimmed().toLower(),
        tr("off").trimmed().toLower(), tr("user").trimmed().toLower(),
    };
    if (falseTokens.contains(lower)) {
        *valueOut = false;
        return true;
    }

    return false;
}

QString MessengerAbstractCommand::supportedMessengerList()
{
    return MessengerUserFacingErrors::supportedMessengerList();
}

bool MessengerAbstractCommand::parseMessengerToken(const QString &token,
                                                   MessengerType *messengerOut)
{
    if (!messengerOut)
        return false;

    const QString normalized = token.trimmed().toLower();
    const MessengerType messenger = messengerTypeFromString(normalized);
    if (messengerTypeToString(messenger) != normalized)
        return false;

    *messengerOut = messenger;
    return true;
}

bool MessengerAbstractCommand::parseGroupListToken(const QString &token,
                                                   QSet<Group> *groupsOut,
                                                   QString *errorOut)
{
    return MessengerUserFacingErrors::parseGroupList(token, groupsOut, errorOut);
}

bool MessengerAbstractCommand::parseUserGroupRolesToken(const QString &token,
                                                        QMap<Group, User::Role> *rolesOut,
                                                        QString *errorOut)
{
    return MessengerUserFacingErrors::parseUserGroupRoles(token, rolesOut, errorOut);
}

QString MessengerAbstractCommand::formatGroupsList(const QSet<Group> &groups)
{
    return MessengerTextRender::formatGroupList(groups, false);
}

QString MessengerAbstractCommand::formatUserGroupRoles(const QMap<Group, User::Role> &roles)
{
    if (roles.isEmpty())
        return tr("Root (all groups)");

    QStringList values;
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
        const QString role = it.value() == User::Role::FixedAdminRole
                                     ? tr("fixed admin", "User role label (fixed admin)")
                             : it.value() == User::Role::AdminRole ? tr("admin")
                                                                   : tr("user");
        values.append(
                QStringLiteral("%1=%2").arg(MessengerTextRender::formatGroupDisplayName(it.key(),
                                                                                        false),
                                            role));
    }
    return values.join(tr(", "));
}

QString MessengerAbstractCommand::formatUserLine(const User &user, bool html)
{
    return MessengerTextRender::formatUserLine(user, html);
}

QString MessengerAbstractCommand::formatResourceLine(const Resource &resource)
{
    return MessengerTextRender::formatResourceLine(resource, false);
}

bool MessengerAbstractCommand::parseBlockDurationToken(const QString &token, QDateTime *untilUtcOut)
{
    const QString normalized = token.trimmed().toLower();
    if (normalized.isEmpty())
        return false;

    if (normalized == QStringLiteral("permanent") || normalized == QStringLiteral("perm")
        || normalized == QStringLiteral("forever") || normalized == tr("permanent")
        || normalized == tr("perm") || normalized == tr("forever")) {
        if (untilUtcOut)
            *untilUtcOut = QDateTime();
        return true;
    }

    int minutes = 0;
    if (!parseDurationMinutesToken(normalized, &minutes) || minutes <= 0)
        return false;

    if (untilUtcOut)
        *untilUtcOut = QDateTime::currentDateTimeUtc().addSecs(static_cast<qint64>(minutes) * 60);

    return true;
}

bool MessengerAbstractCommand::parseDurationMinutesToken(const QString &token, int *durationMinutes)
{
    if (!durationMinutes)
        return false;

    const QHash<QString, qint64> units =
            durationUnitMinutes(tr("minute;minutes;min", "Duration input units: minutes"),
                                tr("hour;hours;hr;hrs", "Duration input units: hours"),
                                tr("day;days", "Duration input units: days"),
                                tr("week;weeks", "Duration input units: weeks"),
                                tr("month;months;mo", "Duration input units: months"));
    const DurationToken parsed = parseDurationToken(token, units);
    if (!parsed.ok || parsed.minutes > std::numeric_limits<int>::max())
        return false;

    *durationMinutes = static_cast<int>(parsed.minutes);
    return true;
}

bool MessengerAbstractCommand::parseDurationChoice(const QString &token,
                                                   const QList<int> &options,
                                                   int *durationMinutes)
{
    if (!durationMinutes)
        return false;

    const QString normalized = token.trimmed();
    if (normalized.isEmpty())
        return false;

    const QHash<QString, qint64> units =
            durationUnitMinutes(tr("minute;minutes;min", "Duration input units: minutes"),
                                tr("hour;hours;hr;hrs", "Duration input units: hours"),
                                tr("day;days", "Duration input units: days"),
                                tr("week;weeks", "Duration input units: weeks"),
                                tr("month;months;mo", "Duration input units: months"));
    const DurationToken parsed = parseDurationToken(normalized, units);
    if (parsed.ok && parsed.hasExplicitUnit) {
        if (parsed.minutes > std::numeric_limits<int>::max())
            return false;

        const int minutes = static_cast<int>(parsed.minutes);
        if (options.isEmpty() || options.contains(minutes)) {
            *durationMinutes = minutes;
            return true;
        }
        return false;
    }

    bool ok = false;
    const int numeric = normalized.toInt(&ok);
    if (!ok)
        return false;

    if (options.isEmpty()) {
        if (numeric >= 0) {
            *durationMinutes = numeric;
            return true;
        }
        return false;
    }

    if (options.contains(numeric)) {
        *durationMinutes = numeric;
        return true;
    }

    if (numeric > 0 && numeric <= options.size()) {
        *durationMinutes = options.at(numeric - 1);
        return true;
    }

    return false;
}

bool MessengerAbstractCommand::isAfterReleaseStartToken(const QString &token)
{
    const QString normalized = token.trimmed().toLower();
    if (normalized.isEmpty())
        return false;

    if (normalized == QStringLiteral("0") || normalized == QStringLiteral("after_release")
        || normalized == QStringLiteral("after-release")
        || normalized == QStringLiteral("when released")
        || normalized == QStringLiteral("when the resource is released")) {
        return true;
    }

    return normalized == QString::fromUtf8("при освобождении ресурса");
}

bool MessengerAbstractCommand::parseScheduleDateTimeToken(const QString &token,
                                                          const QString &dateTimeFormat,
                                                          QDateTime *utcOut,
                                                          const UserTimeZone &timeZone)
{
    return ServerDateTimeFormat::parseUserDateTime(token,
                                                   dateTimeFormat,
                                                   ServerDateTimeFormat::ParseMode::LocalToUtc,
                                                   utcOut,
                                                   timeZone);
}

QString MessengerAbstractCommand::buildDurationPrompt(const QString &resourceName,
                                                      const QList<int> &options)
{
    QStringList lines;
    if (!resourceName.trimmed().isEmpty()) {
        lines << tr("Specify duration for resource '%1'.").arg(resourceName.trimmed());
    }

    if (options.isEmpty()) {
        lines << tr("Only indefinite occupation is configured. Use value 0.");
        return lines.join(QStringLiteral("\n"));
    }

    lines << tr("Available durations (enter duration or option number):");
    int index = 1;
    for (int minutes : options) {
        lines << tr("%1) %2 (%3 min)")
                         .arg(QString::number(index++),
                              MessengerTextRender::formatDurationMinutes(minutes),
                              QString::number(minutes));
    }

    lines << tr("Example: /occupy \"%1\" %2")
                     .arg(resourceName.isEmpty() ? QStringLiteral("<resource>") : resourceName,
                          QString::number(options.first()));
    return lines.join(QStringLiteral("\n"));
}

void MessengerAbstractCommand::setParserCommand(const QString &command)
{
    m_parser.setCommand(command);
}
