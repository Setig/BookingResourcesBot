#include <QtTest>
#include "TestRegistry.h"

#include "MessengerCommandParser.h"
#include "MessengerAbstractCommand.h"
#include "MessengerCommandCompletions.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>

class CommandDurationTestAccess : public MessengerAbstractCommand
{
public:
    static bool parseChoice(const QString &token, const QList<int> &options, int *minutesOut)
    {
        return parseDurationChoice(token, options, minutesOut);
    }

    static bool parseBlock(const QString &token, QDateTime *untilUtcOut)
    {
        return parseBlockDurationToken(token, untilUtcOut);
    }
};

class MessengerCommandParserTests : public QObject
{
    Q_OBJECT

private slots:
    void parsesWhitespaceDelimitedInput();
    void parsesQuotedArgumentsWithSpaces();
    void parsesEscapedWhitespaceArguments();
    void splitsMessageCommandsByNewlines();
    void preservesQuotedAndEscapedNewlinesInCommands();
    void rejectsUnclosedQuoteInCommandList();
    void parsesQuotedMultilineArguments();
    void parsesEscapedNewlinesAsLiteral();
    void rejectsLegacyPipeSeparatedInput();
    void parsesQuotedPipeAsLiteral();
    void parsesEscapedPipeAsLiteral();
    void rejectsUnexpectedArguments();
    void distinguishesOmittedAndExplicitEmptyArguments();
    void greedyArgumentEnforcesRequirement();
    void parsesGreedyDurationArgumentsWithUnits();
    void parsesDurationMinutesTokens();
    void parsesDurationChoicesWithUnits();
    void parsesBlockDurationTokens();
    void completionContextForCommandName();
    void completionContextForQuotedArgument();
    void completionWhenConditionsMatch();
};

void MessengerCommandParserTests::parsesWhitespaceDelimitedInput()
{
    MessengerCommandParser parser(QStringLiteral("/book"),
                                  QStringLiteral("Reserve a shared resource."));
    parser.addArgument(QStringLiteral("resource"));
    parser.addArgument(QStringLiteral("duration"));

    const MessengerCommandParser::ParseResult result =
            parser.parse(QStringLiteral("   projector   60  "));

    QVERIFY(result.ok);
    QCOMPARE(result.values.size(), 2);
    QCOMPARE(result.values.at(0), QStringLiteral("projector"));
    QCOMPARE(result.values.at(1), QStringLiteral("60"));
    QVERIFY(result.errorText.isEmpty());
    QVERIFY(result.usage.contains(QStringLiteral("/book")));
}

void MessengerCommandParserTests::parsesQuotedArgumentsWithSpaces()
{
    MessengerCommandParser parser(QStringLiteral("/occupy"));
    parser.addArgument(QStringLiteral("resource"));
    parser.addArgument(QStringLiteral("duration"));

    const MessengerCommandParser::ParseResult result =
            parser.parse(QStringLiteral("\"Room A\" 60"));

    QVERIFY(result.ok);
    QCOMPARE(result.values.size(), 2);
    QCOMPARE(result.values.at(0), QStringLiteral("Room A"));
    QCOMPARE(result.values.at(1), QStringLiteral("60"));
}

void MessengerCommandParserTests::parsesEscapedWhitespaceArguments()
{
    MessengerCommandParser parser(QStringLiteral("/occupy"));
    parser.addArgument(QStringLiteral("resource"));
    parser.addArgument(QStringLiteral("duration"));

    const MessengerCommandParser::ParseResult result = parser.parse(QStringLiteral("Room\\ A 60"));

    QVERIFY(result.ok);
    QCOMPARE(result.values.size(), 2);
    QCOMPARE(result.values.at(0), QStringLiteral("Room A"));
    QCOMPARE(result.values.at(1), QStringLiteral("60"));
}

