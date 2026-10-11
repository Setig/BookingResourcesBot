#include "BookingResourcesApplication.h"

#include "ConsoleInterruptPolicy.h"

#include <QDebug>
#include <QEvent>
#include <QLoggingCategory>
#include <QMetaObject>
#ifdef Q_OS_UNIX
#  include <QSocketNotifier>
#endif

#include <atomic>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <string.h>
#endif
#ifdef Q_OS_UNIX
#  include <cerrno>
#  include <fcntl.h>
#  include <signal.h>
#  include <unistd.h>
#endif

class BookingResourcesApplicationPrivate
{
public:
    BookingResourcesApplicationPrivate(const QString &applicationDisplayName,
                                       const QLoggingCategory &loggingCategory);

    QString applicationDisplayName;
    const QLoggingCategory *loggingCategory;
    bool startupLogged;
    bool shutdownPerformed;
    bool shutdownStartedLogged;
    bool shutdownFinishedLogged;
};

BookingResourcesApplicationPrivate::BookingResourcesApplicationPrivate(
        const QString &applicationDisplayName, const QLoggingCategory &loggingCategory)
    : applicationDisplayName(applicationDisplayName)
    , loggingCategory(&loggingCategory)
    , startupLogged(false)
    , shutdownPerformed(false)
    , shutdownStartedLogged(false)
    , shutdownFinishedLogged(false)
{
    // do nothing
}

#if defined(Q_OS_UNIX) && QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
class UnixSignalDrain : public QObject
{
    Q_OBJECT

public:
    explicit UnixSignalDrain(QObject *parent);

public slots:
    void drain();
};
#endif

namespace {

QString titleCaseLabel(const QString &label)
{
    QString result = label;
    if (!result.isEmpty())
        result[0] = result.at(0).toUpper();
    return result;
}

void logInfo(const QLoggingCategory *category, const char *function, const QString &message)
{
    if (!category || !category->isInfoEnabled())
        return;

    QMessageLogger(__FILE__, __LINE__, function).info(*category).noquote() << message;
}

std::atomic<int> s_localCliSessionCount{ 0 };
std::atomic<bool> s_forceQuit{ false };

bool shouldSuppressConsoleInterrupt(ConsoleInterruptPolicy::Signal signal);
void requestQuit(ConsoleInterruptPolicy::Signal signal);

#ifdef Q_OS_WIN
HWND s_terminationWindow = nullptr;

bool fileNameEquals(const wchar_t *path, const wchar_t *expected)
{
    if (!path || !expected)
        return false;

    const wchar_t *name = path;
    for (const wchar_t *cursor = path; *cursor; ++cursor) {
        if (*cursor == L'\\' || *cursor == L'/')
            name = cursor + 1;
    }
    return _wcsicmp(name, expected) == 0;
}

bool consoleHasLocalCliProcess()
{
    DWORD pids[64] = {};
    const DWORD count = GetConsoleProcessList(pids, 64);
    if (count <= 1)
        return false;

    const DWORD self = GetCurrentProcessId();
    for (DWORD i = 0; i < count && i < 64; ++i) {
        if (pids[i] == 0 || pids[i] == self)
            continue;

        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pids[i]);
        if (!process)
            continue;

        wchar_t path[MAX_PATH] = {};
        DWORD size = MAX_PATH;
        const BOOL ok = QueryFullProcessImageNameW(process, 0, path, &size);
        CloseHandle(process);
        if (!ok)
            continue;

        if (fileNameEquals(path, L"booking-resources-cli.exe")
            || fileNameEquals(path, L"booking-resources-cli")) {
            return true;
        }
    }
    return false;
}

