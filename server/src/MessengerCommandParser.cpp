#include "MessengerCommandParser.h"

#include <utility>

#include <QChar>

namespace {

void appendNonEmptyCommand(QStringList *commands, const QString &command)
{
    if (!commands)
        return;

    const QString trimmed = command.trimmed();
    if (!trimmed.isEmpty())
        commands->append(trimmed);
}

} // namespace

MessengerCommandParser::MessengerCommandParser()
    : m_tokenMode(TokenMode::Whitespace)
    , m_allowEmptyParts(false)
{
    // do nothing
}

MessengerCommandParser::MessengerCommandParser(QString command, QString description)
    : m_command(std::move(command))
    , m_description(std::move(description))
    , m_tokenMode(TokenMode::Whitespace)
    , m_allowEmptyParts(false)
{
    // do nothing
}

void MessengerCommandParser::setCommand(const QString &command)
{
    m_command = command;
}

QString MessengerCommandParser::command() const
{
    return m_command;
}

void MessengerCommandParser::setDescription(const QString &description)
{
    m_description = description;
}

QString MessengerCommandParser::description() const
{
    return m_description;
}

void MessengerCommandParser::setTokenMode(TokenMode mode)
{
    if (mode == TokenMode::Whitespace) {
        m_tokenMode = TokenMode::Whitespace;
        return;
    }

    // Pipe-delimited syntax is removed; keep whitespace mode for legacy callers.
    m_tokenMode = TokenMode::Whitespace;
}

MessengerCommandParser::TokenMode MessengerCommandParser::tokenMode() const
{
    return m_tokenMode;
}

void MessengerCommandParser::setAllowEmptyParts(bool allow)
{
    m_allowEmptyParts = allow;
}

bool MessengerCommandParser::allowEmptyParts() const
{
    return m_allowEmptyParts;
}

void MessengerCommandParser::addArgument(const QString &name,
                                         const QString &description,
                                         ArgumentFlags flags)
{
    Argument argument;
    argument.name = name;
    argument.description = description;
    argument.flags = flags;
    m_arguments.append(argument);
}

int MessengerCommandParser::argumentCount() const
{
    return m_arguments.size();
}

QVector<MessengerCommandParser::ArgumentInfo> MessengerCommandParser::arguments() const
{
    QVector<ArgumentInfo> result;
    result.reserve(m_arguments.size());
    for (const Argument &argument : m_arguments) {
        ArgumentInfo info;
        info.name = argument.name;
        info.optional = argument.isOptional();
        info.greedy = argument.isGreedy();
        info.allowEmpty = argument.allowsEmpty();
        result.append(info);
    }
    return result;
}

MessengerCommandParser::ParseResult MessengerCommandParser::parse(const QString &input) const
{
    ParseResult result;
    result.usage = usageSignature();

    if (m_arguments.isEmpty()) {
        if (input.trimmed().isEmpty()) {
            result.ok = true;
        } else {
            result.errorText =
                    tr("Command does not accept arguments.\nUsage: %1").arg(result.usage);
        }
        return result;
    }

    QStringList tokens;
    QString tokenError;
    if (!tokenize(input, &tokens, &tokenError)) {
        const QString message =
                tokenError.isEmpty() ? tr("Failed to parse arguments.") : tokenError;
        result.errorText = tr("%1\nUsage: %2").arg(message, result.usage);
        return result;
    }
    QStringList values;
    values.reserve(m_arguments.size());
    QVector<bool> provided;
    provided.reserve(m_arguments.size());

    int tokenIndex = 0;
    for (const Argument &argument : m_arguments) {
        if (argument.isGreedy()) {
            QString remainder;
            const bool hasToken = tokenIndex < tokens.size();
            if (tokenIndex < tokens.size()) {
                remainder = tokens.mid(tokenIndex).join(QStringLiteral(" ")).trimmed();
                tokenIndex = tokens.size();
            }

            if (!hasToken && !argument.isOptional()) {
                result.errorText = tr("Missing required argument %1.\nUsage: %2")
                                           .arg(argumentToken(argument), result.usage);
                return result;
            }
            if (hasToken && remainder.isEmpty() && !argument.allowsEmpty()) {
                result.errorText = tr("Missing required argument %1.\nUsage: %2")
                                           .arg(argumentToken(argument), result.usage);
                return result;
            }

            values.append(remainder);
            provided.append(hasToken);
            continue;
        }

        if (tokenIndex >= tokens.size()) {
            if (argument.isOptional()) {
                values.append(QString());
                provided.append(false);
                continue;
            }

            result.errorText = tr("Missing required argument %1.\nUsage: %2")
                                       .arg(argumentToken(argument), result.usage);
            return result;
        }

        const QString value = tokens.at(tokenIndex);
        if (value.isEmpty() && !argument.allowsEmpty()) {
            result.errorText = tr("Argument %1 cannot be empty.\nUsage: %2")
                                       .arg(argumentToken(argument), result.usage);
            return result;
        }

        values.append(value);
        provided.append(true);
        ++tokenIndex;
    }

    if (tokenIndex < tokens.size()) {
        result.errorText = tr("Unexpected argument \"%1\".\nUsage: %2")
                                   .arg(tokens.at(tokenIndex), result.usage);
        return result;
    }

    result.ok = true;
    result.values = values;
    result.provided = provided;
    return result;
}

