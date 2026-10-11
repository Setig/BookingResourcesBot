#include "WebSessionStore.h"

#include <QDir>
#include <QFile>
#include <QUuid>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QtGlobal>
#include <QDateTime>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QReadLocker>
#include <QWriteLocker>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QReadWriteLock>
#include <QMutexLocker>
#include <QWaitCondition>
#include <QStandardPaths>
#include <QCryptographicHash>

struct SessionRecord
{
    QString userId;
    QDateTime expiresAtUtc;
};

struct EventRecord
{
    qint64 id;
    QJsonObject payload;
};

struct PasswordResetRecord
{
    QString userId;
    QString purpose;
    QDateTime expiresAtUtc;
};

class WebSessionStorePrivate
{
public:
    WebSessionStorePrivate();

    mutable QReadWriteLock lock;
    QMutex eventsWaitMutex;
    QWaitCondition eventsChanged;
    QString storagePath;
    mutable QString lastError;
    QHash<QString, WebSessionStore::User> users;
    QHash<QString, WebSessionStore::Credential> credentialsByLogin;
    QHash<QString, QString> credentialLoginsByUser;
    QHash<QString, SessionRecord> sessionsByTokenHash;
    QHash<QString, QList<EventRecord>> eventsByUser;
    QHash<QString, QHash<QString, WebSessionStore::StoredFile>> filesByUser;
    QHash<QString, QJsonArray> commandsByUserLocale;
    QHash<QString, PasswordResetRecord> passwordResetsByTokenHash;
};

WebSessionStorePrivate::WebSessionStorePrivate()
    : lock()
    , eventsWaitMutex()
    , eventsChanged()
    , storagePath()
    , lastError()
    , users()
    , credentialsByLogin()
    , credentialLoginsByUser()
    , sessionsByTokenHash()
    , eventsByUser()
    , filesByUser()
    , commandsByUserLocale()
    , passwordResetsByTokenHash()
{
    // do nothing
}

namespace {

class DatabaseConnection
{
public:
    explicit DatabaseConnection(const QString &path);
    ~DatabaseConnection();

    QSqlDatabase db;
    QString error;

private:
    QString m_name;
};

DatabaseConnection::DatabaseConnection(const QString &path)
    : m_name(QStringLiteral("booking_resources_web_%1")
                     .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
    db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name);
    db.setDatabaseName(path);
    if (!db.open())
        error = db.lastError().text();
}

DatabaseConnection::~DatabaseConnection()
{
    if (db.isOpen())
        db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_name);
}

bool execSql(QSqlDatabase &db, const QString &sql, QString *errorOut)
{
    QSqlQuery query(db);
    if (query.exec(sql))
        return true;
    if (errorOut)
        *errorOut = query.lastError().text();
    return false;
}

bool runQuickCheck(QSqlDatabase &db, QString *errorOut)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA quick_check"))) {
        if (errorOut)
            *errorOut = query.lastError().text();
        return false;
    }
    if (!query.next()) {
        if (errorOut)
            *errorOut = WebSessionStore::tr("SQLite integrity check returned no result.");
        return false;
    }
    const QString result = query.value(0).toString();
    if (result == QLatin1String("ok"))
        return true;
    if (errorOut)
        *errorOut = WebSessionStore::tr("SQLite integrity check failed: %1").arg(result);
    return false;
}

bool configureSqlite(QSqlDatabase &db, QString *errorOut)
{
    return execSql(db, QStringLiteral("PRAGMA journal_mode=WAL;"), errorOut)
           && execSql(db, QStringLiteral("PRAGMA synchronous=NORMAL;"), errorOut);
}

bool isExpired(const QDateTime &expiresAtUtc)
{
    return expiresAtUtc.isValid() && expiresAtUtc < QDateTime::currentDateTimeUtc();
}

bool loadUsers(QSqlDatabase &db, QHash<QString, WebSessionStore::User> *users, QString *errorOut)
{
    if (!users)
        return true;

    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT user_id, first_name, last_name, locale, "
                                 "provider, external_subject FROM web_users"));
    if (!query.exec()) {
        if (errorOut)
            *errorOut = query.lastError().text();
        return false;
    }

    users->clear();
    while (query.next()) {
        WebSessionStore::User user;
        user.userId = query.value(0).toString();
        user.firstName = query.value(1).toString();
        user.lastName = query.value(2).toString();
        user.locale = query.value(3).toString();
        user.provider = query.value(4).toString();
        user.externalSubject = query.value(5).toString();
        if (user.isValid())
            users->insert(user.userId.trimmed(), user);
    }
    return true;
}

bool loadCredentials(QSqlDatabase &db,
                     QHash<QString, WebSessionStore::Credential> *credentialsByLogin,
                     QHash<QString, QString> *credentialLoginsByUser,
                     QString *errorOut)
{
    if (!credentialsByLogin || !credentialLoginsByUser)
        return true;

    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT login, normalized_login, user_id, password_hash "
                                 "FROM web_credentials"));
    if (!query.exec()) {
        if (errorOut)
            *errorOut = query.lastError().text();
        return false;
    }

    credentialsByLogin->clear();
    credentialLoginsByUser->clear();
    while (query.next()) {
        WebSessionStore::Credential credential;
        credential.login = query.value(0).toString();
        credential.normalizedLogin = query.value(1).toString().trimmed();
        credential.userId = query.value(2).toString().trimmed();
        credential.passwordHash = query.value(3).toString().trimmed();
        if (credential.isValid()) {
            credentialsByLogin->insert(credential.normalizedLogin, credential);
            credentialLoginsByUser->insert(credential.userId, credential.normalizedLogin);
        }
    }
    return true;
}

bool loadSessions(QSqlDatabase &db, QHash<QString, SessionRecord> *sessions, QString *errorOut)
{
    if (!sessions)
        return true;

    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT token_hash, user_id, expires_at_utc FROM web_sessions"));
    if (!query.exec()) {
        if (errorOut)
            *errorOut = query.lastError().text();
        return false;
    }

    sessions->clear();
    while (query.next()) {
        SessionRecord record;
        record.userId = query.value(1).toString();
        record.expiresAtUtc = QDateTime::fromString(query.value(2).toString(), Qt::ISODateWithMs);
        if (!record.userId.trimmed().isEmpty() && !isExpired(record.expiresAtUtc))
            sessions->insert(query.value(0).toString(), record);
    }
    return true;
}