LRESULT CALLBACK terminationWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Q_UNUSED(hwnd);
    Q_UNUSED(lParam);

    switch (msg) {
    case WM_CLOSE:
        requestQuit(ConsoleInterruptPolicy::Signal::WindowClose);
        return 0;
    case WM_QUERYENDSESSION:
        requestQuit(ConsoleInterruptPolicy::Signal::Logoff);
        return TRUE;
    case WM_ENDSESSION:
        if (wParam)
            requestQuit(ConsoleInterruptPolicy::Signal::Logoff);
        return 0;
    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

BOOL WINAPI consoleCtrlHandler(DWORD ctrlType)
{
    switch (ctrlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT: {
        const ConsoleInterruptPolicy::Signal signal =
                (ctrlType == CTRL_C_EVENT) ? ConsoleInterruptPolicy::Signal::CtrlC
                                           : ConsoleInterruptPolicy::Signal::CtrlBreak;
        // Qt may also install a handler that posts QEvent::Quit. Ignore Ctrl+C
        // while a Local CLI client is connected, or when the CLI shares this console.
        requestQuit(signal);
        return TRUE;
    }
    case CTRL_CLOSE_EVENT:
        requestQuit(ConsoleInterruptPolicy::Signal::ConsoleClose);
        return TRUE;
    case CTRL_LOGOFF_EVENT:
        requestQuit(ConsoleInterruptPolicy::Signal::Logoff);
        return TRUE;
    case CTRL_SHUTDOWN_EVENT:
        requestQuit(ConsoleInterruptPolicy::Signal::Shutdown);
        return TRUE;
    default:
        return FALSE;
    }
}
#endif

bool localCliSharesConsole()
{
#ifdef Q_OS_WIN
    return consoleHasLocalCliProcess();
#else
    return false;
#endif
}

bool shouldSuppressConsoleInterrupt(ConsoleInterruptPolicy::Signal signal)
{
    if (!ConsoleInterruptPolicy::shouldQuit(signal, s_localCliSessionCount.load()))
        return true;

    switch (signal) {
    case ConsoleInterruptPolicy::Signal::CtrlC:
    case ConsoleInterruptPolicy::Signal::CtrlBreak:
    case ConsoleInterruptPolicy::Signal::QuitEvent:
        return localCliSharesConsole();
    case ConsoleInterruptPolicy::Signal::ConsoleClose:
    case ConsoleInterruptPolicy::Signal::Logoff:
    case ConsoleInterruptPolicy::Signal::Shutdown:
    case ConsoleInterruptPolicy::Signal::WindowClose:
        break;
    }

    return false;
}

void requestQuit(ConsoleInterruptPolicy::Signal signal)
{
    if (shouldSuppressConsoleInterrupt(signal))
        return;

    switch (signal) {
    case ConsoleInterruptPolicy::Signal::ConsoleClose:
    case ConsoleInterruptPolicy::Signal::Logoff:
    case ConsoleInterruptPolicy::Signal::Shutdown:
        s_forceQuit.store(true);
        break;
    case ConsoleInterruptPolicy::Signal::CtrlC:
    case ConsoleInterruptPolicy::Signal::CtrlBreak:
    case ConsoleInterruptPolicy::Signal::QuitEvent:
    case ConsoleInterruptPolicy::Signal::WindowClose:
        break;
    }

    QCoreApplication::quit();
}

#ifdef Q_OS_UNIX
void drainUnixSignals();

int s_signalPipe[2] = { -1, -1 };
QSocketNotifier *s_signalNotifier = nullptr;

void unixSignalHandler(int signo)
{
    const unsigned char byte = static_cast<unsigned char>(signo);
    if (s_signalPipe[1] >= 0)
        (void)::write(s_signalPipe[1], &byte, sizeof(byte));
}

ConsoleInterruptPolicy::Signal policySignalFromUnixSigno(int signo)
{
    switch (signo) {
    case SIGINT:
        return ConsoleInterruptPolicy::Signal::CtrlC;
    case SIGTERM:
        return ConsoleInterruptPolicy::Signal::Shutdown;
    case SIGHUP:
        return ConsoleInterruptPolicy::Signal::ConsoleClose;
    default:
        return ConsoleInterruptPolicy::Signal::QuitEvent;
    }
}

void drainUnixSignals()
{
    if (!s_signalNotifier || s_signalPipe[0] < 0)
        return;

    s_signalNotifier->setEnabled(false);

    unsigned char signo = 0;
    for (;;) {
        const ssize_t n = ::read(s_signalPipe[0], &signo, sizeof(signo));
        if (n > 0) {
            requestQuit(policySignalFromUnixSigno(static_cast<int>(signo)));
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        break;
    }

    s_signalNotifier->setEnabled(true);
}

bool setCloexecNonblock(int fd)
{
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return false;

    const int fdFlags = ::fcntl(fd, F_GETFD, 0);
    if (fdFlags < 0 || ::fcntl(fd, F_SETFD, fdFlags | FD_CLOEXEC) < 0)
        return false;

    return true;
}

void closeSignalPipe()
{
    if (s_signalPipe[0] >= 0)
        (void)::close(s_signalPipe[0]);
    if (s_signalPipe[1] >= 0)
        (void)::close(s_signalPipe[1]);
    s_signalPipe[0] = -1;
    s_signalPipe[1] = -1;
}

void installUnixSignalWatch(QObject *parent)
{
    if (s_signalNotifier || !parent)
        return;

    if (::pipe(s_signalPipe) != 0) {
        s_signalPipe[0] = -1;
        s_signalPipe[1] = -1;
        return;
    }

    if (!setCloexecNonblock(s_signalPipe[0]) || !setCloexecNonblock(s_signalPipe[1])) {
        closeSignalPipe();
        return;
    }

    s_signalNotifier = new QSocketNotifier(s_signalPipe[0], QSocketNotifier::Read, parent);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QObject::connect(s_signalNotifier,
                     &QSocketNotifier::activated,
                     parent,
                     [] { drainUnixSignals(); });
#else
    // Qt 5 keeps QPrivateSignal private, so a pointer-to-member cast is not portable.
    // The meta-object still exposes activated(int) for string-based connections.
    auto *drain = new UnixSignalDrain(parent);
    QObject::connect(s_signalNotifier, SIGNAL(activated(int)), drain, SLOT(drain()));
#endif

    struct sigaction action = {};
    action.sa_handler = unixSignalHandler;
    sigemptyset(&action.sa_mask);
    sigaddset(&action.sa_mask, SIGINT);
    sigaddset(&action.sa_mask, SIGTERM);
    sigaddset(&action.sa_mask, SIGHUP);
    action.sa_flags = 0;
    (void)::sigaction(SIGINT, &action, nullptr);
    (void)::sigaction(SIGTERM, &action, nullptr);
    (void)::sigaction(SIGHUP, &action, nullptr);
}
#endif

void reinstallConsoleCtrlHandler()
{
#ifdef Q_OS_WIN
    (void)SetConsoleCtrlHandler(consoleCtrlHandler, FALSE);
    (void)SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);
#endif
}

void installTerminationHandlers(QObject *parent)
{
#ifdef Q_OS_WIN
    Q_UNUSED(parent);
    static bool installed = false;
    if (installed)
        return;

    installed = true;

    // Register last so this handler runs before Qt's own Ctrl+C handler.
    reinstallConsoleCtrlHandler();

    // Qt Creator and QProcess::terminate() send WM_CLOSE to top-level windows on Windows.
    // Console applications do not have one, so create a hidden window for graceful quits.
    if (!s_terminationWindow) {
        const wchar_t kClassName[] = L"BookingResourcesApplicationTerminationWindow";
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = terminationWindowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClassName;

        const ATOM cls = RegisterClassExW(&wc);
        const DWORD err = (cls == 0) ? GetLastError() : ERROR_SUCCESS;
        if (cls != 0 || err == ERROR_CLASS_ALREADY_EXISTS) {
            s_terminationWindow = CreateWindowExW(0,
                                                  kClassName,
                                                  L"BookingResourcesApplicationTerminationWindow",
                                                  WS_OVERLAPPED,
                                                  0,
                                                  0,
                                                  0,
                                                  0,
                                                  nullptr,
                                                  nullptr,
                                                  wc.hInstance,
                                                  nullptr);
        }
    }
#endif
#ifdef Q_OS_UNIX
    installUnixSignalWatch(parent);
#endif
}

} // namespace

