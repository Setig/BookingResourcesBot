#include <QtTest/QtTest>
#include "TestRegistry.h"

#include <QFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

#include "WebPasswordHasher.h"

#define private public
#include "WebServer.h"
#undef private

#include "WebAuthManager.h"
#include "WebSecurityPolicy.h"

namespace {

class TestWebServer
{
public:
    TestWebServer()
        : dir()
        , store()
        , auth(&store)
        , server(&auth, nullptr, &store, nullptr)
    {
        QVERIFY(dir.isValid());
        QVERIFY(store.open(dir.filePath(QStringLiteral("web.sqlite"))));
    }

    WebSessionStore::User addUser(const QString &userId)
    {
        WebSessionStore::User user;
        user.userId = userId;
        user.firstName = QStringLiteral("User %1").arg(userId);
        user.locale = QStringLiteral("en");
        user.provider = QStringLiteral("authorization");
        if (!store.upsertUser(user)) {
            QTest::qFail("Failed to upsert test web user.", __FILE__, __LINE__);
            return WebSessionStore::User();
        }
        return user;
    }

    QString addSession(const QString &userId)
    {
        addUser(userId);
        const QString token = store.createSession(userId);
        if (token.isEmpty()) {
            QTest::qFail("Failed to create test web session.", __FILE__, __LINE__);
            return QString();
        }
        return token;
    }

    WebServer::HttpRequest request(const QString &method,
                                   const QString &path,
                                   const QString &token = QString()) const
    {
        WebServer::HttpRequest result;
        result.method = method;
        result.path = path;
        result.remoteAddress = QStringLiteral("127.0.0.1");
        if (!token.isEmpty()) {
            result.headers.insert(QStringLiteral("cookie"),
                                  QStringLiteral("br_session=%1").arg(token));
        }
        return result;
    }

    QTemporaryDir dir;
    WebSessionStore store;
    WebAuthManager auth;
    WebServer server;
};

QByteArray rawHttpResponse(WebServer *server, const WebServer::HttpResponse &response)
{
    QTcpServer listener;
    if (!listener.listen(QHostAddress::LocalHost, 0)) {
        QTest::qFail("Failed to listen for response capture.", __FILE__, __LINE__);
        return QByteArray();
    }

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, listener.serverPort());
    if (!client.waitForConnected(1000)) {
        QTest::qFail("Failed to connect response capture client.", __FILE__, __LINE__);
        return QByteArray();
    }
    if (!listener.waitForNewConnection(1000)) {
        QTest::qFail("Failed to accept response capture client.", __FILE__, __LINE__);
        return QByteArray();
    }

    QTcpSocket *accepted = listener.nextPendingConnection();
    if (!accepted) {
        QTest::qFail("Accepted socket is null.", __FILE__, __LINE__);
        return QByteArray();
    }
    server->sendResponse(accepted, response);
    accepted->waitForBytesWritten(1000);
    accepted->disconnectFromHost();

    QByteArray data;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1000) {
        if (client.bytesAvailable() > 0) {
            data += client.readAll();
            break;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(1);
    }

    if (data.isEmpty()) {
        QTest::qFail("Response capture client received no data.", __FILE__, __LINE__);
        return QByteArray();
    }

    while (client.waitForReadyRead(100))
        data += client.readAll();
    return data;
}

QJsonArray responseArray(const WebServer::HttpResponse &response)
{
    return QJsonDocument::fromJson(response.body).array();
}

QByteArray responseHeader(const WebServer::HttpResponse &response, const QByteArray &name)
{
    for (const QPair<QByteArray, QByteArray> &header : response.headers) {
        if (header.first.compare(name, Qt::CaseInsensitive) == 0)
            return header.second;
    }
    return QByteArray();
}

QByteArray readSourceFile(const QString &relativePath)
{
    QFile file(QStringLiteral(WEB_TEST_SOURCE_DIR) + QLatin1Char('/') + relativePath);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return file.readAll();
}

} // namespace

