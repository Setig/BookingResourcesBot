#include "CliCommandLineParser.h"

#include "LocalCliHelper.h"

#include <QStringList>
#include <QTextStream>
#include <QCoreApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>

class CliCommandLineParserPrivate
{
public:
    CliCommandLineParserPrivate();

    QCommandLineParser parser;
    QCommandLineOption listOption;
    QCommandLineOption pidOption;
    QCommandLineOption webSharedMemoryKeyOption;
    QCommandLineOption commandOption;
};

CliCommandLineParserPrivate::CliCommandLineParserPrivate()
    : listOption({ QStringLiteral("l"), QStringLiteral("list") },
                 CliCommandLineParser::tr(
                         "List running booking-resources-server and booking-resources-web "
                         "Local CLI processes."))
    , pidOption({ QStringLiteral("p"), QStringLiteral("pid") },
                CliCommandLineParser::tr(
                        "PID of the booking-resources-server or booking-resources-web process "
                        "to connect to."),
                QStringLiteral("pid"))
    , webSharedMemoryKeyOption({ QStringLiteral("web-shared-memory-key") },
                               CliCommandLineParser::tr(
                                       "Shared-memory key of the booking-resources-web process "
                                       "to connect to."),
                               QStringLiteral("key"))
    , commandOption({ QStringLiteral("c"), QStringLiteral("command") },
                    CliCommandLineParser::tr(
                            "Execute a single admin command and exit (requires --pid or "
                            "--web-shared-memory-key)."),
                    QStringLiteral("command"))
{
    parser.setApplicationDescription(
            CliCommandLineParser::tr("Local administrative shell for booking-resources-server and "
                                     "booking-resources-web."));
    parser.addHelpOption();
    parser.addVersionOption();

    parser.addOption(listOption);
    parser.addOption(pidOption);
    parser.addOption(webSharedMemoryKeyOption);
    parser.addOption(commandOption);
    parser.addPositionalArgument(QStringLiteral("pid"),
                                 CliCommandLineParser::tr(
                                         "PID to connect to (optional when --pid is set)."),
                                 QStringLiteral("[pid]"));
}

CliCommandLineParser::CliCommandLineParser()
    : d(new CliCommandLineParserPrivate())
{
    // do nothing
}

CliCommandLineParser::~CliCommandLineParser()
{
    delete d;
}

void CliCommandLineParser::process(const QCoreApplication &coreApp)
{
    d->parser.process(coreApp);
}

bool CliCommandLineParser::parse(const QStringList &arguments)
{
    return d->parser.parse(arguments);
}

bool CliCommandLineParser::listRequested() const
{
    return d->parser.isSet(d->listOption);
}

bool CliCommandLineParser::commandRequested() const
{
    return d->parser.isSet(d->commandOption);
}

bool CliCommandLineParser::hasPidArgument() const
{
    return d->parser.isSet(d->pidOption) || !d->parser.positionalArguments().isEmpty();
}

bool CliCommandLineParser::hasWebSharedMemoryKey() const
{
    return d->parser.isSet(d->webSharedMemoryKeyOption) && !webSharedMemoryKey().isEmpty();
}

QString CliCommandLineParser::webSharedMemoryKey() const
{
    return d->parser.value(d->webSharedMemoryKeyOption).trimmed();
}

bool CliCommandLineParser::hasTargetArgument() const
{
    return hasPidArgument() || hasWebSharedMemoryKey();
}

CliCommandLineParser::PidParsingResult CliCommandLineParser::targetPid() const
{
    PidParsingResult result;

    if (d->parser.isSet(d->pidOption)) {
        bool ok = false;
        const qint64 pid = LocalCliHelper::parsePid(d->parser.value(d->pidOption), &ok);
        if (!ok) {
            result.status = PidParsingResult::Status::InvalidOptionValue;
            return result;
        }

        result.status = PidParsingResult::Status::Parsed;
        result.pid = pid;
        return result;
    }

    const QStringList positional = d->parser.positionalArguments();
    if (!positional.isEmpty()) {
        bool ok = false;
        const qint64 pid = LocalCliHelper::parsePid(positional.constFirst(), &ok);
        if (!ok) {
            result.status = PidParsingResult::Status::InvalidPositionalValue;
            return result;
        }

        result.status = PidParsingResult::Status::Parsed;
        result.pid = pid;
        return result;
    }

    return result;
}

void CliCommandLineParser::printPidParseError(PidParsingResult::Status status) const
{
    QTextStream ts(stderr);

    switch (status) {
    case PidParsingResult::Status::InvalidOptionValue:
        ts << tr("Invalid PID specified via --pid option.") << Qt::endl;
        break;
    case PidParsingResult::Status::InvalidPositionalValue:
        ts << tr("Invalid PID specified as positional argument.") << Qt::endl;
        break;
    case PidParsingResult::Status::NotProvided:
    case PidParsingResult::Status::Parsed:
        break;
    }
}

QString CliCommandLineParser::commandText() const
{
    return d->parser.value(d->commandOption);
}

void CliCommandLineParser::printCommandRequiresTargetMessage() const
{
    QTextStream(stderr) << tr("--command requires a target process. Provide --pid <pid>, a "
                              "positional PID, or --web-shared-memory-key <key>.")
                        << Qt::endl;
}

void CliCommandLineParser::printEmptyCommandMessage() const
{
    QTextStream(stderr) << tr("Command text must not be empty.") << Qt::endl;
}