bool loadEvents(QSqlDatabase &db,
                QHash<QString, QList<EventRecord>> *eventsByUser,
                QString *errorOut)
{
    if (!eventsByUser)
        return true;

    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT id, user_id, payload FROM web_events ORDER BY id ASC"));
    if (!query.exec()) {
        if (errorOut)
            *errorOut = query.lastError().text();
        return false;
    }

    eventsByUser->clear();
    while (query.next()) {
        const QString userId = query.value(1).toString().trimmed();
        if (userId.isEmpty())
            continue;

        const QJsonDocument doc = QJsonDocument::fromJson(query.value(2).toString().toUtf8());
        if (!doc.isObject())
            continue;

        EventRecord record;
        record.id = query.value(0).toLongLong();
        record.payload = doc.object();
        (*eventsByUser)[userId].append(record);
    }
    return true;
}

bool loadFiles(QSqlDatabase &db,
               QHash<QString, QHash<QString, WebSessionStore::StoredFile>> *filesByUser,
               QString *errorOut)
{
    if (!filesByUser)
        return true;

    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT file_id, user_id, file_path, file_name, file_size, "
                                 "created_at_utc FROM web_files"));
    if (!query.exec()) {
        if (errorOut)
            *errorOut = query.lastError().text();
        return false;
    }

    filesByUser->clear();
    while (query.next()) {
        WebSessionStore::StoredFile file;
        file.fileId = query.value(0).toString().trimmed();
        file.userId = query.value(1).toString().trimmed();
        file.filePath = query.value(2).toString();
        file.fileName = query.value(3).toString();
        file.fileSize = query.value(4).toLongLong();
        file.createdAtUtc = QDateTime::fromString(query.value(5).toString(), Qt::ISODateWithMs);
        if (file.isValid())
            (*filesByUser)[file.userId].insert(file.fileId, file);
    }
    return true;
}

bool loadPasswordResets(QSqlDatabase &db,
                        QHash<QString, PasswordResetRecord> *resets,
                        QString *errorOut)
{
    if (!resets)
        return true;

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
            "SELECT token_hash, user_id, purpose, expires_at_utc FROM web_password_resets"));
    if (!query.exec()) {
        if (errorOut)
            *errorOut = query.lastError().text();
        return false;
    }

    resets->clear();
    while (query.next()) {
        PasswordResetRecord record;
        record.userId = query.value(1).toString().trimmed();
        record.purpose = query.value(2).toString().trimmed();
        record.expiresAtUtc = QDateTime::fromString(query.value(3).toString(), Qt::ISODateWithMs);
        if (record.userId.isEmpty() || record.purpose.isEmpty() || isExpired(record.expiresAtUtc))
            continue;
        resets->insert(query.value(0).toString().trimmed(), record);
    }
    return true;
}

QString commandCacheKey(const QString &userId, const QString &locale)
{
    const QString normalizedLocale =
            locale.trimmed().isEmpty() ? QStringLiteral("en") : locale.trimmed();
    return userId.trimmed() + QLatin1Char('\n') + normalizedLocale;
}

bool loadCommands(QSqlDatabase &db, QHash<QString, QJsonArray> *commands, QString *errorOut)
{
    if (!commands)
        return true;

    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT user_id, locale, value FROM web_command_cache"));
    if (!query.exec()) {
        if (errorOut)
            *errorOut = query.lastError().text();
        return false;
    }

    commands->clear();
    while (query.next()) {
        const QString userId = query.value(0).toString().trimmed();
        const QString locale = query.value(1).toString().trimmed();
        if (userId.isEmpty() || locale.isEmpty())
            continue;

        const QJsonDocument doc = QJsonDocument::fromJson(query.value(2).toString().toUtf8());
        if (doc.isArray())
            commands->insert(commandCacheKey(userId, locale), doc.array());
    }
    return true;
}

WebSessionStore::User normalizedUser(const WebSessionStore::User &user)
{
    WebSessionStore::User normalized;
    normalized.userId = user.userId.trimmed();
    normalized.firstName = user.firstName.trimmed();
    normalized.lastName = user.lastName.trimmed();
    normalized.locale = user.locale.trimmed();
    normalized.provider = user.provider.trimmed();
    normalized.externalSubject = user.externalSubject.trimmed();
    return normalized;
}

QString normalizedLogin(const QString &login)
{
    return login.trimmed().toCaseFolded();
}

WebSessionStore::Credential normalizedCredential(const WebSessionStore::Credential &credential)
{
    WebSessionStore::Credential normalized;
    normalized.login = credential.login.trimmed();
    normalized.normalizedLogin = normalizedLogin(
            credential.normalizedLogin.isEmpty() ? credential.login : credential.normalizedLogin);
    normalized.userId = credential.userId.trimmed();
    normalized.passwordHash = credential.passwordHash.trimmed();
    return normalized;
}

bool deleteEvents(QSqlDatabase &db, const QList<EventRecord> &events, QString *errorOut)
{
    if (events.isEmpty())
        return true;
    if (!db.transaction()) {
        if (errorOut)
            *errorOut = db.lastError().text();
        return false;
    }

    for (const EventRecord &event : events) {
        QSqlQuery query(db);
        query.prepare(QStringLiteral("DELETE FROM web_events WHERE id = ?"));
        query.addBindValue(event.id);
        if (!query.exec()) {
            if (errorOut)
                *errorOut = query.lastError().text();
            db.rollback();
            return false;
        }
    }

    if (!db.commit()) {
        if (errorOut)
            *errorOut = db.lastError().text();
        return false;
    }
    return true;
}

qint64 lastInsertedRowId(QSqlDatabase &db, const QSqlQuery &insertQuery, bool *okOut)
{
    bool ok = false;
    qint64 id = insertQuery.lastInsertId().toLongLong(&ok);
    if (ok && id > 0) {
        if (okOut)
            *okOut = true;
        return id;
    }

    QSqlQuery query(db);
    if (query.exec(QStringLiteral("SELECT last_insert_rowid()")) && query.next()) {
        id = query.value(0).toLongLong(&ok);
        if (ok && id > 0) {
            if (okOut)
                *okOut = true;
            return id;
        }
    }

    if (okOut)
        *okOut = false;
    return 0;
}