class WebServerSecurityTests : public QObject
{
    Q_OBJECT

private slots:
    void httpBindPolicy();
    void httpRedirectPreservesPathAndOmitsHsts();
    void healthEndpointIsPublic();
    void trustedForwardedHeadersAffectSecurityAndRemote();
    void securityHeadersForJsonAndHttps();
    void sessionCookieFlags();
    void protectedEndpointsRequireAuthentication();
    void filesAreIsolatedBySessionUser();
    void commandCacheFallbackIsPerUserAndLocale();
    void authorizeRateLimit();
    void requestParserLimits();
    void staticFilesAreWhitelisted();
    void cspCompatibleStaticSources();
    void passwordPagesAndApisArePublic();
    void passwordPagesKeepFormFieldsOutsideStatus();
    void passwordTokenLookupReturnsPurpose();
    void passwordTokenApisValidatePurposeAndExpiry();
    void passwordSetSuccessDoesNotCreateSession();
    void passwordApisShareAuthorizeRateLimit();
    void logInOnlyDisablesRegisterAndAuthorize();
    void disabledAuthMethodsRejectExtraPosts();
};

void WebServerSecurityTests::httpBindPolicy()
{
    QVERIFY(WebSecurityPolicy::isPlainHttpBindAllowed(QHostAddress::LocalHost, false, false));
    QVERIFY(!WebSecurityPolicy::isPlainHttpBindAllowed(QHostAddress::Any, false, false));
    QVERIFY(WebSecurityPolicy::isPlainHttpBindAllowed(QHostAddress::Any, false, true));
    QVERIFY(WebSecurityPolicy::isPlainHttpBindAllowed(QHostAddress::Any, true, false));
    QVERIFY(!WebSecurityPolicy::isPlainHttpBindAllowed(
            QHostAddress::Any, WebSecurityPolicy::PlainHttpPurpose::Application, false));
    QVERIFY(WebSecurityPolicy::isPlainHttpBindAllowed(QHostAddress::Any,
                                                      WebSecurityPolicy::PlainHttpPurpose::Redirect,
                                                      false));
}

void WebServerSecurityTests::httpRedirectPreservesPathAndOmitsHsts()
{
    TestWebServer harness;
    harness.server.m_httpsEnabled = true;
    harness.server.m_httpsPort = 9443;

    WebServer::HttpRequest request =
            harness.request(QStringLiteral("GET"), QStringLiteral("/chat?x=1"));
    request.listenerRole = WebServer::ListenerRole::HttpRedirect;
    request.headers.insert(QStringLiteral("host"), QStringLiteral("example.test"));

    const WebServer::HttpResponse response = harness.server.processRequest(request);
    QCOMPARE(response.statusCode, 308);
    QCOMPARE(responseHeader(response, QByteArrayLiteral("Location")),
             QByteArrayLiteral("https://example.test:9443/chat?x=1"));

    const QByteArray raw = rawHttpResponse(&harness.server, response);
    QVERIFY(raw.contains("HTTP/1.1 308 Permanent Redirect"));
    QVERIFY(!raw.contains("Strict-Transport-Security"));
}

void WebServerSecurityTests::healthEndpointIsPublic()
{
    TestWebServer harness;
    const WebServer::HttpResponse response = harness.server.processRequest(
            harness.request(QStringLiteral("GET"), QStringLiteral("/health")));
    QCOMPARE(response.statusCode, 200);
    const QJsonObject body = QJsonDocument::fromJson(response.body).object();
    QCOMPARE(body.value(QStringLiteral("ok")).toBool(), true);
}

void WebServerSecurityTests::trustedForwardedHeadersAffectSecurityAndRemote()
{
    TestWebServer harness;
    harness.server.setForwardedHeadersEnabled(true);

    WebServer::HttpRequest request =
            harness.request(QStringLiteral("GET"), QStringLiteral("/api/authorize"));
    request.remoteAddress = QStringLiteral("127.0.0.1");
    request.headers.insert(QStringLiteral("x-forwarded-proto"), QStringLiteral("https"));
    request.headers.insert(QStringLiteral("x-forwarded-host"), QStringLiteral("public.example"));
    request.headers.insert(QStringLiteral("x-forwarded-for"), QStringLiteral("203.0.113.10"));

    harness.server.applyTrustedProxyHeaders(&request);
    QCOMPARE(request.remoteAddress, QStringLiteral("203.0.113.10"));
    QCOMPARE(request.forwardedHost, QStringLiteral("public.example"));
    QVERIFY(request.secureTransport);

    WebServer::HttpRequest untrusted =
            harness.request(QStringLiteral("GET"), QStringLiteral("/api/authorize"));
    untrusted.remoteAddress = QStringLiteral("198.51.100.1");
    untrusted.headers = request.headers;
    harness.server.applyTrustedProxyHeaders(&untrusted);
    QCOMPARE(untrusted.remoteAddress, QStringLiteral("198.51.100.1"));
    QVERIFY(!untrusted.secureTransport);
}