bool MessengerCommandParser::splitMessageCommands(const QString &input,
                                                  QStringList *commands,
                                                  QString *errorOut)
{
    if (!commands)
        return false;

    commands->clear();
    if (errorOut)
        errorOut->clear();

    if (input.trimmed().isEmpty())
        return true;

    QStringList parsedCommands;
    QString current;
    bool inQuotes = false;
    bool escape = false;
    QChar quoteChar;

    for (int i = 0; i < input.size(); ++i) {
        const QChar ch = input.at(i);
        if (escape) {
            current.append(ch);
            if (ch == QLatin1Char('\r') && i + 1 < input.size()
                && input.at(i + 1) == QLatin1Char('\n')) {
                ++i;
                current.append(input.at(i));
            }
            escape = false;
            continue;
        }
        if (ch == QLatin1Char('\\')) {
            current.append(ch);
            escape = true;
            continue;
        }
        if (inQuotes) {
            current.append(ch);
            if (ch == quoteChar)
                inQuotes = false;
            continue;
        }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            current.append(ch);
            inQuotes = true;
            quoteChar = ch;
            continue;
        }
        if (ch == QLatin1Char('\r') || ch == QLatin1Char('\n')) {
            appendNonEmptyCommand(&parsedCommands, current);
            current.clear();
            if (ch == QLatin1Char('\r') && i + 1 < input.size()
                && input.at(i + 1) == QLatin1Char('\n')) {
                ++i;
            }
            continue;
        }
        current.append(ch);
    }

    if (inQuotes) {
        if (errorOut)
            *errorOut = tr("Unclosed quote in arguments.");
        return false;
    }

    appendNonEmptyCommand(&parsedCommands, current);
    *commands = parsedCommands;
    return true;
}

QString MessengerCommandParser::usageSignature() const
{
    QStringList parts;
    if (!m_command.isEmpty())
        parts << m_command;

    for (const Argument &argument : m_arguments) {
        parts << argumentToken(argument);
    }

    return parts.join(QStringLiteral(" ")).trimmed();
}

QString MessengerCommandParser::helpLine() const
{
    const QString usage = usageSignature();
    if (usage.isEmpty())
        return m_description;

    if (m_description.isEmpty())
        return usage;

    return QStringLiteral("%1 - %2").arg(usage, m_description);
}

bool MessengerCommandParser::containsUnquotedPipe(const QString &input) const
{
    bool inQuotes = false;
    bool escape = false;
    QChar quoteChar;

    for (int i = 0; i < input.size(); ++i) {
        const QChar ch = input.at(i);
        if (escape) {
            escape = false;
            continue;
        }
        if (ch == QLatin1Char('\\')) {
            escape = true;
            continue;
        }
        if (inQuotes) {
            if (ch == quoteChar)
                inQuotes = false;
            continue;
        }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            inQuotes = true;
            quoteChar = ch;
            continue;
        }
        if (ch == QLatin1Char('|'))
            return true;
    }
    return false;
}

