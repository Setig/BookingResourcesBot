#ifndef BOOKINGRESOURCES_TRANSLATOR_H
#define BOOKINGRESOURCES_TRANSLATOR_H

#include <QList>
#include <QString>
#include <QStringList>

class QTranslator;

class BookingResourcesTranslatorPrivate;

/**
 * @brief Helper responsible for loading and installing Qt translations.
 *
 * Encapsulates translator search paths, locale selection (including environment overrides),
 * and convenient fallback handling.
 */
class BookingResourcesTranslator final
{
public:
    /**
     * @brief RAII helper that temporarily installs translations for a locale.
     *
     * Instances are returned by useLocale() and uninstall the scoped translator on destruction.
     */
    class ScopedLocaleOverride
    {
    public:
        ScopedLocaleOverride();
        explicit ScopedLocaleOverride(ScopedLocaleOverride &&other) noexcept;

        ScopedLocaleOverride &operator=(ScopedLocaleOverride &&other) noexcept;
        ~ScopedLocaleOverride();

        [[nodiscard]] bool isActive() const;

    private:
        friend class BookingResourcesTranslator;
        ScopedLocaleOverride(BookingResourcesTranslatorPrivate *d,
                             QTranslator *translator,
                             bool suppressBaseTranslator,
                             bool suppressScopedTranslators);

        void reset();

        BookingResourcesTranslatorPrivate *m_private;
        QTranslator *m_translator;
        QList<QTranslator *> m_suppressedScopedTranslators;
        bool m_suppressBaseTranslator;
    };

    explicit BookingResourcesTranslator(const QString &baseName);
    ~BookingResourcesTranslator();

    QString baseName() const;

    void setSearchPaths(const QStringList &paths);
    QStringList searchPaths() const;

    /**
     * @brief Installs translations for the requested locale.
     *
     * @param preferredLocale Direct locale override (e.g. "ru_RU").
     *                        If empty, BOOKING_BOT_LOCALE is checked first and
     *                        the system locale is used as the final fallback.
     *                        English strings are kept when no translation loads.
     * @return true if a translator was installed, false if English fallback is used.
     */
    bool initialize(const QString &preferredLocale = QString());

    /**
     * @brief Resolves the preferred locale without installing a translator.
     *
     * Priority: @p preferredLocale, then BOOKING_BOT_LOCALE, then the system locale.
     */
    [[nodiscard]] static QString resolvePreferredLocale(const QString &preferredLocale = QString());

    QString activeLocale() const;
    QStringList attemptedLocales() const;

    [[nodiscard]] ScopedLocaleOverride useLocale(const QString &localeCode);

private:
    BookingResourcesTranslatorPrivate *d;
};

#endif // BOOKINGRESOURCES_TRANSLATOR_H