void WebServerSecurityTests::securityHeadersForJsonAndHttps()
{
    TestWebServer harness;
    WebServer::HttpResponse response;
    response.body = QByteArrayLiteral("{}");

    QByteArray raw = rawHttpResponse(&harness.server, response);
    QVERIFY(raw.contains("Content-Security-Policy: default-src 'self'"));
    QVERIFY(raw.contains("X-Frame-Options: DENY"));
    QVERIFY(raw.contains("Permissions-Policy: camera=(), microphone=(), geolocation=()"));
    QVERIFY(raw.contains("X-Content-Type-Options: nosniff"));
    QVERIFY(raw.contains("Referrer-Policy: strict-origin-when-cross-origin"));
    QVERIFY(raw.contains("Cache-Control: no-store"));
    QVERIFY(!raw.contains("Strict-Transport-Security"));

    response.secureTransport = true;
    raw = rawHttpResponse(&harness.server, response);
    QVERIFY(raw.contains("Strict-Transport-Security: max-age=31536000"));
}

void WebServerSecurityTests::sessionCookieFlags()
{
    TestWebServer harness;

    QByteArray cookie = harness.server.sessionCookie(QStringLiteral("token"));
    QVERIFY(cookie.contains("HttpOnly"));
    QVERIFY(cookie.contains("SameSite=Lax"));
    QVERIFY(!cookie.contains("Secure"));

    harness.server.m_httpsEnabled = true;
    cookie = harness.server.sessionCookie(QStringLiteral("token"));
    QVERIFY(cookie.contains("Secure"));

    const QByteArray expired = harness.server.expiredSessionCookie();
    QVERIFY(expired.contains("Max-Age=0"));
    QVERIFY(expired.contains("HttpOnly"));
    QVERIFY(expired.contains("SameSite=Lax"));
    QVERIFY(expired.contains("Secure"));

    const QString token = harness.addSession(QStringLiteral("logout-user"));
    WebServer::HttpRequest logoutRequest =
            harness.request(QStringLiteral("POST"), QStringLiteral("/api/logout"), token);
    logoutRequest.listenerRole = WebServer::ListenerRole::HttpsApplication;
    logoutRequest.secureTransport = true;
    const WebServer::HttpResponse response = harness.server.processRequest(logoutRequest);
    QCOMPARE(response.statusCode, 200);
    QVERIFY(harness.store.resolveSession(token).isEmpty());
    QVERIFY(responseHeader(response, QByteArrayLiteral("Set-Cookie")).contains("Max-Age=0"));
}

void WebServerSecurityTests::protectedEndpointsRequireAuthentication()
{
    TestWebServer harness;

    const QVector<QPair<QString, QString>> endpoints = {
        { QStringLiteral("GET"), QStringLiteral("/api/session") },
        { QStringLiteral("POST"), QStringLiteral("/api/profile") },
        { QStringLiteral("GET"), QStringLiteral("/api/commands") },
        { QStringLiteral("GET"), QStringLiteral("/api/user-settings") },
        { QStringLiteral("POST"), QStringLiteral("/api/user-settings") },
        { QStringLiteral("POST"), QStringLiteral("/api/messages") },
        { QStringLiteral("POST"), QStringLiteral("/api/callbacks") },
        { QStringLiteral("GET"), QStringLiteral("/api/events") },
        { QStringLiteral("GET"), QStringLiteral("/api/files/file-id") },
    };

    for (const QPair<QString, QString> &endpoint : endpoints) {
        const WebServer::HttpResponse response =
                harness.server.processRequest(harness.request(endpoint.first, endpoint.second));
        QCOMPARE(response.statusCode, 401);
    }
}

void WebServerSecurityTests::filesAreIsolatedBySessionUser()
{
    TestWebServer harness;
    const QString userToken = harness.addSession(QStringLiteral("user"));
    harness.addUser(QStringLiteral("other"));

    WebSessionStore::StoredFile file;
    QVERIFY(harness.store.registerFileData(QStringLiteral("other"),
                                           QStringLiteral("secret.txt"),
                                           QByteArrayLiteral("secret"),
                                           &file));

    const WebServer::HttpResponse response = harness.server.processRequest(
            harness.request(QStringLiteral("GET"),
                            QStringLiteral("/api/files/%1").arg(file.fileId),
                            userToken));
    QCOMPARE(response.statusCode, 404);
}