bool MessengerCommandParser::tokenize(const QString &input,
                                      QStringList *tokens,
                                      QString *errorOut) const
{
    if (!tokens)
        return false;
    tokens->clear();
    if (errorOut)
        errorOut->clear();

    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty())
        return true;

    if (containsUnquotedPipe(trimmed)) {
        if (errorOut) {
            *errorOut = tr("Separator '|' is not supported. Use quotes for arguments with spaces.");
        }
        return false;
    }

    QString current;
    bool inQuotes = false;
    bool escape = false;
    bool tokenWasQuoted = false;
    QChar quoteChar;

    auto flushToken = [&]() {
        const QString token = current;
        const bool allowEmpty = tokenWasQuoted;
        if (!token.isEmpty() || allowEmpty)
            tokens->append(token);
        current.clear();
        tokenWasQuoted = false;
    };

    for (int i = 0; i < trimmed.size(); ++i) {
        const QChar ch = trimmed.at(i);
        if (escape) {
            current.append(ch);
            if (ch == QLatin1Char('\r') && i + 1 < trimmed.size()
                && trimmed.at(i + 1) == QLatin1Char('\n')) {
                ++i;
                current.append(trimmed.at(i));
            }
            escape = false;
            continue;
        }
        if (ch == QLatin1Char('\\')) {
            escape = true;
            continue;
        }
        if (inQuotes) {
            if (ch == quoteChar) {
                inQuotes = false;
                tokenWasQuoted = true;
                continue;
            }
            current.append(ch);
            continue;
        }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            inQuotes = true;
            quoteChar = ch;
            tokenWasQuoted = true;
            continue;
        }
        if (ch.isSpace()) {
            if (!current.isEmpty() || tokenWasQuoted)
                flushToken();
            continue;
        }
        current.append(ch);
    }

    if (escape)
        current.append(QLatin1Char('\\'));

    if (inQuotes) {
        if (errorOut)
            *errorOut = tr("Unclosed quote in arguments.");
        return false;
    }

    if (!current.isEmpty() || tokenWasQuoted)
        flushToken();

    return true;
}

QString MessengerCommandParser::argumentToken(const Argument &argument) const
{
    QString label = argument.name;
    if (argument.isGreedy() && !label.endsWith(QStringLiteral("..."))) {
        label.append(QStringLiteral("..."));
    }

    if (argument.isOptional())
        return QStringLiteral("[%1]").arg(label);

    return QStringLiteral("<%1>").arg(label);
}