void MessengerCommandParserTests::splitsMessageCommandsByNewlines()
{
    QStringList commands;
    QString error;
    const QString input = QStringLiteral(
            "  /start  \n\n  /help  \r\n/add_resource \"Printer\" \"Desk\" \"Line 1\"\n");

    QVERIFY(MessengerCommandParser::splitMessageCommands(input, &commands, &error));
    QVERIFY(error.isEmpty());
    QCOMPARE(commands.size(), 3);
    QCOMPARE(commands.at(0), QStringLiteral("/start"));
    QCOMPARE(commands.at(1), QStringLiteral("/help"));
    QCOMPARE(commands.at(2), QStringLiteral("/add_resource \"Printer\" \"Desk\" \"Line 1\""));
}

void MessengerCommandParserTests::preservesQuotedAndEscapedNewlinesInCommands()
{
    QStringList commands;
    QString error;
    const QString input = QStringLiteral("/add_resource \"Printer\" \"Desk\" \"Line 1\nLine 2\"\n"
                                         "/send_notification hello\\\nworld");

    QVERIFY(MessengerCommandParser::splitMessageCommands(input, &commands, &error));
    QVERIFY(error.isEmpty());
    QCOMPARE(commands.size(), 2);
    QCOMPARE(commands.at(0),
             QStringLiteral("/add_resource \"Printer\" \"Desk\" \"Line 1\nLine 2\""));
    QCOMPARE(commands.at(1), QStringLiteral("/send_notification hello\\\nworld"));
}

void MessengerCommandParserTests::rejectsUnclosedQuoteInCommandList()
{
    QStringList commands;
    QString error;

    QVERIFY(!MessengerCommandParser::splitMessageCommands(
            QStringLiteral("/add_resource \"Printer\"\n/send_notification \"Line"),
            &commands,
            &error));
    QVERIFY(commands.isEmpty());
    QCOMPARE(error, QStringLiteral("Unclosed quote in arguments."));
}

void MessengerCommandParserTests::parsesQuotedMultilineArguments()
{
    MessengerCommandParser parser(QStringLiteral("/add_resource"));
    parser.addArgument(QStringLiteral("name"));
    parser.addArgument(QStringLiteral("description"));
    parser.addArgument(QStringLiteral("start-message"));

    const MessengerCommandParser::ParseResult result =
            parser.parse(QStringLiteral("\"Printer\" \"Desk\" \"Line 1\nLine 2\""));

    QVERIFY(result.ok);
    QCOMPARE(result.values.size(), 3);
    QCOMPARE(result.values.at(2), QStringLiteral("Line 1\nLine 2"));
}

void MessengerCommandParserTests::parsesEscapedNewlinesAsLiteral()
{
    MessengerCommandParser parser(QStringLiteral("/send_notification"));
    parser.addArgument(QStringLiteral("message"));

    const MessengerCommandParser::ParseResult result =
            parser.parse(QStringLiteral("hello\\\nworld"));

    QVERIFY(result.ok);
    QCOMPARE(result.values.size(), 1);
    QCOMPARE(result.values.at(0), QStringLiteral("hello\nworld"));
}

void MessengerCommandParserTests::rejectsLegacyPipeSeparatedInput()
{
    MessengerCommandParser parser(QStringLiteral("/schedule"),
                                  QStringLiteral("Schedule a resource with notes."));
    parser.setTokenMode(MessengerCommandParser::TokenMode::PipeDelimited);
    parser.addArgument(QStringLiteral("resource"));
    parser.addArgument(QStringLiteral("notes"),
                       QString(),
                       MessengerCommandParser::ArgumentFlag::Optional
                               | MessengerCommandParser::ArgumentFlag::AllowEmpty);

    const MessengerCommandParser::ParseResult result =
            parser.parse(QStringLiteral("room-a | custom reminder"));

    QVERIFY(!result.ok);
    QVERIFY(result.errorText.contains(QStringLiteral("Separator '|' is not supported.")));
}

