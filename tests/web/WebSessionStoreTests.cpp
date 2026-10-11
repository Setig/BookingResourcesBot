#include <QtTest/QtTest>
#include "TestRegistry.h"

#include "WebPasswordHasher.h"
#include "WebSessionStore.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

class WebSessionStoreTests : public QObject
{
    Q_OBJECT

private slots:
    void createAndResolveSession();
    void revokesSession();
    void queuesEvents();
    void isolatesFilesByUser();
    void isolatesCommandCacheByUserAndLocale();
    void loadsPersistentState();
    void credentialsAreUniqueAndLoginCannotChange();
    void pendingPasswordHashIsRejected();
    void deleteUserRemovesRelatedState();
    void passwordResetTokensCreateConsumeExpireAndReplace();
};

namespace {

bool expirePasswordResets(const QString &storageFile)
{
    const QString name =
            QStringLiteral("expire-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
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

void WebSessionStoreTests::createAndResolveSession()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    WebSessionStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("web.sqlite"))));

    WebSessionStore::User user;
    user.userId = QStringLiteral("u1");
    user.firstName = QStringLiteral("Alice");
    user.provider = QStringLiteral("authorization");
    QVERIFY(store.upsertUser(user));

    const QString token = store.createSession(user.userId);
    QVERIFY(!token.isEmpty());
    QCOMPARE(store.resolveSession(token), user.userId);
}

void WebSessionStoreTests::revokesSession()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    WebSessionStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("web.sqlite"))));

    WebSessionStore::User user;
    user.userId = QStringLiteral("u1");
    user.firstName = QStringLiteral("Alice");
    user.provider = QStringLiteral("authorization");
    QVERIFY(store.upsertUser(user));

    const QString token = store.createSession(user.userId);
    QVERIFY(!token.isEmpty());
    QCOMPARE(store.resolveSession(token), user.userId);
    QVERIFY(store.revokeSession(token));
    QVERIFY(store.resolveSession(token).isEmpty());
}

void WebSessionStoreTests::queuesEvents()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    WebSessionStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("web.sqlite"))));

    QJsonObject event;
    event.insert(QStringLiteral("type"), QStringLiteral("bot_message"));
    event.insert(QStringLiteral("text"), QStringLiteral("hello"));
    QVERIFY(store.appendEvent(QStringLiteral("u1"), event));

    const QJsonArray events = store.takeEvents(QStringLiteral("u1"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).toObject().value(QStringLiteral("text")).toString(),
             QStringLiteral("hello"));
    QVERIFY(store.takeEvents(QStringLiteral("u1")).isEmpty());
}

void WebSessionStoreTests::isolatesFilesByUser()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    WebSessionStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("web.sqlite"))));

    WebSessionStore::StoredFile file;
    QVERIFY(store.registerFileData(QStringLiteral("u1"),
                                   QStringLiteral("report.txt"),
                                   QByteArrayLiteral("hello"),
                                   &file));
    QVERIFY(file.isValid());
    QVERIFY(store.file(QStringLiteral("u1"), file.fileId).isValid());
    QVERIFY(!store.file(QStringLiteral("u2"), file.fileId).isValid());
}

void WebSessionStoreTests::isolatesCommandCacheByUserAndLocale()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    WebSessionStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("web.sqlite"))));

    QJsonArray userCommands;
    userCommands.append(QStringLiteral("/help"));
    QJsonArray adminCommands;
    adminCommands.append(QStringLiteral("/settings"));

    QVERIFY(store.saveCommands(QStringLiteral("u1"), QStringLiteral("en"), userCommands));
    QVERIFY(store.saveCommands(QStringLiteral("admin"), QStringLiteral("en"), adminCommands));

    QCOMPARE(store.commands(QStringLiteral("u1"), QStringLiteral("en")).size(), 1);
    QCOMPARE(store.commands(QStringLiteral("u1"), QStringLiteral("en")).at(0).toString(),
             QStringLiteral("/help"));
    QCOMPARE(store.commands(QStringLiteral("admin"), QStringLiteral("en")).at(0).toString(),
             QStringLiteral("/settings"));
    QVERIFY(store.commands(QStringLiteral("u1"), QStringLiteral("ru")).isEmpty());
    QVERIFY(store.commands(QStringLiteral("unknown"), QStringLiteral("en")).isEmpty());
}

