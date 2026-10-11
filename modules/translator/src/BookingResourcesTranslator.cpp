#include "BookingResourcesTranslator.h"

#include <QDir>
#include <QSet>
#include <QHash>
#include <QLocale>
#include <QFileInfo>
#include <QTranslator>
#include <QSharedPointer>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QProcessEnvironment>

Q_LOGGING_CATEGORY(lcTranslator, "booking.modules.translator")

class BookingResourcesTranslatorPrivate
{
public:
    BookingResourcesTranslatorPrivate(const QString &baseName);

    static QStringList sanitizeSearchPaths(const QStringList &paths);

    const QString baseName;

    QStringList searchPaths;
    QString activeLocale;
    QStringList attemptedLocales;
    QTranslator translator;
    bool translatorInstalled = false;
    int baseTranslatorSuppressionCount = 0;
    QHash<QString, QSharedPointer<QTranslator>> scopedTranslators;
    QHash<QTranslator *, int> scopedTranslatorRefCounts;
    QHash<QTranslator *, int> scopedTranslatorSuppressionCounts;
    QList<QTranslator *> scopedTranslatorOrder;

    QString resolveInitialLocale(const QString &preferredLocale) const
    {
        if (!preferredLocale.trimmed().isEmpty())
            return preferredLocale.trimmed();

        const QByteArray env = qgetenv("BOOKING_BOT_LOCALE");
        if (!env.trimmed().isEmpty()) {
            qCInfo(lcTranslator).nospace()
                    << "BOOKING_BOT_LOCALE is set to \"" << qPrintable(env) << "\".";
            return QString::fromUtf8(env);
        }

        return QLocale::system().name();
    }

    QStringList buildFallbackLocales(const QString &localeCode) const
    {
        QStringList candidates;
        candidates.reserve(6);
        QSet<QString> seen;

        const auto appendIfNew = [&](const QString &code) {
            const QString normalized = code.trimmed();
            if (normalized.isEmpty() || seen.contains(normalized))
                return;
            seen.insert(normalized);
            candidates << normalized;
        };

        const QString trimmed = localeCode.trimmed();
        appendIfNew(trimmed);
        QString normalized = trimmed;
        normalized.replace(QLatin1Char('-'), QLatin1Char('_'));
        appendIfNew(normalized);

        if (normalized.compare(QStringLiteral("ru_stand"), Qt::CaseInsensitive) == 0) {
            appendIfNew(QStringLiteral("ru"));
            appendIfNew(QStringLiteral("en"));
            return candidates;
        }

        const QLocale locale(normalized);
        const QString localeName = locale.name();
        if (localeName != QStringLiteral("C")
            || normalized.compare(QStringLiteral("c"), Qt::CaseInsensitive) == 0) {
            appendIfNew(localeName);
        }

        const QString language = normalized.section(QLatin1Char('_'), 0, 0).toLower();
        appendIfNew(language);
        appendIfNew(QStringLiteral("en"));

        return candidates;
    }

    bool installTranslatorForLocale(const QString &localeCode)
    {
        if (localeCode.trimmed().isEmpty())
            return false;

        if (loadTranslator(&translator, localeCode)) {
            if (!translatorInstalled) {
                QCoreApplication::installTranslator(&translator);
                translatorInstalled = true;
            }
            return true;
        }

        qCInfo(lcTranslator).nospace()
                << "Failed load translations for locale \"" << qPrintable(localeCode) << "\"!";
        return false;
    }

    void uninstallTranslator()
    {
        if (!translatorInstalled)
            return;

        QCoreApplication::removeTranslator(&translator);
        translatorInstalled = false;
        baseTranslatorSuppressionCount = 0;
    }

    void suppressBaseTranslator()
    {
        if (!translatorInstalled)
            return;

        if (baseTranslatorSuppressionCount == 0)
            QCoreApplication::removeTranslator(&translator);
        baseTranslatorSuppressionCount += 1;
    }

