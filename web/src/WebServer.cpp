#include "WebServer.h"

#include "WebClient.h"
#include "WebAuthManager.h"
#include "WebAuthMethods.h"
#include "WebSessionStore.h"
#include "WebStaticTranslations.h"
#include "BookingResourcesTranslator.h"

#include <QUrl>
#include <QFile>
#include <QTimer>
#include <QThread>
#include <QPointer>
#include <QFileInfo>
#include <QUrlQuery>
#include <QRunnable>
#include <QDateTime>
#include <QJsonArray>
#include <QTcpServer>
#include <QTcpSocket>
#include <QMetaObject>
#include <QStringList>
#include <QJsonObject>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QCoreApplication>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcWebServer, "booking.web.server")

#if QT_CONFIG(ssl)
#  include <QSslCertificate>
#  include <QSslConfiguration>
#  include <QSslError>
#  include <QSslKey>
#  include <QSslSocket>
#endif

namespace {

constexpr int kMaxHeaderBytes = 16 * 1024;
constexpr int kMaxBodyBytes = 1024 * 1024;
constexpr int kRequestTimeoutMs = 30 * 1000;
constexpr int kMaxEventsWaitMs = 25 * 1000;
constexpr qint64 kAuthorizeRateLimitWindowSeconds = 60;
constexpr int kAuthorizeRateLimitPerRemote = 10;
constexpr int kAuthorizeRateLimitGlobal = 100;

bool looksLikeTlsClientHello(const QByteArray &buffer)
{
    if (buffer.size() < 3)
        return false;

    const uchar contentType = static_cast<uchar>(buffer.at(0));
    const uchar majorVersion = static_cast<uchar>(buffer.at(1));
    const uchar minorVersion = static_cast<uchar>(buffer.at(2));
    return contentType == 0x16 && majorVersion == 0x03 && minorVersion <= 0x04;
}

class WebTcpServer final : public QTcpServer
{
public:
    explicit WebTcpServer(QObject *parent = nullptr)
        : QTcpServer(parent)
        , m_tlsEnabled(false)
    {
        // do nothing
    }

    void clearTlsConfiguration()
    {
        m_tlsEnabled = false;
#if QT_CONFIG(ssl)
        m_tlsConfiguration = QSslConfiguration();
#endif
    }

#if QT_CONFIG(ssl)
    void setTlsConfiguration(const QSslConfiguration &configuration)
    {
        m_tlsConfiguration = configuration;
        m_tlsEnabled = true;
    }
#endif

protected:
    void incomingConnection(qintptr socketDescriptor) override
    {
        if (!m_tlsEnabled) {
            QTcpServer::incomingConnection(socketDescriptor);
            return;
        }

#if QT_CONFIG(ssl)
        QSslSocket *socket = new QSslSocket(this);
        if (!socket->setSocketDescriptor(socketDescriptor)) {
            delete socket;
            return;
        }

        socket->setSslConfiguration(m_tlsConfiguration);
        addPendingConnection(socket);
#else
        QTcpSocket *socket = new QTcpSocket(this);
        if (socket->setSocketDescriptor(socketDescriptor))
            socket->disconnectFromHost();
        delete socket;
#endif
    }

private:
    bool m_tlsEnabled;
#if QT_CONFIG(ssl)
    QSslConfiguration m_tlsConfiguration;
#endif
};

QByteArray reasonPhrase(int statusCode)
{
    switch (statusCode) {
    case 200:
        return QByteArrayLiteral("OK");
    case 202:
        return QByteArrayLiteral("Accepted");
    case 308:
        return QByteArrayLiteral("Permanent Redirect");
    case 400:
        return QByteArrayLiteral("Bad Request");
    case 401:
        return QByteArrayLiteral("Unauthorized");
    case 404:
        return QByteArrayLiteral("Not Found");
    case 405:
        return QByteArrayLiteral("Method Not Allowed");
    case 413:
        return QByteArrayLiteral("Payload Too Large");
    case 429:
        return QByteArrayLiteral("Too Many Requests");
    case 500:
        return QByteArrayLiteral("Internal Server Error");
    case 503:
        return QByteArrayLiteral("Service Unavailable");
    default:
        return QByteArrayLiteral("OK");
    }
}

QByteArray statusLine(int statusCode)
{
    return QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(statusCode) + QByteArrayLiteral(" ")
           + reasonPhrase(statusCode) + QByteArrayLiteral("\r\n");
}

QString normalizedPath(const QString &rawPath)
{
    const QUrl url(rawPath);
    const QString path = url.path().trimmed();
    return path.isEmpty() ? QStringLiteral("/") : path;
}

#if QT_CONFIG(ssl)
QSslKey loadPrivateKey(const QByteArray &data)
{
    const QList<QSsl::KeyAlgorithm> algorithms = { QSsl::Rsa, QSsl::Dsa, QSsl::Ec };
    for (const QSsl::KeyAlgorithm algorithm : algorithms) {
        const QSslKey key(data, algorithm, QSsl::Pem);
        if (!key.isNull())
            return key;
    }
    return QSslKey();
}
#endif

QString safeDownloadFileName(const QString &fileName)
{
    QString result = fileName.trimmed();
    result.remove(QLatin1Char('\r'));
    result.remove(QLatin1Char('\n'));
    if (result.isEmpty())
        result = QStringLiteral("download");
    return result;
}

QByteArray contentDispositionHeader(const QString &fileName)
{
    const QString safeName = safeDownloadFileName(fileName);
    QString fallbackName;
    fallbackName.reserve(safeName.size());
    for (const QChar ch : safeName) {
        const ushort unicode = ch.unicode();
        if (unicode >= 0x20 && unicode <= 0x7e && ch != QLatin1Char('"') && ch != QLatin1Char('\\')
            && ch != QLatin1Char(';')) {
            fallbackName.append(ch);
        } else {
            fallbackName.append(QLatin1Char('_'));
        }
    }
    if (fallbackName.trimmed().isEmpty())
        fallbackName = QStringLiteral("download");

    return QByteArrayLiteral("attachment; filename=\"") + fallbackName.toLatin1()
           + QByteArrayLiteral("\"; filename*=UTF-8''") + QUrl::toPercentEncoding(safeName);
}

} // namespace

class WebServer::RequestTask final : public QRunnable
{
public:
    RequestTask(WebServer *server, QTcpSocket *socket, const WebServer::HttpRequest &request)
        : m_server(server)
        , m_socket(socket)
        , m_request(request)
    {
        // do nothing
    }

    void run() override
    {
        WebServer *server = m_server.data();
        if (!server)
            return;

        const WebServer::HttpResponse response = server->processRequest(m_request);
        QMetaObject::invokeMethod(
                server,
                [serverPointer = m_server, socketPointer = m_socket, response]() {
                    WebServer *targetServer = serverPointer.data();
                    QTcpSocket *targetSocket = socketPointer.data();
                    if (targetServer && targetSocket)
                        targetServer->finishSocket(targetSocket, response);
                },
                Qt::QueuedConnection);
    }

private:
    QPointer<WebServer> m_server;
    QPointer<QTcpSocket> m_socket;
    WebServer::HttpRequest m_request;
};

WebServer::WebServer(WebAuthManager *authManager,
                     WebClient *webClient,
                     WebSessionStore *sessionStore,
                     BookingResourcesTranslator *translator,
                     QObject *parent)
    : QObject(parent)
    , m_listeners()
    , m_authManager(authManager)
    , m_webClient(webClient)
    , m_sessionStore(sessionStore)
    , m_translator(translator)
    , m_tlsSettings()
    , m_tlsReloadTimer(new QTimer(this))
    , m_httpsEnabled(false)
    , m_externalHttpsEnabled(false)
    , m_redirectHttpToHttps(true)
    , m_forwardedHeadersEnabled(false)
    , m_publicHttpsHost()
    , m_trustedProxies()
    , m_httpsPort(0)
    , m_pageTitle(QStringLiteral("Booking Resources"))
    , m_authMethods(WebAuthMethods::defaults())
    , m_buffers()
    , m_rateLimitMutex()
    , m_authorizeAttemptsByRemote()
    , m_authorizeAttempts()
    , m_threadPool()
    , m_lastError()
{
    QObject::connect(m_tlsReloadTimer, &QTimer::timeout, this, &WebServer::reloadTlsSettings);
    setThreadCount(QThread::idealThreadCount());
}

