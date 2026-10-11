#include "ConsoleInterruptPolicy.h"

namespace ConsoleInterruptPolicy {

bool shouldQuit(Signal signal, int localCliSessionCount)
{
    switch (signal) {
    case Signal::CtrlC:
    case Signal::CtrlBreak:
    case Signal::QuitEvent:
    case Signal::WindowClose:
        return localCliSessionCount <= 0;
    case Signal::ConsoleClose:
    case Signal::Logoff:
    case Signal::Shutdown:
        return true;
    }

    return true;
}

} // namespace ConsoleInterruptPolicy
