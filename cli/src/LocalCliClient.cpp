#include "LocalCliClient.h"

#include "CliConsoleInterruptPolicy.h"
#include "CliHistory.h"
#include "CliLineEditor.h"
#include "LocalCliHelper.h"

#include <QTextStream>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QRegularExpression>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#  include <QStringConverter>
#else
#  include <QTextCodec>
#endif
#ifdef Q_OS_WIN
#  include <windows.h>
#  ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#    define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#  endif
#elif defined(Q_OS_UNIX)
#  include <signal.h>
#  include <unistd.h>
#endif

#include <atomic>

namespace {

constexpr int kConnectTimeoutMs = 3000;
constexpr int kDisconnectTimeoutMs = 1000;
constexpr int kDefaultFrameTimeoutMs = 30000;
constexpr int kListProbeTimeoutMs = 200;
constexpr int kListPidWidth = 8;
constexpr int kListKindWidth = 6;
constexpr int kListStatusWidth = 7;
constexpr int kListMinEndpointWidth = 22;
constexpr int kListMinKeyWidth = 24;
constexpr int kCommandResponseTimeoutMs = 120000;
constexpr int kCompleteTimeoutMs = 3000;
constexpr int kHandshakeInfoWaitMs = 200;

std::atomic<bool> s_interruptRequested{ false };

#ifdef Q_OS_WIN
HANDLE s_mainThread = nullptr;

BOOL WINAPI cliConsoleCtrlHandler(DWORD ctrlType)
{
    if (ctrlType != CTRL_C_EVENT && ctrlType != CTRL_BREAK_EVENT)
        return FALSE;

    s_interruptRequested.store(true);
    if (s_mainThread)
        CancelSynchronousIo(s_mainThread);
    return TRUE;
}

class ConsoleInterruptGuard
{
public:
    ConsoleInterruptGuard();
    ~ConsoleInterruptGuard();

    ConsoleInterruptGuard(const ConsoleInterruptGuard &) = delete;
    ConsoleInterruptGuard &operator=(const ConsoleInterruptGuard &) = delete;
    ConsoleInterruptGuard(ConsoleInterruptGuard &&) = delete;
    ConsoleInterruptGuard &operator=(ConsoleInterruptGuard &&) = delete;

private:
    bool m_handlerInstalled;
};

ConsoleInterruptGuard::ConsoleInterruptGuard()
    : m_handlerInstalled(false)
{
    s_interruptRequested.store(false);

    HANDLE currentThread = GetCurrentThread();
    if (!DuplicateHandle(GetCurrentProcess(),
                         currentThread,
                         GetCurrentProcess(),
                         &s_mainThread,
                         0,
                         FALSE,
                         DUPLICATE_SAME_ACCESS)) {
        s_mainThread = nullptr;
    }

    m_handlerInstalled = SetConsoleCtrlHandler(cliConsoleCtrlHandler, TRUE) != FALSE;
}

ConsoleInterruptGuard::~ConsoleInterruptGuard()
{
    if (m_handlerInstalled)
        SetConsoleCtrlHandler(cliConsoleCtrlHandler, FALSE);
    if (s_mainThread) {
        CloseHandle(s_mainThread);
        s_mainThread = nullptr;
    }
    s_interruptRequested.store(false);
}
#elif defined(Q_OS_UNIX)
void cliSigintHandler(int signalNumber)
{
    Q_UNUSED(signalNumber);
    s_interruptRequested.store(true);
}

class ConsoleInterruptGuard
{
public:
    ConsoleInterruptGuard();
    ~ConsoleInterruptGuard();