WebServer::~WebServer()
{
    close();
}

void WebServer::setThreadCount(int count)
{
    m_threadPool.setMaxThreadCount(qMax(1, count));
}

void WebServer::setExternalHttpsEnabled(bool enabled)
{
    m_externalHttpsEnabled = enabled;
}

void WebServer::setPageTitle(const QString &title)
{
    const QString normalized = title.trimmed();
    m_pageTitle = normalized.isEmpty() ? QStringLiteral("Booking Resources") : normalized;
}

void WebServer::setAuthMethods(const QStringList &methods)
{
    m_authMethods = WebAuthMethods::normalizeList(methods);
}

void WebServer::setRedirectHttpToHttps(bool enabled)
{
    m_redirectHttpToHttps = enabled;
}

void WebServer::setPublicHttpsHost(const QString &host)
{
    m_publicHttpsHost = host.trimmed();
}

void WebServer::setForwardedHeadersEnabled(bool enabled)
{
    m_forwardedHeadersEnabled = enabled;
}

void WebServer::setTrustedProxies(const QStringList &proxies)
{
    m_trustedProxies.clear();
    for (const QString &proxy : proxies) {
        const QString normalized = proxy.trimmed();
        if (!normalized.isEmpty())
            m_trustedProxies.append(normalized);
    }
}

void WebServer::setTlsReloadIntervalSeconds(int seconds)
{
    if (seconds <= 0) {
        m_tlsReloadTimer->stop();
        return;
    }

    m_tlsReloadTimer->setInterval(seconds * 1000);
    if (m_tlsSettings.enabled)
        m_tlsReloadTimer->start();
}

bool WebServer::setTlsSettings(const TlsSettings &settings)
{
    m_tlsSettings = settings;
    m_httpsEnabled = false;
    for (Listener &listener : m_listeners) {
        if (listener.role == ListenerRole::HttpsApplication)
            static_cast<WebTcpServer *>(listener.server)->clearTlsConfiguration();
    }

    if (!settings.enabled) {
        m_tlsReloadTimer->stop();
        m_lastError.clear();
        return true;
    }

#if !QT_CONFIG(ssl)
    setLastError(tr("HTTPS is not available because Qt was built without SSL support."));
    return false;
#else
    if (!QSslSocket::supportsSsl()) {
        setLastError(tr("HTTPS is not available because no SSL backend is available."));
        return false;
    }
    if (settings.certificateFile.trimmed().isEmpty()) {
        setLastError(tr("TLS certificate file is required when HTTPS is enabled."));
        return false;
    }
    if (settings.privateKeyFile.trimmed().isEmpty()) {
        setLastError(tr("TLS private key file is required when HTTPS is enabled."));
        return false;
    }

    QFile certificateFile(settings.certificateFile);
    if (!certificateFile.open(QIODevice::ReadOnly)) {
        setLastError(tr("Unable to read TLS certificate file \"%1\": %2")
                             .arg(settings.certificateFile, certificateFile.errorString()));
        return false;
    }

    const QList<QSslCertificate> certificates =
            QSslCertificate::fromDevice(&certificateFile, QSsl::Pem);
    if (certificates.isEmpty()) {
        setLastError(tr("TLS certificate file \"%1\" does not contain a PEM certificate.")
                             .arg(settings.certificateFile));
        return false;
    }

    QFile privateKeyFile(settings.privateKeyFile);
    if (!privateKeyFile.open(QIODevice::ReadOnly)) {
        setLastError(tr("Unable to read TLS private key file \"%1\": %2")
                             .arg(settings.privateKeyFile, privateKeyFile.errorString()));
        return false;
    }

    const QSslKey privateKey = loadPrivateKey(privateKeyFile.readAll());
    if (privateKey.isNull()) {
        setLastError(tr("TLS private key file \"%1\" does not contain a supported PEM key.")
                             .arg(settings.privateKeyFile));
        return false;
    }

    QSslConfiguration sslConfiguration = QSslConfiguration::defaultConfiguration();
    sslConfiguration.setLocalCertificateChain(certificates);
    sslConfiguration.setPrivateKey(privateKey);
    // AutoVerifyPeer asks browsers for a client certificate on server sockets.
    sslConfiguration.setPeerVerifyMode(QSslSocket::VerifyNone);

    for (Listener &listener : m_listeners) {
        if (listener.role == ListenerRole::HttpsApplication)
            static_cast<WebTcpServer *>(listener.server)->setTlsConfiguration(sslConfiguration);
    }
    m_httpsEnabled = true;
    m_lastError.clear();
    if (m_tlsReloadTimer->interval() > 0)
        m_tlsReloadTimer->start();
    return true;
#endif
}

bool WebServer::reloadTlsSettings()
{
    if (!m_tlsSettings.enabled)
        return true;

#if !QT_CONFIG(ssl)
    setLastError(tr("HTTPS is not available because Qt was built without SSL support."));
    return false;
#else
    if (!QSslSocket::supportsSsl()) {
        setLastError(tr("HTTPS is not available because no SSL backend is available."));
        return false;
    }

    QFile certificateFile(m_tlsSettings.certificateFile);
    if (!certificateFile.open(QIODevice::ReadOnly)) {
        setLastError(tr("Unable to read TLS certificate file \"%1\": %2")
                             .arg(m_tlsSettings.certificateFile, certificateFile.errorString()));
        return false;
    }

    const QList<QSslCertificate> certificates =
            QSslCertificate::fromDevice(&certificateFile, QSsl::Pem);
    if (certificates.isEmpty()) {
        setLastError(tr("TLS certificate file \"%1\" does not contain a PEM certificate.")
                             .arg(m_tlsSettings.certificateFile));
        return false;
    }

    QFile privateKeyFile(m_tlsSettings.privateKeyFile);
    if (!privateKeyFile.open(QIODevice::ReadOnly)) {
        setLastError(tr("Unable to read TLS private key file \"%1\": %2")
                             .arg(m_tlsSettings.privateKeyFile, privateKeyFile.errorString()));
        return false;
    }

    const QSslKey privateKey = loadPrivateKey(privateKeyFile.readAll());
    if (privateKey.isNull()) {
        setLastError(tr("TLS private key file \"%1\" does not contain a supported PEM key.")
                             .arg(m_tlsSettings.privateKeyFile));
        return false;
    }

    QSslConfiguration sslConfiguration = QSslConfiguration::defaultConfiguration();
    sslConfiguration.setLocalCertificateChain(certificates);
    sslConfiguration.setPrivateKey(privateKey);
    // AutoVerifyPeer asks browsers for a client certificate on server sockets.
    sslConfiguration.setPeerVerifyMode(QSslSocket::VerifyNone);

    for (Listener &listener : m_listeners) {
        if (listener.role == ListenerRole::HttpsApplication)
            static_cast<WebTcpServer *>(listener.server)->setTlsConfiguration(sslConfiguration);
    }
    m_httpsEnabled = true;
    m_lastError.clear();
    qCInfo(lcWebServer) << "Reloaded TLS certificate and private key for future connections.";
    return true;
#endif
}

int WebServer::threadCount() const
{
    return m_threadPool.maxThreadCount();
}

bool WebServer::httpsEnabled() const
{
    return m_httpsEnabled;
}

bool WebServer::secureTransportEnabled() const
{
    return m_httpsEnabled || m_externalHttpsEnabled;
}

bool WebServer::isListening() const
{
    for (const Listener &listener : m_listeners) {
        if (listener.server && listener.server->isListening())
            return true;
    }
    return false;
}

QString WebServer::lastError() const
{
    return m_lastError;
}