QString safeStoredFileName(const QString &fileName)
{
    QString result = QFileInfo(fileName.trimmed()).fileName();
    if (result.isEmpty())
        result = QStringLiteral("download");
    return result;
}

} // namespace

WebSessionStore::StoredFile::StoredFile()
    : fileId()
    , userId()
    , filePath()
    , fileName()
    , fileSize(0)
    , createdAtUtc()
{
    // do nothing
}

WebSessionStore::WebSessionStore(QObject *parent)
    : QObject(parent)
    , d(new WebSessionStorePrivate())
{
    // do nothing
}

WebSessionStore::~WebSessionStore()
{
    delete d;
}

QString WebSessionStore::storagePath() const
{
    QReadLocker locker(&d->lock);
    return d->storagePath;
}

QString WebSessionStore::lastError() const
{
    QReadLocker locker(&d->lock);
    return d->lastError;
}

bool WebSessionStore::open(const QString &filePath)
{
    const QString effectivePath =
            filePath.trimmed().isEmpty() ? defaultStoragePath() : filePath.trimmed();
    QFileInfo info(effectivePath);
    QDir dir(info.absolutePath());
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        setLastError(tr("Failed to create web storage directory: %1").arg(dir.absolutePath()));
        return false;
    }

    DatabaseConnection connection(effectivePath);
    if (!connection.error.isEmpty()) {
        setLastError(connection.error);
        return false;
    }

    QString error;
    const bool ok = runQuickCheck(connection.db, &error) && configureSqlite(connection.db, &error)
                    && execSql(connection.db,
                               QStringLiteral("CREATE TABLE IF NOT EXISTS web_users ("
                                              "user_id TEXT PRIMARY KEY,"
                                              "first_name TEXT,"
                                              "last_name TEXT,"
                                              "locale TEXT,"
                                              "provider TEXT,"
                                              "external_subject TEXT,"
                                              "created_at_utc TEXT NOT NULL)"),
                               &error)
                    && execSql(connection.db,
                               QStringLiteral("CREATE TABLE IF NOT EXISTS web_sessions ("
                                              "token_hash TEXT PRIMARY KEY,"
                                              "user_id TEXT NOT NULL,"
                                              "expires_at_utc TEXT NOT NULL,"
                                              "created_at_utc TEXT NOT NULL)"),
                               &error)
                    && execSql(connection.db,
                               QStringLiteral("CREATE TABLE IF NOT EXISTS web_credentials ("
                                              "normalized_login TEXT PRIMARY KEY,"
                                              "login TEXT NOT NULL,"
                                              "user_id TEXT NOT NULL UNIQUE,"
                                              "password_hash TEXT NOT NULL,"
                                              "created_at_utc TEXT NOT NULL,"
                                              "updated_at_utc TEXT NOT NULL)"),
                               &error)
                    && execSql(connection.db,
                               QStringLiteral("CREATE TABLE IF NOT EXISTS web_events ("
                                              "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                              "user_id TEXT NOT NULL,"
                                              "payload TEXT NOT NULL,"
                                              "created_at_utc TEXT NOT NULL)"),
                               &error)
                    && execSql(connection.db,
                               QStringLiteral("CREATE TABLE IF NOT EXISTS web_files ("
                                              "file_id TEXT PRIMARY KEY,"
                                              "user_id TEXT NOT NULL,"
                                              "file_path TEXT NOT NULL,"
                                              "file_name TEXT NOT NULL,"
                                              "file_size INTEGER NOT NULL,"
                                              "created_at_utc TEXT NOT NULL)"),
                               &error)
                    && execSql(connection.db,
                               QStringLiteral("CREATE TABLE IF NOT EXISTS web_state ("
                                              "key TEXT PRIMARY KEY,"
                                              "value TEXT NOT NULL,"
                                              "updated_at_utc TEXT NOT NULL)"),
                               &error)
                    && execSql(connection.db,
                               QStringLiteral("CREATE TABLE IF NOT EXISTS web_command_cache ("
                                              "user_id TEXT NOT NULL,"
                                              "locale TEXT NOT NULL,"
                                              "value TEXT NOT NULL,"
                                              "updated_at_utc TEXT NOT NULL,"
                                              "PRIMARY KEY(user_id, locale))"),
                               &error)
                    && execSql(connection.db,
                               QStringLiteral("CREATE TABLE IF NOT EXISTS web_password_resets ("
                                              "token_hash TEXT PRIMARY KEY,"
                                              "user_id TEXT NOT NULL,"
                                              "purpose TEXT NOT NULL,"
                                              "expires_at_utc TEXT NOT NULL,"
                                              "created_at_utc TEXT NOT NULL)"),
                               &error);
    if (!ok) {
        setLastError(error);
        return false;
    }

    QHash<QString, WebSessionStore::User> loadedUsers;
    QHash<QString, WebSessionStore::Credential> loadedCredentials;
    QHash<QString, QString> loadedCredentialLoginsByUser;
    QHash<QString, SessionRecord> loadedSessions;
    QHash<QString, QList<EventRecord>> loadedEvents;
    QHash<QString, QHash<QString, WebSessionStore::StoredFile>> loadedFiles;
    QHash<QString, QJsonArray> loadedCommands;
    QHash<QString, PasswordResetRecord> loadedPasswordResets;
    if (!loadUsers(connection.db, &loadedUsers, &error)
        || !loadCredentials(connection.db,
                            &loadedCredentials,
                            &loadedCredentialLoginsByUser,
                            &error)
        || !loadSessions(connection.db, &loadedSessions, &error)
        || !loadEvents(connection.db, &loadedEvents, &error)
        || !loadFiles(connection.db, &loadedFiles, &error)
        || !loadCommands(connection.db, &loadedCommands, &error)
        || !loadPasswordResets(connection.db, &loadedPasswordResets, &error)) {
        setLastError(error);
        return false;
    }

    QWriteLocker locker(&d->lock);
    d->storagePath = effectivePath;
    d->lastError.clear();
    d->users = loadedUsers;
    d->credentialsByLogin = loadedCredentials;
    d->credentialLoginsByUser = loadedCredentialLoginsByUser;
    d->sessionsByTokenHash = loadedSessions;
    d->eventsByUser = loadedEvents;
    d->filesByUser = loadedFiles;
    d->commandsByUserLocale = loadedCommands;
    d->passwordResetsByTokenHash = loadedPasswordResets;
    return true;
}

