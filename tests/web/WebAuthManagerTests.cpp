#include <QtTest/QtTest>
#include "TestRegistry.h"

#include "WebAuthManager.h"
#include "WebPasswordHasher.h"
#include "WebSessionStore.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

class WebAuthManagerTests : public QObject
{
    Q_OBJECT

private slots:
    void acceptsValidProfile();
    void rejectsEmptyFirstName();
    void rejectsLongProfileFields();
    void rejectsInvalidLocale();
    void registersAndLogsInLocalPasswordUser();
    void rejectsLoginWithPendingPassword();
    void attachesLoginToWebProfile();
    void setsAndResetsPasswordByToken();
    void rejectsExpiredAndReusedPasswordTokens();
    void rejectsOccupiedLogin();
    void changesLocalPassword();
};

namespace {

bool expirePasswordResets(const QString &storageFile)
{
    const QString name = QStringLiteral("auth-expire-%1")
                                 .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(storageFile);
    if (!db.open()) {
        QSqlDatabase::removeDatabase(name);
        return false;
    }

    QSqlQuery query(db);
    const bool ok = query.exec(QStringLiteral(
            "UPDATE web_password_resets SET expires_at_utc = '2000-01-01T00:00:00.000Z'"));
    db.close();
    QSqlDatabase::removeDatabase(name);
    return ok;
}

} // namespace

void WebAuthManagerTests::acceptsValidProfile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    WebSessionStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("web.sqlite"))));
    WebAuthManager manager(&store);

    QJsonObject request;
    request.insert(QStringLiteral("first_name"), QStringLiteral("Alice"));
    request.insert(QStringLiteral("last_name"), QStringLiteral("Example"));
    request.insert(QStringLiteral("locale"), QStringLiteral("en"));

    const WebAuthManager::AuthResult result = manager.authorizeUser(request);
    QVERIFY(result.ok);
    QVERIFY(!result.userId.isEmpty());
    QVERIFY(!result.sessionToken.isEmpty());
    QCOMPARE(store.resolveSession(result.sessionToken), result.userId);
}

void WebAuthManagerTests::rejectsEmptyFirstName()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    WebSessionStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("web.sqlite"))));
    WebAuthManager manager(&store);

    QJsonObject request;
    request.insert(QStringLiteral("first_name"), QString());

    const WebAuthManager::AuthResult result = manager.authorizeUser(request);
    QVERIFY(!result.ok);
    QVERIFY(result.userId.isEmpty());
    QVERIFY(result.sessionToken.isEmpty());
}

void WebAuthManagerTests::rejectsLongProfileFields()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    WebSessionStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("web.sqlite"))));
    WebAuthManager manager(&store);

    QJsonObject request;
    request.insert(QStringLiteral("first_name"), QString(81, QLatin1Char('a')));
    request.insert(QStringLiteral("locale"), QStringLiteral("en"));

    const WebAuthManager::AuthResult result = manager.authorizeUser(request);
    QVERIFY(!result.ok);
}

void WebAuthManagerTests::rejectsInvalidLocale()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    WebSessionStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("web.sqlite"))));
    WebAuthManager manager(&store);

    QJsonObject request;
    request.insert(QStringLiteral("first_name"), QStringLiteral("Alice"));
    request.insert(QStringLiteral("locale"), QStringLiteral("../ru"));

    const WebAuthManager::AuthResult result = manager.authorizeUser(request);
    QVERIFY(!result.ok);
}

void WebAuthManagerTests::registersAndLogsInLocalPasswordUser()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    WebSessionStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("web.sqlite"))));
    WebAuthManager manager(&store);

    QJsonObject request;
    request.insert(QStringLiteral("login"), QStringLiteral("alice"));
    request.insert(QStringLiteral("password"), QStringLiteral("password1"));
    request.insert(QStringLiteral("first_name"), QStringLiteral("Alice"));
    request.insert(QStringLiteral("locale"), QStringLiteral("en"));

    const WebAuthManager::AuthResult created = manager.registerLocalPasswordUser(request);
    QVERIFY(created.ok);
    QVERIFY(!created.userId.isEmpty());
    QVERIFY(!created.sessionToken.isEmpty());

    QJsonObject login;
    login.insert(QStringLiteral("login"), QStringLiteral("alice"));
    login.insert(QStringLiteral("password"), QStringLiteral("password1"));
    const WebAuthManager::AuthResult loggedIn = manager.loginLocalPasswordUser(login);
    QVERIFY(loggedIn.ok);
    QCOMPARE(loggedIn.userId, created.userId);
}