#if defined(Q_OS_UNIX) && QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
UnixSignalDrain::UnixSignalDrain(QObject *parent)
    : QObject(parent)
{
    // do nothing
}

void UnixSignalDrain::drain()
{
    drainUnixSignals();
}
#endif

BookingResourcesApplication::BookingResourcesApplication(int &argc,
                                                         char **argv,
                                                         const QString &applicationName,
                                                         const QString &applicationVersion,
                                                         const QString &applicationDisplayName,
                                                         const QLoggingCategory &loggingCategory)
    : QCoreApplication(argc, argv)
    , d(new BookingResourcesApplicationPrivate(applicationDisplayName, loggingCategory))
{
    QCoreApplication::setApplicationName(applicationName);
    QCoreApplication::setApplicationVersion(applicationVersion);

    QObject::connect(this,
                     &QCoreApplication::aboutToQuit,
                     this,
                     &BookingResourcesApplication::shutdown);

    installTerminationHandlers(this);

    // Qt may install its Ctrl+C handler when exec() starts the dispatcher.
    // Re-arm after the first queued event so our handler stays first.
    QMetaObject::invokeMethod(this, []() { reinstallConsoleCtrlHandler(); }, Qt::QueuedConnection);
}

BookingResourcesApplication::~BookingResourcesApplication()
{
    delete d;
}

