#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <QList>
#include <QPair>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QByteArray>
#include <QJsonArray>
#include <QStringList>
#include <QJsonObject>
#include <QThreadPool>
#include <QHostAddress>

#include "WebAuthManager.h"
#include "WebSessionStore.h"

class WebClient;
class QTcpServer;
class QTcpSocket;
class QTimer;
class BookingResourcesTranslator;

class WebServer final : public QObject
{
    Q_OBJECT

public:
    enum class ListenerRole {
        HttpApplication,
        HttpRedirect,
        HttpsApplication
    };

    struct TlsSettings
    {
        bool enabled = false;
        QString certificateFile;
        QString privateKeyFile;
    };

    explicit WebServer(WebAuthManager *authManager,
                       WebClient *webClient,
                       WebSessionStore *sessionStore,
                       BookingResourcesTranslator *translator,
                       QObject *parent = nullptr);
    ~WebServer() override;

    void setThreadCount(int count);
    void setExternalHttpsEnabled(bool enabled);
    void setPageTitle(const QString &title);
    void setAuthMethods(const QStringList &methods);
    void setRedirectHttpToHttps(bool enabled);
    void setPublicHttpsHost(const QString &host);
    void setForwardedHeadersEnabled(bool enabled);
    void setTrustedProxies(const QStringList &proxies);
    void setTlsReloadIntervalSeconds(int seconds);
    bool setTlsSettings(const TlsSettings &settings);
    bool reloadTlsSettings();
    [[nodiscard]] int threadCount() const;
    [[nodiscard]] bool httpsEnabled() const;
    [[nodiscard]] bool secureTransportEnabled() const;
    [[nodiscard]] bool isListening() const;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] QString publicOrigin() const;

public slots:
    bool listen(const QHostAddress &address, quint16 port);
    bool listenHttp(const QHostAddress &address, quint16 port);
    bool listenHttpRedirect(const QHostAddress &address, quint16 port);
    bool listenHttps(const QHostAddress &address, quint16 port);
    void close();

signals:
    void errorOccurred(const QString &description);

