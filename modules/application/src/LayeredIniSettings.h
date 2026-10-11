#ifndef LAYEREDINISETTINGS_H
#define LAYEREDINISETTINGS_H

#include <QScopedPointer>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariant>

/**
 * @brief Layered INI settings wrapper with explicit, user, and system sources.
 *
 * The wrapper keeps fallback lookup order deterministic and exposes the source
 * file used for each resolved key. It uses standalone QSettings instances with
 * Qt fallbacks disabled and applies the fallback order explicitly:
 * explicit file -> user scope -> system scope.
 */
class LayeredIniSettings final
{
public:
    enum class ValueSource {
        ExplicitFile,
        UserScope,
        SystemScope,
        DefaultValue,
    };

    struct LookupResult
    {
        QVariant value;
        QString key;
        QString fileName;
        ValueSource source;
    };

    LayeredIniSettings(const QString &organization,
                       const QString &application,
                       const QString &explicitFilePath = QString());
    ~LayeredIniSettings();

    QVariant value(const QString &key, const QVariant &defaultValue = QVariant()) const;
    LookupResult lookup(const QString &key, const QVariant &defaultValue = QVariant()) const;
    LookupResult lookupAny(const QStringList &keys,
                           const QVariant &defaultValue = QVariant()) const;

    void setValue(const QString &key, const QVariant &value);
    void sync();

    QString fileName() const;
    QString userScopeFileName() const;
    QString systemScopeFileName() const;
    QString explicitFileName() const;
    bool hasExplicitFile() const;

    static QString sourceName(ValueSource source);

private:
    LookupResult lookupInSettings(const QSettings &settings,
                                  ValueSource source,
                                  const QStringList &keys) const;

    QSettings m_userSettings;
    QSettings m_systemSettings;
    QScopedPointer<QSettings> m_explicitSettings;
    QSettings *m_writableSettings;
};

#endif // LAYEREDINISETTINGS_H
