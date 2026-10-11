#ifndef CLICONSOLEINTERRUPTPOLICY_H
#define CLICONSOLEINTERRUPTPOLICY_H

/**
 * @brief Decides how the Local CLI interactive shell reacts to Ctrl+C.
 *
 * The connected server/web process must keep running. The CLI only disconnects
 * or, when already idle, exits itself.
 */
namespace CliConsoleInterruptPolicy {

enum class Action {
    Disconnect,
    Exit,
};

[[nodiscard]] Action onInteractiveInterrupt(bool connected);

} // namespace CliConsoleInterruptPolicy

#endif // CLICONSOLEINTERRUPTPOLICY_H