    ConsoleInterruptGuard(const ConsoleInterruptGuard &) = delete;
    ConsoleInterruptGuard &operator=(const ConsoleInterruptGuard &) = delete;
    ConsoleInterruptGuard(ConsoleInterruptGuard &&) = delete;
    ConsoleInterruptGuard &operator=(ConsoleInterruptGuard &&) = delete;

private:
    struct sigaction m_previousAction;
    bool m_handlerInstalled;
};

ConsoleInterruptGuard::ConsoleInterruptGuard()
    : m_previousAction()
    , m_handlerInstalled(false)
{
    s_interruptRequested.store(false);

    struct sigaction action = {};
    action.sa_handler = cliSigintHandler;
    sigemptyset(&action.sa_mask);
    // Do not set SA_RESTART. glibc signal() enables it, so a blocking stdin
    // read keeps waiting after SIGINT until another key (usually Enter).
    action.sa_flags = 0;
    m_handlerInstalled = (sigaction(SIGINT, &action, &m_previousAction) == 0);
}

ConsoleInterruptGuard::~ConsoleInterruptGuard()
{
    if (m_handlerInstalled)
        sigaction(SIGINT, &m_previousAction, nullptr);
    s_interruptRequested.store(false);
}
#else
class ConsoleInterruptGuard
{
public:
    ConsoleInterruptGuard();
    ~ConsoleInterruptGuard();

    ConsoleInterruptGuard(const ConsoleInterruptGuard &) = delete;
    ConsoleInterruptGuard &operator=(const ConsoleInterruptGuard &) = delete;
    ConsoleInterruptGuard(ConsoleInterruptGuard &&) = delete;
    ConsoleInterruptGuard &operator=(ConsoleInterruptGuard &&) = delete;
};

ConsoleInterruptGuard::ConsoleInterruptGuard()
{
    // do nothing
}

ConsoleInterruptGuard::~ConsoleInterruptGuard()
{
    // do nothing
}
#endif
constexpr qsizetype kMaxBufferedBytes = LocalCliProtocol::kMaxPayloadSize * 2;
constexpr char kPromptColorEnvVar[] = "BOOKING_RESOURCES_CLI_COLOR";

enum class PromptColorPreference {
    Auto,
    Always,
    Never,
};

PromptColorPreference promptColorPreference()
{
    const QByteArray raw = qgetenv(kPromptColorEnvVar);
    if (raw.isEmpty())
        return PromptColorPreference::Auto;

    const QByteArray normalized = raw.trimmed().toLower();
    if (normalized == "always" || normalized == "on" || normalized == "1" || normalized == "true")
        return PromptColorPreference::Always;

    if (normalized == "never" || normalized == "off" || normalized == "0" || normalized == "false")
        return PromptColorPreference::Never;

    return PromptColorPreference::Auto;
}

QString entryDisplayKey(const LocalCliRegistry::Entry &entry)
{
    const QString key = entry.sharedMemoryKey.trimmed();
    return key.isEmpty() ? QStringLiteral("-") : key;
}

QString formatListColumns(const QString &pid,
                          const QString &kind,
                          const QString &status,
                          const QString &endpoint,
                          const QString &key,
                          const QString &started,
                          int endpointWidth,
                          int keyWidth,
                          bool rightJustifyPid)
{
    return QStringLiteral("%1  %2  %3  %4  %5  %6")
            .arg(rightJustifyPid ? pid.rightJustified(kListPidWidth, QLatin1Char(' '))
                                 : pid.leftJustified(kListPidWidth, QLatin1Char(' ')),
                 kind.leftJustified(kListKindWidth, QLatin1Char(' ')),
                 status.leftJustified(kListStatusWidth, QLatin1Char(' ')),
                 endpoint.leftJustified(endpointWidth, QLatin1Char(' ')),
                 key.leftJustified(keyWidth, QLatin1Char(' ')),
                 started);
}

void expandListColumnWidths(const QVector<LocalCliRegistry::Entry> &entries,
                            int &endpointWidth,
                            int &keyWidth)
{
    for (const LocalCliRegistry::Entry &entry : entries) {
        endpointWidth = qMax(endpointWidth, int(entry.serverName.size()));
        keyWidth = qMax(keyWidth, int(entryDisplayKey(entry).size()));
    }
}

bool probeEndpoint(const QString &serverName, int timeoutMs)
{
    if (serverName.isEmpty())
        return false;

    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (!socket.waitForConnected(timeoutMs))
        return false;

    socket.disconnectFromServer();
    return true;
}

void configureTextStreamUtf8(QTextStream &stream)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    stream.setEncoding(QStringConverter::Utf8);
#else
    stream.setCodec("UTF-8");
#endif
}

