#include <QtTest>
#include "TestRegistry.h"

#include <QEvent>
#include <QLoggingCategory>

#include "BookingResourcesApplication.h"

Q_LOGGING_CATEGORY(lcApplicationTests, "booking.application.tests")

class BookingResourcesApplicationTests : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void retainBlocksQuitUntilReleased();
    void extraReleaseDoesNotGoNegative();
    void quitEventHonorsLocalCliSession();
};

void BookingResourcesApplicationTests::init()
{
    BookingResourcesApplication::resetConsoleInterruptSuppression();
}

void BookingResourcesApplicationTests::cleanup()
{
    BookingResourcesApplication::resetConsoleInterruptSuppression();
}

void BookingResourcesApplicationTests::retainBlocksQuitUntilReleased()
{
    QCOMPARE(BookingResourcesApplication::consoleInterruptSuppressionCount(), 0);
    QVERIFY(BookingResourcesApplication::shouldAcceptQuitEvent());

    BookingResourcesApplication::retainConsoleInterruptSuppression();
    QCOMPARE(BookingResourcesApplication::consoleInterruptSuppressionCount(), 1);
    QVERIFY(!BookingResourcesApplication::shouldAcceptQuitEvent());

    BookingResourcesApplication::retainConsoleInterruptSuppression();
    QCOMPARE(BookingResourcesApplication::consoleInterruptSuppressionCount(), 2);
    QVERIFY(!BookingResourcesApplication::shouldAcceptQuitEvent());

    BookingResourcesApplication::releaseConsoleInterruptSuppression();
    QCOMPARE(BookingResourcesApplication::consoleInterruptSuppressionCount(), 1);
    QVERIFY(!BookingResourcesApplication::shouldAcceptQuitEvent());

    BookingResourcesApplication::releaseConsoleInterruptSuppression();
    QCOMPARE(BookingResourcesApplication::consoleInterruptSuppressionCount(), 0);
    QVERIFY(BookingResourcesApplication::shouldAcceptQuitEvent());
}

void BookingResourcesApplicationTests::extraReleaseDoesNotGoNegative()
{
    BookingResourcesApplication::releaseConsoleInterruptSuppression();
    QCOMPARE(BookingResourcesApplication::consoleInterruptSuppressionCount(), 0);
    QVERIFY(BookingResourcesApplication::shouldAcceptQuitEvent());
}

void BookingResourcesApplicationTests::quitEventHonorsLocalCliSession()
{
    auto *app = static_cast<BookingResourcesApplication *>(QCoreApplication::instance());
    QVERIFY(app);
    QVERIFY(!app->shutdownPerformed());

    BookingResourcesApplication::retainConsoleInterruptSuppression();
    QEvent ignoredQuit(QEvent::Quit);
    QVERIFY(QCoreApplication::sendEvent(app, &ignoredQuit));
    QVERIFY(!app->shutdownPerformed());

    BookingResourcesApplication::releaseConsoleInterruptSuppression();
    QEvent acceptedQuit(QEvent::Quit);
    QVERIFY(QCoreApplication::sendEvent(app, &acceptedQuit));
    QVERIFY(app->shutdownPerformed());
}

BOOKING_RESOURCES_REGISTER_TEST(BookingResourcesApplicationTests)

#include "BookingResourcesApplicationTests.moc"
