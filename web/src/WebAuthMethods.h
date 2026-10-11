#ifndef WEBAUTHMETHODS_H
#define WEBAUTHMETHODS_H

#include <QSet>
#include <QString>
#include <QJsonArray>
#include <QStringList>

namespace WebAuthMethods {

inline QString signWithoutRegistration()
{
    return QStringLiteral("sign-without-registration");
}

inline QString logInOnly()
{
    return QStringLiteral("log-in-only");
}

inline QString logInWithRegistration()
{
    return QStringLiteral("log-in-with-registration");
}

inline QStringList defaults()
{
    return { signWithoutRegistration() };
}

inline QString normalize(const QString &method)
{
    const QString normalized = method.trimmed().toLower().replace(QLatin1Char('_'), QLatin1Char('-'));
    if (normalized == signWithoutRegistration() || normalized == QLatin1String("web-profile"))
        return signWithoutRegistration();
    if (normalized == logInWithRegistration() || normalized == QLatin1String("local-password"))
        return logInWithRegistration();
    if (normalized == logInOnly())
        return logInOnly();
    return QString();
}

inline QStringList normalizeList(const QStringList &values)
{
    QStringList result;
    QSet<QString> seen;
    for (const QString &value : values) {
        const QStringList parts = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            const QString method = normalize(part);
            if (!method.isEmpty() && !seen.contains(method)) {
                seen.insert(method);
                result.append(method);
            }
        }
    }
    if (seen.contains(logInOnly()) && seen.contains(logInWithRegistration()))
        result.removeAll(logInOnly());
    return result.isEmpty() ? defaults() : result;
}

inline bool contains(const QStringList &methods, const QString &method)
{
    return normalizeList(methods).contains(normalize(method));
}

inline bool allowsSignWithoutRegistration(const QStringList &methods)
{
    return contains(methods, signWithoutRegistration());
}

inline bool allowsLogin(const QStringList &methods)
{
    const QStringList normalized = normalizeList(methods);
    return normalized.contains(logInOnly()) || normalized.contains(logInWithRegistration());
}

inline bool allowsRegistration(const QStringList &methods)
{
    return contains(methods, logInWithRegistration());
}

inline bool allowsLocalCredentials(const QStringList &methods)
{
    return allowsLogin(methods);
}

inline QJsonArray toJsonArray(const QStringList &methods)
{
    QJsonArray result;
    const QStringList normalized = normalizeList(methods);
    for (const QString &method : normalized)
        result.append(method);
    return result;
}

} // namespace WebAuthMethods

#endif // WEBAUTHMETHODS_H
