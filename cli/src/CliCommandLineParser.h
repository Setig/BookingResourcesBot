#ifndef CLICOMMANDLINEPARSER_H
#define CLICOMMANDLINEPARSER_H

#include <QCoreApplication>
#include <QStringList>

class CliCommandLineParserPrivate;

/**
 * @brief Command-line parser helper for the local CLI process.
 *
 * Encapsulates QCommandLineParser configuration, exposes parsed flags, and
 * prints localized validation errors for CLI-specific arguments.
 */
class CliCommandLineParser final
{
    Q_DECLARE_TR_FUNCTIONS(CliCommandLineParser)

public:
    CliCommandLineParser();
    ~CliCommandLineParser();

    /**
     * @brief Parses the command-line arguments of the given Qt application.
     */
    void process(const QCoreApplication &coreApp);

    /**
     * @brief Parses @p arguments without exiting on unknown or invalid options.
     *
     * @return true when QCommandLineParser accepted the argument list.
     */
    bool parse(const QStringList &arguments);

    /**
     * @brief Indicates whether '--list' was provided.
     */
    bool listRequested() const;

    /**
     * @brief Indicates whether '--command' was provided.
     */
    bool commandRequested() const;

    /**
     * @brief Returns true if a PID was provided via option or positional arg.
     */
    bool hasPidArgument() const;

    /**
     * @brief Returns true if '--web-shared-memory-key' was provided.
     */
    bool hasWebSharedMemoryKey() const;

    /**
     * @brief Returns the trimmed web shared-memory key, if provided.
     */
    QString webSharedMemoryKey() const;

    /**
     * @brief Returns true if a PID or web shared-memory key was provided.
     */
    bool hasTargetArgument() const;

    /**
     * @brief Result of parsing a target process ID.
     */
    struct PidParsingResult
    {
        /**
         * @brief Parsing status for PID input.
         */
        enum class Status {
            NotProvided,
            Parsed,
            InvalidOptionValue,
            InvalidPositionalValue,
        };

        Status status = Status::NotProvided;
        qint64 pid = 0;
    };

    /**
     * @brief Returns PID parsing status and value.
     */
    PidParsingResult targetPid() const;

    /**
     * @brief Prints localized error for invalid PID input.
     */
    void printPidParseError(PidParsingResult::Status status) const;

    /**
     * @brief Returns the raw command text supplied via '--command'.
     */
    QString commandText() const;

    /**
     * @brief Prints an error if '--command' is used without a target process.
     */
    void printCommandRequiresTargetMessage() const;

    /**
     * @brief Prints an error when '--command' value is empty.
     */
    void printEmptyCommandMessage() const;

private:
    CliCommandLineParserPrivate *d;
};

#endif // CLICOMMANDLINEPARSER_H
