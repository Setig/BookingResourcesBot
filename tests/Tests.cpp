#include "TestRegistry.h"

#include "BookingResourcesApplication.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QScopedPointer>
#include <QStringList>
#include <QTemporaryFile>
#include <QTextStream>
#include <QVector>
#include <QtTest>

Q_LOGGING_CATEGORY(lcTests, "booking.tests")

namespace {

void writeLine(const QString &line)
{
    QTextStream out(stdout);
    out << line << '\n';
    out.flush();
}

QStringList sortedSuiteNames()
{
    QStringList names;
    const QVector<TestRegistry::Entry> &entries = TestRegistry::all();
    for (const TestRegistry::Entry &entry : entries)
        names.append(entry.name);
    names.sort();
    return names;
}

const TestRegistry::Entry *findSuite(const QString &name)
{
    const QVector<TestRegistry::Entry> &entries = TestRegistry::all();
    for (const TestRegistry::Entry &entry : entries) {
        if (entry.name == name)
            return &entry;
    }
    return nullptr;
}

bool optionTakesValue(const QString &option)
{
    return option == QLatin1String("-o") || option == QLatin1String("-maxwarnings")
           || option == QLatin1String("-eventdelay") || option == QLatin1String("-keydelay")
           || option == QLatin1String("-mousedelay");
}

struct TestTotals
{
    TestTotals()
        : passed(0)
        , failed(0)
        , skipped(0)
        , blacklisted(0)
        , durationMs(0)
    {
        // do nothing
    }

    int passed;
    int failed;
    int skipped;
    int blacklisted;
    qint64 durationMs;
};

void addTotals(TestTotals *destination, const TestTotals &source)
{
    if (!destination)
        return;
    destination->passed += source.passed;
    destination->failed += source.failed;
    destination->skipped += source.skipped;
    destination->blacklisted += source.blacklisted;
    destination->durationMs += source.durationMs;
}

bool parseTotalsText(const QString &text, TestTotals *totals)
{
    if (!totals)
        return false;

    static const QRegularExpression pattern(QStringLiteral(
            R"(Totals:\s+(\d+) passed,\s+(\d+) failed,\s+(\d+) skipped,\s+(\d+) blacklisted,\s+(\d+)ms)"));
    QRegularExpressionMatchIterator it = pattern.globalMatch(text);
    QRegularExpressionMatch match;
    while (it.hasNext())
        match = it.next();
    if (!match.hasMatch())
        return false;

    totals->passed = match.captured(1).toInt();
    totals->failed = match.captured(2).toInt();
    totals->skipped = match.captured(3).toInt();
    totals->blacklisted = match.captured(4).toInt();
    totals->durationMs = match.captured(5).toLongLong();
    return true;
}

bool parseTotalsFromFile(const QString &path, TestTotals *totals)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    return parseTotalsText(QString::fromLocal8Bit(file.readAll()), totals);
}

bool hasOutputDestination(const QStringList &arguments)
{
    for (const QString &argument : arguments) {
        if (argument == QLatin1String("-o") || argument == QLatin1String("-txt")
            || argument == QLatin1String("-xml") || argument == QLatin1String("-lightxml")
            || argument == QLatin1String("-csv") || argument == QLatin1String("-xunitxml")
            || argument == QLatin1String("-junitxml") || argument == QLatin1String("-teamcity")
            || argument == QLatin1String("-tap")) {
            return true;
        }
    }
    return false;
}

QStringList argumentsWithTotalsOutput(const QStringList &qtestArguments, const QString &outputPath)
{
    QStringList arguments = qtestArguments;
    if (!hasOutputDestination(arguments))
        arguments << QStringLiteral("-o") << QStringLiteral("-,txt");
    arguments << QStringLiteral("-o") << QStringLiteral("%1,txt").arg(outputPath);
    return arguments;
}

void printTotals(const TestTotals &totals)
{
    writeLine(QStringLiteral("Tests totals: %1 passed, %2 failed, %3 skipped, %4 blacklisted, %5ms")
                      .arg(totals.passed)
                      .arg(totals.failed)
                      .arg(totals.skipped)
                      .arg(totals.blacklisted)
                      .arg(totals.durationMs));
}

struct ParsedArguments
{
    bool showHelp;
    bool listSuites;
    QString suiteName;
    QStringList qtestArguments;
};