    void restoreBaseTranslator()
    {
        if (baseTranslatorSuppressionCount <= 0)
            return;

        baseTranslatorSuppressionCount -= 1;
        if (baseTranslatorSuppressionCount == 0 && translatorInstalled)
            QCoreApplication::installTranslator(&translator);
    }

    static QString normalizedLocaleKey(const QString &code)
    {
        QString key = code.trimmed().toLower();
        key.replace(QLatin1Char('-'), QLatin1Char('_'));
        return key;
    }

    static bool isSpecializedRussianLocale(const QString &normalized)
    {
        return normalized.compare(QStringLiteral("ru_stand"), Qt::CaseInsensitive) == 0;
    }

    QStringList translationFileLocales(const QString &localeCode) const
    {
        QStringList names;
        QSet<QString> seen;
        const auto appendIfNew = [&](const QString &code) {
            const QString value = code.trimmed();
            if (value.isEmpty() || seen.contains(value))
                return;
            seen.insert(value);
            names << value;
        };

        const QString trimmed = localeCode.trimmed();
        appendIfNew(trimmed);
        const QString key = normalizedLocaleKey(trimmed);
        appendIfNew(key);
        if (isSpecializedRussianLocale(key))
            return names;

        const QString language = key.section(QLatin1Char('_'), 0, 0);
        if (language == QLatin1String("ru") || language == QLatin1String("en"))
            appendIfNew(language);
        return names;
    }

    bool loadTranslatorFromDirectory(QTranslator *translatorPtr,
                                     const QString &dir,
                                     const QString &localeCode) const
    {
        if (!translatorPtr || localeCode.trimmed().isEmpty())
            return false;

        const QString composed = QStringLiteral("%1_%2").arg(baseName, localeCode.trimmed());
        // Empty (non-null) delimiters disable QTranslator suffix stripping so
        // "ru" cannot match "ru_stand" and vice versa.
        return translatorPtr->load(composed, dir, QStringLiteral(""), QStringLiteral(".qm"));
    }

    bool loadTranslator(QTranslator *translatorPtr, const QString &localeCode) const
    {
        if (!translatorPtr)
            return false;

        const QStringList fileLocales = translationFileLocales(localeCode);
        for (const QString &fileLocale : fileLocales) {
            for (const QString &dir : std::as_const(searchPaths)) {
                if (loadTranslatorFromDirectory(translatorPtr, dir, fileLocale))
                    return true;
            }
        }
        return false;
    }

    QTranslator *compatibleScopedTranslator(const QString &localeKey) const
    {
        if (localeKey.isEmpty() || isSpecializedRussianLocale(localeKey))
            return nullptr;

        const QString language = localeKey.section(QLatin1Char('_'), 0, 0);
        if (language != QLatin1String("ru") && language != QLatin1String("en"))
            return nullptr;

        QHash<QString, QSharedPointer<QTranslator>>::const_iterator it =
                scopedTranslators.constBegin();
        for (; it != scopedTranslators.constEnd(); ++it) {
            const QString cachedKey = it.key();
            if (isSpecializedRussianLocale(cachedKey))
                continue;
            if (cachedKey == language || cachedKey.startsWith(language + QLatin1Char('_')))
                return it.value().data();
        }
        return nullptr;
    }

    QTranslator *ensureScopedTranslator(const QString &localeCode)
    {
        const QString trimmed = localeCode.trimmed();
        if (trimmed.isEmpty())
            return nullptr;

        const QString key = normalizedLocaleKey(trimmed);
        const QHash<QString, QSharedPointer<QTranslator>>::const_iterator it =
                scopedTranslators.constFind(key);
        if (it != scopedTranslators.cend())
            return it.value().data();

        QSharedPointer<QTranslator> translatorPtr(new QTranslator);
        if (loadTranslator(translatorPtr.data(), trimmed)) {
            scopedTranslators.insert(key, translatorPtr);
            return translatorPtr.data();
        }

        return compatibleScopedTranslator(key);
    }

