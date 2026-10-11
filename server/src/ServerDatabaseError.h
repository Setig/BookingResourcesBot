#ifndef SERVERDATABASEERROR_H
#define SERVERDATABASEERROR_H

#include <QCoreApplication>

#include <QSqlError>
#include <QString>

/**
 * @brief Describes an error returned by ServerDatabase write operations.
 *
 * Designed in a Qt-style fashion similar to QSqlError/QSslError: the default-constructed value
 * represents "no error", while errorString() provides a translated, user-facing message.
 */
class ServerDatabaseError
{
    Q_GADGET
    Q_DECLARE_TR_FUNCTIONS(ServerDatabaseError)

public:
    enum class Operation {
        None = 0,
        AddResource,
        UpdateResource,
        SetResourceStatus,
        DeleteResource,
        RenameResource,
        AddUser,
        UpdateUser,
        DeleteUser,
        EnsureFixedAdmin,
        UpdateServerSetting,
        AddOccupationDuration,
        RemoveOccupationDuration,
        SaveBlacklistEntry,
        RemoveBlacklistEntry,
        BanUser,
        UnbanUser,
        Enqueue,
        Schedule,
        Unschedule,
        Dequeue,
        Release,
        RemoveResourceQueue,
        RemoveUserQueues,
        RecordPendingAccessUser,
        ClearPendingAccessUser,
    };
    Q_ENUM(Operation)

    enum class Code {
        NoError = 0,

        DatabaseNotReady,
        DatabaseOpenFailed,

        TransactionBeginFailed,
        TransactionCommitFailed,
        TransactionRollbackFailed,

        ResourceNameEmpty,
        ResourceNotFound,
        ResourceAlreadyExists,
        ResourceTemporarilyUnavailable,
        ResourceOff,

        UserInvalid,
        UserNotFound,
        UserAlreadyExists,

        BlacklistEntryInvalid,
        UserNotBlacklisted,

        PersistenceFailed,
        OperationFailed,
    };
    Q_ENUM(Code)

    ServerDatabaseError();
    explicit ServerDatabaseError(Code code, Operation operation = Operation::None);
    ServerDatabaseError(Code code,
                        Operation operation,
                        QString subject,
                        QString details = QString(),
                        QSqlError sqlError = QSqlError());

    [[nodiscard]] bool isValid() const { return m_code != Code::NoError; }

    explicit operator bool() const { return isValid(); }

    [[nodiscard]] Code code() const { return m_code; }

    [[nodiscard]] Operation operation() const { return m_operation; }

    [[nodiscard]] QString subject() const { return m_subject; }

    [[nodiscard]] QString details() const { return m_details; }

    [[nodiscard]] QSqlError sqlError() const { return m_sqlError; }

    /**
     * @brief Returns a translated, user-facing error message.
     */
    [[nodiscard]] QString errorString() const;

    /**
     * @brief Returns a debug-oriented string with extra details (SQL error, context, etc.).
     */
    [[nodiscard]] QString debugString() const;

private:
    [[nodiscard]] QString defaultPersistenceFailureText() const;

    Code m_code;
    Operation m_operation;
    QString m_subject;
    QString m_details;
    QSqlError m_sqlError;
};

#endif // SERVERDATABASEERROR_H