bool enablePromptHighlighting(PromptColorPreference preference)
{
#ifdef Q_OS_WIN
    Q_UNUSED(preference);

    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE)
        return false;

    DWORD mode = 0;
    if (!GetConsoleMode(handle, &mode))
        return false;

    if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) == 0) {
        if (!SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
            return false;
    }

    return true;
#elif defined(Q_OS_UNIX)
    if (preference == PromptColorPreference::Always)
        return true;

    if (!isatty(fileno(stdout)))
        return false;

    const QByteArray term = qgetenv("TERM").trimmed().toLower();
    if (term.isEmpty() || term == "dumb")
        return false;

    return true;
#else
    Q_UNUSED(preference);
    return false;
#endif
}

} // namespace

LocalCliClient::LocalCliClient(QObject *parent)
    : QObject(parent)
    , m_stdout(stdout)
    , m_stderr(stderr)
    , m_promptColorEnabled(false)
    , m_supportsComplete(false)
{
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    configureTextStreamUtf8(m_stdout);
    configureTextStreamUtf8(m_stderr);
    const PromptColorPreference colorPreference = promptColorPreference();
    if (colorPreference == PromptColorPreference::Never) {
        m_promptColorEnabled = false;
    } else {
        m_promptColorEnabled = enablePromptHighlighting(colorPreference);
    }

    cleanupStaleEntries();

    // do nothing
}

LocalCliClient::~LocalCliClient()
{
    disconnectFromProcess();
}

bool LocalCliClient::listInstances() const
{
    const QVector<LocalCliRegistry::Entry> entries = LocalCliRegistry::entries();
    if (entries.isEmpty()) {
        m_stdout << tr("No active Local CLI processes were found.") << Qt::endl;
        return true;
    }

    const QString headerEndpoint = tr("Endpoint");
    const QString headerKey = tr("Key");
    int endpointWidth = qMax(kListMinEndpointWidth, int(headerEndpoint.size()));
    int keyWidth = qMax(kListMinKeyWidth, int(headerKey.size()));
    expandListColumnWidths(entries, endpointWidth, keyWidth);

    m_stdout << formatListColumns(tr("PID"),
                                  tr("Kind"),
                                  tr("Status"),
                                  headerEndpoint,
                                  headerKey,
                                  tr("Started"),
                                  endpointWidth,
                                  keyWidth,
                                  false)
             << Qt::endl;
    for (const LocalCliRegistry::Entry &entry : entries) {
        const bool reachable = probeEndpoint(entry.serverName, kListProbeTimeoutMs);
        QString line = formatEntryLine(entry, reachable, endpointWidth, keyWidth);
        if (!reachable) {
            line.append(tr("  (stale entry removed)"));
            LocalCliRegistry::unregisterInstance(entry.pid);
        }
        m_stdout << line << Qt::endl;
    }
    return true;
}

bool LocalCliClient::connectToProcess(qint64 pid)
{
    if (pid <= 0) {
        m_stderr << tr("PID must be greater than zero.") << Qt::endl;
        return false;
    }

    LocalCliRegistry::Entry entry;
    const bool registeredEntryFound = LocalCliRegistry::entryForPid(pid, &entry);
    if (!registeredEntryFound) {
        entry.pid = pid;
        entry.kind = LocalCliRegistry::Kind::Server;
        entry.serverName = LocalCliRegistry::defaultServerName(pid);
    }

    return connectToEntry(entry, registeredEntryFound);
}

bool LocalCliClient::connectToWebKey(const QString &sharedMemoryKey)
{
    const QString trimmedKey = sharedMemoryKey.trimmed();
    if (trimmedKey.isEmpty()) {
        m_stderr << tr("Web shared-memory key must not be empty.") << Qt::endl;
        return false;
    }

    LocalCliRegistry::Entry entry;
    if (!LocalCliRegistry::entryForSharedMemoryKey(trimmedKey, &entry)) {
        m_stderr << tr("No booking-resources-web Local CLI process was found for key \"%1\".")
                            .arg(trimmedKey)
                 << Qt::endl;
        return false;
    }

    return connectToEntry(entry, true);
}