void WebServerSecurityTests::commandCacheFallbackIsPerUserAndLocale()
{
    TestWebServer harness;
    const QString userToken = harness.addSession(QStringLiteral("user"));
    harness.addUser(QStringLiteral("admin"));

    QJsonArray userCommands;
    userCommands.append(QStringLiteral("/help"));
    QJsonArray adminCommands;
    adminCommands.append(QStringLiteral("/settings"));
    QVERIFY(harness.store.saveCommands(QStringLiteral("user"), QStringLiteral("en"), userCommands));
    QVERIFY(harness.store.saveCommands(QStringLiteral("admin"),
                                       QStringLiteral("en"),
                                       adminCommands));

    WebServer::HttpResponse response =
            harness.server.processRequest(harness.request(QStringLiteral("GET"),
                                                          QStringLiteral("/api/commands?locale=en"),
                                                          userToken));
    QCOMPARE(response.statusCode, 200);
    QCOMPARE(responseArray(response).size(), 1);
    QCOMPARE(responseArray(response).at(0).toString(), QStringLiteral("/help"));

    response =
            harness.server.processRequest(harness.request(QStringLiteral("GET"),
                                                          QStringLiteral("/api/commands?locale=ru"),
                                                          userToken));
    QCOMPARE(response.statusCode, 200);
    QVERIFY(responseArray(response).isEmpty());
}

void WebServerSecurityTests::authorizeRateLimit()
{
    {
        TestWebServer harness;
        const QString remote = QStringLiteral("203.0.113.10");

        for (int i = 0; i < 10; ++i)
            QVERIFY(!harness.server.isAuthorizeRateLimited(remote));
        QVERIFY(harness.server.isAuthorizeRateLimited(remote));
    }

    {
        TestWebServer harness;
        for (int i = 0; i < 100; ++i) {
            QVERIFY(!harness.server.isAuthorizeRateLimited(QStringLiteral("198.51.100.%1").arg(i)));
        }
        QVERIFY(harness.server.isAuthorizeRateLimited(QStringLiteral("198.51.100.200")));
    }
}

void WebServerSecurityTests::requestParserLimits()
{
    TestWebServer harness;
    WebServer::HttpRequest request;
    WebServer::HttpResponse error;

    QByteArray invalidLength =
            QByteArrayLiteral("POST /api/messages HTTP/1.1\r\nContent-Length: nope\r\n\r\n");
    QVERIFY(!harness.server.tryParseRequest(&invalidLength, &request, &error));
    QCOMPARE(error.statusCode, 400);

    QByteArray largeBody =
            QByteArrayLiteral("POST /api/messages HTTP/1.1\r\nContent-Length: 2000000\r\n\r\n");
    QVERIFY(!harness.server.tryParseRequest(&largeBody, &request, &error));
    QCOMPARE(error.statusCode, 413);

    QByteArray largeHeaders = QByteArrayLiteral("GET / HTTP/1.1\r\nX-Fill: ");
    largeHeaders += QByteArray(20 * 1024, 'x');
    QVERIFY(!harness.server.tryParseRequest(&largeHeaders, &request, &error));
    QCOMPARE(error.statusCode, 413);
}

void WebServerSecurityTests::staticFilesAreWhitelisted()
{
    TestWebServer harness;

    WebServer::HttpResponse response = harness.server.processRequest(
            harness.request(QStringLiteral("GET"), QStringLiteral("/index.html")));
    QCOMPARE(response.statusCode, 200);
    QVERIFY(response.contentType.startsWith(QByteArrayLiteral("text/html")));

    response = harness.server.processRequest(
            harness.request(QStringLiteral("GET"), QStringLiteral("/../../CMakeLists.txt")));
    QCOMPARE(response.statusCode, 404);

    response = harness.server.processRequest(
            harness.request(QStringLiteral("GET"), QStringLiteral("/set-password.html")));
    QCOMPARE(response.statusCode, 200);
    QVERIFY(response.contentType.startsWith(QByteArrayLiteral("text/html")));

    response = harness.server.processRequest(
            harness.request(QStringLiteral("GET"), QStringLiteral("/reset-password.html")));
    QCOMPARE(response.statusCode, 200);
    QVERIFY(response.contentType.startsWith(QByteArrayLiteral("text/html")));
}