    void retainScopedTranslator(QTranslator *translatorPtr)
    {
        if (!translatorPtr)
            return;

        const int previous = scopedTranslatorRefCounts.value(translatorPtr, 0);
        scopedTranslatorRefCounts.insert(translatorPtr, previous + 1);
        if (previous == 0 && scopedTranslatorSuppressionCounts.value(translatorPtr, 0) == 0) {
            QCoreApplication::installTranslator(translatorPtr);
            if (!scopedTranslatorOrder.contains(translatorPtr))
                scopedTranslatorOrder << translatorPtr;
        }
    }

    void releaseScopedTranslator(QTranslator *translatorPtr)
    {
        if (!translatorPtr)
            return;

        const QHash<QTranslator *, int>::iterator it =
                scopedTranslatorRefCounts.find(translatorPtr);
        if (it == scopedTranslatorRefCounts.end())
            return;

        it.value() -= 1;
        if (it.value() <= 0) {
            scopedTranslatorRefCounts.erase(it);
            if (scopedTranslatorSuppressionCounts.value(translatorPtr, 0) == 0) {
                QCoreApplication::removeTranslator(translatorPtr);
                scopedTranslatorOrder.removeAll(translatorPtr);
            }
        }
    }

    bool hasActiveScopedTranslatorsExcept(QTranslator *translatorPtr) const
    {
        for (QTranslator *installedTranslator : std::as_const(scopedTranslatorOrder)) {
            if (installedTranslator != translatorPtr)
                return true;
        }
        return false;
    }

    QList<QTranslator *> suppressScopedTranslatorsExcept(QTranslator *translatorPtr)
    {
        QList<QTranslator *> suppressedTranslators;
        for (QTranslator *installedTranslator : std::as_const(scopedTranslatorOrder)) {
            if (installedTranslator == translatorPtr)
                continue;

            const int previous = scopedTranslatorSuppressionCounts.value(installedTranslator, 0);
            scopedTranslatorSuppressionCounts.insert(installedTranslator, previous + 1);
            if (previous == 0)
                suppressedTranslators << installedTranslator;
        }

        for (QTranslator *suppressedTranslator : std::as_const(suppressedTranslators)) {
            QCoreApplication::removeTranslator(suppressedTranslator);
            scopedTranslatorOrder.removeAll(suppressedTranslator);
        }

        return suppressedTranslators;
    }

    void restoreScopedTranslators(const QList<QTranslator *> &translators)
    {
        for (QTranslator *translatorPtr : translators) {
            const QHash<QTranslator *, int>::iterator it =
                    scopedTranslatorSuppressionCounts.find(translatorPtr);
            if (it == scopedTranslatorSuppressionCounts.end())
                continue;

            it.value() -= 1;
            if (it.value() > 0)
                continue;

            scopedTranslatorSuppressionCounts.erase(it);
            if (scopedTranslatorRefCounts.value(translatorPtr, 0) <= 0)
                continue;

            QCoreApplication::installTranslator(translatorPtr);
            if (!scopedTranslatorOrder.contains(translatorPtr))
                scopedTranslatorOrder << translatorPtr;
        }
    }
};

BookingResourcesTranslatorPrivate::BookingResourcesTranslatorPrivate(const QString &baseName)
    : baseName(baseName)
{
    // do nothing
}

QStringList BookingResourcesTranslatorPrivate::sanitizeSearchPaths(const QStringList &paths)
{
    QStringList unique;
    unique.reserve(paths.size());
    QSet<QString> seen;

    const auto appendIfNew = [&](const QString &value) {
        if (value.isEmpty() || seen.contains(value))
            return;
        seen.insert(value);
        unique << value;
    };

    for (const QString &path : paths) {
        const QString trimmed = path.trimmed();
        if (trimmed.isEmpty())
            continue;

        if (trimmed.startsWith(QStringLiteral(":/"))) {
            appendIfNew(trimmed);
            continue;
        }

        const QString normalized = QFileInfo(trimmed).absoluteFilePath();
        appendIfNew(normalized);
    }
    return unique;
}

