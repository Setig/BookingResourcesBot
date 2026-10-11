#include "WebAuthManager.h"

#include "WebPasswordHasher.h"

#include <QUuid>
#include <QJsonObject>
#include <QRegularExpression>

namespace {

bool containsControlCharacters(const QString &value)
{
    for (const QChar ch : value) {
        if (ch.category() == QChar::Other_Control)
            return true;
    }
    return false;
}

bool validateTextField(const QString &value, int maxLength, QString *errorOut, const QString &label)
{
    if (value.size() > maxLength) {
        if (errorOut)
            *errorOut = WebAuthManager::tr("%1 is too long.").arg(label);
        return false;
    }
    if (containsControlCharacters(value)) {
        if (errorOut)
            *errorOut = WebAuthManager::tr("%1 must not contain control characters.").arg(label);
        return false;
    }
    return true;
}

bool validateLocale(const QString &locale, QString *errorOut)
{
    static const QRegularExpression pattern(
            QStringLiteral("^[A-Za-z]{2,3}([_-][A-Za-z0-9]{2,8})?$"));
    if (locale.size() > 32 || !pattern.match(locale).hasMatch()) {
        if (errorOut)
            *errorOut = WebAuthManager::tr("Locale is invalid.");
        return false;
    }
    return true;
}

bool validateLogin(const QString &login, QString *errorOut)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_.@+-]{3,128}$"));
    if (!pattern.match(login).hasMatch()) {
        if (errorOut) {
            *errorOut = WebAuthManager::tr(
                    "Login must be 3-128 characters and contain only letters, digits, dot, "
                    "underscore, plus, at sign, or hyphen.");
        }
        return false;
    }
    return true;
}

bool validatePassword(const QString &password, QString *errorOut, const QString &label)
{
    if (password.size() < 8) {
        if (errorOut)
            *errorOut = WebAuthManager::tr("%1 must contain at least 8 characters.").arg(label);
        return false;
    }
    if (password.size() > 1024) {
        if (errorOut)
            *errorOut = WebAuthManager::tr("%1 is too long.").arg(label);
        return false;
    }
    if (containsControlCharacters(password)) {
        if (errorOut)
            *errorOut = WebAuthManager::tr("%1 must not contain control characters.").arg(label);
        return false;
    }
    return true;
}

QString generateUserId(WebSessionStore *store)
{
    if (!store)
        return QString();

    for (int attempt = 0; attempt < 16; ++attempt) {
        const QString candidate = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!store->user(candidate).isValid())
            return candidate;
    }
    return QString();
}

bool fillProfile(const QJsonObject &request,
                 WebSessionStore::User *user,
                 QString *errorOut,
                 const QString &userId)
{
    if (!user)
        return false;

    const QString firstName = request.value(QStringLiteral("first_name")).toString().trimmed();
    if (firstName.isEmpty()) {
        if (errorOut)
            *errorOut = WebAuthManager::tr("First name must not be empty.");
        return false;
    }
    const QString lastName = request.value(QStringLiteral("last_name")).toString().trimmed();
    QString locale =
            request.value(QStringLiteral("locale")).toString(QStringLiteral("en")).trimmed();
    if (locale.isEmpty())
        locale = QStringLiteral("en");

    if (!validateTextField(firstName, 80, errorOut, WebAuthManager::tr("First name"))
        || !validateTextField(lastName, 80, errorOut, WebAuthManager::tr("Last name"))
        || !validateLocale(locale, errorOut)) {
        return false;
    }

    user->userId = userId;
    user->firstName = firstName;
    user->lastName = lastName;
    user->locale = locale;
    return true;
}

} // namespace

WebAuthManager::WebAuthManager(WebSessionStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
    // do nothing
}

WebAuthManager::~WebAuthManager()
{
    // do nothing
}

WebAuthManager::AuthResult WebAuthManager::authorizeUser(const QJsonObject &request)
{
    AuthResult result;
    if (!m_store) {
        result.error = tr("Web authentication is not initialized.");
        return result;
    }

    const QString userId = generateUserId(m_store);

    if (userId.isEmpty()) {
        result.error = tr("Unable to allocate web user id.");
        return result;
    }

    WebSessionStore::User user;
    if (!fillProfile(request, &user, &result.error, userId))
        return result;
    user.provider = QStringLiteral("authorization");
    user.externalSubject = user.userId;

    if (!m_store->upsertUser(user)) {
        result.error = m_store->lastError();
        return result;
    }

    const QString sessionToken = m_store->createSession(user.userId);
    if (sessionToken.isEmpty()) {
        result.error = m_store->lastError();
        return result;
    }

    result.ok = true;
    result.userId = user.userId;
    result.sessionToken = sessionToken;
    return result;
}

