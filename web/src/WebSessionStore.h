#ifndef WEBSESSIONSTORE_H
#define WEBSESSIONSTORE_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QByteArray>
#include <QJsonArray>
#include <QStringList>
#include <QJsonObject>

class WebSessionStorePrivate;

class WebSessionStore final : public QObject
{
    Q_OBJECT

public:
    struct User
    {
        QString userId;
        QString firstName;
        QString lastName;
        QString locale;
        QString provider;
        QString externalSubject;

        [[nodiscard]] bool isValid() const { return !userId.trimmed().isEmpty(); }
    };

    struct Credential
    {
        QString login;
        QString normalizedLogin;
        QString userId;
        QString passwordHash;

        [[nodiscard]] bool isValid() const
        {
            return !normalizedLogin.trimmed().isEmpty() && !userId.trimmed().isEmpty()
                   && !passwordHash.trimmed().isEmpty();
        }
    };

    struct StoredFile
    {
        StoredFile();

        QString fileId;
        QString userId;
        QString filePath;
        QString fileName;
        qint64 fileSize;
        QDateTime createdAtUtc;

        [[nodiscard]] bool isValid() const
        {
            return !fileId.trimmed().isEmpty() && !userId.trimmed().isEmpty()
                   && !filePath.trimmed().isEmpty();
        }
    };

    static constexpr int kPasswordResetTokenValidDays = 1;

    explicit WebSessionStore(QObject *parent = nullptr);
    ~WebSessionStore() override;

    [[nodiscard]] QString storagePath() const;
    [[nodiscard]] QString lastError() const;

    bool open(const QString &filePath = QString());
    bool upsertUser(const User &user);
    User user(const QString &userId) const;
    bool deleteUser(const QString &userId);
    bool upsertCredential(const Credential &credential);
    Credential credentialByLogin(const QString &login) const;
    Credential credentialForUser(const QString &userId) const;
    QString resolveUserId(const QString &loginOrUserId) const;
    QStringList userIds() const;
    QString createSession(const QString &userId);
    QString resolveSession(const QString &token) const;
    bool revokeSession(const QString &token);
    bool revokeSessionsForUser(const QString &userId);
    QString createPasswordResetToken(const QString &userId, const QString &purpose);
    QString passwordResetUserId(const QString &token, const QString &purpose) const;
    bool lookupPasswordResetToken(const QString &token,
                                  QString *userIdOut,
                                  QString *purposeOut) const;
    bool consumePasswordResetToken(const QString &token,
                                   const QString &purpose,
                                   QString *userIdOut);
    bool appendEvent(const QString &userId, const QJsonObject &event);
    QJsonArray takeEvents(const QString &userId, int limit = 50);
    QJsonArray waitAndTakeEvents(const QString &userId, int limit = 50, int timeoutMs = 0);
    bool registerFile(const QString &userId,
                      const QString &filePath,
                      StoredFile *fileOut = nullptr);
    bool registerFileData(const QString &userId,
                          const QString &fileName,
                          const QByteArray &fileData,
                          StoredFile *fileOut = nullptr);
    StoredFile file(const QString &userId, const QString &fileId) const;
    bool saveCommands(const QString &userId, const QString &locale, const QJsonArray &commands);
    QJsonArray commands(const QString &userId, const QString &locale) const;
    bool clearCommands(const QString &userId);
    bool clearAllCommands();

private:
    [[nodiscard]] QString defaultStoragePath() const;
    [[nodiscard]] QString tokenHash(const QString &token) const;
    void setLastError(const QString &error) const;

    WebSessionStorePrivate *d;
};

#endif // WEBSESSIONSTORE_H