bool WebSessionStore::upsertUser(const WebSessionStore::User &user)
{
    const WebSessionStore::User normalized = normalizedUser(user);
    if (!normalized.isValid())
        return false;

    const QString path = storagePath();
    if (path.isEmpty()) {
        setLastError(tr("Web storage is not initialized."));
        return false;
    }

    DatabaseConnection connection(path);
    if (!connection.error.isEmpty()) {
        setLastError(connection.error);
        return false;
    }

    QSqlQuery query(connection.db);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO web_users "
                                 "(user_id, first_name, last_name, locale, provider, "
                                 "external_subject, created_at_utc) "
                                 "VALUES (?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(normalized.userId);
    query.addBindValue(normalized.firstName);
    query.addBindValue(normalized.lastName);
    query.addBindValue(normalized.locale);
    query.addBindValue(normalized.provider);
    query.addBindValue(normalized.externalSubject);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }

    QWriteLocker locker(&d->lock);
    d->users.insert(normalized.userId, normalized);
    return true;
}

WebSessionStore::User WebSessionStore::user(const QString &userId) const
{
    QReadLocker locker(&d->lock);
    return d->users.value(userId.trimmed());
}

bool WebSessionStore::deleteUser(const QString &userId)
{
    const QString trimmedUserId = userId.trimmed();
    if (trimmedUserId.isEmpty())
        return false;

    const QString path = storagePath();
    if (path.isEmpty()) {
        setLastError(tr("Web storage is not initialized."));
        return false;
    }

    QList<WebSessionStore::StoredFile> filesToRemove;
    {
        QReadLocker locker(&d->lock);
        filesToRemove = d->filesByUser.value(trimmedUserId).values();
    }

    DatabaseConnection connection(path);
    if (!connection.error.isEmpty()) {
        setLastError(connection.error);
        return false;
    }

    if (!connection.db.transaction()) {
        setLastError(connection.db.lastError().text());
        return false;
    }

    const QStringList statements = {
        QStringLiteral("DELETE FROM web_password_resets WHERE user_id = ?"),
        QStringLiteral("DELETE FROM web_command_cache WHERE user_id = ?"),
        QStringLiteral("DELETE FROM web_files WHERE user_id = ?"),
        QStringLiteral("DELETE FROM web_events WHERE user_id = ?"),
        QStringLiteral("DELETE FROM web_sessions WHERE user_id = ?"),
        QStringLiteral("DELETE FROM web_credentials WHERE user_id = ?"),
        QStringLiteral("DELETE FROM web_users WHERE user_id = ?"),
    };
    for (const QString &sql : statements) {
        QSqlQuery query(connection.db);
        query.prepare(sql);
        query.addBindValue(trimmedUserId);
        if (!query.exec()) {
            connection.db.rollback();
            setLastError(query.lastError().text());
            return false;
        }
    }

    if (!connection.db.commit()) {
        setLastError(connection.db.lastError().text());
        return false;
    }

    for (const WebSessionStore::StoredFile &file : filesToRemove) {
        if (!file.filePath.trimmed().isEmpty())
            QFile::remove(file.filePath);
    }

    QWriteLocker locker(&d->lock);
    d->users.remove(trimmedUserId);
    const QString login = d->credentialLoginsByUser.take(trimmedUserId);
    if (!login.isEmpty())
        d->credentialsByLogin.remove(login);
    const QList<QString> sessionHashes = d->sessionsByTokenHash.keys();
    for (const QString &hash : sessionHashes) {
        if (d->sessionsByTokenHash.value(hash).userId == trimmedUserId)
            d->sessionsByTokenHash.remove(hash);
    }
    d->eventsByUser.remove(trimmedUserId);
    d->filesByUser.remove(trimmedUserId);
    const QString keyPrefix = trimmedUserId + QLatin1Char('\n');
    const QList<QString> commandKeys = d->commandsByUserLocale.keys();
    for (const QString &key : commandKeys) {
        if (key.startsWith(keyPrefix))
            d->commandsByUserLocale.remove(key);
    }
    const QList<QString> resetHashes = d->passwordResetsByTokenHash.keys();
    for (const QString &hash : resetHashes) {
        if (d->passwordResetsByTokenHash.value(hash).userId == trimmedUserId)
            d->passwordResetsByTokenHash.remove(hash);
    }
    d->lastError.clear();
    return true;
}