bool LocalCliClient::connectToEntry(const LocalCliRegistry::Entry &entry, bool registeredEntryFound)
{
    if (isConnected()) {
        disconnectFromProcess();
    } else {
        resetConnection();
    }

    m_socket.connectToServer(entry.serverName);
    if (!m_socket.waitForConnected(kConnectTimeoutMs)) {
        if (entry.isWeb() && !entry.sharedMemoryKey.isEmpty()) {
            m_stderr << tr("Failed to connect to web key \"%1\": %2")
                                .arg(entry.sharedMemoryKey, m_socket.errorString())
                     << Qt::endl;
        } else {
            m_stderr << tr("Failed to connect to PID %1: %2")
                                .arg(entry.pid)
                                .arg(m_socket.errorString())
                     << Qt::endl;
        }
        if (registeredEntryFound)
            LocalCliRegistry::unregisterInstance(entry.pid);
        resetConnection();
        return false;
    }

    m_targetEntry = entry;

    if (!performHandshake()) {
        resetConnection();
        return false;
    }

    return true;
}

void LocalCliClient::disconnectFromProcess()
{
    if (!isConnected()) {
        resetConnection();
        return;
    }

    LocalCliProtocol::Frame frame;
    frame.type = QStringLiteral("bye");
    sendFrame(frame);

    m_socket.flush();
    m_socket.disconnectFromServer();
    if (m_socket.state() != QLocalSocket::UnconnectedState)
        m_socket.waitForDisconnected(kDisconnectTimeoutMs);
    resetConnection();
}

bool LocalCliClient::executeCommand(const QString &command)
{
    if (!isConnected())
        return false;

    if (!sendCommand(command))
        return false;

    bool commandSucceeded = false;
    if (!readCommandResponse(&commandSucceeded))
        return false;
    return commandSucceeded;
}

int LocalCliClient::interactiveShell()
{
    QTextStream stdinStream(stdin);
    configureTextStreamUtf8(stdinStream);
    ConsoleInterruptGuard interruptGuard;
    CliHistory history;
    CliLineEditor editor;
    editor.setOutput(&m_stdout);
    editor.setFallbackInput(&stdinStream);
    editor.setHistory(&history);
    editor.setInterruptCheck([]() { return s_interruptRequested.exchange(false); });
    editor.setCompletionHandler(
            [this](const QString &line, int cursor) { return completeLine(line, cursor); });
    bool exitRequested = false;

    while (!exitRequested) {
        editor.setPrompt(prompt(), promptVisibleWidth());

        QString line;
        const CliLineEditor::Status status = editor.readLine(&line);
        const bool interrupted = status == CliLineEditor::Status::Interrupted;
        const bool eof = status == CliLineEditor::Status::Eof;
        if (interrupted) {
            m_stdout << QStringLiteral("^C") << Qt::endl;
            if (CliConsoleInterruptPolicy::onInteractiveInterrupt(isConnected())
                == CliConsoleInterruptPolicy::Action::Disconnect) {
                disconnectFromProcess();
                m_stdout << tr("Disconnected from the Local CLI process.") << Qt::endl;
                continue;
            }
            break;
        }
        if (eof) {
            m_stdout << Qt::endl;
            break;
        }

        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;

        if (trimmed.startsWith(QLatin1Char('\\'))) {
            if (!handleMetaCommand(trimmed, &exitRequested)) {
                m_stdout << tr("Unknown meta-command. Type \\help to see available shortcuts.")
                         << Qt::endl;
            }
            continue;
        }

        if (!isConnected()) {
            m_stderr << tr("Not connected to a Local CLI process. "
                           "Use \\connect <pid> or \\connect web <key> first "
                           "(see \\help for details).")
                     << Qt::endl;
            continue;
        }

        if (!trimmed.startsWith(QLatin1Char('/'))) {
            m_stderr << tr("Commands for the connected process must start with '/'. "
                           "Type /help or \\help.")
                     << Qt::endl;
            continue;
        }

        if (!sendCommand(line)) {
            m_stderr << tr("Failed to send command to the bot.") << Qt::endl;
            disconnectFromProcess();
            return 1;
        }

        if (!readCommandResponse()) {
            disconnectFromProcess();
            return 1;
        }
    }

    disconnectFromProcess();
    return 0;
}