void WebServerSecurityTests::cspCompatibleStaticSources()
{
    const QByteArray indexHtml = readSourceFile(QStringLiteral("src/html/index.html"));
    QVERIFY(!indexHtml.isEmpty());
    QVERIFY(!indexHtml.contains("<script>"));
    QVERIFY(indexHtml.contains("src=\"/locale-boot.js\""));
    QVERIFY(indexHtml.contains("src=\"/index.js\""));

    const QByteArray setPasswordHtml = readSourceFile(QStringLiteral("src/html/set-password.html"));
    QVERIFY(!setPasswordHtml.isEmpty());
    QVERIFY(!setPasswordHtml.contains("<script>"));
    QVERIFY(setPasswordHtml.contains("src=\"/locale-boot.js\""));
    QVERIFY(setPasswordHtml.contains("src=\"/set-password.js\""));

    const QByteArray resetPasswordHtml =
            readSourceFile(QStringLiteral("src/html/reset-password.html"));
    QVERIFY(!resetPasswordHtml.isEmpty());
    QVERIFY(!resetPasswordHtml.contains("<script>"));
    QVERIFY(resetPasswordHtml.contains("src=\"/locale-boot.js\""));
    QVERIFY(resetPasswordHtml.contains("src=\"/reset-password.js\""));
    QVERIFY(resetPasswordHtml.contains("name=\"new_password\""));
    QVERIFY(resetPasswordHtml.contains("name=\"confirm_new_password\""));
    QVERIFY(setPasswordHtml.contains("name=\"new_password\""));
    QVERIFY(setPasswordHtml.contains("name=\"confirm_new_password\""));

    const QByteArray authorizeHtml = readSourceFile(QStringLiteral("src/html/authorize.html"));
    QVERIFY(authorizeHtml.contains("id=\"registrationForm\""));
    QVERIFY(authorizeHtml.contains("name=\"confirm_password\""));
    const QByteArray authorizeJs = readSourceFile(QStringLiteral("src/html/authorize.js"));
    QVERIFY(authorizeJs.contains("confirm_password"));
    QVERIFY(authorizeJs.contains("passwords_do_not_match"));

    const QStringList scripts = {
        QStringLiteral("src/html/app.js"),          QStringLiteral("src/html/authorize.js"),
        QStringLiteral("src/html/chat.js"),         QStringLiteral("src/html/i18n.js"),
        QStringLiteral("src/html/index.js"),        QStringLiteral("src/html/locale-boot.js"),
        QStringLiteral("src/html/reset-password.js"),
        QStringLiteral("src/html/set-password.js"), QStringLiteral("src/html/settings.js"),
        QStringLiteral("src/html/theme.js"),
    };
    for (const QString &script : scripts) {
        const QByteArray data = readSourceFile(script);
        QVERIFY2(!data.isEmpty(), qPrintable(script));
        QVERIFY2(!data.contains(".style."), qPrintable(script));
        QVERIFY2(!data.contains("setAttribute('style'"), qPrintable(script));
        QVERIFY2(!data.contains("setAttribute(\"style\""), qPrintable(script));
    }
}

void WebServerSecurityTests::passwordPagesAndApisArePublic()
{
    TestWebServer harness;
    harness.server.setAuthMethods({ QStringLiteral("log-in-with-registration") });

    WebServer::HttpResponse response = harness.server.processRequest(
            harness.request(QStringLiteral("GET"), QStringLiteral("/set-password.html")));
    QCOMPARE(response.statusCode, 200);

    response = harness.server.processRequest(
            harness.request(QStringLiteral("GET"), QStringLiteral("/reset-password.html")));
    QCOMPARE(response.statusCode, 200);

    WebServer::HttpRequest setRequest =
            harness.request(QStringLiteral("POST"), QStringLiteral("/api/account/password-set"));
    setRequest.body = QByteArrayLiteral("{\"token\":\"invalid\",\"new_password\":\"password1\"}");
    response = harness.server.processRequest(setRequest);
    QVERIFY(response.statusCode != 401);
    QCOMPARE(response.statusCode, 400);

    WebServer::HttpRequest resetRequest =
            harness.request(QStringLiteral("POST"), QStringLiteral("/api/account/password-reset"));
    resetRequest.body = QByteArrayLiteral("{\"token\":\"invalid\",\"new_password\":\"password1\"}");
    response = harness.server.processRequest(resetRequest);
    QVERIFY(response.statusCode != 401);
    QCOMPARE(response.statusCode, 400);
}