QString WebServer::publicOrigin() const
{
    if (!m_publicHttpsHost.trimmed().isEmpty()) {
        QString host = m_publicHttpsHost.trimmed();
        if (host.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive))
            host.remove(0, 7);
        if (host.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))
            host.remove(0, 8);
        host = host.section(QLatin1Char('/'), 0, 0);
        QUrl origin;
        origin.setScheme(QStringLiteral("https"));
        const QUrl parsed(QStringLiteral("https://%1").arg(host));
        origin.setHost(parsed.host().isEmpty() ? host : parsed.host());
        int port = parsed.port(-1);
        if (port < 0 && m_httpsPort != 0 && m_httpsPort != 443)
            port = m_httpsPort;
        if (port > 0 && port != 443)
            origin.setPort(port);
        return origin.toString();
    }

    quint16 httpsPort = m_httpsPort;
    quint16 httpPort = 0;
    QHostAddress bindAddress = QHostAddress::LocalHost;
    bool hasHttps = m_httpsEnabled || m_externalHttpsEnabled;
    bool hasHttp = false;
    for (const Listener &listener : m_listeners) {
        if (!listener.server || !listener.server->isListening())
            continue;
        if (listener.role == ListenerRole::HttpsApplication) {
            hasHttps = true;
            httpsPort = listener.port;
            bindAddress = listener.server->serverAddress();
        } else if (listener.role == ListenerRole::HttpApplication) {
            hasHttp = true;
            httpPort = listener.port;
            if (!hasHttps)
                bindAddress = listener.server->serverAddress();
        }
    }

    QString host = QStringLiteral("127.0.0.1");
    if (!bindAddress.isNull() && bindAddress != QHostAddress::Any
        && bindAddress != QHostAddress::AnyIPv4 && bindAddress != QHostAddress::AnyIPv6
        && !bindAddress.isLoopback()) {
        host = bindAddress.toString();
    }

    QUrl origin;
    if (hasHttps) {
        origin.setScheme(QStringLiteral("https"));
        origin.setHost(host);
        if (httpsPort != 0 && httpsPort != 443)
            origin.setPort(httpsPort);
        return origin.toString();
    }

    origin.setScheme(QStringLiteral("http"));
    origin.setHost(host);
    if (hasHttp && httpPort != 0 && httpPort != 80)
        origin.setPort(httpPort);
    return origin.toString();
}

bool WebServer::listen(const QHostAddress &address, quint16 port)
{
    return listenHttp(address, port);
}

bool WebServer::listenHttp(const QHostAddress &address, quint16 port)
{
    return listenWithRole(address, port, ListenerRole::HttpApplication);
}

bool WebServer::listenHttpRedirect(const QHostAddress &address, quint16 port)
{
    return listenWithRole(address, port, ListenerRole::HttpRedirect);
}

bool WebServer::listenHttps(const QHostAddress &address, quint16 port)
{
    if (!m_tlsSettings.enabled) {
        setLastError(QStringLiteral("HTTPS listener requires enabled TLS settings."));
        return false;
    }

    const bool listening = listenWithRole(address, port, ListenerRole::HttpsApplication);
    if (listening)
        m_httpsPort = port;
    return listening;
}

bool WebServer::listenWithRole(const QHostAddress &address, quint16 port, ListenerRole role)
{
    WebTcpServer *server = new WebTcpServer(this);
    if (role == ListenerRole::HttpsApplication) {
        if (!m_tlsSettings.enabled) {
            server->deleteLater();
            setLastError(QStringLiteral("HTTPS listener requires enabled TLS settings."));
            return false;
        }
    }

    Listener listener;
    listener.server = server;
    listener.role = role;
    listener.port = port;
    m_listeners.append(listener);

    if (role == ListenerRole::HttpsApplication && !setTlsSettings(m_tlsSettings)) {
        m_listeners.removeLast();
        server->deleteLater();
        return false;
    }

    if (!server->listen(address, port)) {
        setLastError(server->errorString());
        m_listeners.removeLast();
        server->deleteLater();
        return false;
    }

    m_listeners.last().port = server->serverPort();

    QObject::connect(server, &QTcpServer::newConnection, this, [this, server]() {
        handleNewConnection(server);
    });
    return true;
}

void WebServer::close()
{
    m_tlsReloadTimer->stop();
    for (const Listener &listener : m_listeners) {
        if (listener.server) {
            listener.server->close();
            listener.server->deleteLater();
        }
    }
    m_listeners.clear();
    QList<QTcpSocket *> sockets = m_buffers.keys();
    const QList<QTcpSocket *> childSockets = findChildren<QTcpSocket *>();
    for (QTcpSocket *socket : childSockets) {
        if (!sockets.contains(socket))
            sockets.append(socket);
    }
    for (QTcpSocket *socket : sockets) {
        if (socket)
            socket->disconnectFromHost();
    }
    m_buffers.clear();
    m_threadPool.waitForDone();
}

void WebServer::handleNewConnection(QTcpServer *server)
{
    Listener *listener = listenerForServer(server);
    if (!listener)
        return;

    while (server->hasPendingConnections()) {
        QTcpSocket *socket = server->nextPendingConnection();
        if (!socket)
            continue;

        prepareSocket(socket);
        socket->setProperty("bookingResources.listenerRole", static_cast<int>(listener->role));
        if (listener->role != ListenerRole::HttpsApplication) {
            activateHttpSocket(socket);
            continue;
        }

#if QT_CONFIG(ssl)
        QSslSocket *sslSocket = qobject_cast<QSslSocket *>(socket);
        if (!sslSocket) {
            socket->disconnectFromHost();
            continue;
        }

        QObject::connect(sslSocket, &QSslSocket::encrypted, this, [this, sslSocket]() {
            activateHttpSocket(sslSocket);
        });
        QObject::connect(sslSocket,
                         QOverload<const QList<QSslError> &>::of(&QSslSocket::sslErrors),
                         sslSocket,
                         [sslSocket](const QList<QSslError> &) {
                             sslSocket->disconnectFromHost();
                         });
        sslSocket->startServerEncryption();
#else
        socket->disconnectFromHost();
#endif
    }
}

void WebServer::prepareSocket(QTcpSocket *socket)
{
    if (!socket)
        return;

    socket->setParent(this);

    QTimer *requestTimer = new QTimer(socket);
    requestTimer->setObjectName(QStringLiteral("bookingResources.requestTimer"));
    requestTimer->setSingleShot(true);
    requestTimer->setInterval(kRequestTimeoutMs);
    QObject::connect(requestTimer, &QTimer::timeout, socket, &QTcpSocket::disconnectFromHost);
    requestTimer->start();

    QObject::connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
        m_buffers.remove(socket);
        socket->deleteLater();
    });
}

void WebServer::activateHttpSocket(QTcpSocket *socket)
{
    if (!socket || m_buffers.contains(socket))
        return;

    m_buffers.insert(socket, QByteArray());
    QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
        QTimer *timer = socket ? socket->findChild<QTimer *>(
                                         QStringLiteral("bookingResources.requestTimer"))
                               : nullptr;
        if (timer)
            timer->start();
        handleReadyRead(socket);
    });

    if (socket->bytesAvailable() > 0)
        handleReadyRead(socket);
}

void WebServer::handleReadyRead(QTcpSocket *socket)
{
    if (!socket)
        return;
    if (socket->property("bookingResources.processing").toBool())
        return;

    QByteArray &buffer = m_buffers[socket];
    buffer.append(socket->readAll());
    if (looksLikeTlsClientHello(buffer)) {
        socket->disconnectFromHost();
        return;
    }

    HttpRequest request;
    HttpResponse errorResponse;
    if (!tryParseRequest(&buffer, &request, &errorResponse)) {
        if (errorResponse.statusCode != 200)
            finishSocket(socket, errorResponse);
        return;
    }
    request.remoteAddress = socket->peerAddress().toString();
    request.listenerRole = listenerRoleForSocket(socket);
    request.secureTransport = request.listenerRole == ListenerRole::HttpsApplication;
    applyTrustedProxyHeaders(&request);

    m_buffers.remove(socket);
    dispatchRequest(socket, request);
}