void BookingResourcesApplication::logStartup()
{
    if (d->startupLogged)
        return;

    d->startupLogged = true;

    const QString message =
            QStringLiteral("Starting %1 application (process_id=%2, name=%3, version=%4).")
                    .arg(d->applicationDisplayName,
                         QString::number(QCoreApplication::applicationPid()),
                         QCoreApplication::applicationName(),
                         QCoreApplication::applicationVersion());
    logInfo(d->loggingCategory, Q_FUNC_INFO, message);

    // Qt may install its Ctrl+C handler when the dispatcher starts; keep ours last.
    reinstallConsoleCtrlHandler();
    QMetaObject::invokeMethod(this, []() { reinstallConsoleCtrlHandler(); }, Qt::QueuedConnection);
}

void BookingResourcesApplication::shutdown()
{
    if (d->shutdownPerformed)
        return;

    d->shutdownPerformed = true;
    performShutdown();
}

bool BookingResourcesApplication::event(QEvent *e)
{
    if (e && e->type() == QEvent::Quit) {
        // Qt's own console handler may post Quit even when we ignore CTRL_C_EVENT.
        if (!shouldAcceptQuitEvent()) {
            reinstallConsoleCtrlHandler();
            return true;
        }

        shutdown();
    }

    return QCoreApplication::event(e);
}

void BookingResourcesApplication::retainConsoleInterruptSuppression()
{
    s_localCliSessionCount.fetch_add(1);
    reinstallConsoleCtrlHandler();
}

void BookingResourcesApplication::releaseConsoleInterruptSuppression()
{
    const int previous = s_localCliSessionCount.fetch_sub(1);
    if (previous <= 0)
        s_localCliSessionCount.store(0);
}

void BookingResourcesApplication::resetConsoleInterruptSuppression()
{
    s_localCliSessionCount.store(0);
    s_forceQuit.store(false);
}

int BookingResourcesApplication::consoleInterruptSuppressionCount()
{
    return s_localCliSessionCount.load();
}

bool BookingResourcesApplication::shouldAcceptQuitEvent()
{
    if (s_forceQuit.load())
        return true;

    return !shouldSuppressConsoleInterrupt(ConsoleInterruptPolicy::Signal::QuitEvent);
}

bool BookingResourcesApplication::shutdownPerformed() const
{
    return d->shutdownPerformed;
}

void BookingResourcesApplication::performShutdown()
{
    // do nothing
}

void BookingResourcesApplication::logShutdownStarted()
{
    if (!d->startupLogged || d->shutdownStartedLogged)
        return;

    d->shutdownStartedLogged = true;

    const QString message =
            QStringLiteral("%1 shutdown started...").arg(titleCaseLabel(d->applicationDisplayName));
    logInfo(d->loggingCategory, Q_FUNC_INFO, message);
}

void BookingResourcesApplication::logShutdownFinished(qint64 elapsedMs)
{
    if (!d->startupLogged || d->shutdownFinishedLogged)
        return;

    d->shutdownFinishedLogged = true;

    QString message = QStringLiteral("%1 shutdown finished (process_id=%2")
                              .arg(titleCaseLabel(d->applicationDisplayName),
                                   QString::number(QCoreApplication::applicationPid()));
    if (elapsedMs >= 0) {
        message += QStringLiteral(", shutdown_duration=\"%1ms\"").arg(elapsedMs);
    }
    message += QStringLiteral(").");
    logInfo(d->loggingCategory, Q_FUNC_INFO, message);
}

bool BookingResourcesApplication::startupLogged() const
{
    return d->startupLogged;
}

bool BookingResourcesApplication::shutdownStartedLogged() const
{
    return d->shutdownStartedLogged;
}

bool BookingResourcesApplication::shutdownFinishedLogged() const
{
    return d->shutdownFinishedLogged;
}

#if defined(Q_OS_UNIX) && QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#  include "BookingResourcesApplication.moc"
#endif
