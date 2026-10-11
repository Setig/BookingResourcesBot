#include <QtTest/QtTest>
#include "TestRegistry.h"

#include "WebAuthManager.h"
#include "WebLocalCliCommands.h"
#include "WebPasswordHasher.h"
#include "WebSessionStore.h"

#include <QJsonObject>
#include <QTemporaryDir>

class WebLocalCliCommandTests : public QObject
{
    Q_OBJECT

private slots:
    void helpListsCommands();
    void addUserCreatesPendingPasswordAndSetLink();
    void addUserRejectsDuplicateLogin();
    void listUsersShowsPendingAndSetPasswords();
    void deleteUserByLoginAndWebKey();
    void resetPasswordForLocalPasswordAndWebProfile();
    void resetPasswordRequiresLoginForWebProfile();
    void resetPasswordRejectsOccupiedLogin();
    void localPasswordDisabledBlocksAddAndReset();
    void logInOnlyAllowsAddUser();
    void completeSuggestsCommandsAndUsers();
};

namespace {

class Harness
{
public:
    Harness()
        : store()
        , auth(&store)
        , commands(&auth, &store)
    {
        QVERIFY(dir.isValid());
        QVERIFY(store.open(dir.filePath(QStringLiteral("web.sqlite"))));
        commands.setAuthMethods({ QStringLiteral("log-in-with-registration") });
        commands.setPublicOrigin(QStringLiteral("https://booking.example.com"));
    }

    QTemporaryDir dir;
    WebSessionStore store;
    WebAuthManager auth;
    WebLocalCliCommands commands;
};

} // namespace

void WebLocalCliCommandTests::helpListsCommands()
{
    Harness harness;
    const WebLocalCliCommands::Result result = harness.commands.execute(QStringLiteral("/help"));
    QVERIFY(result.ok);
    QVERIFY(result.text.contains(QStringLiteral("/list_users")));
    QVERIFY(result.text.contains(QStringLiteral("/add_user")));
    QVERIFY(result.text.contains(QStringLiteral("/delete_user")));
    QVERIFY(result.text.contains(QStringLiteral("/reset_password")));
}

void WebLocalCliCommandTests::addUserCreatesPendingPasswordAndSetLink()
{
    Harness harness;
    const WebLocalCliCommands::Result result =
            harness.commands.execute(QStringLiteral("/add_user alice Alice Example en"));
    QVERIFY(result.ok);
    QVERIFY(result.text.contains(QStringLiteral("/set-password.html?token=")));
    QVERIFY(!result.text.contains(QStringLiteral("public_https_host")));

    const QString userId = harness.store.resolveUserId(QStringLiteral("alice"));
    QVERIFY(!userId.isEmpty());
    QVERIFY(WebPasswordHasher::isPendingPasswordHash(
            harness.store.credentialForUser(userId).passwordHash));
}

void WebLocalCliCommandTests::addUserRejectsDuplicateLogin()
{
    Harness harness;
    QVERIFY(harness.commands.execute(QStringLiteral("/add_user alice Alice")).ok);
    const WebLocalCliCommands::Result result =
            harness.commands.execute(QStringLiteral("/add_user alice Other"));
    QVERIFY(!result.ok);
}

void WebLocalCliCommandTests::listUsersShowsPendingAndSetPasswords()
{
    Harness harness;
    QVERIFY(harness.commands.execute(QStringLiteral("/add_user alice Alice")).ok);

    QJsonObject registerRequest;
    registerRequest.insert(QStringLiteral("login"), QStringLiteral("bob"));
    registerRequest.insert(QStringLiteral("password"), QStringLiteral("password1"));
    registerRequest.insert(QStringLiteral("first_name"), QStringLiteral("Bob"));
    QVERIFY(harness.auth.registerLocalPasswordUser(registerRequest).ok);

    const WebLocalCliCommands::Result result =
            harness.commands.execute(QStringLiteral("/list_users"));
    QVERIFY(result.ok);
    QVERIFY(result.text.contains(QStringLiteral("alice")));
    QVERIFY(result.text.contains(QStringLiteral("pending")));
    QVERIFY(result.text.contains(QStringLiteral("bob")));
    QVERIFY(result.text.contains(QStringLiteral("set")));
}

void WebLocalCliCommandTests::deleteUserByLoginAndWebKey()
{
    Harness harness;
    QVERIFY(harness.commands.execute(QStringLiteral("/add_user alice Alice")).ok);
    QVERIFY(harness.commands.execute(QStringLiteral("/add_user bob Bob")).ok);

    const QString bobId = harness.store.resolveUserId(QStringLiteral("bob"));
    QVERIFY(!bobId.isEmpty());

    QVERIFY(harness.commands.execute(QStringLiteral("/delete_user alice")).ok);
    QVERIFY(harness.store.resolveUserId(QStringLiteral("alice")).isEmpty());

    const WebLocalCliCommands::Result byKey =
            harness.commands.execute(QStringLiteral("/delete_user web:%1").arg(bobId));
    QVERIFY(byKey.ok);
    QVERIFY(harness.store.resolveUserId(QStringLiteral("bob")).isEmpty());
}