bool WebServer::tryParseRequest(QByteArray *buffer,
                                HttpRequest *request,
                                HttpResponse *errorResponseOut) const
{
    if (!buffer || !request)
        return false;

    const int headerEnd = buffer->indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        if (buffer->size() > kMaxHeaderBytes && errorResponseOut) {
            *errorResponseOut = errorResponse(413, tr("Request headers are too large."));
        }
        return false;
    }

    if (headerEnd > kMaxHeaderBytes) {
        if (errorResponseOut)
            *errorResponseOut = errorResponse(413, tr("Request headers are too large."));
        return false;
    }

    const QByteArray headerBlock = buffer->left(headerEnd);
    const QList<QByteArray> lines = headerBlock.split('\n');
    if (lines.isEmpty())
        return false;

    const QList<QByteArray> requestLine = lines.constFirst().trimmed().split(' ');
    if (requestLine.size() < 2)
        return false;

    HttpRequest parsed;
    parsed.method = QString::fromLatin1(requestLine.at(0)).trimmed().toUpper();
    parsed.path = QString::fromUtf8(requestLine.at(1)).trimmed();

    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        const QString key = QString::fromLatin1(line.left(colon)).trimmed().toLower();
        const QString value = QString::fromUtf8(line.mid(colon + 1)).trimmed();
        parsed.headers.insert(key, value);
    }

    bool contentLengthOk = true;
    const QString contentLengthText = parsed.headers.value(QStringLiteral("content-length"));
    const int contentLength =
            contentLengthText.isEmpty() ? 0 : contentLengthText.toInt(&contentLengthOk);
    if (!contentLengthOk || contentLength < 0) {
        if (errorResponseOut)
            *errorResponseOut = errorResponse(400, tr("Invalid Content-Length header."));
        return false;
    }
    if (contentLength > kMaxBodyBytes) {
        if (errorResponseOut)
            *errorResponseOut = errorResponse(413, tr("Request body is too large."));
        return false;
    }

    const int totalSize = headerEnd + 4 + qMax(0, contentLength);
    if (buffer->size() < totalSize)
        return false;

    parsed.body = buffer->mid(headerEnd + 4, qMax(0, contentLength));
    buffer->remove(0, totalSize);
    *request = parsed;
    return true;
}

void WebServer::dispatchRequest(QTcpSocket *socket, const HttpRequest &request)
{
    if (!socket)
        return;

    socket->setProperty("bookingResources.processing", true);
    if (QTimer *timer =
                socket->findChild<QTimer *>(QStringLiteral("bookingResources.requestTimer")))
        timer->stop();
    m_threadPool.start(new RequestTask(this, socket, request));
}

WebServer::HttpResponse WebServer::processRequest(const HttpRequest &request)
{
    const QString path = normalizedPath(request.path);
    // Isolate /api/i18n from Accept-Language so a nested useLocale("ru")
    // cannot suppress a matching translator installed for ru-RU.
    const QString preferredLocale =
            (request.method == QLatin1String("GET") && path == QLatin1String("/api/i18n"))
                    ? QString()
                    : preferredLocaleForRequest(request);
    BookingResourcesTranslator::ScopedLocaleOverride localeGuard =
            m_translator ? m_translator->useLocale(preferredLocale)
                         : BookingResourcesTranslator::ScopedLocaleOverride();
    Q_UNUSED(localeGuard);

    if (request.listenerRole == ListenerRole::HttpRedirect)
        return handleHttpRedirect(request);

    HttpResponse response;
    if (request.method == QLatin1String("GET") && path == QLatin1String("/health")) {
        response = handleHealth();
    } else if (request.method == QLatin1String("GET") && path == QLatin1String("/")) {
        response = handleIndex();
    } else if (request.method == QLatin1String("GET") && path == QLatin1String("/index.html")) {
        response = handleIndex();
    } else if (request.method == QLatin1String("GET") && path == QLatin1String("/api/session")) {
        response = handleSession(request);
    } else if (request.method == QLatin1String("GET")
               && path == QLatin1String("/api/runtime-config")) {
        response = handleRuntimeConfig();
    } else if (request.method == QLatin1String("POST") && path == QLatin1String("/api/profile")) {
        response = handleProfile(request);
    } else if (request.method == QLatin1String("GET") && path == QLatin1String("/api/i18n")) {
        response = handleI18n(request);
    } else if (request.method == QLatin1String("GET") && path == QLatin1String("/api/commands")) {
        response = handleCommands(request);
    } else if ((request.method == QLatin1String("GET") || request.method == QLatin1String("POST"))
               && path == QLatin1String("/api/user-settings")) {
        response = handleUserSettings(request);
    } else if (request.method == QLatin1String("POST") && path == QLatin1String("/api/authorize")) {
        response = handleAuthorize(request);
    } else if (request.method == QLatin1String("POST") && path == QLatin1String("/api/register")) {
        response = handleRegister(request);
    } else if (request.method == QLatin1String("POST") && path == QLatin1String("/api/login")) {
        response = handleLogin(request);
    } else if (request.method == QLatin1String("POST")
               && path == QLatin1String("/api/account/password")) {
        response = handleAccountPassword(request);
    } else if (request.method == QLatin1String("GET")
               && path == QLatin1String("/api/account/password-token")) {
        response = handlePasswordToken(request);
    } else if (request.method == QLatin1String("POST")
               && path == QLatin1String("/api/account/password-set")) {
        response = handlePasswordSet(request);
    } else if (request.method == QLatin1String("POST")
               && path == QLatin1String("/api/account/password-reset")) {
        response = handlePasswordResetByToken(request);
    } else if (request.method == QLatin1String("POST") && path == QLatin1String("/api/logout")) {
        response = handleLogout(request);
    } else if (request.method == QLatin1String("POST") && path == QLatin1String("/api/messages")) {
        response = handleMessage(request);
    } else if (request.method == QLatin1String("POST") && path == QLatin1String("/api/callbacks")) {
        response = handleCallback(request);
    } else if (request.method == QLatin1String("GET") && path == QLatin1String("/api/events")) {
        response = handleEvents(request);
    } else if (request.method == QLatin1String("GET")
               && path.startsWith(QStringLiteral("/api/files/"))) {
        response = handleFile(request);
    } else if (request.method == QLatin1String("GET")) {
        response = handleStaticFile(path);
    } else {
        response = errorResponse(404, tr("Endpoint not found."));
    }

    response.secureTransport = secureTransportEnabled(request);
    return response;
}

void WebServer::sendResponse(QTcpSocket *socket, const HttpResponse &response)
{
    if (!socket)
        return;

    QByteArray data = statusLine(response.statusCode);
    data += QByteArrayLiteral("Connection: close\r\n");
    data += QByteArrayLiteral("Content-Type: ") + response.contentType + QByteArrayLiteral("\r\n");
    data += QByteArrayLiteral("Content-Length: ") + QByteArray::number(response.body.size())
            + QByteArrayLiteral("\r\n");
    data += QByteArrayLiteral("X-Content-Type-Options: nosniff\r\n");
    data += QByteArrayLiteral("Referrer-Policy: strict-origin-when-cross-origin\r\n");
    data += QByteArrayLiteral("X-Frame-Options: DENY\r\n");
    data += QByteArrayLiteral("Permissions-Policy: camera=(), microphone=(), geolocation=()\r\n");
    data += QByteArrayLiteral(
            "Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; "
            "img-src 'self' data:; connect-src 'self'; object-src 'none'; base-uri 'none'; "
            "frame-ancestors 'none'; form-action 'self'\r\n");
    bool hasCacheControl = false;
    for (const QPair<QByteArray, QByteArray> &header : response.headers) {
        if (header.first.compare(QByteArrayLiteral("Cache-Control"), Qt::CaseInsensitive) == 0) {
            hasCacheControl = true;
            break;
        }
    }
    if (response.contentType.startsWith(QByteArrayLiteral("application/json")) && !hasCacheControl)
        data += QByteArrayLiteral("Cache-Control: no-store\r\n");
    if (response.secureTransport)
        data += QByteArrayLiteral("Strict-Transport-Security: max-age=31536000\r\n");
    for (const QPair<QByteArray, QByteArray> &header : response.headers) {
        data += header.first + QByteArrayLiteral(": ") + header.second + QByteArrayLiteral("\r\n");
    }
    data += QByteArrayLiteral("\r\n");
    data += response.body;
    socket->write(data);
}

void WebServer::finishSocket(QTcpSocket *socket, const HttpResponse &response)
{
    sendResponse(socket, response);
    if (socket)
        socket->disconnectFromHost();
}

WebServer::HttpResponse WebServer::handleHealth() const
{
    QJsonObject body;
    body.insert(QStringLiteral("ok"), true);
    return jsonResponse(body);
}

