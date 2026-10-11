#include <QtTest>
#include "TestRegistry.h"

#include "BookingResourcesTranslator.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QCoreApplication>

namespace {

const QString kTestTranslatorBaseName = QStringLiteral("booking-resources-translator-tests");
const QString kRuGreeting = QStringLiteral("ru::greeting translation");
const QString kRuFarewell = QStringLiteral("ru::farewell translation");
const QString kRuStatus = QStringLiteral("ru::status translation");
// NOTE: The tests pass source texts via variables. Qt lupdate can only extract
// translatable strings when they are string literals or special NOOP macros.
// Keep these declarations in sync with the expectations in
// `translations/booking-resources-translator-tests_ru.ts`.
constexpr const char kSourceGreeting[] =
        QT_TRANSLATE_NOOP("TestTranslatableStrings", "Test greeting");
constexpr const char kSourceFarewell[] =
        QT_TRANSLATE_NOOP("TestTranslatableStrings", "Test farewell");
constexpr const char kSourceStatus[] = QT_TRANSLATE_NOOP("TestTranslatableStrings", "Test status");

QStringList translationSearchPaths()
{
    return QStringList() << QStringLiteral(":/translations");
}

class TestTranslatableStrings
{
    Q_DECLARE_TR_FUNCTIONS(TestTranslatableStrings)
};

} // namespace

class BookingResourcesTranslatorTests : public QObject
{
    Q_OBJECT

private slots:
    void sanitizesCustomSearchPaths();
    void initializeHonorsPreferredLocale();
    void initializeRespectsEnvironmentOverride();
    void useLocaleFallsBackToEnglish();
    void useLocaleOverridesInitializedTranslator();
    void nestedLocaleOverridesUseLatestScope();
};

void BookingResourcesTranslatorTests::sanitizesCustomSearchPaths()
{
    BookingResourcesTranslator translator(kTestTranslatorBaseName);
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QStringList paths;
    paths << QStringLiteral("  ") << tempDir.path() << tempDir.path()
          << QStringLiteral(":/translations") << QStringLiteral(":/translations");

    translator.setSearchPaths(paths);
    const QStringList sanitized = translator.searchPaths();

    QCOMPARE(sanitized.size(), 2);
    QCOMPARE(sanitized.at(0), QFileInfo(tempDir.path()).absoluteFilePath());
    QCOMPARE(sanitized.at(1), QStringLiteral(":/translations"));
}

void BookingResourcesTranslatorTests::initializeHonorsPreferredLocale()
{
    BookingResourcesTranslator translator(kTestTranslatorBaseName);
    translator.setSearchPaths(translationSearchPaths());

    const bool loaded = translator.initialize(QStringLiteral("ru_RU"));
    QVERIFY(loaded);

    const QStringList attempted = translator.attemptedLocales();
    QCOMPARE(attempted.value(0), QStringLiteral("ru_RU"));
    QCOMPARE(attempted.value(1), QStringLiteral("ru"));
    QCOMPARE(attempted.value(2), QStringLiteral("en"));
    QCOMPARE(translator.activeLocale(), QStringLiteral("ru_RU"));

    QCOMPARE(TestTranslatableStrings::tr(kSourceGreeting), kRuGreeting);
    QCOMPARE(TestTranslatableStrings::tr(kSourceFarewell), kRuFarewell);
    QCOMPARE(TestTranslatableStrings::tr(kSourceStatus), kRuStatus);
}

void BookingResourcesTranslatorTests::initializeRespectsEnvironmentOverride()
{
    const QByteArray previous = qgetenv("BOOKING_BOT_LOCALE");
    QVERIFY(qputenv("BOOKING_BOT_LOCALE", QByteArray("ru_RU")));

    BookingResourcesTranslator translator(kTestTranslatorBaseName);
    translator.setSearchPaths(translationSearchPaths());

    const bool loaded = translator.initialize();
    QVERIFY(loaded);

    const QStringList attempted = translator.attemptedLocales();
    QCOMPARE(attempted.value(0), QStringLiteral("ru_RU"));
    QCOMPARE(TestTranslatableStrings::tr(kSourceGreeting), kRuGreeting);

    if (previous.isNull()) {
        qunsetenv("BOOKING_BOT_LOCALE");
    } else {
        QVERIFY(qputenv("BOOKING_BOT_LOCALE", previous));
    }
}

void BookingResourcesTranslatorTests::useLocaleFallsBackToEnglish()
{
    BookingResourcesTranslator translator(kTestTranslatorBaseName);
    translator.setSearchPaths(translationSearchPaths());

    {
        BookingResourcesTranslator::ScopedLocaleOverride guard =
                translator.useLocale(QStringLiteral("ru_RU"));
        QVERIFY(guard.isActive());
        QCOMPARE(TestTranslatableStrings::tr(kSourceStatus), kRuStatus);
    }

    QCOMPARE(TestTranslatableStrings::tr(kSourceStatus), QString::fromLatin1(kSourceStatus));

    BookingResourcesTranslator::ScopedLocaleOverride missing =
            translator.useLocale(QStringLiteral("uk_UA"));
    QVERIFY(!missing.isActive());
}

void BookingResourcesTranslatorTests::useLocaleOverridesInitializedTranslator()
{
    BookingResourcesTranslator translator(kTestTranslatorBaseName);
    translator.setSearchPaths(translationSearchPaths());

    const bool loaded = translator.initialize(QStringLiteral("ru_RU"));
    QVERIFY(loaded);
    QCOMPARE(TestTranslatableStrings::tr(kSourceStatus), kRuStatus);

    {
        BookingResourcesTranslator::ScopedLocaleOverride english =
                translator.useLocale(QStringLiteral("en"));
        QVERIFY(english.isActive());
        QCOMPARE(TestTranslatableStrings::tr(kSourceStatus), QString::fromLatin1(kSourceStatus));
    }

    QCOMPARE(TestTranslatableStrings::tr(kSourceStatus), kRuStatus);

    {
        BookingResourcesTranslator::ScopedLocaleOverride missing =
                translator.useLocale(QStringLiteral("uk_UA"));
        QVERIFY(missing.isActive());
        QCOMPARE(TestTranslatableStrings::tr(kSourceStatus), QString::fromLatin1(kSourceStatus));
    }

    QCOMPARE(TestTranslatableStrings::tr(kSourceStatus), kRuStatus);
}

void BookingResourcesTranslatorTests::nestedLocaleOverridesUseLatestScope()
{
    BookingResourcesTranslator translator(kTestTranslatorBaseName);
    translator.setSearchPaths(translationSearchPaths());

    const bool loaded = translator.initialize(QStringLiteral("ru_RU"));
    QVERIFY(loaded);
    QCOMPARE(TestTranslatableStrings::tr(kSourceStatus), kRuStatus);

    {
        BookingResourcesTranslator::ScopedLocaleOverride outerRu =
                translator.useLocale(QStringLiteral("ru"));
        Q_UNUSED(outerRu);

        {
            BookingResourcesTranslator::ScopedLocaleOverride innerEn =
                    translator.useLocale(QStringLiteral("en"));
            QVERIFY(innerEn.isActive());
            QCOMPARE(TestTranslatableStrings::tr(kSourceStatus),
                     QString::fromLatin1(kSourceStatus));
        }

        QCOMPARE(TestTranslatableStrings::tr(kSourceStatus), kRuStatus);
    }

    QCOMPARE(TestTranslatableStrings::tr(kSourceStatus), kRuStatus);
}

BOOKING_RESOURCES_REGISTER_TEST(BookingResourcesTranslatorTests)

#include "BookingResourcesTranslatorTests.moc"