void MessengerCommandParserTests::parsesQuotedPipeAsLiteral()
{
    MessengerCommandParser parser(QStringLiteral("/schedule"));
    parser.addArgument(QStringLiteral("resource"));
    parser.addArgument(QStringLiteral("duration"));

    const MessengerCommandParser::ParseResult result =
            parser.parse(QStringLiteral("\"Room | A\" 60"));

    QVERIFY(result.ok);
    QCOMPARE(result.values.size(), 2);
    QCOMPARE(result.values.at(0), QStringLiteral("Room | A"));
    QCOMPARE(result.values.at(1), QStringLiteral("60"));
}

void MessengerCommandParserTests::parsesEscapedPipeAsLiteral()
{
    MessengerCommandParser parser(QStringLiteral("/schedule"));
    parser.addArgument(QStringLiteral("resource"));
    parser.addArgument(QStringLiteral("duration"));

    const MessengerCommandParser::ParseResult result = parser.parse(QStringLiteral("Room\\|A 60"));

    QVERIFY(result.ok);
    QCOMPARE(result.values.size(), 2);
    QCOMPARE(result.values.at(0), QStringLiteral("Room|A"));
    QCOMPARE(result.values.at(1), QStringLiteral("60"));
}

void MessengerCommandParserTests::rejectsUnexpectedArguments()
{
    MessengerCommandParser parser(QStringLiteral("/ping"));
    parser.addArgument(QStringLiteral("target"));

    const MessengerCommandParser::ParseResult result =
            parser.parse(QStringLiteral("primary extra-token"));

    QVERIFY(!result.ok);
    QVERIFY(result.errorText.contains(QStringLiteral("Unexpected argument")));
    QVERIFY(result.usage.contains(QStringLiteral("/ping")));
}

void MessengerCommandParserTests::distinguishesOmittedAndExplicitEmptyArguments()
{
    MessengerCommandParser parser(QStringLiteral("/optional"));
    parser.addArgument(QStringLiteral("required"));
    parser.addArgument(QStringLiteral("optional"),
                       QString(),
                       MessengerCommandParser::ArgumentFlag::Optional
                               | MessengerCommandParser::ArgumentFlag::AllowEmpty);

    const MessengerCommandParser::ParseResult omitted = parser.parse(QStringLiteral("value"));
    QVERIFY(omitted.ok);
    QCOMPARE(omitted.values.size(), 2);
    QCOMPARE(omitted.provided.size(), 2);
    QVERIFY(omitted.provided.at(0));
    QVERIFY(!omitted.provided.at(1));

    const MessengerCommandParser::ParseResult explicitEmpty =
            parser.parse(QStringLiteral("value \"\""));
    QVERIFY(explicitEmpty.ok);
    QCOMPARE(explicitEmpty.values.size(), 2);
    QCOMPARE(explicitEmpty.provided.size(), 2);
    QVERIFY(explicitEmpty.provided.at(0));
    QVERIFY(explicitEmpty.provided.at(1));
    QVERIFY(explicitEmpty.values.at(1).isEmpty());

    MessengerCommandParser requiredGreedy(QStringLiteral("/required-greedy"));
    requiredGreedy.addArgument(QStringLiteral("value"),
                               QString(),
                               MessengerCommandParser::ArgumentFlag::Greedy
                                       | MessengerCommandParser::ArgumentFlag::AllowEmpty);
    QVERIFY(!requiredGreedy.parse(QString()).ok);
    const MessengerCommandParser::ParseResult explicitGreedyEmpty =
            requiredGreedy.parse(QStringLiteral("\"\""));
    QVERIFY(explicitGreedyEmpty.ok);
    QVERIFY(explicitGreedyEmpty.provided.at(0));
}

void MessengerCommandParserTests::greedyArgumentEnforcesRequirement()
{
    MessengerCommandParser parser(QStringLiteral("/echo"));
    parser.addArgument(QStringLiteral("message"),
                       QString(),
                       MessengerCommandParser::ArgumentFlag::Greedy);

    const MessengerCommandParser::ParseResult result = parser.parse(QString());
    QVERIFY(!result.ok);
    QVERIFY(result.errorText.contains(QStringLiteral("Missing required argument")));
}

