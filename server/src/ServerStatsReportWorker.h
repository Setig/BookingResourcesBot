#ifndef SERVERSTATSREPORTWORKER_H
#define SERVERSTATSREPORTWORKER_H

#include <QObject>

#include <QString>

#include "ServerStats.h"

class ServerStatsReportWorker final : public QObject
{
    Q_OBJECT

public:
    ServerStatsReportWorker(const QString &baseDatabasePath,
                            const ServerStats::ReportRequest &request,
                            const QString &outputDirectory);

public slots:
    void run();

signals:
    void finished(int format,
                  const QString &tableText,
                  const QString &filePath,
                  const QString &errorText,
                  int rowCount,
                  int totalRows,
                  bool truncated);

private:
    QString m_baseDatabasePath;
    ServerStats::ReportRequest m_request;
    QString m_outputDirectory;
};

#endif // SERVERSTATSREPORTWORKER_H