void WebSessionStoreTests::loadsPersistentState()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString storageFile = dir.filePath(QStringLiteral("web.sqlite"));
    QString token;
    {
        WebSessionStore store;
        QVERIFY(store.open(storageFile));

        WebSessionStore::User user;
        user.userId = QStringLiteral("u1");
        user.firstName = QStringLiteral("Alice");
        user.locale = QStringLiteral("en");
        user.provider = QStringLiteral("authorization");
        QVERIFY(store.upsertUser(user));

        token = store.createSession(user.userId);
        QVERIFY(!token.isEmpty());

        QJsonObject event;
        event.insert(QStringLiteral("type"), QStringLiteral("bot_message"));
        event.insert(QStringLiteral("text"), QStringLiteral("hello"));
        QVERIFY(store.appendEvent(user.userId, event));

        QJsonArray commands;
        commands.append(QStringLiteral("/help"));
        QVERIFY(store.saveCommands(QStringLiteral("u1"), QStringLiteral("en"), commands));
    }

    WebSessionStore store;
    QVERIFY(store.open(storageFile));
    QCOMPARE(store.user(QStringLiteral("u1")).firstName, QStringLiteral("Alice"));
    QCOMPARE(store.resolveSession(token), QStringLiteral("u1"));
    QCOMPARE(store.commands(QStringLiteral("u1"), QStringLiteral("en")).size(), 1);
    QCOMPARE(store.commands(QStringLiteral("u1"), QStringLiteral("en")).at(0).toString(),
             QStringLiteral("/help"));
    QVERIFY(store.commands(QStringLiteral("u2"), QStringLiteral("en")).isEmpty());

    const QJsonArray events = store.takeEvents(QStringLiteral("u1"));
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0).toObject().value(QStringLiteral("text")).toString(),
             QStringLiteral("hello"));

    WebSessionStore reopened;
    QVERIFY(reopened.open(storageFile));
    QVERIFY(reopened.takeEvents(QStringLiteral("u1")).isEmpty());
}

void WebSessionStoreTests::credentialsAreUniqueAndLoginCannotChange()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString storageFile = dir.filePath(QStringLiteral("web.sqlite"));

    WebSessionStore store;
    QVERIFY(store.open(storageFile));

    WebSessionStore::User userA;
    userA.userId = QStringLiteral("u-a");
    userA.firstName = QStringLiteral("Alice");
    userA.provider = QStringLiteral("local-password");
    QVERIFY(store.upsertUser(userA));

    WebSessionStore::User userB;
    userB.userId = QStringLiteral("u-b");
    userB.firstName = QStringLiteral("Bob");
    userB.provider = QStringLiteral("local-password");
    QVERIFY(store.upsertUser(userB));

    WebSessionStore::Credential credential;
    credential.login = QStringLiteral("alice");
    credential.userId = userA.userId;
    credential.passwordHash = WebPasswordHasher::hashPassword(QStringLiteral("password1"));
    QVERIFY(store.upsertCredential(credential));

    WebSessionStore::Credential duplicate;
    duplicate.login = QStringLiteral("alice");
    duplicate.userId = userB.userId;
    duplicate.passwordHash = WebPasswordHasher::hashPassword(QStringLiteral("password2"));
    QVERIFY(!store.upsertCredential(duplicate));
    QCOMPARE(store.lastError(), QStringLiteral("Login is already used."));

    WebSessionStore::Credential renamed;
    renamed.login = QStringLiteral("alice2");
    renamed.userId = userA.userId;
    renamed.passwordHash = WebPasswordHasher::hashPassword(QStringLiteral("password3"));
    QVERIFY(!store.upsertCredential(renamed));
    QCOMPARE(store.lastError(), QStringLiteral("Login cannot be changed."));

    WebSessionStore reopened;
    QVERIFY(reopened.open(storageFile));
    QCOMPARE(reopened.credentialByLogin(QStringLiteral("alice")).userId, userA.userId);
    QCOMPARE(reopened.resolveUserId(QStringLiteral("alice")), userA.userId);
    QCOMPARE(reopened.resolveUserId(QStringLiteral("web:u-a")), userA.userId);
}

void WebSessionStoreTests::pendingPasswordHashIsRejected()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    WebSessionStore store;
    QVERIFY(store.open(dir.filePath(QStringLiteral("web.sqlite"))));

    WebSessionStore::User user;
    user.userId = QStringLiteral("u-pending");
    user.firstName = QStringLiteral("Pat");
    user.provider = QStringLiteral("local-password");
    QVERIFY(store.upsertUser(user));

    WebSessionStore::Credential credential;
    credential.login = QStringLiteral("pending-user");
    credential.userId = user.userId;
    credential.passwordHash = WebPasswordHasher::pendingPasswordHash();
    QVERIFY(store.upsertCredential(credential));
    QVERIFY(WebPasswordHasher::isPendingPasswordHash(
            store.credentialForUser(user.userId).passwordHash));
    QVERIFY(!WebPasswordHasher::verifyPassword(QStringLiteral("anything1"),
                                               credential.passwordHash));
}