void WebAuthManagerTests::rejectsLoginWithPendingPassword()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    WebSessionStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("web.sqlite"))));
    WebAuthManager manager(&store);

    QJsonObject request;
    request.insert(QStringLiteral("login"), QStringLiteral("pending"));
    request.insert(QStringLiteral("first_name"), QStringLiteral("Pat"));
    const WebAuthManager::AuthResult created = manager.addLocalPasswordUserWithoutPassword(request);
    QVERIFY(created.ok);
    QVERIFY(WebPasswordHasher::isPendingPasswordHash(
            store.credentialForUser(created.userId).passwordHash));

    QJsonObject login;
    login.insert(QStringLiteral("login"), QStringLiteral("pending"));
    login.insert(QStringLiteral("password"), QStringLiteral("password1"));
    const WebAuthManager::AuthResult loggedIn = manager.loginLocalPasswordUser(login);
    QVERIFY(!loggedIn.ok);
}

void WebAuthManagerTests::attachesLoginToWebProfile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    WebSessionStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("web.sqlite"))));
    WebAuthManager manager(&store);

    QJsonObject profile;
    profile.insert(QStringLiteral("first_name"), QStringLiteral("Web"));
    profile.insert(QStringLiteral("locale"), QStringLiteral("en"));
    const WebAuthManager::AuthResult authorized = manager.authorizeUser(profile);
    QVERIFY(authorized.ok);

    const WebAuthManager::AuthResult attached =
            manager.attachLocalPasswordLogin(authorized.userId, QStringLiteral("webuser"));
    QVERIFY(attached.ok);
    QCOMPARE(attached.userId, authorized.userId);
    QCOMPARE(store.user(authorized.userId).provider, QStringLiteral("local-password"));
    QCOMPARE(store.user(authorized.userId).externalSubject, QStringLiteral("webuser"));
    QCOMPARE(store.credentialForUser(authorized.userId).login, QStringLiteral("webuser"));
}

void WebAuthManagerTests::setsAndResetsPasswordByToken()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    WebSessionStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("web.sqlite"))));
    WebAuthManager manager(&store);

    QJsonObject request;
    request.insert(QStringLiteral("login"), QStringLiteral("setuser"));
    request.insert(QStringLiteral("first_name"), QStringLiteral("Set"));
    const WebAuthManager::AuthResult created = manager.addLocalPasswordUserWithoutPassword(request);
    QVERIFY(created.ok);

    QJsonObject setBody;
    setBody.insert(QStringLiteral("token"), created.sessionToken);
    setBody.insert(QStringLiteral("new_password"), QStringLiteral("password1"));
    const WebAuthManager::AuthResult setResult =
            manager.setPasswordByToken(setBody, QStringLiteral("set_password"));
    QVERIFY(setResult.ok);

    QJsonObject login;
    login.insert(QStringLiteral("login"), QStringLiteral("setuser"));
    login.insert(QStringLiteral("password"), QStringLiteral("password1"));
    QVERIFY(manager.loginLocalPasswordUser(login).ok);

    const WebAuthManager::AuthResult resetToken =
            manager.issuePasswordToken(created.userId, QStringLiteral("reset_password"));
    QVERIFY(resetToken.ok);
    QVERIFY(WebPasswordHasher::isPendingPasswordHash(
            store.credentialForUser(created.userId).passwordHash));
    QVERIFY(!manager.loginLocalPasswordUser(login).ok);

    QJsonObject resetBody;
    resetBody.insert(QStringLiteral("token"), resetToken.sessionToken);
    resetBody.insert(QStringLiteral("new_password"), QStringLiteral("password2"));
    QVERIFY(manager.setPasswordByToken(resetBody, QStringLiteral("reset_password")).ok);

    login.insert(QStringLiteral("password"), QStringLiteral("password2"));
    QVERIFY(manager.loginLocalPasswordUser(login).ok);
}