bool WebSessionStore::upsertCredential(const WebSessionStore::Credential &credential)
{
    const WebSessionStore::Credential normalized = normalizedCredential(credential);
    if (!normalized.isValid())
        return false;

    const QString path = storagePath();
    if (path.isEmpty()) {
        setLastError(tr("Web storage is not initialized."));
        return false;
    }

    DatabaseConnection connection(path);
    if (!connection.error.isEmpty()) {
        setLastError(connection.error);
        return false;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (!connection.db.transaction()) {
        setLastError(connection.db.lastError().text());
        return false;
    }

    QString existingLogin;
    QString existingDisplayLogin;
    QSqlQuery existingUserQuery(connection.db);
    existingUserQuery.prepare(QStringLiteral(
            "SELECT normalized_login, login FROM web_credentials WHERE user_id = ?"));
    existingUserQuery.addBindValue(normalized.userId);
    if (!existingUserQuery.exec()) {
        connection.db.rollback();
        setLastError(existingUserQuery.lastError().text());
        return false;
    }
    if (existingUserQuery.next()) {
        existingLogin = existingUserQuery.value(0).toString().trimmed();
        existingDisplayLogin = existingUserQuery.value(1).toString().trimmed();
    }

    if (!existingLogin.isEmpty()
        && (existingLogin != normalized.normalizedLogin
            || existingDisplayLogin != normalized.login)) {
        connection.db.rollback();
        setLastError(tr("Login cannot be changed."));
        return false;
    }

    if (existingLogin.isEmpty()) {
        QSqlQuery existingLoginQuery(connection.db);
        existingLoginQuery.prepare(
                QStringLiteral("SELECT user_id FROM web_credentials WHERE normalized_login = ?"));
        existingLoginQuery.addBindValue(normalized.normalizedLogin);
        if (!existingLoginQuery.exec()) {
            connection.db.rollback();
            setLastError(existingLoginQuery.lastError().text());
            return false;
        }
        if (existingLoginQuery.next()) {
            connection.db.rollback();
            setLastError(tr("Login is already used."));
            return false;
        }
    }

    QSqlQuery query(connection.db);
    if (existingLogin.isEmpty()) {
        query.prepare(QStringLiteral("INSERT INTO web_credentials "
                                     "(normalized_login, login, user_id, password_hash, "
                                     "created_at_utc, updated_at_utc) "
                                     "VALUES (?, ?, ?, ?, ?, ?)"));
        query.addBindValue(normalized.normalizedLogin);
        query.addBindValue(normalized.login);
        query.addBindValue(normalized.userId);
        query.addBindValue(normalized.passwordHash);
        query.addBindValue(now.toString(Qt::ISODateWithMs));
        query.addBindValue(now.toString(Qt::ISODateWithMs));
    } else {
        query.prepare(QStringLiteral("UPDATE web_credentials "
                                     "SET password_hash = ?, updated_at_utc = ? "
                                     "WHERE normalized_login = ? AND user_id = ?"));
        query.addBindValue(normalized.passwordHash);
        query.addBindValue(now.toString(Qt::ISODateWithMs));
        query.addBindValue(normalized.normalizedLogin);
        query.addBindValue(normalized.userId);
    }
    if (!query.exec()) {
        connection.db.rollback();
        setLastError(query.lastError().text());
        return false;
    }
    if (!connection.db.commit()) {
        setLastError(connection.db.lastError().text());
        return false;
    }

    QWriteLocker locker(&d->lock);
    d->credentialsByLogin.insert(normalized.normalizedLogin, normalized);
    d->credentialLoginsByUser.insert(normalized.userId, normalized.normalizedLogin);
    return true;
}

WebSessionStore::Credential WebSessionStore::credentialByLogin(const QString &login) const
{
    QReadLocker locker(&d->lock);
    return d->credentialsByLogin.value(normalizedLogin(login));
}

WebSessionStore::Credential WebSessionStore::credentialForUser(const QString &userId) const
{
    const QString trimmedUserId = userId.trimmed();
    if (trimmedUserId.isEmpty())
        return WebSessionStore::Credential();

    QReadLocker locker(&d->lock);
    return d->credentialsByLogin.value(d->credentialLoginsByUser.value(trimmedUserId));
}

QString WebSessionStore::resolveUserId(const QString &loginOrUserId) const
{
    QString trimmed = loginOrUserId.trimmed();
    if (trimmed.startsWith(QLatin1String("web:"), Qt::CaseInsensitive))
        trimmed = trimmed.mid(4).trimmed();
    if (trimmed.isEmpty())
        return QString();

    if (user(trimmed).isValid())
        return trimmed;

    const WebSessionStore::Credential credential = credentialByLogin(trimmed);
    if (credential.isValid())
        return credential.userId;
    return QString();
}

QStringList WebSessionStore::userIds() const
{
    QReadLocker locker(&d->lock);
    QStringList result = d->users.keys();
    result.sort(Qt::CaseInsensitive);
    return result;
}

QString WebSessionStore::createSession(const QString &userId)
{
    const QString trimmedUserId = userId.trimmed();
    if (trimmedUserId.isEmpty())
        return QString();

    const QString path = storagePath();
    if (path.isEmpty()) {
        setLastError(tr("Web storage is not initialized."));
        return QString();
    }

    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces)
                          + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString hash = tokenHash(token);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime expires = now.addDays(30);

    DatabaseConnection connection(path);
    if (!connection.error.isEmpty()) {
        setLastError(connection.error);
        return QString();
    }

    QSqlQuery query(connection.db);
    query.prepare(QStringLiteral("INSERT INTO web_sessions "
                                 "(token_hash, user_id, expires_at_utc, created_at_utc) "
                                 "VALUES (?, ?, ?, ?)"));
    query.addBindValue(hash);
    query.addBindValue(trimmedUserId);
    query.addBindValue(expires.toString(Qt::ISODateWithMs));
    query.addBindValue(now.toString(Qt::ISODateWithMs));
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return QString();
    }

    SessionRecord record;
    record.userId = trimmedUserId;
    record.expiresAtUtc = expires;
    QWriteLocker locker(&d->lock);
    d->sessionsByTokenHash.insert(hash, record);
    return token;
}

QString WebSessionStore::resolveSession(const QString &token) const
{
    const QString trimmedToken = token.trimmed();
    if (trimmedToken.isEmpty())
        return QString();

    QReadLocker locker(&d->lock);
    const SessionRecord record = d->sessionsByTokenHash.value(tokenHash(trimmedToken));
    if (record.userId.trimmed().isEmpty() || isExpired(record.expiresAtUtc))
        return QString();
    return record.userId;
}

bool WebSessionStore::revokeSession(const QString &token)
{
    const QString trimmedToken = token.trimmed();
    if (trimmedToken.isEmpty())
        return true;

    const QString path = storagePath();
    if (path.isEmpty()) {
        setLastError(tr("Web storage is not initialized."));
        return false;
    }

    const QString hash = tokenHash(trimmedToken);
    DatabaseConnection connection(path);
    if (!connection.error.isEmpty()) {
        setLastError(connection.error);
        return false;
    }

    QSqlQuery query(connection.db);
    query.prepare(QStringLiteral("DELETE FROM web_sessions WHERE token_hash = ?"));
    query.addBindValue(hash);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }

    QWriteLocker locker(&d->lock);
    d->sessionsByTokenHash.remove(hash);
    d->lastError.clear();
    return true;
}