WebAuthManager::AuthResult WebAuthManager::registerLocalPasswordUser(const QJsonObject &request)
{
    AuthResult result;
    if (!m_store) {
        result.error = tr("Web authentication is not initialized.");
        return result;
    }

    const QString login = request.value(QStringLiteral("login")).toString().trimmed();
    const QString password = request.value(QStringLiteral("password")).toString();
    if (!validateLogin(login, &result.error)
        || !validatePassword(password, &result.error, tr("Password"))) {
        return result;
    }
    if (m_store->credentialByLogin(login).isValid()) {
        result.error = tr("Login is already used.");
        return result;
    }

    const QString userId = generateUserId(m_store);
    if (userId.isEmpty()) {
        result.error = tr("Unable to allocate web user id.");
        return result;
    }

    WebSessionStore::User user;
    if (!fillProfile(request, &user, &result.error, userId))
        return result;
    user.provider = QStringLiteral("local-password");
    user.externalSubject = login;

    if (!m_store->upsertUser(user)) {
        result.error = m_store->lastError();
        return result;
    }

    WebSessionStore::Credential credential;
    credential.login = login;
    credential.userId = user.userId;
    credential.passwordHash = WebPasswordHasher::hashPassword(password);
    if (!m_store->upsertCredential(credential)) {
        result.error = m_store->lastError();
        return result;
    }

    const QString sessionToken = m_store->createSession(user.userId);
    if (sessionToken.isEmpty()) {
        result.error = m_store->lastError();
        return result;
    }

    result.ok = true;
    result.userId = user.userId;
    result.sessionToken = sessionToken;
    return result;
}

WebAuthManager::AuthResult WebAuthManager::loginLocalPasswordUser(const QJsonObject &request)
{
    AuthResult result;
    if (!m_store) {
        result.error = tr("Web authentication is not initialized.");
        return result;
    }

    const QString login = request.value(QStringLiteral("login")).toString().trimmed();
    const QString password = request.value(QStringLiteral("password")).toString();
    if (!validateLogin(login, &result.error)
        || !validatePassword(password, &result.error, tr("Password"))) {
        result.error = tr("Login or password is invalid.");
        return result;
    }

    const WebSessionStore::Credential credential = m_store->credentialByLogin(login);
    if (!credential.isValid() || WebPasswordHasher::isPendingPasswordHash(credential.passwordHash)
        || !WebPasswordHasher::verifyPassword(password, credential.passwordHash)) {
        result.error = tr("Login or password is invalid.");
        return result;
    }

    const WebSessionStore::User user = m_store->user(credential.userId);
    if (!user.isValid()) {
        result.error = tr("Web user was not found.");
        return result;
    }

    const QString sessionToken = m_store->createSession(user.userId);
    if (sessionToken.isEmpty()) {
        result.error = m_store->lastError();
        return result;
    }

    result.ok = true;
    result.userId = user.userId;
    result.sessionToken = sessionToken;
    return result;
}

WebAuthManager::AuthResult WebAuthManager::changeLocalPassword(const QString &userId,
                                                               const QJsonObject &request)
{
    AuthResult result;
    if (!m_store) {
        result.error = tr("Web authentication is not initialized.");
        return result;
    }

    const WebSessionStore::User user = m_store->user(userId);
    if (!user.isValid()) {
        result.error = tr("Web user was not found.");
        return result;
    }

    const WebSessionStore::Credential current = m_store->credentialForUser(user.userId);
    if (!current.isValid()) {
        result.error = tr("Account password is not configured.");
        return result;
    }

    const QString currentPassword = request.value(QStringLiteral("current_password")).toString();
    const QString newPassword = request.value(QStringLiteral("new_password")).toString();

    if (!validatePassword(newPassword, &result.error, tr("New password")))
        return result;

    if (!WebPasswordHasher::verifyPassword(currentPassword, current.passwordHash)) {
        result.error = tr("Current password is invalid.");
        return result;
    }

    WebSessionStore::Credential updated;
    updated.login = current.login;
    updated.userId = user.userId;
    updated.passwordHash = WebPasswordHasher::hashPassword(newPassword);
    if (!m_store->upsertCredential(updated)) {
        result.error = m_store->lastError();
        return result;
    }

    result.ok = true;
    result.userId = user.userId;
    return result;
}

WebAuthManager::AuthResult WebAuthManager::addLocalPasswordUserWithoutPassword(
        const QJsonObject &request)
{
    AuthResult result;
    if (!m_store) {
        result.error = tr("Web authentication is not initialized.");
        return result;
    }

    const QString login = request.value(QStringLiteral("login")).toString().trimmed();
    if (!validateLogin(login, &result.error))
        return result;
    if (m_store->credentialByLogin(login).isValid()) {
        result.error = tr("Login is already used.");
        return result;
    }

    const QString userId = generateUserId(m_store);
    if (userId.isEmpty()) {
        result.error = tr("Unable to allocate web user id.");
        return result;
    }

    WebSessionStore::User user;
    if (!fillProfile(request, &user, &result.error, userId))
        return result;
    user.provider = QStringLiteral("local-password");
    user.externalSubject = login;

    if (!m_store->upsertUser(user)) {
        result.error = m_store->lastError();
        return result;
    }

    WebSessionStore::Credential credential;
    credential.login = login;
    credential.userId = user.userId;
    credential.passwordHash = WebPasswordHasher::pendingPasswordHash();
    if (!m_store->upsertCredential(credential)) {
        result.error = m_store->lastError();
        return result;
    }

    const QString token =
            m_store->createPasswordResetToken(user.userId, QStringLiteral("set_password"));
    if (token.isEmpty()) {
        result.error = m_store->lastError();
        return result;
    }

    result.ok = true;
    result.userId = user.userId;
    result.sessionToken = token;
    return result;
}

