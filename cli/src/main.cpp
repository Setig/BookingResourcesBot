#include <QCoreApplication>

#include "LocalCliClient.h"
#include "CliCommandLineParser.h"
#include "BookingResourcesTranslator.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("booking-resources-cli"));
    QCoreApplication::setApplicationVersion(QStringLiteral(APP_VERSION));

    BookingResourcesTranslator translator(QStringLiteral("booking-resources-cli"));
    translator.initialize();

    CliCommandLineParser parser;
    parser.process(app);

    LocalCliClient client;

    const bool listRequested = parser.listRequested();
    const bool commandRequested = parser.commandRequested();

    if (listRequested) {
        const bool ok = client.listInstances();
        const bool listOnly = !commandRequested && !parser.hasTargetArgument();
        if (!ok || listOnly)
            return ok ? 0 : 1;
    }

    const CliCommandLineParser::PidParsingResult pidResult = parser.targetPid();
    if (pidResult.status == CliCommandLineParser::PidParsingResult::Status::InvalidOptionValue
        || pidResult.status
                   == CliCommandLineParser::PidParsingResult::Status::InvalidPositionalValue) {
        parser.printPidParseError(pidResult.status);
        return 1;
    }

    const bool pidProvided =
            pidResult.status == CliCommandLineParser::PidParsingResult::Status::Parsed;
    const qint64 targetPid = pidProvided ? pidResult.pid : 0;

    const QString webSharedMemoryKey = parser.webSharedMemoryKey();
    const bool webKeyProvided = parser.hasWebSharedMemoryKey();

    if (commandRequested && !pidProvided && !webKeyProvided) {
        parser.printCommandRequiresTargetMessage();
        return 1;
    }

    if (pidProvided) {
        if (!client.connectToProcess(targetPid))
            return 1;
    } else if (webKeyProvided) {
        if (!client.connectToWebKey(webSharedMemoryKey))
            return 1;
    }

    if (commandRequested) {
        const QString command = parser.commandText();
        if (command.trimmed().isEmpty()) {
            parser.printEmptyCommandMessage();
            client.disconnectFromProcess();
            return 1;
        }
        const bool success = client.executeCommand(command);
        client.disconnectFromProcess();
        return success ? 0 : 1;
    }

    return client.interactiveShell();
}