bool WebSessionStore::revokeSessionsForUser(const QString &userId)
{
    const QString trimmedUserId = userId.trimmed();
    if (trimmedUserId.isEmpty())
        return true;

    const QString path = storagePath();
    if (path.isEmpty()) {
        setLastError(tr("Web storage is not initialized."));
        return false;
    }

    DatabaseConnection connection(path);
    if (!connection.error.isEmpty()) {
        setLastError(connection.error);
        return false;
    }

    QSqlQuery query(connection.db);
    query.prepare(QStringLiteral("DELETE FROM web_sessions WHERE user_id = ?"));
    query.addBindValue(trimmedUserId);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }

    QWriteLocker locker(&d->lock);
    const QList<QString> hashes = d->sessionsByTokenHash.keys();
    for (const QString &hash : hashes) {
        if (d->sessionsByTokenHash.value(hash).userId == trimmedUserId)
            d->sessionsByTokenHash.remove(hash);
    }
    d->lastError.clear();
    return true;
}

QString WebSessionStore::createPasswordResetToken(const QString &userId, const QString &purpose)
{
    const QString trimmedUserId = userId.trimmed();
    const QString trimmedPurpose = purpose.trimmed();
    if (trimmedUserId.isEmpty() || trimmedPurpose.isEmpty())
        return QString();

    const QString path = storagePath();
    if (path.isEmpty()) {
        setLastError(tr("Web storage is not initialized."));
        return QString();
    }

    if (!revokeSessionsForUser(trimmedUserId))
        return QString();

    DatabaseConnection connection(path);
    if (!connection.error.isEmpty()) {
        setLastError(connection.error);
        return QString();
    }

    if (!connection.db.transaction()) {
        setLastError(connection.db.lastError().text());
        return QString();
    }

    QSqlQuery deleteQuery(connection.db);
    deleteQuery.prepare(QStringLiteral("DELETE FROM web_password_resets WHERE user_id = ?"));
    deleteQuery.addBindValue(trimmedUserId);
    if (!deleteQuery.exec()) {
        connection.db.rollback();
        setLastError(deleteQuery.lastError().text());
        return QString();
    }

    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces)
                          + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString hash = tokenHash(token);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime expires = now.addDays(kPasswordResetTokenValidDays);

    QSqlQuery insertQuery(connection.db);
    insertQuery.prepare(QStringLiteral("INSERT INTO web_password_resets "
                                       "(token_hash, user_id, purpose, expires_at_utc, "
                                       "created_at_utc) VALUES (?, ?, ?, ?, ?)"));
    insertQuery.addBindValue(hash);
    insertQuery.addBindValue(trimmedUserId);
    insertQuery.addBindValue(trimmedPurpose);
    insertQuery.addBindValue(expires.toString(Qt::ISODateWithMs));
    insertQuery.addBindValue(now.toString(Qt::ISODateWithMs));
    if (!insertQuery.exec()) {
        connection.db.rollback();
        setLastError(insertQuery.lastError().text());
        return QString();
    }
    if (!connection.db.commit()) {
        setLastError(connection.db.lastError().text());
        return QString();
    }

    PasswordResetRecord record;
    record.userId = trimmedUserId;
    record.purpose = trimmedPurpose;
    record.expiresAtUtc = expires;

    QWriteLocker locker(&d->lock);
    const QList<QString> hashes = d->passwordResetsByTokenHash.keys();
    for (const QString &existingHash : hashes) {
        if (d->passwordResetsByTokenHash.value(existingHash).userId == trimmedUserId)
            d->passwordResetsByTokenHash.remove(existingHash);
    }
    d->passwordResetsByTokenHash.insert(hash, record);
    d->lastError.clear();
    return token;
}

QString WebSessionStore::passwordResetUserId(const QString &token, const QString &purpose) const
{
    QString userId;
    QString actualPurpose;
    if (!lookupPasswordResetToken(token, &userId, &actualPurpose))
        return QString();
    if (actualPurpose != purpose.trimmed())
        return QString();
    return userId;
}

bool WebSessionStore::lookupPasswordResetToken(const QString &token,
                                               QString *userIdOut,
                                               QString *purposeOut) const
{
    const QString trimmedToken = token.trimmed();
    if (trimmedToken.isEmpty())
        return false;

    QReadLocker locker(&d->lock);
    const PasswordResetRecord record = d->passwordResetsByTokenHash.value(tokenHash(trimmedToken));
    if (record.userId.isEmpty() || isExpired(record.expiresAtUtc))
        return false;
    if (userIdOut)
        *userIdOut = record.userId;
    if (purposeOut)
        *purposeOut = record.purpose;
    return true;
}

bool WebSessionStore::consumePasswordResetToken(const QString &token,
                                                const QString &purpose,
                                                QString *userIdOut)
{
    const QString trimmedToken = token.trimmed();
    const QString trimmedPurpose = purpose.trimmed();
    if (trimmedToken.isEmpty() || trimmedPurpose.isEmpty())
        return false;

    const QString hash = tokenHash(trimmedToken);
    const QString path = storagePath();
    if (path.isEmpty()) {
        setLastError(tr("Web storage is not initialized."));
        return false;
    }

    QString userId;
    {
        QReadLocker locker(&d->lock);
        const PasswordResetRecord record = d->passwordResetsByTokenHash.value(hash);
        if (record.userId.isEmpty() || record.purpose != trimmedPurpose
            || isExpired(record.expiresAtUtc)) {
            return false;
        }
        userId = record.userId;
    }

    DatabaseConnection connection(path);
    if (!connection.error.isEmpty()) {
        setLastError(connection.error);
        return false;
    }

    QSqlQuery query(connection.db);
    query.prepare(QStringLiteral("DELETE FROM web_password_resets WHERE token_hash = ?"));
    query.addBindValue(hash);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }

    QWriteLocker locker(&d->lock);
    d->passwordResetsByTokenHash.remove(hash);
    d->lastError.clear();
    if (userIdOut)
        *userIdOut = userId;
    return true;
}

