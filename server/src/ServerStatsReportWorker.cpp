#include "ServerStatsReportWorker.h"

#include <QThread>

ServerStatsReportWorker::ServerStatsReportWorker(const QString &baseDatabasePath,
                                                 const ServerStats::ReportRequest &request,
                                                 const QString &outputDirectory)
    : QObject()
    , m_baseDatabasePath(baseDatabasePath)
    , m_request(request)
    , m_outputDirectory(outputDirectory)
{
    // do nothing
}

void ServerStatsReportWorker::run()
{
    const ServerStats::ReportResult result =
            ServerStats::generateReport(m_baseDatabasePath, m_request, m_outputDirectory, []() {
                QThread *thread = QThread::currentThread();
                return thread && thread->isInterruptionRequested();
            });
    emit finished(static_cast<int>(result.format),
                  result.tableText,
                  result.filePath,
                  result.errorText,
                  result.rowCount,
                  result.totalRows,
                  result.truncated);
}
