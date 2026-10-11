#include "ServerLog.h"

#include "ApplicationLog.h"

void ServerLog::initializeLogging(const QString &filePath)
{
    ApplicationLog::initializeLogging(filePath);
}

ServerLog::ServerLog()
{
    // do nothing
}