BookingResourcesTranslator::BookingResourcesTranslator(const QString &baseName)
    : d(new BookingResourcesTranslatorPrivate(baseName))
{
    QString appDir;
    if (QCoreApplication::instance()) {
        appDir = QCoreApplication::applicationDirPath();
    } else {
        appDir = QDir::currentPath();
    }

    QStringList defaults;
    defaults << QStringLiteral(":/translations") << appDir + QStringLiteral("/translations")
             << appDir;
    d->searchPaths = BookingResourcesTranslatorPrivate::sanitizeSearchPaths(defaults);
}

BookingResourcesTranslator::~BookingResourcesTranslator()
{
    d->uninstallTranslator();
    delete d;
}

QString BookingResourcesTranslator::baseName() const
{
    return d->baseName;
}

void BookingResourcesTranslator::setSearchPaths(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    d->searchPaths = BookingResourcesTranslatorPrivate::sanitizeSearchPaths(paths);
}

QStringList BookingResourcesTranslator::searchPaths() const
{
    return d->searchPaths;
}

QString BookingResourcesTranslator::resolvePreferredLocale(const QString &preferredLocale)
{
    if (!preferredLocale.trimmed().isEmpty())
        return preferredLocale.trimmed();

    const QByteArray env = qgetenv("BOOKING_BOT_LOCALE");
    if (!env.trimmed().isEmpty())
        return QString::fromUtf8(env);

    return QLocale::system().name();
}

bool BookingResourcesTranslator::initialize(const QString &preferredLocale)
{
    const QString localeName = d->resolveInitialLocale(preferredLocale);

    d->attemptedLocales = d->buildFallbackLocales(localeName);
    for (const QString &code : std::as_const(d->attemptedLocales)) {
        if (code.compare(QStringLiteral("en"), Qt::CaseInsensitive) == 0)
            break;

        if (d->installTranslatorForLocale(code)) {
            d->activeLocale = code;
            return true;
        }
    }

    d->activeLocale.clear();
    qCInfo(lcTranslator) << "Using built-in English text strings.";
    return false;
}

QString BookingResourcesTranslator::activeLocale() const
{
    return d->activeLocale;
}

QStringList BookingResourcesTranslator::attemptedLocales() const
{
    return d->attemptedLocales;
}

BookingResourcesTranslator::ScopedLocaleOverride BookingResourcesTranslator::useLocale(
        const QString &localeCode)
{
    const QString trimmed = localeCode.trimmed();
    if (trimmed.isEmpty())
        return ScopedLocaleOverride();

    const QStringList candidates = d->buildFallbackLocales(trimmed);
    const bool baseTranslatorActive =
            d->translatorInstalled && d->baseTranslatorSuppressionCount == 0;
    const QString activeLocaleKey =
            BookingResourcesTranslatorPrivate::normalizedLocaleKey(d->activeLocale);
    bool baseTranslatorIsFallback = false;
    if (baseTranslatorActive) {
        for (const QString &candidate : std::as_const(candidates)) {
            if (candidate.compare(QStringLiteral("en"), Qt::CaseInsensitive) == 0)
                break;
            if (BookingResourcesTranslatorPrivate::normalizedLocaleKey(candidate)
                == activeLocaleKey) {
                baseTranslatorIsFallback = true;
                break;
            }
        }
    }

    QTranslator *scopedTranslator = nullptr;
    for (const QString &candidate : std::as_const(candidates)) {
        if (candidate.compare(QStringLiteral("en"), Qt::CaseInsensitive) == 0)
            break;

        if (baseTranslatorActive
            && BookingResourcesTranslatorPrivate::normalizedLocaleKey(candidate)
                       == activeLocaleKey) {
            break;
        }

        scopedTranslator = d->ensureScopedTranslator(candidate);
        if (scopedTranslator)
            break;
    }

    const bool suppressBaseTranslator = baseTranslatorActive && !baseTranslatorIsFallback;

    const bool suppressScopedTranslators = d->hasActiveScopedTranslatorsExcept(scopedTranslator);

    if (!scopedTranslator && !suppressBaseTranslator && !suppressScopedTranslators)
        return ScopedLocaleOverride();

    return ScopedLocaleOverride(d,
                                scopedTranslator,
                                suppressBaseTranslator,
                                suppressScopedTranslators);
}

