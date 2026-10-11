#ifndef WEBAUTHMANAGER_H
#define WEBAUTHMANAGER_H

#include <QObject>
#include <QString>
#include <QJsonObject>

#include "WebSessionStore.h"

class WebAuthManager final : public QObject
{
    Q_OBJECT

public:
    struct AuthResult
    {
        bool ok = false;
        QString error;
        QString userId;
        QString sessionToken;
    };

    explicit WebAuthManager(WebSessionStore *store, QObject *parent = nullptr);
    ~WebAuthManager() override;

    AuthResult authorizeUser(const QJsonObject &request);
    AuthResult registerLocalPasswordUser(const QJsonObject &request);
    AuthResult loginLocalPasswordUser(const QJsonObject &request);
    AuthResult changeLocalPassword(const QString &userId, const QJsonObject &request);
    AuthResult addLocalPasswordUserWithoutPassword(const QJsonObject &request);
    AuthResult attachLocalPasswordLogin(const QString &userId, const QString &login);
    AuthResult issuePasswordToken(const QString &userId, const QString &purpose);
    AuthResult setPasswordByToken(const QJsonObject &request, const QString &purpose);
    bool deleteUser(const QString &userId);
    QString resolveSession(const QString &sessionToken) const;
    WebSessionStore::User user(const QString &userId) const;

private:
    WebSessionStore *m_store;
};

#endif // WEBAUTHMANAGER_H