void WebAuthManagerTests::rejectsExpiredAndReusedPasswordTokens()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString storageFile = dir.filePath(QStringLiteral("web.sqlite"));

    WebSessionStore store;
    QVERIFY(store.open(storageFile));
    WebAuthManager manager(&store);

    QJsonObject request;
    request.insert(QStringLiteral("login"), QStringLiteral("tokenuser"));
    request.insert(QStringLiteral("first_name"), QStringLiteral("Tok"));
    const WebAuthManager::AuthResult created = manager.addLocalPasswordUserWithoutPassword(request);
    QVERIFY(created.ok);

    QVERIFY(expirePasswordResets(storageFile));
    QVERIFY(store.open(storageFile));

    QJsonObject expiredBody;
    expiredBody.insert(QStringLiteral("token"), created.sessionToken);
    expiredBody.insert(QStringLiteral("new_password"), QStringLiteral("password1"));
    QVERIFY(!manager.setPasswordByToken(expiredBody, QStringLiteral("set_password")).ok);

    const WebAuthManager::AuthResult fresh =
            manager.issuePasswordToken(created.userId, QStringLiteral("set_password"));
    QVERIFY(fresh.ok);

    QJsonObject setBody;
    setBody.insert(QStringLiteral("token"), fresh.sessionToken);
    setBody.insert(QStringLiteral("new_password"), QStringLiteral("password1"));
    QVERIFY(manager.setPasswordByToken(setBody, QStringLiteral("set_password")).ok);
    QVERIFY(!manager.setPasswordByToken(setBody, QStringLiteral("set_password")).ok);
}

void WebAuthManagerTests::rejectsOccupiedLogin()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    WebSessionStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("web.sqlite"))));
    WebAuthManager manager(&store);

    QJsonObject first;
    first.insert(QStringLiteral("login"), QStringLiteral("taken"));
    first.insert(QStringLiteral("first_name"), QStringLiteral("One"));
    QVERIFY(manager.addLocalPasswordUserWithoutPassword(first).ok);

    QJsonObject second;
    second.insert(QStringLiteral("login"), QStringLiteral("taken"));
    second.insert(QStringLiteral("first_name"), QStringLiteral("Two"));
    QVERIFY(!manager.addLocalPasswordUserWithoutPassword(second).ok);

    QJsonObject profile;
    profile.insert(QStringLiteral("first_name"), QStringLiteral("Web"));
    const WebAuthManager::AuthResult authorized = manager.authorizeUser(profile);
    QVERIFY(authorized.ok);
    QVERIFY(!manager.attachLocalPasswordLogin(authorized.userId, QStringLiteral("taken")).ok);
}

void WebAuthManagerTests::changesLocalPassword()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    WebSessionStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("web.sqlite"))));
    WebAuthManager manager(&store);

    QJsonObject request;
    request.insert(QStringLiteral("login"), QStringLiteral("changer"));
    request.insert(QStringLiteral("password"), QStringLiteral("password1"));
    request.insert(QStringLiteral("first_name"), QStringLiteral("Chang"));
    const WebAuthManager::AuthResult created = manager.registerLocalPasswordUser(request);
    QVERIFY(created.ok);

    QJsonObject change;
    change.insert(QStringLiteral("current_password"), QStringLiteral("password1"));
    change.insert(QStringLiteral("new_password"), QStringLiteral("password2"));
    QVERIFY(manager.changeLocalPassword(created.userId, change).ok);

    QJsonObject login;
    login.insert(QStringLiteral("login"), QStringLiteral("changer"));
    login.insert(QStringLiteral("password"), QStringLiteral("password2"));
    QVERIFY(manager.loginLocalPasswordUser(login).ok);
}

BOOKING_RESOURCES_REGISTER_TEST(WebAuthManagerTests)

#include "WebAuthManagerTests.moc"
