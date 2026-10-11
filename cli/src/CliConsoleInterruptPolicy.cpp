#include "CliConsoleInterruptPolicy.h"

namespace CliConsoleInterruptPolicy {

Action onInteractiveInterrupt(bool connected)
{
    return connected ? Action::Disconnect : Action::Exit;
}

} // namespace CliConsoleInterruptPolicy
