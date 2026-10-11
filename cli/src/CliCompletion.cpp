#include "CliCompletion.h"

#include <QSet>

namespace {

struct Token
{
    QString value;
    int start = 0;
    int end = 0;
};

QStringList metaCommandNames()
{
    return QStringList() << QStringLiteral("\\help") << QStringLiteral("\\connect")
                         << QStringLiteral("\\disconnect") << QStringLiteral("\\list")
                         << QStringLiteral("\\quit") << QStringLiteral("\\exit")
                         << QStringLiteral("\\q");
}

bool isSpaceAt(const QString &text, int index)
{
    return index >= 0 && index < text.size() && text.at(index).isSpace();
}

QVector<Token> tokenize(const QString &line)
{
    QVector<Token> tokens;
    int index = 0;
    while (index < line.size()) {
        while (index < line.size() && line.at(index).isSpace())
            ++index;
        if (index >= line.size())
            break;

        Token token;
        token.start = index;
        while (index < line.size() && !line.at(index).isSpace())
            ++index;
        token.end = index;
        token.value = line.mid(token.start, token.end - token.start);
        tokens.append(token);
    }
    return tokens;
}

int activeTokenIndex(const QVector<Token> &tokens, const QString &line, int cursor)
{
    for (int i = 0; i < tokens.size(); ++i) {
        const Token &token = tokens.at(i);
        const bool atEnd = cursor == token.end
                && (cursor == line.size() || !isSpaceAt(line, cursor));
        if ((cursor >= token.start && cursor < token.end) || atEnd)
            return i;
        if (token.end <= cursor && (i + 1 == tokens.size() || tokens.at(i + 1).start > cursor)) {
            if (cursor > token.end)
                return i + 1;
        }
    }
    return tokens.size();
}

QString prefixForToken(const Token &token, int cursor)
{
    if (cursor <= token.start)
        return QString();
    if (cursor >= token.end)
        return token.value;
    return token.value.left(cursor - token.start);
}

} // namespace