WebAuthManager::AuthResult WebAuthManager::attachLocalPasswordLogin(const QString &userId,
                                                                    const QString &login)
{
    AuthResult result;
    if (!m_store) {
        result.error = tr("Web authentication is not initialized.");
        return result;
    }

    const WebSessionStore::User existing = m_store->user(userId);
    if (!existing.isValid()) {
        result.error = tr("Web user was not found.");
        return result;
    }

    if (m_store->credentialForUser(existing.userId).isValid()) {
        result.error = tr("Account password is already configured.");
        return result;
    }

    const QString trimmedLogin = login.trimmed();
    if (!validateLogin(trimmedLogin, &result.error))
        return result;
    if (m_store->credentialByLogin(trimmedLogin).isValid()) {
        result.error = tr("Login is already used.");
        return result;
    }

    WebSessionStore::User updated = existing;
    updated.provider = QStringLiteral("local-password");
    updated.externalSubject = trimmedLogin;
    if (!m_store->upsertUser(updated)) {
        result.error = m_store->lastError();
        return result;
    }

    WebSessionStore::Credential credential;
    credential.login = trimmedLogin;
    credential.userId = existing.userId;
    credential.passwordHash = WebPasswordHasher::pendingPasswordHash();
    if (!m_store->upsertCredential(credential)) {
        result.error = m_store->lastError();
        return result;
    }

    const QString token =
            m_store->createPasswordResetToken(existing.userId, QStringLiteral("set_password"));
    if (token.isEmpty()) {
        result.error = m_store->lastError();
        return result;
    }

    result.ok = true;
    result.userId = existing.userId;
    result.sessionToken = token;
    return result;
}

WebAuthManager::AuthResult WebAuthManager::issuePasswordToken(const QString &userId,
                                                              const QString &purpose)
{
    AuthResult result;
    if (!m_store) {
        result.error = tr("Web authentication is not initialized.");
        return result;
    }

    const WebSessionStore::User user = m_store->user(userId);
    if (!user.isValid()) {
        result.error = tr("Web user was not found.");
        return result;
    }

    const WebSessionStore::Credential credential = m_store->credentialForUser(user.userId);
    if (!credential.isValid()) {
        result.error = tr("Account password is not configured.");
        return result;
    }

    const QString trimmedPurpose = purpose.trimmed();
    const QString token = m_store->createPasswordResetToken(user.userId, trimmedPurpose);
    if (token.isEmpty()) {
        result.error = m_store->lastError();
        return result;
    }

    if (trimmedPurpose == QLatin1String("reset_password")
        && !WebPasswordHasher::isPendingPasswordHash(credential.passwordHash)) {
        WebSessionStore::Credential updated = credential;
        updated.passwordHash = WebPasswordHasher::pendingPasswordHash();
        if (!m_store->upsertCredential(updated)) {
            result.error = m_store->lastError();
            return result;
        }
    }

    result.ok = true;
    result.userId = user.userId;
    result.sessionToken = token;
    return result;
}

WebAuthManager::AuthResult WebAuthManager::setPasswordByToken(const QJsonObject &request,
                                                              const QString &purpose)
{
    AuthResult result;
    if (!m_store) {
        result.error = tr("Web authentication is not initialized.");
        return result;
    }

    const QString token = request.value(QStringLiteral("token")).toString().trimmed();
    const QString newPassword = request.value(QStringLiteral("new_password")).toString();
    if (token.isEmpty()) {
        result.error = tr("Password link is invalid or has expired.");
        return result;
    }
    if (!validatePassword(newPassword, &result.error, tr("New password")))
        return result;

    QString userId;
    if (!m_store->consumePasswordResetToken(token, purpose, &userId) || userId.isEmpty()) {
        result.error = tr("Password link is invalid or has expired.");
        return result;
    }

    const WebSessionStore::User user = m_store->user(userId);
    const WebSessionStore::Credential current = m_store->credentialForUser(userId);
    if (!user.isValid() || !current.isValid()) {
        result.error = tr("Web user was not found.");
        return result;
    }

    WebSessionStore::Credential updated;
    updated.login = current.login;
    updated.userId = user.userId;
    updated.passwordHash = WebPasswordHasher::hashPassword(newPassword);
    if (!m_store->upsertCredential(updated)) {
        result.error = m_store->lastError();
        return result;
    }

    result.ok = true;
    result.userId = user.userId;
    return result;
}

bool WebAuthManager::deleteUser(const QString &userId)
{
    return m_store && m_store->deleteUser(userId);
}

QString WebAuthManager::resolveSession(const QString &sessionToken) const
{
    return m_store ? m_store->resolveSession(sessionToken) : QString();
}

WebSessionStore::User WebAuthManager::user(const QString &userId) const
{
    return m_store ? m_store->user(userId) : WebSessionStore::User();
}