void WebSessionStoreTests::deleteUserRemovesRelatedState()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString storageFile = dir.filePath(QStringLiteral("web.sqlite"));

    WebSessionStore store;
    QVERIFY(store.open(storageFile));

    WebSessionStore::User user;
    user.userId = QStringLiteral("u-del");
    user.firstName = QStringLiteral("Dana");
    user.provider = QStringLiteral("local-password");
    QVERIFY(store.upsertUser(user));

    WebSessionStore::Credential credential;
    credential.login = QStringLiteral("dana");
    credential.userId = user.userId;
    credential.passwordHash = WebPasswordHasher::pendingPasswordHash();
    QVERIFY(store.upsertCredential(credential));

    const QString session = store.createSession(user.userId);
    QVERIFY(!session.isEmpty());

    QJsonObject event;
    event.insert(QStringLiteral("type"), QStringLiteral("bot_message"));
    event.insert(QStringLiteral("text"), QStringLiteral("hello"));
    QVERIFY(store.appendEvent(user.userId, event));

    WebSessionStore::StoredFile file;
    QVERIFY(store.registerFileData(user.userId,
                                   QStringLiteral("note.txt"),
                                   QByteArrayLiteral("data"),
                                   &file));
    QVERIFY(QFile::exists(file.filePath));

    QJsonArray commands;
    commands.append(QStringLiteral("/help"));
    QVERIFY(store.saveCommands(user.userId, QStringLiteral("en"), commands));

    const QString token =
            store.createPasswordResetToken(user.userId, QStringLiteral("set_password"));
    QVERIFY(!token.isEmpty());

    QVERIFY(store.deleteUser(user.userId));
    QVERIFY(!store.user(user.userId).isValid());
    QVERIFY(!store.credentialByLogin(QStringLiteral("dana")).isValid());
    QVERIFY(store.resolveSession(session).isEmpty());
    QVERIFY(store.takeEvents(user.userId).isEmpty());
    QVERIFY(!store.file(user.userId, file.fileId).isValid());
    QVERIFY(!QFile::exists(file.filePath));
    QVERIFY(store.commands(user.userId, QStringLiteral("en")).isEmpty());
    QVERIFY(!store.lookupPasswordResetToken(token, nullptr, nullptr));

    WebSessionStore reopened;
    QVERIFY(reopened.open(storageFile));
    QVERIFY(!reopened.user(user.userId).isValid());
    QVERIFY(!reopened.credentialByLogin(QStringLiteral("dana")).isValid());
}

void WebSessionStoreTests::passwordResetTokensCreateConsumeExpireAndReplace()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString storageFile = dir.filePath(QStringLiteral("web.sqlite"));

    WebSessionStore store;
    QVERIFY(store.open(storageFile));

    WebSessionStore::User user;
    user.userId = QStringLiteral("u-token");
    user.firstName = QStringLiteral("Tina");
    user.provider = QStringLiteral("local-password");
    QVERIFY(store.upsertUser(user));

    WebSessionStore::Credential credential;
    credential.login = QStringLiteral("tina");
    credential.userId = user.userId;
    credential.passwordHash = WebPasswordHasher::pendingPasswordHash();
    QVERIFY(store.upsertCredential(credential));

    const QString session = store.createSession(user.userId);
    QVERIFY(!session.isEmpty());

    const QString first =
            store.createPasswordResetToken(user.userId, QStringLiteral("set_password"));
    QVERIFY(!first.isEmpty());
    QVERIFY(store.resolveSession(session).isEmpty());

    QString userId;
    QString purpose;
    QVERIFY(store.lookupPasswordResetToken(first, &userId, &purpose));
    QCOMPARE(userId, user.userId);
    QCOMPARE(purpose, QStringLiteral("set_password"));

    const QString second =
            store.createPasswordResetToken(user.userId, QStringLiteral("reset_password"));
    QVERIFY(!second.isEmpty());
    QVERIFY(!store.lookupPasswordResetToken(first, nullptr, nullptr));
    QVERIFY(store.lookupPasswordResetToken(second, &userId, &purpose));
    QCOMPARE(purpose, QStringLiteral("reset_password"));

    QVERIFY(store.consumePasswordResetToken(second, QStringLiteral("reset_password"), &userId));
    QCOMPARE(userId, user.userId);
    QVERIFY(!store.lookupPasswordResetToken(second, nullptr, nullptr));

    const QString third =
            store.createPasswordResetToken(user.userId, QStringLiteral("set_password"));
    QVERIFY(!third.isEmpty());
    QVERIFY(expirePasswordResets(storageFile));

    WebSessionStore reopened;
    QVERIFY(reopened.open(storageFile));
    QCOMPARE(reopened.credentialForUser(user.userId).login, QStringLiteral("tina"));
    QVERIFY(!reopened.lookupPasswordResetToken(third, nullptr, nullptr));
}

BOOKING_RESOURCES_REGISTER_TEST(WebSessionStoreTests)

#include "WebSessionStoreTests.moc"