bool WebSessionStore::appendEvent(const QString &userId, const QJsonObject &event)
{
    const QString trimmedUserId = userId.trimmed();
    if (trimmedUserId.isEmpty())
        return false;

    const QString path = storagePath();
    if (path.isEmpty()) {
        setLastError(tr("Web storage is not initialized."));
        return false;
    }

    DatabaseConnection connection(path);
    if (!connection.error.isEmpty()) {
        setLastError(connection.error);
        return false;
    }

    QSqlQuery query(connection.db);
    query.prepare(QStringLiteral("INSERT INTO web_events (user_id, payload, created_at_utc) "
                                 "VALUES (?, ?, ?)"));
    query.addBindValue(trimmedUserId);
    query.addBindValue(QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact)));
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }

    bool idOk = false;
    const qint64 id = lastInsertedRowId(connection.db, query, &idOk);
    if (!idOk || id <= 0) {
        setLastError(tr("Failed to read inserted web event id."));
        return false;
    }

    EventRecord record;
    record.id = id;
    record.payload = event;
    QMutexLocker waitLocker(&d->eventsWaitMutex);
    {
        QWriteLocker locker(&d->lock);
        d->eventsByUser[trimmedUserId].append(record);
    }
    d->eventsChanged.wakeAll();
    return true;
}

QJsonArray WebSessionStore::takeEvents(const QString &userId, int limit)
{
    QJsonArray result;
    const QString trimmedUserId = userId.trimmed();
    if (trimmedUserId.isEmpty())
        return result;

    QWriteLocker locker(&d->lock);
    QList<EventRecord> &events = d->eventsByUser[trimmedUserId];
    if (events.isEmpty()) {
        d->eventsByUser.remove(trimmedUserId);
        return result;
    }

    const int count = qMin(qMax(1, limit), events.size());
    const QList<EventRecord> selected = events.mid(0, count);
    for (const EventRecord &event : selected)
        result.append(event.payload);

    if (d->storagePath.isEmpty()) {
        d->lastError = tr("Web storage is not initialized.");
        return QJsonArray();
    }

    DatabaseConnection connection(d->storagePath);
    if (!connection.error.isEmpty()) {
        d->lastError = connection.error;
        return QJsonArray();
    }

    QString error;
    if (!deleteEvents(connection.db, selected, &error)) {
        d->lastError = error;
        return QJsonArray();
    }

    for (int i = 0; i < count; ++i)
        events.removeFirst();
    if (events.isEmpty())
        d->eventsByUser.remove(trimmedUserId);
    d->lastError.clear();
    return result;
}

QJsonArray WebSessionStore::waitAndTakeEvents(const QString &userId, int limit, int timeoutMs)
{
    const QString trimmedUserId = userId.trimmed();
    if (trimmedUserId.isEmpty())
        return QJsonArray();

    QJsonArray result = takeEvents(trimmedUserId, limit);
    if (!result.isEmpty() || timeoutMs <= 0)
        return result;

    QElapsedTimer timer;
    timer.start();

    int remainingMs = timeoutMs;
    while (remainingMs > 0) {
        QMutexLocker waitLocker(&d->eventsWaitMutex);
        bool hasEvents = false;
        {
            QReadLocker locker(&d->lock);
            hasEvents = d->eventsByUser.contains(trimmedUserId)
                        && !d->eventsByUser.value(trimmedUserId).isEmpty();
        }

        if (!hasEvents)
            d->eventsChanged.wait(&d->eventsWaitMutex, static_cast<unsigned long>(remainingMs));

        waitLocker.unlock();
        result = takeEvents(trimmedUserId, limit);
        if (!result.isEmpty())
            return result;

        remainingMs = timeoutMs - static_cast<int>(timer.elapsed());
    }

    return result;
}

bool WebSessionStore::registerFile(const QString &userId,
                                   const QString &filePath,
                                   WebSessionStore::StoredFile *fileOut)
{
    const QString trimmedUserId = userId.trimmed();
    const QString trimmedFilePath = filePath.trimmed();
    if (trimmedUserId.isEmpty() || trimmedFilePath.isEmpty())
        return false;

    QFileInfo fileInfo(trimmedFilePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        setLastError(tr("File is not available."));
        return false;
    }

    const QString path = storagePath();
    if (path.isEmpty()) {
        setLastError(tr("Web storage is not initialized."));
        return false;
    }

    WebSessionStore::StoredFile storedFile;
    storedFile.fileId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    storedFile.userId = trimmedUserId;
    storedFile.filePath = fileInfo.absoluteFilePath();
    storedFile.fileName = fileInfo.fileName().trimmed();
    if (storedFile.fileName.isEmpty())
        storedFile.fileName = QStringLiteral("download");
    storedFile.fileSize = fileInfo.size();
    storedFile.createdAtUtc = QDateTime::currentDateTimeUtc();

    DatabaseConnection connection(path);
    if (!connection.error.isEmpty()) {
        setLastError(connection.error);
        return false;
    }

    QSqlQuery query(connection.db);
    query.prepare(QStringLiteral("INSERT INTO web_files "
                                 "(file_id, user_id, file_path, file_name, file_size, "
                                 "created_at_utc) VALUES (?, ?, ?, ?, ?, ?)"));
    query.addBindValue(storedFile.fileId);
    query.addBindValue(storedFile.userId);
    query.addBindValue(storedFile.filePath);
    query.addBindValue(storedFile.fileName);
    query.addBindValue(storedFile.fileSize);
    query.addBindValue(storedFile.createdAtUtc.toString(Qt::ISODateWithMs));
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }

    QWriteLocker locker(&d->lock);
    d->filesByUser[storedFile.userId].insert(storedFile.fileId, storedFile);
    d->lastError.clear();
    if (fileOut)
        *fileOut = storedFile;
    return true;
}

WebSessionStore::StoredFile WebSessionStore::file(const QString &userId,
                                                  const QString &fileId) const
{
    const QString trimmedUserId = userId.trimmed();
    const QString trimmedFileId = fileId.trimmed();
    if (trimmedUserId.isEmpty() || trimmedFileId.isEmpty())
        return WebSessionStore::StoredFile();

    QReadLocker locker(&d->lock);
    return d->filesByUser.value(trimmedUserId).value(trimmedFileId);
}