private:
    class RequestTask;

    struct HttpRequest
    {
        QString method;
        QString path;
        QString remoteAddress;
        QString forwardedHost;
        QHash<QString, QString> headers;
        QByteArray body;
        ListenerRole listenerRole = ListenerRole::HttpApplication;
        bool secureTransport = false;
    };

    struct HttpResponse
    {
        int statusCode = 200;
        QByteArray reason = "OK";
        QByteArray contentType = "application/json; charset=utf-8";
        QByteArray body;
        QList<QPair<QByteArray, QByteArray>> headers;
        bool secureTransport = false;
    };

    struct Listener
    {
        QTcpServer *server = nullptr;
        ListenerRole role = ListenerRole::HttpApplication;
        quint16 port = 0;
    };

    void handleNewConnection(QTcpServer *server);
    void prepareSocket(QTcpSocket *socket);
    void activateHttpSocket(QTcpSocket *socket);
    void handleReadyRead(QTcpSocket *socket);
    bool tryParseRequest(QByteArray *buffer,
                         HttpRequest *request,
                         HttpResponse *errorResponseOut = nullptr) const;
    void dispatchRequest(QTcpSocket *socket, const HttpRequest &request);
    HttpResponse processRequest(const HttpRequest &request);
    void sendResponse(QTcpSocket *socket, const HttpResponse &response);
    void finishSocket(QTcpSocket *socket, const HttpResponse &response);

    HttpResponse handleHealth() const;
    HttpResponse handleHttpRedirect(const HttpRequest &request) const;
    HttpResponse handleIndex() const;
    HttpResponse handleScript() const;
    HttpResponse handleStaticFile(const QString &path) const;
    HttpResponse handleAuthorize(const HttpRequest &request);
    HttpResponse handleRegister(const HttpRequest &request);
    HttpResponse handleLogin(const HttpRequest &request);
    HttpResponse handleAccountPassword(const HttpRequest &request);
    HttpResponse handlePasswordToken(const HttpRequest &request);
    HttpResponse handlePasswordSet(const HttpRequest &request);
    HttpResponse handlePasswordResetByToken(const HttpRequest &request);
    HttpResponse handleLogout(const HttpRequest &request);
    HttpResponse handleMessage(const HttpRequest &request);
    HttpResponse handleCallback(const HttpRequest &request);
    HttpResponse handleEvents(const HttpRequest &request);
    HttpResponse handleFile(const HttpRequest &request);
    HttpResponse handleSession(const HttpRequest &request);
    HttpResponse handleRuntimeConfig() const;
    HttpResponse handleProfile(const HttpRequest &request);
    HttpResponse handleI18n(const HttpRequest &request);
    HttpResponse handleCommands(const HttpRequest &request);
    HttpResponse handleUserSettings(const HttpRequest &request);

    QString sessionToken(const HttpRequest &request) const;
    QString authenticatedUserId(const HttpRequest &request) const;
    QString preferredLocaleForRequest(const HttpRequest &request) const;
    QString messengerLocaleForRequest(const HttpRequest &request,
                                      const WebSessionStore::User &user) const;
    HttpResponse finalizeAuthorization(const HttpRequest &request,
                                       const WebAuthManager::AuthResult &auth);
    bool recordWebAuthorization(const QString &userId, QString *errorOut) const;
    bool secureTransportEnabled(const HttpRequest &request) const;
    bool isAuthorizeRateLimited(const QString &remoteAddress);
    QJsonObject userSettingsPayload(const HttpRequest &request,
                                    const QString &userId,
                                    const WebSessionStore::User &user) const;
    QByteArray contentTypeForPath(const QString &path) const;
    QByteArray sessionCookie(const QString &token, bool secureTransport) const;
    QByteArray sessionCookie(const QString &token) const;
    QByteArray expiredSessionCookie(bool secureTransport) const;
    QByteArray expiredSessionCookie() const;
    QJsonObject jsonBody(const HttpRequest &request, QString *errorOut = nullptr) const;
    bool isServerAvailable() const;
    HttpResponse serverUnavailableResponse() const;
    HttpResponse jsonResponse(const QJsonObject &object, int statusCode = 200) const;
    HttpResponse jsonArrayResponse(const QJsonArray &array) const;
    HttpResponse errorResponse(int statusCode,
                               const QString &message,
                               const QString &errorCode = QString()) const;
    bool listenWithRole(const QHostAddress &address, quint16 port, ListenerRole role);
    Listener *listenerForServer(QTcpServer *server);
    ListenerRole listenerRoleForSocket(QTcpSocket *socket) const;
    void applyTrustedProxyHeaders(HttpRequest *request) const;
    bool isTrustedProxy(const QHostAddress &address) const;
    QString redirectHost(const HttpRequest &request) const;
    QString firstHeaderValue(const QString &value) const;
    void setLastError(const QString &description);

    QList<Listener> m_listeners;
    WebAuthManager *m_authManager;
    WebClient *m_webClient;
    WebSessionStore *m_sessionStore;
    BookingResourcesTranslator *m_translator;
    TlsSettings m_tlsSettings;
    QTimer *m_tlsReloadTimer;
    bool m_httpsEnabled;
    bool m_externalHttpsEnabled;
    bool m_redirectHttpToHttps;
    bool m_forwardedHeadersEnabled;
    QString m_publicHttpsHost;
    QStringList m_trustedProxies;
    quint16 m_httpsPort;
    QString m_pageTitle;
    QStringList m_authMethods;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    QMutex m_rateLimitMutex;
    QHash<QString, QList<qint64>> m_authorizeAttemptsByRemote;
    QList<qint64> m_authorizeAttempts;
    QThreadPool m_threadPool;
    QString m_lastError;
};

#endif // WEBSERVER_H