BookingResourcesTranslator::ScopedLocaleOverride::ScopedLocaleOverride()
    : m_private(nullptr)
    , m_translator(nullptr)
    , m_suppressedScopedTranslators()
    , m_suppressBaseTranslator(false)
{
    // do nothing
}

BookingResourcesTranslator::ScopedLocaleOverride::ScopedLocaleOverride(
        BookingResourcesTranslatorPrivate *dPtr,
        QTranslator *translator,
        bool suppressBaseTranslator,
        bool suppressScopedTranslators)
    : m_private(dPtr)
    , m_translator(translator)
    , m_suppressedScopedTranslators()
    , m_suppressBaseTranslator(suppressBaseTranslator)
{
    if (!m_private || (!m_translator && !m_suppressBaseTranslator && !suppressScopedTranslators)) {
        m_private = nullptr;
        m_translator = nullptr;
        m_suppressBaseTranslator = false;
        return;
    }

    if (suppressScopedTranslators) {
        m_suppressedScopedTranslators = m_private->suppressScopedTranslatorsExcept(m_translator);
    }
    if (m_suppressBaseTranslator)
        m_private->suppressBaseTranslator();
    if (m_translator)
        m_private->retainScopedTranslator(m_translator);
}

BookingResourcesTranslator::ScopedLocaleOverride::ScopedLocaleOverride(
        ScopedLocaleOverride &&other) noexcept
    : m_private(other.m_private)
    , m_translator(other.m_translator)
    , m_suppressedScopedTranslators(other.m_suppressedScopedTranslators)
    , m_suppressBaseTranslator(other.m_suppressBaseTranslator)
{
    other.m_private = nullptr;
    other.m_translator = nullptr;
    other.m_suppressedScopedTranslators.clear();
    other.m_suppressBaseTranslator = false;
}

BookingResourcesTranslator::ScopedLocaleOverride &
BookingResourcesTranslator::ScopedLocaleOverride::operator=(ScopedLocaleOverride &&other) noexcept
{
    if (this != &other) {
        reset();
        m_private = other.m_private;
        m_translator = other.m_translator;
        m_suppressedScopedTranslators = other.m_suppressedScopedTranslators;
        m_suppressBaseTranslator = other.m_suppressBaseTranslator;
        other.m_private = nullptr;
        other.m_translator = nullptr;
        other.m_suppressedScopedTranslators.clear();
        other.m_suppressBaseTranslator = false;
    }
    return *this;
}

BookingResourcesTranslator::ScopedLocaleOverride::~ScopedLocaleOverride()
{
    reset();
}

bool BookingResourcesTranslator::ScopedLocaleOverride::isActive() const
{
    return m_private
           && (m_translator || m_suppressBaseTranslator
               || !m_suppressedScopedTranslators.isEmpty());
}

void BookingResourcesTranslator::ScopedLocaleOverride::reset()
{
    if (m_private && m_translator) {
        m_private->releaseScopedTranslator(m_translator);
    }
    if (m_private && m_suppressBaseTranslator) {
        m_private->restoreBaseTranslator();
    }
    if (m_private && !m_suppressedScopedTranslators.isEmpty()) {
        m_private->restoreScopedTranslators(m_suppressedScopedTranslators);
    }
    m_private = nullptr;
    m_translator = nullptr;
    m_suppressedScopedTranslators.clear();
    m_suppressBaseTranslator = false;
}