WebServer::HttpResponse WebServer::handleHttpRedirect(const HttpRequest &request) const
{
    HttpResponse response;
    response.statusCode = 308;
    response.reason = reasonPhrase(response.statusCode);
    response.contentType = QByteArrayLiteral("text/plain; charset=utf-8");
    response.body = QByteArrayLiteral("Redirecting to HTTPS.");

    QUrl requestUrl(request.path);
    QString targetPath = requestUrl.path().trimmed();
    if (targetPath.isEmpty())
        targetPath = normalizedPath(request.path);
    if (targetPath.isEmpty())
        targetPath = QStringLiteral("/");
    if (requestUrl.hasQuery())
        targetPath += QLatin1Char('?') + requestUrl.query(QUrl::FullyEncoded);

    const QString target = QStringLiteral("https://%1%2").arg(redirectHost(request), targetPath);
    response.headers.append({ QByteArrayLiteral("Location"), target.toUtf8() });
    response.headers.append({ QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("no-store") });
    return response;
}

WebServer::HttpResponse WebServer::handleIndex() const
{
    return handleStaticFile(QStringLiteral("/index.html"));
}

WebServer::HttpResponse WebServer::handleScript() const
{
    return handleStaticFile(QStringLiteral("/app.js"));
}

WebServer::HttpResponse WebServer::handleStaticFile(const QString &path) const
{
    const QString normalized = normalizedPath(path);
    const QString resourcePath = QStringLiteral(":/web%1").arg(normalized);
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly))
        return errorResponse(404, tr("Endpoint not found."));

    HttpResponse response;
    response.contentType = contentTypeForPath(normalized);
    response.body = file.readAll();
    return response;
}

WebServer::HttpResponse WebServer::handleAuthorize(const HttpRequest &request)
{
    if (!WebAuthMethods::allowsSignWithoutRegistration(m_authMethods))
        return errorResponse(404, tr("Authorization method is not enabled."));

    if (isAuthorizeRateLimited(request.remoteAddress))
        return errorResponse(429, tr("Too many authorization attempts. Try again later."));

    QString error;
    const QJsonObject body = jsonBody(request, &error);
    if (!error.isEmpty())
        return errorResponse(400, error);

    const WebAuthManager::AuthResult auth = m_authManager->authorizeUser(body);
    if (!auth.ok)
        return errorResponse(400, auth.error);

    return finalizeAuthorization(request, auth);
}

WebServer::HttpResponse WebServer::handleRegister(const HttpRequest &request)
{
    if (!WebAuthMethods::allowsRegistration(m_authMethods))
        return errorResponse(404, tr("Authorization method is not enabled."));

    if (isAuthorizeRateLimited(request.remoteAddress))
        return errorResponse(429, tr("Too many authorization attempts. Try again later."));

    QString error;
    const QJsonObject body = jsonBody(request, &error);
    if (!error.isEmpty())
        return errorResponse(400, error);

    const WebAuthManager::AuthResult auth = m_authManager->registerLocalPasswordUser(body);
    if (!auth.ok)
        return errorResponse(400, auth.error);

    return finalizeAuthorization(request, auth);
}

WebServer::HttpResponse WebServer::handleLogin(const HttpRequest &request)
{
    if (!WebAuthMethods::allowsLogin(m_authMethods))
        return errorResponse(404, tr("Authorization method is not enabled."));

    if (isAuthorizeRateLimited(request.remoteAddress))
        return errorResponse(429, tr("Too many authorization attempts. Try again later."));

    QString error;
    const QJsonObject body = jsonBody(request, &error);
    if (!error.isEmpty())
        return errorResponse(400, error);

    const WebAuthManager::AuthResult auth = m_authManager->loginLocalPasswordUser(body);
    if (!auth.ok)
        return errorResponse(400, auth.error);

    return finalizeAuthorization(request, auth);
}

WebServer::HttpResponse WebServer::handleAccountPassword(const HttpRequest &request)
{
    if (!WebAuthMethods::allowsLocalCredentials(m_authMethods))
        return errorResponse(404, tr("Authorization method is not enabled."));

    const QString userId = authenticatedUserId(request);
    if (userId.isEmpty())
        return errorResponse(401, tr("Authentication is required."));

    QString error;
    const QJsonObject body = jsonBody(request, &error);
    if (!error.isEmpty())
        return errorResponse(400, error);

    const WebAuthManager::AuthResult auth = m_authManager->changeLocalPassword(userId, body);
    if (!auth.ok)
        return errorResponse(400, auth.error);

    QJsonObject responseBody;
    responseBody.insert(QStringLiteral("ok"), true);
    responseBody.insert(QStringLiteral("user_id"), auth.userId);
    const WebSessionStore::Credential credential =
            m_sessionStore ? m_sessionStore->credentialForUser(auth.userId)
                           : WebSessionStore::Credential();
    if (credential.isValid())
        responseBody.insert(QStringLiteral("local_password_login"), credential.login);
    return jsonResponse(responseBody);
}

WebServer::HttpResponse WebServer::handlePasswordToken(const HttpRequest &request)
{
    if (!WebAuthMethods::allowsLocalCredentials(m_authMethods))
        return errorResponse(404, tr("Authorization method is not enabled."));

    if (isAuthorizeRateLimited(request.remoteAddress))
        return errorResponse(429, tr("Too many authorization attempts. Try again later."));

    const QUrl url(request.path);
    const QUrlQuery query(url);
    const QString token = query.queryItemValue(QStringLiteral("token")).trimmed();
    QString userId;
    QString purpose;
    if (!m_sessionStore || !m_sessionStore->lookupPasswordResetToken(token, &userId, &purpose))
        return errorResponse(400, tr("Password link is invalid or has expired."));

    const WebSessionStore::Credential credential = m_sessionStore->credentialForUser(userId);
    QJsonObject body;
    body.insert(QStringLiteral("ok"), true);
    body.insert(QStringLiteral("purpose"), purpose);
    if (credential.isValid())
        body.insert(QStringLiteral("login"), credential.login);
    return jsonResponse(body);
}

WebServer::HttpResponse WebServer::handlePasswordSet(const HttpRequest &request)
{
    if (!WebAuthMethods::allowsLocalCredentials(m_authMethods))
        return errorResponse(404, tr("Authorization method is not enabled."));

    if (isAuthorizeRateLimited(request.remoteAddress))
        return errorResponse(429, tr("Too many authorization attempts. Try again later."));

    QString error;
    const QJsonObject body = jsonBody(request, &error);
    if (!error.isEmpty())
        return errorResponse(400, error);

    const WebAuthManager::AuthResult auth =
            m_authManager->setPasswordByToken(body, QStringLiteral("set_password"));
    if (!auth.ok)
        return errorResponse(400, auth.error);

    QJsonObject responseBody;
    responseBody.insert(QStringLiteral("ok"), true);
    return jsonResponse(responseBody);
}

WebServer::HttpResponse WebServer::handlePasswordResetByToken(const HttpRequest &request)
{
    if (!WebAuthMethods::allowsLocalCredentials(m_authMethods))
        return errorResponse(404, tr("Authorization method is not enabled."));

    if (isAuthorizeRateLimited(request.remoteAddress))
        return errorResponse(429, tr("Too many authorization attempts. Try again later."));

    QString error;
    const QJsonObject body = jsonBody(request, &error);
    if (!error.isEmpty())
        return errorResponse(400, error);

    const WebAuthManager::AuthResult auth =
            m_authManager->setPasswordByToken(body, QStringLiteral("reset_password"));
    if (!auth.ok)
        return errorResponse(400, auth.error);

    QJsonObject responseBody;
    responseBody.insert(QStringLiteral("ok"), true);
    return jsonResponse(responseBody);
}

WebServer::HttpResponse WebServer::finalizeAuthorization(const HttpRequest &request,
                                                         const WebAuthManager::AuthResult &auth)
{
    const WebSessionStore::User user =
            m_authManager ? m_authManager->user(auth.userId) : WebSessionStore::User();
    QString error;
    if (!recordWebAuthorization(auth.userId, &error)) {
        if (!isServerAvailable())
            return serverUnavailableResponse();
        return errorResponse(503, error);
    }

    QJsonObject responseBody;
    responseBody.insert(QStringLiteral("ok"), true);
    responseBody.insert(QStringLiteral("user_id"), auth.userId);
    responseBody.insert(QStringLiteral("first_name"), user.firstName);
    responseBody.insert(QStringLiteral("last_name"), user.lastName);
    responseBody.insert(QStringLiteral("locale"), user.locale);

    HttpResponse response = jsonResponse(responseBody);
    response.headers.append(
            qMakePair(QByteArrayLiteral("Set-Cookie"),
                      sessionCookie(auth.sessionToken, secureTransportEnabled(request))));
    return response;
}