void WebServerSecurityTests::passwordPagesKeepFormFieldsOutsideStatus()
{
    const QByteArray setPasswordHtml = readSourceFile(QStringLiteral("src/html/set-password.html"));
    const QByteArray resetPasswordHtml =
            readSourceFile(QStringLiteral("src/html/reset-password.html"));

    const int setForm = setPasswordHtml.indexOf("id=\"setPasswordForm\"");
    const int setError = setPasswordHtml.indexOf("id=\"setPasswordError\"");
    const int setPasswordField = setPasswordHtml.indexOf("name=\"new_password\"");
    QVERIFY(setError >= 0);
    QVERIFY(setForm > setError);
    QVERIFY(setPasswordField > setForm);

    const int resetForm = resetPasswordHtml.indexOf("id=\"resetPasswordForm\"");
    const int resetError = resetPasswordHtml.indexOf("id=\"resetPasswordError\"");
    const int resetPasswordField = resetPasswordHtml.indexOf("name=\"new_password\"");
    QVERIFY(resetError >= 0);
    QVERIFY(resetForm > resetError);
    QVERIFY(resetPasswordField > resetForm);

    const QByteArray resetPasswordJs = readSourceFile(QStringLiteral("src/html/reset-password.js"));
    QVERIFY(resetPasswordJs.contains("/api/account/password-reset"));
    QVERIFY(!resetPasswordJs.contains("password-token"));

    const QByteArray styles = readSourceFile(QStringLiteral("src/html/styles.css"));
    QVERIFY(styles.contains(".form + .actions"));
    QVERIFY(styles.contains(".button[hidden]"));
}

void WebServerSecurityTests::passwordTokenLookupReturnsPurpose()
{
    TestWebServer harness;
    harness.server.setAuthMethods({ QStringLiteral("log-in-with-registration") });

    QJsonObject create;
    create.insert(QStringLiteral("login"), QStringLiteral("lookuser"));
    create.insert(QStringLiteral("first_name"), QStringLiteral("Look"));
    const WebAuthManager::AuthResult created =
            harness.auth.addLocalPasswordUserWithoutPassword(create);
    QVERIFY(created.ok);

    WebServer::HttpResponse response = harness.server.processRequest(harness.request(
            QStringLiteral("GET"),
            QStringLiteral("/api/account/password-token?token=%1").arg(created.sessionToken)));
    QCOMPARE(response.statusCode, 200);
    QJsonObject body = QJsonDocument::fromJson(response.body).object();
    QCOMPARE(body.value(QStringLiteral("purpose")).toString(), QStringLiteral("set_password"));
    QCOMPARE(body.value(QStringLiteral("login")).toString(), QStringLiteral("lookuser"));

    QJsonObject setBody;
    setBody.insert(QStringLiteral("token"), created.sessionToken);
    setBody.insert(QStringLiteral("new_password"), QStringLiteral("password1"));
    QVERIFY(harness.auth.setPasswordByToken(setBody, QStringLiteral("set_password")).ok);

    const WebAuthManager::AuthResult reset =
            harness.auth.issuePasswordToken(created.userId, QStringLiteral("reset_password"));
    QVERIFY(reset.ok);

    response = harness.server.processRequest(harness.request(
            QStringLiteral("GET"),
            QStringLiteral("/api/account/password-token?token=%1").arg(reset.sessionToken)));
    QCOMPARE(response.statusCode, 200);
    body = QJsonDocument::fromJson(response.body).object();
    QCOMPARE(body.value(QStringLiteral("purpose")).toString(), QStringLiteral("reset_password"));
    QCOMPARE(body.value(QStringLiteral("login")).toString(), QStringLiteral("lookuser"));
}

