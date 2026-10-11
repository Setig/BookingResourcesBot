#include <QtTest/QtTest>
#include "TestRegistry.h"

#include "CliCommandLineParser.h"

class CliCommandLineParserTests : public QObject
{
    Q_OBJECT

private slots:
    void acceptsWebSharedMemoryKeyWithCommand();
    void rejectsWebSharedMemorySecret();
    void commandWithoutTargetIsNotComplete();
};

void CliCommandLineParserTests::acceptsWebSharedMemoryKeyWithCommand()
{
    CliCommandLineParser parser;
    const QStringList arguments = {
        QStringLiteral("booking-resources-cli"),
        QStringLiteral("--web-shared-memory-key"),
        QStringLiteral("booking-resources-web"),
        QStringLiteral("--command"),
        QStringLiteral("/help"),
    };

    QVERIFY(parser.parse(arguments));
    QVERIFY(parser.hasWebSharedMemoryKey());
    QCOMPARE(parser.webSharedMemoryKey(), QStringLiteral("booking-resources-web"));
    QVERIFY(parser.commandRequested());
    QVERIFY(parser.hasTargetArgument());
    QVERIFY(!parser.hasPidArgument());
    QCOMPARE(parser.commandText(), QStringLiteral("/help"));
}

void CliCommandLineParserTests::rejectsWebSharedMemorySecret()
{
    CliCommandLineParser parser;
    const QStringList arguments = {
        QStringLiteral("booking-resources-cli"),
        QStringLiteral("--web-shared-memory-secret"),
        QStringLiteral("secret"),
    };

    QVERIFY(!parser.parse(arguments));
    QVERIFY(!parser.hasWebSharedMemoryKey());
}

void CliCommandLineParserTests::commandWithoutTargetIsNotComplete()
{
    CliCommandLineParser parser;
    const QStringList arguments = {
        QStringLiteral("booking-resources-cli"),
        QStringLiteral("--command"),
        QStringLiteral("/help"),
    };

    QVERIFY(parser.parse(arguments));
    QVERIFY(parser.commandRequested());
    QVERIFY(!parser.hasPidArgument());
    QVERIFY(!parser.hasWebSharedMemoryKey());
    QVERIFY(!parser.hasTargetArgument());
}

BOOKING_RESOURCES_REGISTER_TEST(CliCommandLineParserTests)

#include "CliCommandLineParserTests.moc"