bool LocalCliClient::sendFrame(const LocalCliProtocol::Frame &frame)
{
    if (!isConnected())
        return false;

    const QByteArray payload = LocalCliProtocol::serialize(frame);
    if (payload.isEmpty())
        return false;

    qint64 totalWritten = 0;
    QElapsedTimer timer;
    timer.start();
    while (totalWritten < payload.size()) {
        const qint64 written =
                m_socket.write(payload.constData() + totalWritten, payload.size() - totalWritten);
        if (written < 0)
            return false;
        if (written == 0) {
            if (m_socket.bytesToWrite() == 0)
                return false;
            const qint64 remaining = kDefaultFrameTimeoutMs - timer.elapsed();
            if (remaining <= 0)
                return false;
            if (!m_socket.waitForBytesWritten(static_cast<int>(remaining))
                && m_socket.bytesToWrite() > 0) {
                return false;
            }
            continue;
        }
        totalWritten += written;
    }

    while (m_socket.bytesToWrite() > 0) {
        const qint64 remaining = kDefaultFrameTimeoutMs - timer.elapsed();
        if (remaining <= 0)
            return false;
        m_socket.flush();
        if (m_socket.bytesToWrite() == 0)
            break;
        if (!m_socket.waitForBytesWritten(static_cast<int>(remaining))
            && m_socket.bytesToWrite() > 0) {
            return false;
        }
    }

    return true;
}

bool LocalCliClient::waitForFrame(LocalCliProtocol::Frame *frame, int timeoutMs, bool *timedOut)
{
    if (!frame)
        return false;

    if (timedOut)
        *timedOut = false;

    QElapsedTimer timer;
    if (timeoutMs >= 0)
        timer.start();

    while (true) {
        if (LocalCliProtocol::takeFrame(&m_buffer, frame))
            return true;

        if (!isConnected())
            return false;

        int waitTime = kDefaultFrameTimeoutMs;
        if (timeoutMs >= 0) {
            const qint64 remaining = timeoutMs - timer.elapsed();
            if (remaining <= 0) {
                if (timedOut)
                    *timedOut = true;
                return false;
            }
            waitTime = static_cast<int>(remaining);
        }

        if (!m_socket.waitForReadyRead(waitTime)) {
            if (timeoutMs >= 0 && timer.elapsed() >= timeoutMs) {
                if (timedOut)
                    *timedOut = true;
                return false;
            }
            if (timeoutMs < 0 && m_socket.state() == QLocalSocket::ConnectedState)
                continue;
            return false;
        }

        const QByteArray chunk = m_socket.readAll();
        if (chunk.isEmpty())
            continue;

        const qsizetype newSize = m_buffer.size() + chunk.size();
        if (newSize > kMaxBufferedBytes) {
            m_stderr << tr("Incoming data exceeds the allowed buffer size.") << Qt::endl;
            m_buffer.clear();
            m_socket.abort();
            return false;
        }

        m_buffer.append(chunk);
    }
}

bool LocalCliClient::performHandshake()
{
    LocalCliProtocol::Frame frame;
    frame.type = QStringLiteral("hello");
    QJsonObject payload;
    payload.insert(QStringLiteral("pid"), static_cast<double>(QCoreApplication::applicationPid()));
    payload.insert(QStringLiteral("label"), LocalCliHelper::localHostLabel());
    frame.payload = payload;

    if (!sendFrame(frame)) {
        m_stderr << tr("Failed to send handshake to the server: %1").arg(m_socket.errorString())
                 << Qt::endl;
        return false;
    }

    LocalCliProtocol::Frame response;
    bool timedOut = false;
    if (!waitForFrame(&response, kConnectTimeoutMs, &timedOut)) {
        if (timedOut) {
            m_stderr << tr("Timed out while waiting for server handshake.") << Qt::endl;
        } else {
            m_stderr << tr("Failed to read server handshake: %1").arg(m_socket.errorString())
                     << Qt::endl;
        }
        return false;
    }

    if (response.type != QLatin1String("hello-ack")) {
        m_stderr << tr("Unexpected handshake response from the server.") << Qt::endl;
        return false;
    }

    m_supportsComplete = false;
    const QJsonArray features = response.payload.value(QStringLiteral("features")).toArray();
    for (const QJsonValue &feature : features) {
        if (feature.toString() == QLatin1String("complete")) {
            m_supportsComplete = true;
            break;
        }
    }

    const QString message = response.payload.value(QStringLiteral("message")).toString();
    if (!message.isEmpty())
        m_stdout << message << Qt::endl;

    consumePendingServerMessages(kHandshakeInfoWaitMs);
    return true;
}