WebServer::HttpResponse WebServer::handleLogout(const HttpRequest &request)
{
    const QString token = sessionToken(request);
    if (!token.isEmpty() && m_sessionStore)
        m_sessionStore->revokeSession(token);

    QJsonObject responseBody;
    responseBody.insert(QStringLiteral("ok"), true);

    HttpResponse response = jsonResponse(responseBody);
    response.headers.append(qMakePair(QByteArrayLiteral("Set-Cookie"),
                                      expiredSessionCookie(secureTransportEnabled(request))));
    return response;
}

WebServer::HttpResponse WebServer::handleMessage(const HttpRequest &request)
{
    const QString userId = authenticatedUserId(request);
    if (userId.isEmpty())
        return errorResponse(401, tr("Authentication is required."));

    QString error;
    const QJsonObject body = jsonBody(request, &error);
    if (!error.isEmpty())
        return errorResponse(400, error);

    if (!isServerAvailable())
        return serverUnavailableResponse();

    const WebSessionStore::User user = m_authManager->user(userId);
    QJsonObject payload;
    payload.insert(QStringLiteral("user_id"), userId);
    payload.insert(QStringLiteral("chat_id"), userId);
    payload.insert(QStringLiteral("text"), body.value(QStringLiteral("text")).toString());
    payload.insert(QStringLiteral("first_name"), user.firstName);
    payload.insert(QStringLiteral("last_name"), user.lastName);
    payload.insert(QStringLiteral("language_code"), messengerLocaleForRequest(request, user));

    if (!m_webClient->sendMessage(payload))
        return serverUnavailableResponse();

    QJsonObject userEvent;
    userEvent.insert(QStringLiteral("type"), QStringLiteral("user_message"));
    userEvent.insert(QStringLiteral("text"), payload.value(QStringLiteral("text")).toString());
    m_sessionStore->appendEvent(userId, userEvent);

    QJsonObject ok;
    ok.insert(QStringLiteral("ok"), true);
    return jsonResponse(ok, 202);
}

WebServer::HttpResponse WebServer::handleCallback(const HttpRequest &request)
{
    const QString userId = authenticatedUserId(request);
    if (userId.isEmpty())
        return errorResponse(401, tr("Authentication is required."));

    QString error;
    const QJsonObject body = jsonBody(request, &error);
    if (!error.isEmpty())
        return errorResponse(400, error);

    if (!isServerAvailable())
        return serverUnavailableResponse();

    const WebSessionStore::User user = m_authManager->user(userId);
    QJsonObject payload;
    payload.insert(QStringLiteral("user_id"), userId);
    payload.insert(QStringLiteral("chat_id"), userId);
    payload.insert(QStringLiteral("data"), body.value(QStringLiteral("data")).toString());
    payload.insert(QStringLiteral("original_message_id"),
                   body.value(QStringLiteral("original_message_id")).toString());
    payload.insert(QStringLiteral("first_name"), user.firstName);
    payload.insert(QStringLiteral("last_name"), user.lastName);
    payload.insert(QStringLiteral("language_code"), messengerLocaleForRequest(request, user));

    if (!m_webClient->sendCallback(payload))
        return serverUnavailableResponse();

    QJsonObject ok;
    ok.insert(QStringLiteral("ok"), true);
    return jsonResponse(ok, 202);
}

WebServer::HttpResponse WebServer::handleEvents(const HttpRequest &request)
{
    const QString userId = authenticatedUserId(request);
    if (userId.isEmpty())
        return errorResponse(401, tr("Authentication is required."));

    QUrl url(request.path);
    QUrlQuery query(url);
    bool waitOk = true;
    const int requestedWaitMs = query.queryItemValue(QStringLiteral("wait")).toInt(&waitOk);
    const int waitMs =
            isServerAvailable() && waitOk ? qBound(0, requestedWaitMs, kMaxEventsWaitMs) : 0;

    const QJsonArray events = waitMs > 0 ? m_sessionStore->waitAndTakeEvents(userId, 50, waitMs)
                                         : m_sessionStore->takeEvents(userId);

    QJsonObject body;
    body.insert(QStringLiteral("ok"), true);
    body.insert(QStringLiteral("events"), events);
    body.insert(QStringLiteral("server_available"), isServerAvailable());
    return jsonResponse(body);
}

WebServer::HttpResponse WebServer::handleFile(const HttpRequest &request)
{
    const QString userId = authenticatedUserId(request);
    if (userId.isEmpty())
        return errorResponse(401, tr("Authentication is required."));

    static const QString prefix = QStringLiteral("/api/files/");
    const QString path = normalizedPath(request.path);
    const QString fileId = path.mid(prefix.size()).trimmed();
    if (fileId.isEmpty() || fileId.contains(QLatin1Char('/')))
        return errorResponse(404, tr("File is not available."));

    const WebSessionStore::StoredFile storedFile =
            m_sessionStore ? m_sessionStore->file(userId, fileId) : WebSessionStore::StoredFile();
    if (!storedFile.isValid())
        return errorResponse(404, tr("File is not available."));

    QFileInfo fileInfo(storedFile.filePath);
    if (!fileInfo.exists() || !fileInfo.isFile())
        return errorResponse(404, tr("File is not available."));

    QFile file(storedFile.filePath);
    if (!file.open(QIODevice::ReadOnly))
        return errorResponse(404, tr("File is not available."));

    HttpResponse response;
    response.contentType = contentTypeForPath(fileInfo.fileName());
    response.body = file.readAll();
    response.headers.append(qMakePair(QByteArrayLiteral("Content-Disposition"),
                                      contentDispositionHeader(storedFile.fileName)));
    response.headers.append(
            qMakePair(QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("no-store")));
    return response;
}

WebServer::HttpResponse WebServer::handleSession(const HttpRequest &request)
{
    const QString userId = authenticatedUserId(request);
    if (userId.isEmpty())
        return errorResponse(401, tr("Authentication is required."));

    const WebSessionStore::User user =
            m_authManager ? m_authManager->user(userId) : WebSessionStore::User();
    QJsonObject response;
    response.insert(QStringLiteral("ok"), true);
    response.insert(QStringLiteral("user_id"), userId);
    response.insert(QStringLiteral("first_name"), user.firstName);
    response.insert(QStringLiteral("last_name"), user.lastName);
    response.insert(QStringLiteral("locale"), user.locale);
    const WebSessionStore::Credential credential =
            m_sessionStore ? m_sessionStore->credentialForUser(userId)
                           : WebSessionStore::Credential();
    if (credential.isValid())
        response.insert(QStringLiteral("local_password_login"), credential.login);
    return jsonResponse(response);
}

WebServer::HttpResponse WebServer::handleRuntimeConfig() const
{
    QJsonObject response;
    response.insert(QStringLiteral("ok"), true);
    response.insert(QStringLiteral("page_title"), m_pageTitle);
    response.insert(QStringLiteral("auth_methods"), WebAuthMethods::toJsonArray(m_authMethods));
    return jsonResponse(response);
}

WebServer::HttpResponse WebServer::handleProfile(const HttpRequest &request)
{
    const QString userId = authenticatedUserId(request);
    if (userId.isEmpty())
        return errorResponse(401, tr("Authentication is required."));

    WebSessionStore::User user =
            m_authManager ? m_authManager->user(userId) : WebSessionStore::User();
    if (!user.isValid())
        return errorResponse(401, tr("Authentication is required."));

    QString error;
    const QJsonObject body = jsonBody(request, &error);
    if (!error.isEmpty())
        return errorResponse(400, error);

    if (body.contains(QStringLiteral("locale")))
        user.locale = body.value(QStringLiteral("locale")).toString().trimmed();

    if (!m_sessionStore || !m_sessionStore->upsertUser(user))
        return errorResponse(500,
                             m_sessionStore ? m_sessionStore->lastError()
                                            : tr("Web storage is not available."));

    QJsonObject response;
    response.insert(QStringLiteral("ok"), true);
    response.insert(QStringLiteral("user_id"), user.userId);
    response.insert(QStringLiteral("first_name"), user.firstName);
    response.insert(QStringLiteral("last_name"), user.lastName);
    response.insert(QStringLiteral("locale"), user.locale);
    return jsonResponse(response);
}