namespace CliCompletion {

Result meta(const QString &line, int cursor, const QVector<LocalCliRegistry::Entry> &entries)
{
    const int clamped = qBound(0, cursor, line.size());
    const QVector<Token> tokens = tokenize(line);
    const int index = activeTokenIndex(tokens, line, clamped);

    Result result;
    result.appendSpace = true;

    if (tokens.isEmpty() || index <= 0) {
        if (tokens.isEmpty()) {
            result.replaceFrom = clamped;
            result.replaceTo = clamped;
            result.values = filterPrefix(metaCommandNames(), QString());
            return result;
        }

        const int tokenIndex = index < tokens.size() ? index : 0;
        const Token active = tokenIndex < tokens.size() ? tokens.at(tokenIndex) : Token();
        if (index >= tokens.size()) {
            result.replaceFrom = clamped;
            result.replaceTo = clamped;
            result.values = filterPrefix(metaCommandNames(), QString());
            return result;
        }

        result.replaceFrom = active.start;
        result.replaceTo = active.end;
        QString prefix = prefixForToken(active, clamped);
        if (prefix.startsWith(QLatin1Char('\\')))
            prefix.remove(0, 1);
        QStringList names = metaCommandNames();
        QStringList prefixed;
        for (const QString &name : names) {
            const QString withoutSlash = name.mid(1);
            if (prefix.isEmpty() || withoutSlash.startsWith(prefix, Qt::CaseInsensitive)
                || name.startsWith(QLatin1Char('\\') + prefix, Qt::CaseInsensitive)) {
                prefixed.append(name);
            }
        }
        result.values = prefixed;
        return result;
    }

    const Token commandToken = tokens.first();
    QString command = commandToken.value;
    if (command.startsWith(QLatin1Char('\\')))
        command.remove(0, 1);
    command = command.toLower();

    if (command != QLatin1String("connect"))
        return result;

    if (index == 1) {
        const Token active = index < tokens.size() ? tokens.at(index) : Token();
        result.replaceFrom = index < tokens.size() ? active.start : clamped;
        result.replaceTo = index < tokens.size() ? active.end : clamped;
        const QString prefix = index < tokens.size() ? prefixForToken(active, clamped) : QString();

        QStringList values;
        values << QStringLiteral("web");
        for (const LocalCliRegistry::Entry &entry : entries) {
            if (entry.pid > 0)
                values.append(QString::number(entry.pid));
        }
        result.values = filterPrefix(values, prefix);
        return result;
    }

    QString connectKind;
    if (tokens.size() > 1)
        connectKind = tokens.at(1).value.toLower();

    if (index >= 2 && connectKind == QLatin1String("web")) {
        const Token active = index < tokens.size() ? tokens.at(index) : Token();
        result.replaceFrom = index < tokens.size() ? active.start : clamped;
        result.replaceTo = index < tokens.size() ? active.end : clamped;
        const QString prefix = index < tokens.size() ? prefixForToken(active, clamped) : QString();

        QStringList values;
        for (const LocalCliRegistry::Entry &entry : entries) {
            const QString key = entry.sharedMemoryKey.trimmed();
            if (!key.isEmpty())
                values.append(key);
        }
        result.values = filterPrefix(values, prefix);
        return result;
    }

    return result;
}

QString quoteToken(const QString &value)
{
    const QString trimmed = value;
    if (trimmed.isEmpty())
        return QStringLiteral("\"\"");

    // A leading '\' is the Local CLI meta-command prefix, not an escape.
    int inspectFrom = 0;
    if (trimmed.startsWith(QLatin1Char('\\')))
        inspectFrom = 1;

    bool needQuotes = false;
    for (int i = inspectFrom; i < trimmed.size(); ++i) {
        const QChar ch = trimmed.at(i);
        if (ch.isSpace() || ch == QLatin1Char('"') || ch == QLatin1Char('\\')
            || ch == QLatin1Char('\'')) {
            needQuotes = true;
            break;
        }
    }
    if (!needQuotes)
        return trimmed;

    QString escaped;
    escaped.reserve(trimmed.size() + 2);
    for (const QChar ch : trimmed) {
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\\'))
            escaped.append(QLatin1Char('\\'));
        escaped.append(ch);
    }
    return QStringLiteral("\"%1\"").arg(escaped);
}

QString commonPrefix(const QStringList &values)
{
    if (values.isEmpty())
        return QString();

    QString prefix = values.first();
    for (int i = 1; i < values.size(); ++i) {
        const QString &value = values.at(i);
        int shared = 0;
        const int limit = qMin(prefix.size(), value.size());
        while (shared < limit && prefix.at(shared).toLower() == value.at(shared).toLower())
            ++shared;
        prefix = prefix.left(shared);
        if (prefix.isEmpty())
            break;
    }
    return prefix;
}

QStringList filterPrefix(const QStringList &values, const QString &prefix)
{
    QStringList result;
    QSet<QString> seen;
    const QString normalized = prefix.trimmed();
    for (const QString &raw : values) {
        const QString value = raw.trimmed();
        if (value.isEmpty())
            continue;
        if (!normalized.isEmpty() && !value.startsWith(normalized, Qt::CaseInsensitive))
            continue;
        const QString key = value.toLower();
        if (seen.contains(key))
            continue;
        seen.insert(key);
        result.append(value);
        if (result.size() >= kMaxListedValues)
            break;
    }
    return result;
}

Result merge(const Result &left, const Result &right)
{
    Result result = left;
    if (right.values.isEmpty())
        return result;
    if (left.values.isEmpty())
        return right;

    result.replaceFrom = qMin(left.replaceFrom, right.replaceFrom);
    result.replaceTo = qMax(left.replaceTo, right.replaceTo);
    QStringList values = left.values;
    values.append(right.values);
    result.values = filterPrefix(values, QString());
    result.appendSpace = left.appendSpace || right.appendSpace;
    return result;
}

bool firstTokenIsMeta(const QString &line, int cursor)
{
    Q_UNUSED(cursor);
    const QString trimmed = line.trimmed();
    return trimmed.startsWith(QLatin1Char('\\'));
}

bool firstTokenIsSlash(const QString &line, int cursor)
{
    Q_UNUSED(cursor);
    const QString trimmed = line.trimmed();
    return trimmed.startsWith(QLatin1Char('/'));
}

} // namespace CliCompletion