MessengerCommandParser::CompletionContext MessengerCommandParser::contextAtCursor(
        const QString &text, int cursor, const QVector<ArgumentInfo> &arguments)
{
    CompletionContext result;
    const int clamped = qBound(0, cursor, text.size());
    result.replaceFrom = clamped;
    result.replaceTo = clamped;

    int commandStart = 0;
    while (commandStart < text.size() && text.at(commandStart).isSpace())
        ++commandStart;

    if (commandStart >= text.size()) {
        result.ok = true;
        result.completingCommand = true;
        return result;
    }

    int commandEnd = commandStart;
    while (commandEnd < text.size() && !text.at(commandEnd).isSpace())
        ++commandEnd;

    result.commandName = text.mid(commandStart, commandEnd - commandStart);
    if (clamped <= commandEnd) {
        result.ok = true;
        result.completingCommand = true;
        result.prefix = text.mid(commandStart, clamped - commandStart);
        result.replaceFrom = commandStart;
        result.replaceTo = commandEnd;
        return result;
    }

    MessengerCommandParser probe;
    if (probe.containsUnquotedPipe(text.mid(commandEnd))) {
        return result;
    }

    int argsStart = commandEnd;
    while (argsStart < text.size() && text.at(argsStart).isSpace())
        ++argsStart;

    const QString args = text.mid(argsStart);
    const int argsCursor = qBound(0, clamped - argsStart, args.size());

    struct Token
    {
        QString value;
        QString prefix;
        int start = 0;
        int end = 0;
        bool quoted = false;
    };

    QVector<Token> tokens;
    QString current;
    QString currentPrefix;
    bool inToken = false;
    bool inQuotes = false;
    bool escape = false;
    bool tokenWasQuoted = false;
    int tokenStart = 0;
    QChar quoteChar;

    auto startToken = [&](int index) {
        inToken = true;
        tokenStart = index;
        current.clear();
        currentPrefix.clear();
        tokenWasQuoted = false;
        inQuotes = false;
        escape = false;
        quoteChar = QChar();
    };

    auto appendTokenChar = [&](QChar ch, int index) {
        current.append(ch);
        if (index < argsCursor)
            currentPrefix.append(ch);
    };

    auto flushToken = [&](int end) {
        if (!inToken)
            return;
        Token token;
        token.value = current;
        token.prefix = currentPrefix;
        token.start = tokenStart;
        token.end = end;
        token.quoted = tokenWasQuoted;
        tokens.append(token);
        inToken = false;
        inQuotes = false;
        escape = false;
        tokenWasQuoted = false;
        current.clear();
        currentPrefix.clear();
        quoteChar = QChar();
    };

    for (int index = 0; index < args.size(); ++index) {
        const QChar ch = args.at(index);
        if (!inToken) {
            if (ch.isSpace())
                continue;
            startToken(index);
        }
        if (escape) {
            appendTokenChar(ch, index);
            escape = false;
            continue;
        }
        if (ch == QLatin1Char('\\')) {
            escape = true;
            continue;
        }
        if (inQuotes) {
            if (ch == quoteChar) {
                inQuotes = false;
                tokenWasQuoted = true;
                continue;
            }
            appendTokenChar(ch, index);
            continue;
        }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            inQuotes = true;
            tokenWasQuoted = true;
            quoteChar = ch;
            continue;
        }
        if (ch.isSpace()) {
            flushToken(index);
            continue;
        }
        appendTokenChar(ch, index);
    }

    if (escape)
        appendTokenChar(QLatin1Char('\\'), qMax(0, args.size() - 1));
    if (inToken)
        flushToken(args.size());

    int tokenIndex = 0;
    int activeTokenIndex = -1;
    for (int index = 0; index < tokens.size(); ++index) {
        const Token &token = tokens.at(index);
        const bool cursorAtTokenEnd =
                argsCursor == token.end
                && (argsCursor == args.size() || !args.at(argsCursor).isSpace());
        const bool cursorInsideToken = argsCursor >= token.start && argsCursor < token.end;
        if (cursorInsideToken || cursorAtTokenEnd) {
            activeTokenIndex = index;
            tokenIndex = index;
            break;
        }
        if (token.end <= argsCursor)
            tokenIndex = index + 1;
    }

    if (activeTokenIndex >= 0) {
        const Token &active = tokens.at(activeTokenIndex);
        result.prefix = active.prefix;
        result.replaceFrom = argsStart + active.start;
        result.replaceTo = argsStart + active.end;
    } else {
        result.replaceFrom = clamped;
        result.replaceTo = clamped;
        result.prefix.clear();
    }

    result.argumentIndex = tokenIndex;
    if (!arguments.isEmpty()) {
        result.argumentIndex = arguments.size();
        for (int index = 0; index < arguments.size(); ++index) {
            const ArgumentInfo &argument = arguments.at(index);
            if (argument.greedy && tokenIndex >= index) {
                result.argumentIndex = index;
                break;
            }
            if (tokenIndex == index) {
                result.argumentIndex = index;
                break;
            }
        }
    }

    const int previousCount = qMin(tokenIndex, tokens.size());
    for (int index = 0; index < previousCount; ++index)
        result.previousValues.append(tokens.at(index).value);

    result.ok = true;
    return result;
}