WebServer::HttpResponse WebServer::handleI18n(const HttpRequest &request)
{
    QUrl url(request.path);
    QUrlQuery query(url);
    QString locale = query.queryItemValue(QStringLiteral("locale")).trimmed();
    if (locale.isEmpty())
        locale = preferredLocaleForRequest(request);
    if (locale.isEmpty())
        locale = BookingResourcesTranslator::resolvePreferredLocale();

    BookingResourcesTranslator::ScopedLocaleOverride localeGuard =
            m_translator ? m_translator->useLocale(locale)
                         : BookingResourcesTranslator::ScopedLocaleOverride();
    Q_UNUSED(localeGuard);

    QJsonObject dictionary;
    const QVector<WebStaticTranslations::Entry> entries = WebStaticTranslations::entries();
    for (const WebStaticTranslations::Entry &entry : entries) {
        dictionary.insert(QString::fromLatin1(entry.key),
                          QCoreApplication::translate("WebStaticTranslations", entry.source));
    }
    return jsonResponse(dictionary);
}

WebServer::HttpResponse WebServer::handleCommands(const HttpRequest &request)
{
    const QString userId = authenticatedUserId(request);
    if (userId.isEmpty())
        return errorResponse(401, tr("Authentication is required."));

    QUrl url(request.path);
    QUrlQuery query(url);
    const WebSessionStore::User user =
            m_authManager ? m_authManager->user(userId) : WebSessionStore::User();
    QString locale = query.queryItemValue(QStringLiteral("locale")).trimmed();
    if (locale.isEmpty())
        locale = messengerLocaleForRequest(request, user);

    QJsonObject payload;
    payload.insert(QStringLiteral("user_id"), userId);
    payload.insert(QStringLiteral("locale"), locale);

    const QJsonObject response = m_webClient ? m_webClient->commandList(payload) : QJsonObject();
    if (response.value(QStringLiteral("ok")).toBool()) {
        const QJsonArray commands = response.value(QStringLiteral("commands")).toArray();
        if (m_sessionStore)
            m_sessionStore->saveCommands(userId, locale, commands);
        return jsonArrayResponse(commands);
    }

    return jsonArrayResponse(m_sessionStore ? m_sessionStore->commands(userId, locale)
                                            : QJsonArray());
}

WebServer::HttpResponse WebServer::handleUserSettings(const HttpRequest &request)
{
    const QString userId = authenticatedUserId(request);
    if (userId.isEmpty())
        return errorResponse(401, tr("Authentication is required."));
    if (!isServerAvailable())
        return serverUnavailableResponse();

    const WebSessionStore::User user =
            m_authManager ? m_authManager->user(userId) : WebSessionStore::User();
    QJsonObject payload = userSettingsPayload(request, userId, user);

    QJsonObject response;
    if (request.method == QLatin1String("GET")) {
        response = m_webClient->userSettings(payload);
    } else {
        QString error;
        const QJsonObject body = jsonBody(request, &error);
        if (!error.isEmpty())
            return errorResponse(400, error);
        payload.insert(QStringLiteral("settings"),
                       body.value(QStringLiteral("settings")).toObject());
        response = m_webClient->updateUserSettings(payload);
    }

    if (response.isEmpty()
        || response.value(QStringLiteral("error_code")).toString()
                   == QLatin1String("server_unavailable")) {
        return serverUnavailableResponse();
    }
    if (!response.value(QStringLiteral("ok")).toBool()) {
        return errorResponse(request.method == QLatin1String("GET") ? 503 : 400,
                             response.value(QStringLiteral("error"))
                                     .toString(tr("User settings are not available.")));
    }

    return jsonResponse(response);
}

QString WebServer::sessionToken(const HttpRequest &request) const
{
    const QString cookieHeader = request.headers.value(QStringLiteral("cookie"));
    const QStringList cookies = cookieHeader.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &cookie : cookies) {
        const int equals = cookie.indexOf(QLatin1Char('='));
        if (equals <= 0)
            continue;
        const QString name = cookie.left(equals).trimmed();
        if (name != QLatin1String("br_session"))
            continue;
        return cookie.mid(equals + 1).trimmed();
    }
    return QString();
}

QString WebServer::authenticatedUserId(const HttpRequest &request) const
{
    return m_authManager ? m_authManager->resolveSession(sessionToken(request)) : QString();
}

QString WebServer::preferredLocaleForRequest(const HttpRequest &request) const
{
    const QString explicitLocale =
            request.headers.value(QStringLiteral("x-booking-resources-locale")).trimmed();
    if (!explicitLocale.isEmpty())
        return explicitLocale;

    const QString acceptedLocale =
            firstHeaderValue(request.headers.value(QStringLiteral("accept-language")))
                    .section(QLatin1Char(';'), 0, 0)
                    .trimmed();
    return acceptedLocale;
}

QString WebServer::messengerLocaleForRequest(const HttpRequest &request,
                                             const WebSessionStore::User &user) const
{
    const QString preferred = preferredLocaleForRequest(request);
    if (!preferred.isEmpty())
        return preferred;
    return user.locale.trimmed();
}

bool WebServer::recordWebAuthorization(const QString &userId, QString *errorOut) const
{
    if (!isServerAvailable()) {
        if (errorOut)
            *errorOut = tr("Server is not available.");
        return false;
    }

    const WebSessionStore::User user =
            m_authManager ? m_authManager->user(userId) : WebSessionStore::User();
    QJsonObject authorizationPayload;
    authorizationPayload.insert(QStringLiteral("user_id"), userId);
    authorizationPayload.insert(QStringLiteral("first_name"), user.firstName);
    authorizationPayload.insert(QStringLiteral("last_name"), user.lastName);
    authorizationPayload.insert(QStringLiteral("language_code"), user.locale);

    const QJsonObject authorizationResponse = m_webClient->authorizeUser(authorizationPayload);
    if (authorizationResponse.isEmpty()
        || authorizationResponse.value(QStringLiteral("error_code")).toString()
                   == QLatin1String("server_unavailable")) {
        if (errorOut)
            *errorOut = tr("Server is not available.");
        return false;
    }
    if (!authorizationResponse.value(QStringLiteral("ok")).toBool()) {
        if (errorOut) {
            *errorOut = authorizationResponse.value(QStringLiteral("error"))
                                .toString(tr("Unable to record web user authorization."));
        }
        return false;
    }
    return true;
}

bool WebServer::secureTransportEnabled(const HttpRequest &request) const
{
    if (request.listenerRole == ListenerRole::HttpRedirect)
        return false;
    return request.secureTransport || m_externalHttpsEnabled;
}

bool WebServer::isAuthorizeRateLimited(const QString &remoteAddress)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 cutoff = now - kAuthorizeRateLimitWindowSeconds;
    const QString key =
            remoteAddress.trimmed().isEmpty() ? QStringLiteral("unknown") : remoteAddress.trimmed();

    QMutexLocker locker(&m_rateLimitMutex);

    const auto prune = [cutoff](QList<qint64> *attempts) {
        while (attempts && !attempts->isEmpty() && attempts->constFirst() < cutoff)
            attempts->removeFirst();
    };

    prune(&m_authorizeAttempts);
    QList<qint64> &remoteAttempts = m_authorizeAttemptsByRemote[key];
    prune(&remoteAttempts);

    if (remoteAttempts.size() >= kAuthorizeRateLimitPerRemote
        || m_authorizeAttempts.size() >= kAuthorizeRateLimitGlobal) {
        return true;
    }

    remoteAttempts.append(now);
    m_authorizeAttempts.append(now);
    return false;
}

