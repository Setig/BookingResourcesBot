#include <QtTest/QtTest>
#include "TestRegistry.h"

#include "CliHistory.h"

class CliHistoryTests : public QObject
{
    Q_OBJECT

private slots:
    void skipsEmptyAndConsecutiveDuplicates();
    void trimsToMaximumCount();
    void navigationKeepsOriginalEntries();
};

void CliHistoryTests::skipsEmptyAndConsecutiveDuplicates()
{
    CliHistory history;
    history.add(QString());
    history.add(QStringLiteral("   "));
    history.add(QStringLiteral("/help"));
    history.add(QStringLiteral("/help"));
    history.add(QStringLiteral("/list_users"));
    QCOMPARE(history.count(), 2);
    QCOMPARE(history.entries(),
             (QStringList() << QStringLiteral("/help") << QStringLiteral("/list_users")));
}

void CliHistoryTests::trimsToMaximumCount()
{
    CliHistory history;
    history.setMaximumCount(2);
    history.add(QStringLiteral("one"));
    history.add(QStringLiteral("two"));
    history.add(QStringLiteral("three"));
    QCOMPARE(history.count(), 2);
    QCOMPARE(history.entries().first(), QStringLiteral("two"));
    QCOMPARE(history.entries().last(), QStringLiteral("three"));
}

void CliHistoryTests::navigationKeepsOriginalEntries()
{
    CliHistory history;
    history.add(QStringLiteral("first"));
    history.add(QStringLiteral("second"));
    history.beginNavigation(QStringLiteral("draft"));
    QCOMPARE(history.moveUp(), QStringLiteral("second"));
    QCOMPARE(history.moveUp(), QStringLiteral("first"));
    QCOMPARE(history.moveUp(), QStringLiteral("first"));
    QCOMPARE(history.moveDown(), QStringLiteral("second"));
    QCOMPARE(history.moveDown(), QStringLiteral("draft"));
    QCOMPARE(history.entries(),
             (QStringList() << QStringLiteral("first") << QStringLiteral("second")));
}

BOOKING_RESOURCES_REGISTER_TEST(CliHistoryTests)

#include "CliHistoryTests.moc"
