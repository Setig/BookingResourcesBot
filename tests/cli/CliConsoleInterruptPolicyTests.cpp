#include <QtTest>
#include "TestRegistry.h"

#include "CliConsoleInterruptPolicy.h"

class CliConsoleInterruptPolicyTests : public QObject
{
    Q_OBJECT

private slots:
    void ctrlCDisconnectsWhenConnected();
    void ctrlCExitsWhenIdle();
};

void CliConsoleInterruptPolicyTests::ctrlCDisconnectsWhenConnected()
{
    QCOMPARE(CliConsoleInterruptPolicy::onInteractiveInterrupt(true),
             CliConsoleInterruptPolicy::Action::Disconnect);
}

void CliConsoleInterruptPolicyTests::ctrlCExitsWhenIdle()
{
    QCOMPARE(CliConsoleInterruptPolicy::onInteractiveInterrupt(false),
             CliConsoleInterruptPolicy::Action::Exit);
}

BOOKING_RESOURCES_REGISTER_TEST(CliConsoleInterruptPolicyTests)

#include "CliConsoleInterruptPolicyTests.moc"