bool LocalCliClient::sendCommand(const QString &command)
{
    LocalCliProtocol::Frame frame;
    frame.type = QStringLiteral("command");
    QJsonObject payload;
    payload.insert(QStringLiteral("text"), command);
    frame.payload = payload;
    return sendFrame(frame);
}

void LocalCliClient::printServerFrame(const LocalCliProtocol::Frame &frame)
{
    const QString type = frame.type.trimmed().toLower();
    const QString text = frame.payload.value(QStringLiteral("text")).toString();
    if (type == QLatin1String("response")) {
        if (!text.isEmpty())
            m_stdout << text << Qt::endl;
        return;
    }
    if (type == QLatin1String("info")) {
        if (!text.isEmpty())
            m_stdout << tr("[info] %1").arg(text) << Qt::endl;
        return;
    }
    if (type == QLatin1String("error"))
        m_stderr << text << Qt::endl;
}

void LocalCliClient::consumePendingServerMessages(int waitMs)
{
    QElapsedTimer timer;
    if (waitMs > 0)
        timer.start();

    while (isConnected()) {
        LocalCliProtocol::Frame frame;
        if (LocalCliProtocol::takeFrame(&m_buffer, &frame)) {
            printServerFrame(frame);
            continue;
        }

        if (waitMs <= 0)
            break;
        const qint64 remaining = waitMs - timer.elapsed();
        if (remaining <= 0)
            break;
        if (!m_socket.waitForReadyRead(static_cast<int>(remaining)))
            break;

        const QByteArray chunk = m_socket.readAll();
        if (chunk.isEmpty())
            continue;

        const qsizetype newSize = m_buffer.size() + chunk.size();
        if (newSize > kMaxBufferedBytes) {
            m_stderr << tr("Incoming data exceeds the allowed buffer size.") << Qt::endl;
            m_buffer.clear();
            m_socket.abort();
            return;
        }
        m_buffer.append(chunk);
    }
}

bool LocalCliClient::readCommandResponse(bool *commandSucceeded)
{
    if (commandSucceeded)
        *commandSucceeded = false;

    while (true) {
        LocalCliProtocol::Frame frame;
        bool timedOut = false;
        if (!waitForFrame(&frame, kCommandResponseTimeoutMs, &timedOut)) {
            if (timedOut) {
                m_stderr << tr("Timed out while waiting for the server response.") << Qt::endl;
            } else {
                m_stderr << tr("Failed to read the server response.") << Qt::endl;
            }
            return false;
        }

        const QString type = frame.type.trimmed().toLower();
        if (type == QLatin1String("response") || type == QLatin1String("info")
            || type == QLatin1String("error")) {
            printServerFrame(frame);
            continue;
        }
        if (type == QLatin1String("result")) {
            const QString status = frame.payload.value(QStringLiteral("status")).toString();
            if (commandSucceeded)
                *commandSucceeded = status.toLower() != QLatin1String("error");
            return true;
        }

        m_stderr << tr("Received unexpected frame type: %1").arg(type) << Qt::endl;
    }
}

