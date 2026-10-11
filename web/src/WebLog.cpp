#include "WebLog.h"

#include "ApplicationLog.h"

void WebLog::initializeLogging(const QString &filePath)
{
    ApplicationLog::initializeLogging(filePath);
}

WebLog::WebLog()
{
    // do nothing
}