ParsedArguments parseArguments(const QStringList &arguments)
{
    ParsedArguments parsed;
    parsed.showHelp = false;
    parsed.listSuites = false;
    if (!arguments.isEmpty())
        parsed.qtestArguments.append(arguments.first());

    for (int i = 1; i < arguments.size(); ++i) {
        const QString &argument = arguments.at(i);
        if (argument == QLatin1String("-h") || argument == QLatin1String("--help")) {
            parsed.showHelp = true;
            continue;
        }
        if (argument == QLatin1String("-l") || argument == QLatin1String("--list")) {
            parsed.listSuites = true;
            continue;
        }
        if (argument.startsWith(QLatin1Char('-'))) {
            parsed.qtestArguments.append(argument);
            if (optionTakesValue(argument) && (i + 1) < arguments.size()) {
                ++i;
                parsed.qtestArguments.append(arguments.at(i));
            }
            continue;
        }
        if (parsed.suiteName.isEmpty()) {
            parsed.suiteName = argument;
            continue;
        }
        parsed.qtestArguments.append(argument);
    }

    return parsed;
}

void printHelp()
{
    writeLine(QStringLiteral("Usage: tests [options] [suite] [qtest-options...]"));
    writeLine(QString());
    writeLine(QStringLiteral("  -h, --help    Show this help"));
    writeLine(QStringLiteral("  -l, --list    List registered test suites"));
    writeLine(QString());
    writeLine(QStringLiteral("Run all suites, one suite, or a suite function:"));
    writeLine(QStringLiteral("  tests"));
    writeLine(QStringLiteral("  tests ServerDatabaseTests"));
    writeLine(QStringLiteral("  tests ServerDatabaseTests testDatabaseRows"));
}

int runSuite(const TestRegistry::Entry &entry,
             const QStringList &qtestArguments,
             TestTotals *totals)
{
    QScopedPointer<QObject> testObject(entry.factory());

    QTemporaryFile outputFile(QDir::tempPath()
                              + QLatin1String("/BookingResourcesTestTotals_XXXXXX.txt"));
    outputFile.setAutoRemove(true);
    if (!outputFile.open()) {
        const int status = QTest::qExec(testObject.data(), qtestArguments);
        if (totals && status != 0)
            totals->failed += qMax(1, status);
        return status;
    }
    const QString outputPath = outputFile.fileName();
    outputFile.close();

    const int status =
            QTest::qExec(testObject.data(), argumentsWithTotalsOutput(qtestArguments, outputPath));
    TestTotals suiteTotals;
    if (parseTotalsFromFile(outputPath, &suiteTotals)) {
        addTotals(totals, suiteTotals);
    } else if (totals && status != 0) {
        totals->failed += qMax(1, status);
    }
    return status;
}

} // namespace

int main(int argc, char **argv)
{
    BookingResourcesApplication app(argc,
                                    argv,
                                    QStringLiteral("tests"),
                                    QStringLiteral(APP_VERSION),
                                    QStringLiteral("tests"),
                                    lcTests());

    const ParsedArguments parsed = parseArguments(QCoreApplication::arguments());
    if (parsed.showHelp) {
        printHelp();
        return 0;
    }

    if (TestRegistry::all().isEmpty()) {
        writeLine(QStringLiteral("No test suites are registered."));
        return 1;
    }

    if (parsed.listSuites) {
        const QStringList names = sortedSuiteNames();
        for (const QString &name : names)
            writeLine(name);
        return 0;
    }

    if (!parsed.suiteName.isEmpty()) {
        const TestRegistry::Entry *entry = findSuite(parsed.suiteName);
        if (!entry) {
            writeLine(QStringLiteral("Unknown test suite: %1").arg(parsed.suiteName));
            writeLine(QStringLiteral("Available suites:"));
            const QStringList names = sortedSuiteNames();
            for (const QString &name : names)
                writeLine(QStringLiteral("  %1").arg(name));
            return 1;
        }
        TestTotals totals;
        const int status = runSuite(*entry, parsed.qtestArguments, &totals);
        printTotals(totals);
        return status;
    }

    int status = 0;
    TestTotals totals;
    const QStringList names = sortedSuiteNames();
    for (const QString &name : names) {
        const TestRegistry::Entry *entry = findSuite(name);
        writeLine(QStringLiteral("********* Start suite %1 *********").arg(name));
        status |= runSuite(*entry, parsed.qtestArguments, &totals);
    }
    printTotals(totals);
    return status;
}