bool LocalCliClient::handleMetaCommand(const QString &line, bool *exitRequested)
{
    if (!exitRequested)
        return false;

    QString payload = line;
    if (payload.startsWith(QLatin1Char('\\')))
        payload.remove(0, 1);
    payload = payload.trimmed();
    if (payload.isEmpty())
        return false;

    const QStringList tokens =
            payload.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (tokens.isEmpty())
        return false;

    const QString token = tokens.first().toLower();
    const QStringList args = tokens.mid(1);

    if (token == QStringLiteral("q") || token == QStringLiteral("quit")
        || token == QStringLiteral("exit")) {
        *exitRequested = true;
        return true;
    }

    if (token == QStringLiteral("help")) {
        printHelp();
        return true;
    }

    if (token == QStringLiteral("list")) {
        const bool ok = listInstances();
        Q_UNUSED(ok);
        return true;
    }

    if (token == QStringLiteral("connect")) {
        if (args.isEmpty()) {
            m_stderr << tr("Usage: \\connect <pid> or \\connect web <key>") << Qt::endl;
            return true;
        }
        if (args.first().compare(QLatin1String("web"), Qt::CaseInsensitive) == 0) {
            if (args.size() < 2) {
                m_stderr << tr("Usage: \\connect web <key>") << Qt::endl;
                return true;
            }
            const QString key = args.mid(1).join(QLatin1Char(' ')).trimmed();
            if (connectToWebKey(key)) {
                m_stdout << tr("Connected to web key \"%1\".").arg(key) << Qt::endl;
            }
            return true;
        }
        bool ok = false;
        const qint64 pid = LocalCliHelper::parsePid(args.first(), &ok);
        if (!ok || pid <= 0) {
            m_stderr << tr("Invalid PID value.") << Qt::endl;
            return true;
        }
        if (connectToProcess(pid)) {
            m_stdout << tr("Connected to PID %1.").arg(pid) << Qt::endl;
        }
        return true;
    }

    if (token == QStringLiteral("disconnect")) {
        if (isConnected()) {
            disconnectFromProcess();
            m_stdout << tr("Disconnected from the Local CLI process.") << Qt::endl;
        } else {
            m_stdout << tr("Not connected.") << Qt::endl;
        }
        return true;
    }

    return false;
}

void LocalCliClient::printHelp() const
{
    m_stdout << tr("Meta commands:\n"
                   "  \\help                 Show Local CLI meta-command help\n"
                   "  \\connect <pid>        Connect to a Local CLI process by PID\n"
                   "  \\connect web <key>    Connect to booking-resources-web by shared-memory "
                   "key\n"
                   "  \\disconnect           Disconnect from the current process\n"
                   "  \\list                 List running Local CLI processes\n"
                   "  \\quit                 Disconnect and exit\n"
                   "Interactive input (TTY only):\n"
                   "  Up/Down               Previous and next commands\n"
                   "  Tab                   Complete meta-commands, slash-commands, and arguments\n"
                   "Command history is kept in memory for this process only.\n"
                   "Commands beginning with '/' are forwarded to the connected process.\n"
                   "Server connections accept admin slash-commands. Web connections accept "
                   "web user-management commands.\n"
                   "Use /help on the connected process or \\help for Local CLI meta commands.\n")
             << Qt::endl;
}

QString LocalCliClient::prompt() const
{
    QString basePrompt = tr("booking-resources-cli> ");
    if (m_targetEntry.isWeb() && !m_targetEntry.sharedMemoryKey.trimmed().isEmpty()) {
        basePrompt = tr("booking-resources-cli[web:%1]> ").arg(m_targetEntry.sharedMemoryKey);
    } else if (m_targetEntry.pid > 0) {
        basePrompt = tr("booking-resources-cli[%1]> ").arg(m_targetEntry.pid);
    }

    if (!m_promptColorEnabled)
        return basePrompt;

    static const QString colorPrefix = QStringLiteral("\x1b[1;36m");
    static const QString colorReset = QStringLiteral("\x1b[0m");
    return colorPrefix + basePrompt + colorReset;
}

int LocalCliClient::promptVisibleWidth() const
{
    QString basePrompt = tr("booking-resources-cli> ");
    if (m_targetEntry.isWeb() && !m_targetEntry.sharedMemoryKey.trimmed().isEmpty()) {
        basePrompt = tr("booking-resources-cli[web:%1]> ").arg(m_targetEntry.sharedMemoryKey);
    } else if (m_targetEntry.pid > 0) {
        basePrompt = tr("booking-resources-cli[%1]> ").arg(m_targetEntry.pid);
    }
    return basePrompt.size();
}

