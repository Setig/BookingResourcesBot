#include <QtTest>
#include "TestRegistry.h"

#include "ConsoleInterruptPolicy.h"

class ConsoleInterruptPolicyTests : public QObject
{
    Q_OBJECT

private slots:
    void ctrlCDoesNotQuitWhileLocalCliSessionIsActive();
    void ctrlCQuitsWhenNoLocalCliSession();
    void quitEventFollowsSessionCount();
    void windowCloseFollowsSessionCount();
    void consoleCloseAlwaysQuits();
    void logoffAndShutdownAlwaysQuit();
};

void ConsoleInterruptPolicyTests::ctrlCDoesNotQuitWhileLocalCliSessionIsActive()
{
    QVERIFY(!ConsoleInterruptPolicy::shouldQuit(ConsoleInterruptPolicy::Signal::CtrlC, 1));
    QVERIFY(!ConsoleInterruptPolicy::shouldQuit(ConsoleInterruptPolicy::Signal::CtrlBreak, 2));
}

void ConsoleInterruptPolicyTests::ctrlCQuitsWhenNoLocalCliSession()
{
    QVERIFY(ConsoleInterruptPolicy::shouldQuit(ConsoleInterruptPolicy::Signal::CtrlC, 0));
    QVERIFY(ConsoleInterruptPolicy::shouldQuit(ConsoleInterruptPolicy::Signal::CtrlBreak, 0));
}

void ConsoleInterruptPolicyTests::quitEventFollowsSessionCount()
{
    QVERIFY(!ConsoleInterruptPolicy::shouldQuit(ConsoleInterruptPolicy::Signal::QuitEvent, 1));
    QVERIFY(ConsoleInterruptPolicy::shouldQuit(ConsoleInterruptPolicy::Signal::QuitEvent, 0));
}

void ConsoleInterruptPolicyTests::windowCloseFollowsSessionCount()
{
    QVERIFY(!ConsoleInterruptPolicy::shouldQuit(ConsoleInterruptPolicy::Signal::WindowClose, 1));
    QVERIFY(ConsoleInterruptPolicy::shouldQuit(ConsoleInterruptPolicy::Signal::WindowClose, 0));
}

void ConsoleInterruptPolicyTests::consoleCloseAlwaysQuits()
{
    QVERIFY(ConsoleInterruptPolicy::shouldQuit(ConsoleInterruptPolicy::Signal::ConsoleClose, 0));
    QVERIFY(ConsoleInterruptPolicy::shouldQuit(ConsoleInterruptPolicy::Signal::ConsoleClose, 3));
}

void ConsoleInterruptPolicyTests::logoffAndShutdownAlwaysQuit()
{
    QVERIFY(ConsoleInterruptPolicy::shouldQuit(ConsoleInterruptPolicy::Signal::Logoff, 1));
    QVERIFY(ConsoleInterruptPolicy::shouldQuit(ConsoleInterruptPolicy::Signal::Shutdown, 1));
}

BOOKING_RESOURCES_REGISTER_TEST(ConsoleInterruptPolicyTests)

#include "ConsoleInterruptPolicyTests.moc"