void MessengerCommandParserTests::parsesGreedyDurationArgumentsWithUnits()
{
    {
        MessengerCommandParser parser(QStringLiteral("/occupy"));
        parser.addArgument(QStringLiteral("resource"));
        parser.addArgument(QStringLiteral("duration"),
                           QString(),
                           MessengerCommandParser::ArgumentFlag::Optional
                                   | MessengerCommandParser::ArgumentFlag::Greedy
                                   | MessengerCommandParser::ArgumentFlag::AllowEmpty);

        const MessengerCommandParser::ParseResult result =
                parser.parse(QStringLiteral("Printer 5 minutes"));

        QVERIFY(result.ok);
        QCOMPARE(result.values.size(), 2);
        QCOMPARE(result.values.at(0), QStringLiteral("Printer"));
        QCOMPARE(result.values.at(1), QStringLiteral("5 minutes"));
    }

    {
        MessengerCommandParser parser(QStringLiteral("/schedule"));
        parser.addArgument(QStringLiteral("resource"));
        parser.addArgument(QStringLiteral("start"));
        parser.addArgument(QStringLiteral("end"),
                           QString(),
                           MessengerCommandParser::ArgumentFlag::Optional
                                   | MessengerCommandParser::ArgumentFlag::Greedy
                                   | MessengerCommandParser::ArgumentFlag::AllowEmpty);

        const MessengerCommandParser::ParseResult result =
                parser.parse(QStringLiteral("Printer 0 5 minutes"));

        QVERIFY(result.ok);
        QCOMPARE(result.values.size(), 3);
        QCOMPARE(result.values.at(0), QStringLiteral("Printer"));
        QCOMPARE(result.values.at(1), QStringLiteral("0"));
        QCOMPARE(result.values.at(2), QStringLiteral("5 minutes"));
    }
}

void MessengerCommandParserTests::parsesDurationMinutesTokens()
{
    const QList<QPair<QString, int>> cases = {
        { QStringLiteral("5"), 5 },
        { QStringLiteral("5 min"), 5 },
        { QStringLiteral("5 minutes"), 5 },
        { QString::fromUtf8("5 минут"), 5 },
        { QStringLiteral("2 hours"), 120 },
        { QString::fromUtf8("2 часа"), 120 },
        { QStringLiteral("3 days"), 3 * 24 * 60 },
        { QStringLiteral("1 week"), 7 * 24 * 60 },
        { QStringLiteral("1 month"), 30 * 24 * 60 },
        { QString::fromUtf8("2 месяца"), 2 * 30 * 24 * 60 },
        { QStringLiteral("0 minutes"), 0 },
    };

    for (const QPair<QString, int> &testCase : cases) {
        int minutes = -1;
        QVERIFY2(MessengerAbstractCommand::parseDurationMinutesToken(testCase.first, &minutes),
                 qPrintable(testCase.first));
        QCOMPARE(minutes, testCase.second);
    }

    int minutes = -1;
    QVERIFY(!MessengerAbstractCommand::parseDurationMinutesToken(QStringLiteral("5 parsecs"),
                                                                 &minutes));
    QVERIFY(!MessengerAbstractCommand::parseDurationMinutesToken(QStringLiteral("-5 minutes"),
                                                                 &minutes));
}

