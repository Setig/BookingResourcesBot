#ifndef CONSOLEINTERRUPTPOLICY_H
#define CONSOLEINTERRUPTPOLICY_H

/**
 * @brief Decides whether a console/OS interrupt should quit a long-running process.
 *
 * Ctrl+C is delivered to every process attached to the same Windows console, and Qt
 * may later post QEvent::Quit from its own handler. While a Local CLI client is
 * connected, those interrupts must not stop booking-resources-server/web. The
 * application layer also ignores Ctrl+C/Quit when booking-resources-cli shares
 * the console, because the CLI disconnects on the same keypress. On Unix, SIGINT
 * is treated as Ctrl+C, SIGTERM as Shutdown, and SIGHUP as ConsoleClose.
 */
namespace ConsoleInterruptPolicy {

enum class Signal {
    CtrlC,
    CtrlBreak,
    ConsoleClose,
    Logoff,
    Shutdown,
    QuitEvent,
    WindowClose,
};

[[nodiscard]] bool shouldQuit(Signal signal, int localCliSessionCount);

} // namespace ConsoleInterruptPolicy

#endif // CONSOLEINTERRUPTPOLICY_H