QJsonObject WebServer::userSettingsPayload(const HttpRequest &request,
                                           const QString &userId,
                                           const WebSessionStore::User &user) const
{
    QJsonObject payload;
    payload.insert(QStringLiteral("user_id"), userId);
    payload.insert(QStringLiteral("reported_locale"), messengerLocaleForRequest(request, user));
    return payload;
}

QByteArray WebServer::contentTypeForPath(const QString &path) const
{
    if (path.endsWith(QStringLiteral(".html")))
        return QByteArrayLiteral("text/html; charset=utf-8");
    if (path.endsWith(QStringLiteral(".css")))
        return QByteArrayLiteral("text/css; charset=utf-8");
    if (path.endsWith(QStringLiteral(".js")))
        return QByteArrayLiteral("application/javascript; charset=utf-8");
    if (path.endsWith(QStringLiteral(".svg")))
        return QByteArrayLiteral("image/svg+xml");
    if (path.endsWith(QStringLiteral(".csv")))
        return QByteArrayLiteral("text/csv; charset=utf-8");
    return QByteArrayLiteral("application/octet-stream");
}

QByteArray WebServer::sessionCookie(const QString &token, bool secureTransport) const
{
    QByteArray cookie = QByteArrayLiteral("br_session=") + token.toUtf8()
                        + QByteArrayLiteral("; Path=/; HttpOnly; SameSite=Lax");
    if (secureTransport)
        cookie += QByteArrayLiteral("; Secure");
    return cookie;
}

QByteArray WebServer::sessionCookie(const QString &token) const
{
    return sessionCookie(token, secureTransportEnabled());
}

QByteArray WebServer::expiredSessionCookie(bool secureTransport) const
{
    QByteArray cookie = QByteArrayLiteral("br_session=; Max-Age=0; Path=/; HttpOnly; SameSite=Lax");
    if (secureTransport)
        cookie += QByteArrayLiteral("; Secure");
    return cookie;
}

QByteArray WebServer::expiredSessionCookie() const
{
    return expiredSessionCookie(secureTransportEnabled());
}

QJsonObject WebServer::jsonBody(const HttpRequest &request, QString *errorOut) const
{
    if (errorOut)
        errorOut->clear();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(request.body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut)
            *errorOut = tr("Request body must be a JSON object.");
        return QJsonObject();
    }
    return doc.object();
}

WebServer::HttpResponse WebServer::jsonResponse(const QJsonObject &object, int statusCode) const
{
    HttpResponse response;
    response.statusCode = statusCode;
    response.reason = reasonPhrase(statusCode);
    response.body = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return response;
}

bool WebServer::isServerAvailable() const
{
    return m_webClient && m_webClient->isConnected();
}

WebServer::HttpResponse WebServer::serverUnavailableResponse() const
{
    return errorResponse(503, tr("Server is not available."), QStringLiteral("server_unavailable"));
}

WebServer::HttpResponse WebServer::jsonArrayResponse(const QJsonArray &array) const
{
    HttpResponse response;
    response.body = QJsonDocument(array).toJson(QJsonDocument::Compact);
    return response;
}

WebServer::HttpResponse WebServer::errorResponse(int statusCode,
                                                 const QString &message,
                                                 const QString &errorCode) const
{
    QJsonObject body;
    body.insert(QStringLiteral("ok"), false);
    body.insert(QStringLiteral("error"), message);
    if (!errorCode.isEmpty())
        body.insert(QStringLiteral("error_code"), errorCode);
    return jsonResponse(body, statusCode);
}

WebServer::Listener *WebServer::listenerForServer(QTcpServer *server)
{
    for (Listener &listener : m_listeners) {
        if (listener.server == server)
            return &listener;
    }
    return nullptr;
}

WebServer::ListenerRole WebServer::listenerRoleForSocket(QTcpSocket *socket) const
{
    if (!socket)
        return ListenerRole::HttpApplication;
    return static_cast<ListenerRole>(socket->property("bookingResources.listenerRole").toInt());
}

void WebServer::applyTrustedProxyHeaders(HttpRequest *request) const
{
    if (!request || !m_forwardedHeadersEnabled)
        return;

    const QHostAddress peerAddress(request->remoteAddress);
    if (!isTrustedProxy(peerAddress))
        return;

    const QString forwarded = request->headers.value(QStringLiteral("forwarded"));
    if (!forwarded.isEmpty()) {
        const QString firstForwarded = forwarded.split(QLatin1Char(',')).constFirst();
        const QStringList parts = firstForwarded.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            const int equals = part.indexOf(QLatin1Char('='));
            if (equals <= 0)
                continue;
            const QString key = part.left(equals).trimmed().toLower();
            QString value = part.mid(equals + 1).trimmed();
            if (value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"'))
                && value.size() > 1) {
                value = value.mid(1, value.size() - 2);
            }
            if (key == QLatin1String("proto")
                && value.compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0) {
                request->secureTransport = true;
            } else if (key == QLatin1String("host") && request->forwardedHost.isEmpty()) {
                request->forwardedHost = value;
            } else if (key == QLatin1String("for") && !value.isEmpty()) {
                request->remoteAddress = value;
            }
        }
    }

    const QString forwardedProto =
            firstHeaderValue(request->headers.value(QStringLiteral("x-forwarded-proto")));
    if (forwardedProto.compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0)
        request->secureTransport = true;

    const QString forwardedHost =
            firstHeaderValue(request->headers.value(QStringLiteral("x-forwarded-host")));
    if (!forwardedHost.isEmpty())
        request->forwardedHost = forwardedHost;

    const QString forwardedFor =
            firstHeaderValue(request->headers.value(QStringLiteral("x-forwarded-for")));
    if (!forwardedFor.isEmpty())
        request->remoteAddress = forwardedFor;
}

bool WebServer::isTrustedProxy(const QHostAddress &address) const
{
    if (address.isLoopback())
        return true;

    for (const QString &proxy : m_trustedProxies) {
        if (proxy.contains(QLatin1Char('/'))) {
            const QPair<QHostAddress, int> subnet = QHostAddress::parseSubnet(proxy);
            if (!subnet.first.isNull() && address.isInSubnet(subnet))
                return true;
            continue;
        }

        if (address == QHostAddress(proxy))
            return true;
    }
    return false;
}

QString WebServer::redirectHost(const HttpRequest &request) const
{
    const bool publicHostConfigured = !m_publicHttpsHost.trimmed().isEmpty();
    QString host = m_publicHttpsHost.trimmed();
    if (host.isEmpty())
        host = request.forwardedHost.trimmed();
    if (host.isEmpty())
        host = request.headers.value(QStringLiteral("host")).trimmed();
    if (host.isEmpty())
        host = QStringLiteral("localhost");

    if (host.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive))
        host.remove(0, 7);
    if (host.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))
        host.remove(0, 8);
    host = host.section(QLatin1Char('/'), 0, 0);

    const QUrl parsedHost(QStringLiteral("https://%1").arg(host));
    if (parsedHost.isValid() && !parsedHost.host().isEmpty()) {
        QUrl redirectAuthority;
        redirectAuthority.setScheme(QStringLiteral("https"));
        redirectAuthority.setHost(parsedHost.host());

        int port = parsedHost.port(-1);
        if (m_httpsPort != 0) {
            // Browser Host for the redirect listener contains the HTTP port; replace it with
            // the actual HTTPS listener port.
            if (publicHostConfigured) {
                if (port < 0 && m_httpsPort != 443)
                    port = m_httpsPort;
            } else {
                port = m_httpsPort == 443 ? -1 : m_httpsPort;
            }
        }
        if (port > 0)
            redirectAuthority.setPort(port);
        return redirectAuthority.authority();
    }

    if (m_httpsPort != 0 && m_httpsPort != 443 && !host.contains(QLatin1Char(':')))
        host += QStringLiteral(":%1").arg(m_httpsPort);
    return host;
}

QString WebServer::firstHeaderValue(const QString &value) const
{
    QString result = value.split(QLatin1Char(',')).constFirst().trimmed();
    if (result.startsWith(QLatin1Char('"')) && result.endsWith(QLatin1Char('"'))
        && result.size() > 1) {
        result = result.mid(1, result.size() - 2);
    }
    return result;
}

void WebServer::setLastError(const QString &description)
{
    m_lastError = description;
    emit errorOccurred(m_lastError);
}