bool WebSessionStore::registerFileData(const QString &userId,
                                       const QString &fileName,
                                       const QByteArray &fileData,
                                       WebSessionStore::StoredFile *fileOut)
{
    const QString trimmedUserId = userId.trimmed();
    if (trimmedUserId.isEmpty() || fileData.isEmpty())
        return false;

    const QString path = storagePath();
    if (path.isEmpty()) {
        setLastError(tr("Web storage is not initialized."));
        return false;
    }

    WebSessionStore::StoredFile storedFile;
    storedFile.fileId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    storedFile.userId = trimmedUserId;
    storedFile.fileName = safeStoredFileName(fileName);
    storedFile.fileSize = fileData.size();
    storedFile.createdAtUtc = QDateTime::currentDateTimeUtc();

    const QFileInfo storageInfo(path);
    QDir fileDir(storageInfo.absoluteDir().filePath(storageInfo.completeBaseName()
                                                    + QStringLiteral(".files")));
    if (!fileDir.exists() && !fileDir.mkpath(QStringLiteral("."))) {
        setLastError(tr("Failed to create web storage directory: %1").arg(fileDir.absolutePath()));
        return false;
    }

    const QString storedName = QStringLiteral("%1_%2").arg(storedFile.fileId, storedFile.fileName);
    storedFile.filePath = fileDir.filePath(storedName);
    QFile output(storedFile.filePath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setLastError(tr("File is not available."));
        return false;
    }
    if (output.write(fileData) != fileData.size()) {
        output.remove();
        setLastError(tr("File is not available."));
        return false;
    }
    output.close();

    DatabaseConnection connection(path);
    if (!connection.error.isEmpty()) {
        QFile::remove(storedFile.filePath);
        setLastError(connection.error);
        return false;
    }

    QSqlQuery query(connection.db);
    query.prepare(QStringLiteral("INSERT INTO web_files "
                                 "(file_id, user_id, file_path, file_name, file_size, "
                                 "created_at_utc) VALUES (?, ?, ?, ?, ?, ?)"));
    query.addBindValue(storedFile.fileId);
    query.addBindValue(storedFile.userId);
    query.addBindValue(storedFile.filePath);
    query.addBindValue(storedFile.fileName);
    query.addBindValue(storedFile.fileSize);
    query.addBindValue(storedFile.createdAtUtc.toString(Qt::ISODateWithMs));
    if (!query.exec()) {
        QFile::remove(storedFile.filePath);
        setLastError(query.lastError().text());
        return false;
    }

    QWriteLocker locker(&d->lock);
    d->filesByUser[storedFile.userId].insert(storedFile.fileId, storedFile);
    d->lastError.clear();
    if (fileOut)
        *fileOut = storedFile;
    return true;
}

bool WebSessionStore::saveCommands(const QString &userId,
                                   const QString &locale,
                                   const QJsonArray &commands)
{
    const QString trimmedUserId = userId.trimmed();
    const QString trimmedLocale =
            locale.trimmed().isEmpty() ? QStringLiteral("en") : locale.trimmed();
    if (trimmedUserId.isEmpty())
        return false;

    const QString path = storagePath();
    if (path.isEmpty()) {
        setLastError(tr("Web storage is not initialized."));
        return false;
    }

    DatabaseConnection connection(path);
    if (!connection.error.isEmpty()) {
        setLastError(connection.error);
        return false;
    }

    QSqlQuery query(connection.db);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO web_command_cache "
                                 "(user_id, locale, value, updated_at_utc) VALUES (?, ?, ?, ?)"));
    query.addBindValue(trimmedUserId);
    query.addBindValue(trimmedLocale);
    query.addBindValue(QString::fromUtf8(QJsonDocument(commands).toJson(QJsonDocument::Compact)));
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }

    QWriteLocker locker(&d->lock);
    d->commandsByUserLocale.insert(commandCacheKey(trimmedUserId, trimmedLocale), commands);
    return true;
}

QJsonArray WebSessionStore::commands(const QString &userId, const QString &locale) const
{
    const QString trimmedUserId = userId.trimmed();
    const QString trimmedLocale =
            locale.trimmed().isEmpty() ? QStringLiteral("en") : locale.trimmed();
    if (trimmedUserId.isEmpty())
        return QJsonArray();

    QReadLocker locker(&d->lock);
    return d->commandsByUserLocale.value(commandCacheKey(trimmedUserId, trimmedLocale));
}

bool WebSessionStore::clearCommands(const QString &userId)
{
    const QString trimmedUserId = userId.trimmed();
    if (trimmedUserId.isEmpty())
        return false;

    const QString path = storagePath();
    if (path.isEmpty()) {
        setLastError(tr("Web storage is not initialized."));
        return false;
    }

    DatabaseConnection connection(path);
    if (!connection.error.isEmpty()) {
        setLastError(connection.error);
        return false;
    }

    QSqlQuery query(connection.db);
    query.prepare(QStringLiteral("DELETE FROM web_command_cache WHERE user_id = ?"));
    query.addBindValue(trimmedUserId);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }

    const QString keyPrefix = trimmedUserId + QLatin1Char('\n');
    QWriteLocker locker(&d->lock);
    const QList<QString> keys = d->commandsByUserLocale.keys();
    for (const QString &key : keys) {
        if (key.startsWith(keyPrefix))
            d->commandsByUserLocale.remove(key);
    }
    d->lastError.clear();
    return true;
}

bool WebSessionStore::clearAllCommands()
{
    const QString path = storagePath();
    if (path.isEmpty()) {
        setLastError(tr("Web storage is not initialized."));
        return false;
    }

    DatabaseConnection connection(path);
    if (!connection.error.isEmpty()) {
        setLastError(connection.error);
        return false;
    }

    QSqlQuery query(connection.db);
    if (!query.exec(QStringLiteral("DELETE FROM web_command_cache"))) {
        setLastError(query.lastError().text());
        return false;
    }

    QWriteLocker locker(&d->lock);
    d->commandsByUserLocale.clear();
    d->lastError.clear();
    return true;
}

QString WebSessionStore::defaultStoragePath() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::currentPath();
    return QDir(base).filePath(QStringLiteral("booking-resources-web.sqlite"));
}

QString WebSessionStore::tokenHash(const QString &token) const
{
    const QByteArray digest =
            QCryptographicHash::hash(token.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QString::fromLatin1(digest);
}

void WebSessionStore::setLastError(const QString &error) const
{
    QWriteLocker locker(&d->lock);
    d->lastError = error;
}