void WebLocalCliCommandTests::resetPasswordForLocalPasswordAndWebProfile()
{
    Harness harness;
    QJsonObject registerRequest;
    registerRequest.insert(QStringLiteral("login"), QStringLiteral("alice"));
    registerRequest.insert(QStringLiteral("password"), QStringLiteral("password1"));
    registerRequest.insert(QStringLiteral("first_name"), QStringLiteral("Alice"));
    const WebAuthManager::AuthResult created =
            harness.auth.registerLocalPasswordUser(registerRequest);
    QVERIFY(created.ok);

    QJsonObject login;
    login.insert(QStringLiteral("login"), QStringLiteral("alice"));
    login.insert(QStringLiteral("password"), QStringLiteral("password1"));
    QVERIFY(harness.auth.loginLocalPasswordUser(login).ok);

    const WebLocalCliCommands::Result resetLocal =
            harness.commands.execute(QStringLiteral("/reset_password alice"));
    QVERIFY(resetLocal.ok);
    QVERIFY(resetLocal.text.contains(QStringLiteral("/reset-password.html?token=")));
    QVERIFY(resetLocal.text.contains(QStringLiteral("valid for 1 day")));
    QVERIFY(WebPasswordHasher::isPendingPasswordHash(
            harness.store.credentialForUser(created.userId).passwordHash));
    QVERIFY(!harness.auth.loginLocalPasswordUser(login).ok);

    QJsonObject profile;
    profile.insert(QStringLiteral("first_name"), QStringLiteral("Web"));
    const WebAuthManager::AuthResult authorized = harness.auth.authorizeUser(profile);
    QVERIFY(authorized.ok);

    const WebLocalCliCommands::Result resetProfile = harness.commands.execute(
            QStringLiteral("/reset_password web:%1 webuser").arg(authorized.userId));
    QVERIFY(resetProfile.ok);
    QVERIFY(resetProfile.text.contains(QStringLiteral("/set-password.html?token=")));
    QCOMPARE(harness.store.user(authorized.userId).provider, QStringLiteral("local-password"));
    QCOMPARE(harness.store.credentialForUser(authorized.userId).login, QStringLiteral("webuser"));
}

void WebLocalCliCommandTests::resetPasswordRequiresLoginForWebProfile()
{
    Harness harness;
    QJsonObject profile;
    profile.insert(QStringLiteral("first_name"), QStringLiteral("Web"));
    const WebAuthManager::AuthResult authorized = harness.auth.authorizeUser(profile);
    QVERIFY(authorized.ok);

    const WebLocalCliCommands::Result result = harness.commands.execute(
            QStringLiteral("/reset_password web:%1").arg(authorized.userId));
    QVERIFY(!result.ok);
}

void WebLocalCliCommandTests::resetPasswordRejectsOccupiedLogin()
{
    Harness harness;
    QVERIFY(harness.commands.execute(QStringLiteral("/add_user alice Alice")).ok);

    QJsonObject profile;
    profile.insert(QStringLiteral("first_name"), QStringLiteral("Web"));
    const WebAuthManager::AuthResult authorized = harness.auth.authorizeUser(profile);
    QVERIFY(authorized.ok);

    const WebLocalCliCommands::Result result = harness.commands.execute(
            QStringLiteral("/reset_password web:%1 alice").arg(authorized.userId));
    QVERIFY(!result.ok);
}

void WebLocalCliCommandTests::localPasswordDisabledBlocksAddAndReset()
{
    Harness harness;
    harness.commands.setAuthMethods({ QStringLiteral("sign-without-registration") });

    QVERIFY(!harness.commands.execute(QStringLiteral("/add_user alice Alice")).ok);
    QVERIFY(!harness.commands.execute(QStringLiteral("/reset_password alice")).ok);

    QJsonObject profile;
    profile.insert(QStringLiteral("first_name"), QStringLiteral("Web"));
    const WebAuthManager::AuthResult authorized = harness.auth.authorizeUser(profile);
    QVERIFY(authorized.ok);

    const WebLocalCliCommands::Result deleted =
            harness.commands.execute(QStringLiteral("/delete_user web:%1").arg(authorized.userId));
    QVERIFY(deleted.ok);
}

void WebLocalCliCommandTests::completeSuggestsCommandsAndUsers()
{
    Harness harness;
    const WebLocalCliCommands::Completion commands =
            harness.commands.complete(QStringLiteral("/ad"), 3);
    QVERIFY(commands.values.contains(QStringLiteral("/add_user")));

    QVERIFY(harness.commands.execute(QStringLiteral("/add_user alice Alice")).ok);
    const WebLocalCliCommands::Completion users =
            harness.commands.complete(QStringLiteral("/delete_user al"), 14);
    QVERIFY(users.values.contains(QStringLiteral("alice")));

    const WebLocalCliCommands::Completion locales =
            harness.commands.complete(QStringLiteral("/add_user alice Alice Example r"), 31);
    QVERIFY(locales.values.contains(QStringLiteral("ru")));
    QVERIFY(locales.values.contains(QStringLiteral("ru_stand")));
}

void WebLocalCliCommandTests::logInOnlyAllowsAddUser()
{
    Harness harness;
    harness.commands.setAuthMethods({ QStringLiteral("log-in-only") });

    const WebLocalCliCommands::Result result =
            harness.commands.execute(QStringLiteral("/add_user alice Alice"));
    QVERIFY(result.ok);
    QVERIFY(result.text.contains(QStringLiteral("/set-password.html?token=")));
}

BOOKING_RESOURCES_REGISTER_TEST(WebLocalCliCommandTests)

#include "WebLocalCliCommandTests.moc"
