#include "ServerDatabaseError.h"

#include "MessengerUserFacingErrors.h"
#include "ResourceQueueUserMessages.h"

#include <QStringList>

namespace {

QString trimmedOrEmpty(const QString &value)
{
    return value.trimmed();
}

} // namespace

ServerDatabaseError::ServerDatabaseError()
    : m_code(Code::NoError)
    , m_operation(Operation::None)
{
    // do nothing
}

ServerDatabaseError::ServerDatabaseError(Code code, Operation operation)
    : m_code(code)
    , m_operation(operation)
{
    // do nothing
}

ServerDatabaseError::ServerDatabaseError(
        Code code, Operation operation, QString subject, QString details, QSqlError sqlError)
    : m_code(code)
    , m_operation(operation)
    , m_subject(std::move(subject))
    , m_details(std::move(details))
    , m_sqlError(std::move(sqlError))
{
    // do nothing
}

QString ServerDatabaseError::defaultPersistenceFailureText() const
{
    switch (m_operation) {
    case Operation::AddResource:
        return tr("Failed to add resource.");
    case Operation::UpdateResource:
        return tr("Failed to update resource.");
    case Operation::SetResourceStatus:
        return tr("Failed to update resource status.");
    case Operation::DeleteResource:
        return tr("Failed to delete resource.");
    case Operation::RenameResource:
        return tr("Failed to rename resource.");
    case Operation::AddUser:
        return tr("Failed to add user.");
    case Operation::UpdateUser:
        return tr("Failed to update user.");
    case Operation::DeleteUser:
        return tr("Failed to delete user.");
    case Operation::EnsureFixedAdmin:
        return tr("Failed to update fixed admin.");
    case Operation::UpdateServerSetting:
        return tr("Failed to update server setting.");
    case Operation::AddOccupationDuration:
        return tr("Failed to add occupation duration.");
    case Operation::RemoveOccupationDuration:
        return tr("Failed to remove occupation duration.");
    case Operation::SaveBlacklistEntry:
        return tr("Failed to update blacklist.");
    case Operation::RemoveBlacklistEntry:
        return tr("Failed to remove user from blacklist.");
    case Operation::BanUser:
        return tr("Failed to update blacklist.");
    case Operation::UnbanUser:
        return tr("Failed to remove user from blacklist.");
    case Operation::Enqueue:
        return tr("Failed to occupy resource.");
    case Operation::Schedule:
        return tr("Failed to schedule resource.");
    case Operation::Unschedule:
        return tr("Failed to remove schedule.");
    case Operation::Dequeue:
        return tr("Failed to leave queue.");
    case Operation::Release:
        return tr("Failed to release resource.");
    case Operation::RemoveResourceQueue:
        return tr("Failed to update queue.");
    case Operation::RemoveUserQueues:
        return tr("Failed to update queue.");
    case Operation::RecordPendingAccessUser:
        return tr("Failed to persist user awaiting access activation.");
    case Operation::ClearPendingAccessUser:
        return tr("Failed to update users awaiting access activation.");
    case Operation::None:
        break;
    }
    return tr("Operation failed.");
}

QString ServerDatabaseError::errorString() const
{
    if (!isValid())
        return QString();

    const QString detailsTrimmed = trimmedOrEmpty(m_details);

    switch (m_code) {
    case Code::NoError:
        return QString();

    case Code::DatabaseNotReady:
    case Code::DatabaseOpenFailed:
        return tr("Database is not available right now.");

    case Code::TransactionBeginFailed:
        return tr("Failed to start database transaction.");
    case Code::TransactionCommitFailed:
        return tr("Failed to commit database transaction.");
    case Code::TransactionRollbackFailed:
        return tr("Failed to rollback database transaction.");

    case Code::ResourceNameEmpty:
        return tr("Resource name cannot be empty.");
    case Code::ResourceNotFound:
        return ResourceQueueUserMessages::resourceNotFound(m_subject);
    case Code::ResourceAlreadyExists:
        return tr("Resource '%1' already exists.").arg(m_subject);
    case Code::ResourceTemporarilyUnavailable:
        return tr("Resource '%1' is temporarily unavailable.").arg(m_subject);
    case Code::ResourceOff:
        return tr("Resource '%1' is turned off.").arg(m_subject);

    case Code::UserInvalid:
        return MessengerUserFacingErrors::invalidUserValue();
    case Code::UserNotFound:
        return MessengerUserFacingErrors::userNotFound(m_subject);
    case Code::UserAlreadyExists:
        return tr("User %1 already exists.").arg(m_subject);

    case Code::BlacklistEntryInvalid:
        return MessengerUserFacingErrors::invalidUserValue();
    case Code::UserNotBlacklisted:
        return MessengerUserFacingErrors::userNotInBlacklist(m_subject);

    case Code::PersistenceFailed:
        return detailsTrimmed.isEmpty() ? defaultPersistenceFailureText() : detailsTrimmed;

    case Code::OperationFailed:
        return detailsTrimmed.isEmpty() ? defaultPersistenceFailureText() : detailsTrimmed;
    }

    return detailsTrimmed.isEmpty() ? defaultPersistenceFailureText() : detailsTrimmed;
}

QString ServerDatabaseError::debugString() const
{
    if (!isValid())
        return QString();

    QStringList parts;
    parts << errorString();

    const QString detailsTrimmed = trimmedOrEmpty(m_details);
    if (!detailsTrimmed.isEmpty() && detailsTrimmed != errorString())
        parts << detailsTrimmed;

    if (m_sqlError.isValid()) {
        const QString sqlText = trimmedOrEmpty(m_sqlError.text());
        if (!sqlText.isEmpty())
            parts << sqlText;
    }

    const QString subjectTrimmed = trimmedOrEmpty(m_subject);
    if (!subjectTrimmed.isEmpty())
        parts << QStringLiteral("subject=%1").arg(subjectTrimmed);

    return parts.join(QStringLiteral(" | "));
}