CliCompletion::Result LocalCliClient::completeLine(const QString &line, int cursor)
{
    const CliCompletion::Result local = CliCompletion::meta(line, cursor, LocalCliRegistry::entries());
    const bool metaLine = CliCompletion::firstTokenIsMeta(line, cursor);
    const bool slashLine = CliCompletion::firstTokenIsSlash(line, cursor);
    const bool emptyLine = line.trimmed().isEmpty();

    if (metaLine || !isConnected() || !m_supportsComplete)
        return local;

    CliCompletion::Result host;
    if ((slashLine || emptyLine) && requestHostCompletion(line, cursor, &host)) {
        if (emptyLine)
            return CliCompletion::merge(local, host);
        return host;
    }
    return local;
}

bool LocalCliClient::requestHostCompletion(const QString &line,
                                           int cursor,
                                           CliCompletion::Result *result)
{
    if (!result)
        return false;

    LocalCliProtocol::Frame request;
    request.type = QStringLiteral("complete");
    QJsonObject payload;
    payload.insert(QStringLiteral("text"), line);
    payload.insert(QStringLiteral("cursor"), cursor);
    request.payload = payload;
    if (!sendFrame(request))
        return false;

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < kCompleteTimeoutMs) {
        LocalCliProtocol::Frame frame;
        bool timedOut = false;
        const int remaining = qMax(1, kCompleteTimeoutMs - static_cast<int>(timer.elapsed()));
        if (!waitForFrame(&frame, remaining, &timedOut)) {
            return false;
        }

        const QString type = frame.type.trimmed().toLower();
        if (type == QLatin1String("complete-result")) {
            result->replaceFrom = frame.payload.value(QStringLiteral("replace_from")).toInt(0);
            result->replaceTo = frame.payload.value(QStringLiteral("replace_to")).toInt(0);
            result->appendSpace = frame.payload.value(QStringLiteral("append_space")).toBool(true);
            const QJsonArray values = frame.payload.value(QStringLiteral("values")).toArray();
            QStringList list;
            for (const QJsonValue &value : values)
                list.append(value.toString());
            result->values = list;
            return true;
        }
        if (type == QLatin1String("error"))
            return false;
        if (type == QLatin1String("info") || type == QLatin1String("response")) {
            printServerFrame(frame);
            continue;
        }
    }
    return false;
}

QString LocalCliClient::formatEntryLine(const LocalCliRegistry::Entry &entry,
                                        bool reachable,
                                        int endpointWidth,
                                        int keyWidth)
{
    const QString status = reachable ? tr("online") : tr("stale");
    QString started = entry.startedAtUtc.isValid() ? entry.startedAtUtc.toLocalTime().toString(
                                                             QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                                                   : tr("unknown");

    return formatListColumns(QString::number(entry.pid),
                             LocalCliRegistry::kindKey(entry.kind),
                             status,
                             entry.serverName,
                             entryDisplayKey(entry),
                             started,
                             endpointWidth,
                             keyWidth,
                             true);
}

bool LocalCliClient::isConnected() const
{
    return m_socket.state() == QLocalSocket::ConnectedState;
}

void LocalCliClient::cleanupStaleEntries() const
{
    const QVector<LocalCliRegistry::Entry> entries = LocalCliRegistry::entries();
    if (entries.isEmpty())
        return;

    for (const LocalCliRegistry::Entry &entry : entries) {
        if (!probeEndpoint(entry.serverName, kListProbeTimeoutMs))
            LocalCliRegistry::unregisterInstance(entry.pid);
    }
}

void LocalCliClient::resetConnection()
{
    if (m_socket.state() != QLocalSocket::UnconnectedState)
        m_socket.abort();
    m_socket.close();
    m_buffer.clear();
    m_targetEntry = LocalCliRegistry::Entry();
    m_supportsComplete = false;
}