void WebServerSecurityTests::passwordTokenApisValidatePurposeAndExpiry()
{
    TestWebServer harness;
    harness.server.setAuthMethods({ QStringLiteral("log-in-with-registration") });

    QJsonObject create;
    create.insert(QStringLiteral("login"), QStringLiteral("tokuser"));
    create.insert(QStringLiteral("first_name"), QStringLiteral("Tok"));
    const WebAuthManager::AuthResult created =
            harness.auth.addLocalPasswordUserWithoutPassword(create);
    QVERIFY(created.ok);

    WebServer::HttpRequest wrongPurpose =
            harness.request(QStringLiteral("POST"), QStringLiteral("/api/account/password-reset"));
    QJsonObject wrongBody;
    wrongBody.insert(QStringLiteral("token"), created.sessionToken);
    wrongBody.insert(QStringLiteral("new_password"), QStringLiteral("password1"));
    wrongPurpose.body = QJsonDocument(wrongBody).toJson(QJsonDocument::Compact);
    QCOMPARE(harness.server.processRequest(wrongPurpose).statusCode, 400);

    const QString name =
            QStringLiteral("sec-expire-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(harness.store.storagePath());
    QVERIFY(db.open());
    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral(
            "UPDATE web_password_resets SET expires_at_utc = '2000-01-01T00:00:00.000Z'")));
    db.close();
    QSqlDatabase::removeDatabase(name);
    QVERIFY(harness.store.open(harness.store.storagePath()));

    WebServer::HttpRequest expired =
            harness.request(QStringLiteral("POST"), QStringLiteral("/api/account/password-set"));
    expired.body = QJsonDocument(wrongBody).toJson(QJsonDocument::Compact);
    QCOMPARE(harness.server.processRequest(expired).statusCode, 400);
}

void WebServerSecurityTests::passwordSetSuccessDoesNotCreateSession()
{
    TestWebServer harness;
    harness.server.setAuthMethods({ QStringLiteral("log-in-with-registration") });

    QJsonObject create;
    create.insert(QStringLiteral("login"), QStringLiteral("setok"));
    create.insert(QStringLiteral("first_name"), QStringLiteral("Set"));
    const WebAuthManager::AuthResult created =
            harness.auth.addLocalPasswordUserWithoutPassword(create);
    QVERIFY(created.ok);

    WebServer::HttpRequest request =
            harness.request(QStringLiteral("POST"), QStringLiteral("/api/account/password-set"));
    QJsonObject body;
    body.insert(QStringLiteral("token"), created.sessionToken);
    body.insert(QStringLiteral("new_password"), QStringLiteral("password1"));
    request.body = QJsonDocument(body).toJson(QJsonDocument::Compact);

    const WebServer::HttpResponse response = harness.server.processRequest(request);
    QCOMPARE(response.statusCode, 200);
    QVERIFY(responseHeader(response, QByteArrayLiteral("Set-Cookie")).isEmpty());
    QVERIFY(!WebPasswordHasher::isPendingPasswordHash(
            harness.store.credentialForUser(created.userId).passwordHash));
    QVERIFY(WebPasswordHasher::verifyPassword(QStringLiteral("password1"),
                                              harness.store.credentialForUser(created.userId)
                                                      .passwordHash));
    QVERIFY(!harness.store.lookupPasswordResetToken(created.sessionToken, nullptr, nullptr));
}

void WebServerSecurityTests::passwordApisShareAuthorizeRateLimit()
{
    TestWebServer harness;
    harness.server.setAuthMethods({ QStringLiteral("log-in-with-registration") });
    const QString remote = QStringLiteral("203.0.113.77");

    for (int i = 0; i < 10; ++i) {
        WebServer::HttpRequest request =
                harness.request(QStringLiteral("POST"),
                                QStringLiteral("/api/account/password-set"));
        request.remoteAddress = remote;
        request.body = QByteArrayLiteral("{\"token\":\"invalid\",\"new_password\":\"password1\"}");
        QCOMPARE(harness.server.processRequest(request).statusCode, 400);
    }

    WebServer::HttpRequest limited =
            harness.request(QStringLiteral("POST"), QStringLiteral("/api/account/password-reset"));
    limited.remoteAddress = remote;
    limited.body = QByteArrayLiteral("{\"token\":\"invalid\",\"new_password\":\"password1\"}");
    QCOMPARE(harness.server.processRequest(limited).statusCode, 429);
}