void MessengerCommandParserTests::parsesDurationChoicesWithUnits()
{
    const QList<int> options = { 0, 5, 30, 60, 120, 240 };
    int minutes = -1;

    QVERIFY(CommandDurationTestAccess::parseChoice(QStringLiteral("5 minutes"), options, &minutes));
    QCOMPARE(minutes, 5);

    QVERIFY(CommandDurationTestAccess::parseChoice(QStringLiteral("2 hours"), options, &minutes));
    QCOMPARE(minutes, 120);

    QVERIFY(CommandDurationTestAccess::parseChoice(QString::fromUtf8("1 час"), options, &minutes));
    QCOMPARE(minutes, 60);

    QVERIFY(CommandDurationTestAccess::parseChoice(QStringLiteral("2"), options, &minutes));
    QCOMPARE(minutes, 5);

    QVERIFY(!CommandDurationTestAccess::parseChoice(QStringLiteral("2 months"), options, &minutes));
}

void MessengerCommandParserTests::parsesBlockDurationTokens()
{
    const QDateTime beforeUtc = QDateTime::currentDateTimeUtc();
    QDateTime untilUtc;
    QVERIFY(CommandDurationTestAccess::parseBlock(QStringLiteral("2 hours"), &untilUtc));
    const QDateTime afterUtc = QDateTime::currentDateTimeUtc();
    QVERIFY(untilUtc.isValid());
    QVERIFY(beforeUtc.addSecs(2 * 60 * 60) <= untilUtc);
    QVERIFY(untilUtc <= afterUtc.addSecs(2 * 60 * 60));

    QVERIFY(CommandDurationTestAccess::parseBlock(QStringLiteral("1 month"), &untilUtc));
    QVERIFY(untilUtc.isValid());

    QVERIFY(CommandDurationTestAccess::parseBlock(QStringLiteral("permanent"), &untilUtc));
    QVERIFY(!untilUtc.isValid());

    QVERIFY(!CommandDurationTestAccess::parseBlock(QStringLiteral("0 minutes"), &untilUtc));
    QVERIFY(!CommandDurationTestAccess::parseBlock(QStringLiteral("5 parsecs"), &untilUtc));
}

void MessengerCommandParserTests::completionContextForCommandName()
{
    const QString text = QStringLiteral("/lis");
    const MessengerCommandParser::CompletionContext context =
            MessengerCommandParser::contextAtCursor(
                    text, text.size(), QVector<MessengerCommandParser::ArgumentInfo>());
    QVERIFY(context.ok);
    QVERIFY(context.completingCommand);
    QCOMPARE(context.commandName, QStringLiteral("/lis"));
    QCOMPARE(context.prefix, QStringLiteral("/lis"));
    QCOMPARE(context.replaceFrom, 0);
    QCOMPARE(context.replaceTo, 4);
}

void MessengerCommandParserTests::completionContextForQuotedArgument()
{
    MessengerCommandParser parser(QStringLiteral("/queue"));
    parser.addArgument(QStringLiteral("name"));
    const QString text = QStringLiteral("/queue \"Stand");
    const MessengerCommandParser::CompletionContext context =
            MessengerCommandParser::contextAtCursor(text, text.size(), parser.arguments());
    QVERIFY(context.ok);
    QVERIFY(!context.completingCommand);
    QCOMPARE(context.argumentIndex, 0);
    QCOMPARE(context.prefix, QStringLiteral("Stand"));
}

void MessengerCommandParserTests::completionWhenConditionsMatch()
{
    QJsonObject when;
    when.insert(QStringLiteral("0"), QJsonArray{ QStringLiteral("set") });
    when.insert(QStringLiteral("1"), QJsonArray{ QStringLiteral("locale") });
    QVERIFY(MessengerCommandCompletions::conditionsMatch(when,
                                                         QStringList()
                                                                 << QStringLiteral("set")
                                                                 << QStringLiteral("locale")));
    QVERIFY(!MessengerCommandCompletions::conditionsMatch(when,
                                                          QStringList()
                                                                  << QStringLiteral("list")
                                                                  << QStringLiteral("locale")));
    QVERIFY(MessengerCommandCompletions::conditionsMatch(QJsonObject(), QStringList()));
}

BOOKING_RESOURCES_REGISTER_TEST(MessengerCommandParserTests)

#include "MessengerCommandParserTests.moc"
