#include "LayeredIniSettings.h"

#include <QFileInfo>

namespace {

QString normalizeSettingsFilePath(const QString &filePath)
{
    const QString trimmed = filePath.trimmed();
    if (trimmed.isEmpty())
        return QString();

    return QFileInfo(trimmed).absoluteFilePath();
}

LayeredIniSettings::LookupResult defaultLookupResult(const QStringList &keys,
                                                     const QVariant &defaultValue)
{
    LayeredIniSettings::LookupResult result;
    result.value = defaultValue;
    result.key = keys.isEmpty() ? QString() : keys.constFirst();
    result.fileName = QString();
    result.source = LayeredIniSettings::ValueSource::DefaultValue;
    return result;
}

} // namespace

LayeredIniSettings::LayeredIniSettings(const QString &organization,
                                       const QString &application,
                                       const QString &explicitFilePath)
    : m_userSettings(QSettings::IniFormat, QSettings::UserScope, organization, application)
    , m_systemSettings(QSettings::IniFormat, QSettings::SystemScope, organization, application)
    , m_explicitSettings()
    , m_writableSettings(&m_userSettings)
{
    m_userSettings.setFallbacksEnabled(false);
    m_systemSettings.setFallbacksEnabled(false);

    const QString normalizedExplicitPath = normalizeSettingsFilePath(explicitFilePath);
    if (!normalizedExplicitPath.isEmpty()) {
        m_explicitSettings.reset(new QSettings(normalizedExplicitPath, QSettings::IniFormat));
        m_explicitSettings->setFallbacksEnabled(false);
        m_writableSettings = m_explicitSettings.data();
    }
}

LayeredIniSettings::~LayeredIniSettings()
{
    // do nothing
}

QVariant LayeredIniSettings::value(const QString &key, const QVariant &defaultValue) const
{
    return lookup(key, defaultValue).value;
}

LayeredIniSettings::LookupResult LayeredIniSettings::lookup(const QString &key,
                                                            const QVariant &defaultValue) const
{
    return lookupAny({ key }, defaultValue);
}

LayeredIniSettings::LookupResult LayeredIniSettings::lookupAny(const QStringList &keys,
                                                               const QVariant &defaultValue) const
{
    if (m_explicitSettings) {
        const LookupResult explicitResult =
                lookupInSettings(*m_explicitSettings, ValueSource::ExplicitFile, keys);
        if (!explicitResult.key.isEmpty())
            return explicitResult;
    }

    const LookupResult userResult = lookupInSettings(m_userSettings, ValueSource::UserScope, keys);
    if (!userResult.key.isEmpty())
        return userResult;

    const LookupResult systemResult =
            lookupInSettings(m_systemSettings, ValueSource::SystemScope, keys);
    if (!systemResult.key.isEmpty())
        return systemResult;

    return defaultLookupResult(keys, defaultValue);
}

void LayeredIniSettings::setValue(const QString &key, const QVariant &value)
{
    if (!m_writableSettings)
        return;

    m_writableSettings->setValue(key, value);
}

void LayeredIniSettings::sync()
{
    if (!m_writableSettings)
        return;

    m_writableSettings->sync();
}

QString LayeredIniSettings::fileName() const
{
    return m_writableSettings ? m_writableSettings->fileName() : QString();
}

QString LayeredIniSettings::userScopeFileName() const
{
    return m_userSettings.fileName();
}

QString LayeredIniSettings::systemScopeFileName() const
{
    return m_systemSettings.fileName();
}

QString LayeredIniSettings::explicitFileName() const
{
    return m_explicitSettings ? m_explicitSettings->fileName() : QString();
}

bool LayeredIniSettings::hasExplicitFile() const
{
    return !explicitFileName().isEmpty();
}

QString LayeredIniSettings::sourceName(ValueSource source)
{
    switch (source) {
    case ValueSource::ExplicitFile:
        return QStringLiteral("explicit settings file");
    case ValueSource::UserScope:
        return QStringLiteral("user-scope settings file");
    case ValueSource::SystemScope:
        return QStringLiteral("system-scope settings file");
    case ValueSource::DefaultValue:
        break;
    }

    return QStringLiteral("default value");
}

LayeredIniSettings::LookupResult LayeredIniSettings::lookupInSettings(const QSettings &settings,
                                                                      ValueSource source,
                                                                      const QStringList &keys) const
{
    for (const QString &key : keys) {
        if (!settings.contains(key))
            continue;

        LookupResult result;
        result.value = settings.value(key);
        result.key = key;
        result.fileName = settings.fileName();
        result.source = source;
        return result;
    }

    return defaultLookupResult({}, QVariant());
}