void WebServerSecurityTests::logInOnlyDisablesRegisterAndAuthorize()
{
    TestWebServer harness;
    harness.server.setAuthMethods({ QStringLiteral("log-in-only") });

    WebServer::HttpRequest authorize =
            harness.request(QStringLiteral("POST"), QStringLiteral("/api/authorize"));
    authorize.body = QByteArrayLiteral("{\"first_name\":\"Ada\"}");
    QCOMPARE(harness.server.processRequest(authorize).statusCode, 404);

    WebServer::HttpRequest registerRequest =
            harness.request(QStringLiteral("POST"), QStringLiteral("/api/register"));
    registerRequest.body = QByteArrayLiteral(
            "{\"login\":\"ada\",\"password\":\"password1\",\"first_name\":\"Ada\"}");
    QCOMPARE(harness.server.processRequest(registerRequest).statusCode, 404);

    WebServer::HttpRequest login =
            harness.request(QStringLiteral("POST"), QStringLiteral("/api/login"));
    login.body = QByteArrayLiteral("{\"login\":\"ada\",\"password\":\"password1\"}");
    QCOMPARE(harness.server.processRequest(login).statusCode, 400);

    WebServer::HttpRequest token = harness.request(
            QStringLiteral("GET"), QStringLiteral("/api/account/password-token?token=invalid"));
    QCOMPARE(harness.server.processRequest(token).statusCode, 400);

    const WebServer::HttpResponse runtime = harness.server.processRequest(
            harness.request(QStringLiteral("GET"), QStringLiteral("/api/runtime-config")));
    QCOMPARE(runtime.statusCode, 200);
    const QJsonArray methods = QJsonDocument::fromJson(runtime.body)
                                       .object()
                                       .value(QStringLiteral("auth_methods"))
                                       .toArray();
    QCOMPARE(methods.size(), 1);
    QCOMPARE(methods.at(0).toString(), QStringLiteral("log-in-only"));
}

void WebServerSecurityTests::disabledAuthMethodsRejectExtraPosts()
{
    TestWebServer withRegistration;
    withRegistration.server.setAuthMethods({ QStringLiteral("log-in-with-registration") });

    WebServer::HttpRequest authorize =
            withRegistration.request(QStringLiteral("POST"), QStringLiteral("/api/authorize"));
    authorize.body = QByteArrayLiteral("{\"first_name\":\"Ada\"}");
    QCOMPARE(withRegistration.server.processRequest(authorize).statusCode, 404);

    WebServer::HttpRequest registerRequest =
            withRegistration.request(QStringLiteral("POST"), QStringLiteral("/api/register"));
    registerRequest.body = QByteArrayLiteral(
            "{\"login\":\"ada\",\"password\":\"password1\",\"first_name\":\"Ada\"}");
    QVERIFY(withRegistration.server.processRequest(registerRequest).statusCode != 404);

    WebServer::HttpRequest login =
            withRegistration.request(QStringLiteral("POST"), QStringLiteral("/api/login"));
    login.body = QByteArrayLiteral("{\"login\":\"missing\",\"password\":\"password1\"}");
    QCOMPARE(withRegistration.server.processRequest(login).statusCode, 400);

    const WebServer::HttpResponse runtime = withRegistration.server.processRequest(
            withRegistration.request(QStringLiteral("GET"), QStringLiteral("/api/runtime-config")));
    QCOMPARE(runtime.statusCode, 200);
    const QJsonArray methods = QJsonDocument::fromJson(runtime.body)
                                       .object()
                                       .value(QStringLiteral("auth_methods"))
                                       .toArray();
    QCOMPARE(methods.size(), 1);
    QCOMPARE(methods.at(0).toString(), QStringLiteral("log-in-with-registration"));

    TestWebServer signWithout;
    WebServer::HttpRequest disabledRegister =
            signWithout.request(QStringLiteral("POST"), QStringLiteral("/api/register"));
    disabledRegister.body = QByteArrayLiteral(
            "{\"login\":\"ada\",\"password\":\"password1\",\"first_name\":\"Ada\"}");
    QCOMPARE(signWithout.server.processRequest(disabledRegister).statusCode, 404);

    WebServer::HttpRequest disabledLogin =
            signWithout.request(QStringLiteral("POST"), QStringLiteral("/api/login"));
    disabledLogin.body = QByteArrayLiteral("{\"login\":\"ada\",\"password\":\"password1\"}");
    QCOMPARE(signWithout.server.processRequest(disabledLogin).statusCode, 404);
}

BOOKING_RESOURCES_REGISTER_TEST(WebServerSecurityTests)

#include "WebServerSecurityTests.moc"
