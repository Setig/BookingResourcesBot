#ifndef MESSENGERCOMMANDPARSER_H
#define MESSENGERCOMMANDPARSER_H

#include <QVector>
#include <QString>
#include <QStringList>
#include <QCoreApplication>

/**
 * @brief Describes and parses a bot command with typed arguments.
 *
 * Supports whitespace tokenization with quoting and escaping, optional/greedy arguments,
 * and can generate usage/help strings.
 */
class MessengerCommandParser
{
    Q_GADGET
    Q_DECLARE_TR_FUNCTIONS(MessengerCommandParser);

public:
    /**
     * @brief Tokenization strategy hint used when parsing raw argument strings.
     *
     * Pipe-delimited mode is no longer supported and is kept only for backward compatibility.
     */
    enum class TokenMode {
        Auto,
        PipeDelimited,
        Whitespace
    };

    /**
     * @brief Flags controlling argument parsing behavior.
     */
    enum class ArgumentFlag : quint8 {
        Optional = 0x1,
        Greedy = 0x2,
        AllowEmpty = 0x4
    };
    Q_DECLARE_FLAGS(ArgumentFlags, ArgumentFlag)

    /**
     * @brief Result returned by parse().
     */
    struct ParseResult
    {
        bool ok = false;
        QStringList values;
        QVector<bool> provided;
        QString errorText;
        QString usage;
    };

    /**
     * @brief Public metadata for a parser argument.
     *
     * This is used by clients that need to reproduce completion-context parsing locally.
     */
    struct ArgumentInfo
    {
        QString name;
        bool optional = false;
        bool greedy = false;
        bool allowEmpty = false;
    };

    /**
     * @brief Cursor-aware parse of a command line for completion.
     */
    struct CompletionContext
    {
        bool ok = false;
        bool completingCommand = false;
        QString commandName;
        int argumentIndex = 0;
        int replaceFrom = 0;
        int replaceTo = 0;
        QString prefix;
        QStringList previousValues;
    };

    MessengerCommandParser();
    explicit MessengerCommandParser(QString command, QString description = QString());

    void setCommand(const QString &command);
    QString command() const;

    void setDescription(const QString &description);
    QString description() const;

    void setTokenMode(TokenMode mode);
    TokenMode tokenMode() const;

    /**
     * @brief Legacy compatibility flag.
     *
     * Empty-part handling was used for pipe-delimited syntax and has no effect now.
     */
    void setAllowEmptyParts(bool allow);
    bool allowEmptyParts() const;

    void addArgument(const QString &name,
                     const QString &description = QString(),
                     ArgumentFlags flags = ArgumentFlags());
    int argumentCount() const;
    QVector<ArgumentInfo> arguments() const;

    ParseResult parse(const QString &input) const;
    static bool splitMessageCommands(const QString &input,
                                     QStringList *commands,
                                     QString *errorOut = nullptr);
    static CompletionContext contextAtCursor(const QString &text,
                                             int cursor,
                                             const QVector<ArgumentInfo> &arguments);

    QString usageSignature() const;
    QString helpLine() const;

private:
    struct Argument
    {
        QString name;
        QString description;
        ArgumentFlags flags;

        bool isOptional() const { return flags.testFlag(ArgumentFlag::Optional); }

        bool isGreedy() const { return flags.testFlag(ArgumentFlag::Greedy); }

        bool allowsEmpty() const { return flags.testFlag(ArgumentFlag::AllowEmpty); }
    };

    bool containsUnquotedPipe(const QString &input) const;
    bool tokenize(const QString &input, QStringList *tokens, QString *errorOut) const;
    QString argumentToken(const Argument &argument) const;

    QString m_command;
    QString m_description;
    TokenMode m_tokenMode;
    bool m_allowEmptyParts;
    QVector<Argument> m_arguments;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(MessengerCommandParser::ArgumentFlags)

#endif // MESSENGERCOMMANDPARSER_H
