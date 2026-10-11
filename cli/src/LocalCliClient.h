#ifndef LOCALCLICLIENT_H
#define LOCALCLICLIENT_H

#include <QObject>

#include <QString>
#include <QByteArray>
#include <QTextStream>
#include <QLocalSocket>

#include "CliCompletion.h"
#include "LocalCliProtocol.h"
#include "LocalCliRegistry.h"

/**
 * @brief CLI-side client that connects to a LocalCliBot instance over QLocalSocket.
 *
 * Provides instance listing, command execution, and an interactive shell.
 */
class LocalCliClient : public QObject
{
    Q_OBJECT

public:
    explicit LocalCliClient(QObject *parent = nullptr);
    ~LocalCliClient() override;

    [[nodiscard]] bool listInstances() const;
    bool connectToProcess(qint64 pid);
    bool connectToWebKey(const QString &sharedMemoryKey);
    void disconnectFromProcess();
    bool executeCommand(const QString &command);
    int interactiveShell();

private:
    bool connectToEntry(const LocalCliRegistry::Entry &entry, bool registeredEntryFound);
    bool sendFrame(const LocalCliProtocol::Frame &frame);
    bool waitForFrame(LocalCliProtocol::Frame *frame, int timeoutMs, bool *timedOut = nullptr);
    bool performHandshake();
    bool sendCommand(const QString &command);
    bool readCommandResponse(bool *commandSucceeded = nullptr);
    void consumePendingServerMessages(int waitMs);
    void printServerFrame(const LocalCliProtocol::Frame &frame);
    bool handleMetaCommand(const QString &line, bool *exitRequested);
    void printHelp() const;
    [[nodiscard]] QString prompt() const;
    [[nodiscard]] int promptVisibleWidth() const;
    [[nodiscard]] CliCompletion::Result completeLine(const QString &line, int cursor);
    bool requestHostCompletion(const QString &line, int cursor, CliCompletion::Result *result);
    [[nodiscard]] static QString formatEntryLine(const LocalCliRegistry::Entry &entry,
                                                 bool reachable,
                                                 int endpointWidth,
                                                 int keyWidth);
    [[nodiscard]] bool isConnected() const;
    void cleanupStaleEntries() const;
    void resetConnection();

    mutable QTextStream m_stdout;
    mutable QTextStream m_stderr;
    QLocalSocket m_socket;
    QByteArray m_buffer;
    LocalCliRegistry::Entry m_targetEntry;
    bool m_promptColorEnabled;
    bool m_supportsComplete;
};

#endif // LOCALCLICLIENT_H
