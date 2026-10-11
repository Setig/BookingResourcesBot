#include <QtTest>
#include "TestRegistry.h"

#include "MessengerLimiter.h"

class ServerAntiSpamTests : public QObject
{
    Q_OBJECT

private slots:
    void warnsBeforeBlocking();
    void resetsCounterAfterBlocking();
    void ignoresEmptyKeys();
};

void ServerAntiSpamTests::warnsBeforeBlocking()
{
    MessengerLimiter limiter;
    limiter.setLimit(3);
    limiter.setWarningThreshold(2);
    const MessengerUserKey userKey(MessengerType::Telegram, QStringLiteral("42"));

    const MessengerLimiter::AttemptResult first = limiter.registerAttempt(userKey);
    QVERIFY(first.valid);
    QCOMPARE(first.attempts, 1);
    QVERIFY(!first.showWarning);
    QVERIFY(!first.reachedLimit);
    QCOMPARE(first.remainingBeforeBlock, 2);

    const MessengerLimiter::AttemptResult second = limiter.registerAttempt(userKey);
    QVERIFY(second.valid);
    QCOMPARE(second.attempts, 2);
    QVERIFY(second.showWarning);
    QVERIFY(!second.reachedLimit);
    QCOMPARE(second.remainingBeforeBlock, 1);

    const MessengerLimiter::AttemptResult third = limiter.registerAttempt(userKey);
    QVERIFY(third.valid);
    QVERIFY(third.reachedLimit);
    QCOMPARE(third.attempts, 3);
    QCOMPARE(third.remainingBeforeBlock, 0);
}

void ServerAntiSpamTests::resetsCounterAfterBlocking()
{
    MessengerLimiter limiter;
    limiter.setLimit(2);
    limiter.setWarningThreshold(1);
    const MessengerUserKey userKey(MessengerType::Telegram, QStringLiteral("77"));

    limiter.registerAttempt(userKey); // count = 1
    const MessengerLimiter::AttemptResult limitHit = limiter.registerAttempt(userKey);
    QVERIFY(limitHit.reachedLimit);
    QCOMPARE(limiter.attemptCount(userKey), 0);

    const MessengerLimiter::AttemptResult next = limiter.registerAttempt(userKey);
    QCOMPARE(next.attempts, 1);
    QVERIFY(!next.reachedLimit);
    QCOMPARE(next.remainingBeforeBlock, 1);
}

void ServerAntiSpamTests::ignoresEmptyKeys()
{
    MessengerLimiter limiter;
    const MessengerUserKey userKey(MessengerType::Telegram, QStringLiteral("   "));
    const MessengerLimiter::AttemptResult result = limiter.registerAttempt(userKey);
    QVERIFY(!result.valid);
    QCOMPARE(result.attempts, 0);
    QCOMPARE(limiter.attemptCount(userKey), 0);
}

BOOKING_RESOURCES_REGISTER_TEST(ServerAntiSpamTests)

#include "ServerAntiSpamTests.moc"
