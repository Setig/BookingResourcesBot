#include "ApplicationLog.h"

#include <cstdio>
#include <cstdlib>

#include <QDir>
#include <QDate>
#include <QFile>
#include <QMutex>
#include <QDateTime>
#include <QFileInfo>
#include <QTextStream>
#include <QMutexLocker>
#include <QLoggingCategory>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#  include <QStringConverter>
#else
#  include <QTextCodec>
#endif

namespace {

QMutex s_logMutex;
QString s_baseLogFilePath;
QFile s_logFile;
QDate s_currentDate;
qint64 s_maxLogFileSize = 5 * 1024 * 1024; // 5 MiB
bool s_logToFile = false;
bool s_messageHandlerInstalled = false;
bool s_filterRulesInstalled = false;

void configureStreamForLocale(QTextStream &stream)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    stream.setEncoding(QStringConverter::System);
#else
    if (QTextCodec *codec = QTextCodec::codecForLocale())
        stream.setCodec(codec);
#endif
}

QString logSeverityToString(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("D");
    case QtInfoMsg:
        return QStringLiteral("I");
    case QtWarningMsg:
        return QStringLiteral("W");
    case QtCriticalMsg:
        return QStringLiteral("E");
    case QtFatalMsg:
        return QStringLiteral("F");
    }

    return QStringLiteral("U");
}

FILE *consoleFileForType(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
    case QtInfoMsg:
    case QtWarningMsg:
        return stdout;
    case QtCriticalMsg:
    case QtFatalMsg:
        return stderr;
    }

    return stderr;
}

void installDefaultFilterRules()
{
    const QByteArray envRules = qgetenv("QT_LOGGING_RULES");

    QString rules = QStringLiteral("*.debug=false");
    if (!envRules.trimmed().isEmpty()) {
        rules += QLatin1Char('\n');
        rules += QString::fromLocal8Bit(envRules);
    }

    QLoggingCategory::setFilterRules(rules);
}

QString buildLogFilePathForDate(const QString &basePath, const QDate &date)
{
    if (basePath.isEmpty())
        return QString();

    const QFileInfo info(basePath);
    QString dirPath = info.absolutePath();

    if (dirPath.isEmpty())
        dirPath = QDir::currentPath();

    const QString baseName = info.completeBaseName();
    const QString suffix = info.completeSuffix().isEmpty()
                                   ? QString()
                                   : QStringLiteral(".") + info.completeSuffix();

    const QString datedName =
            QStringLiteral("%1_%2%3").arg(baseName,
                                          date.toString(QStringLiteral("yyyy-MM-dd")),
                                          suffix);

    return QDir(dirPath).filePath(datedName);
}

void rotateAndCompressCurrentLogIfNeeded()
{
    if (!s_logFile.isOpen())
        return;

    if (s_logFile.size() < s_maxLogFileSize)
        return;

    const QString currentPath = s_logFile.fileName();
    s_logFile.close();

    QFile inFile(currentPath);
    if (!inFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    const QByteArray data = inFile.readAll();
    inFile.close();

    const QByteArray compressed = qCompress(data, 9);

    const QString rotatedPathBase =
            currentPath + QStringLiteral(".")
            + QDateTime::currentDateTime().toString(QStringLiteral("HHmmss"));
    const QString rotatedCompressedPath = rotatedPathBase + QStringLiteral(".qz");

    QFile outFile(rotatedCompressedPath);
    if (outFile.open(QIODevice::WriteOnly)) {
        outFile.write(compressed);
        outFile.close();

        QFile::remove(currentPath);
    }
}

void ensureLogFile()
{
    if (s_baseLogFilePath.isEmpty()) {
        s_logToFile = false;
        return;
    }

    const QDate today = QDate::currentDate();

    if (s_logFile.isOpen()) {
        if (s_currentDate != today) {
            s_logFile.close();
        } else {
            rotateAndCompressCurrentLogIfNeeded();
            if (s_logFile.isOpen())
                return;
        }
    }

    const QString logFilePath = buildLogFilePathForDate(s_baseLogFilePath, today);

    const QFileInfo info(logFilePath);
    QDir dir(info.absolutePath());
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }

    s_logFile.setFileName(logFilePath);
    if (!s_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        s_logToFile = false;

        // Log a warning directly to stdout if the log file cannot be opened.
        const QString timestamp =
                QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));
        QTextStream consoleStream(consoleFileForType(QtWarningMsg));
        configureStreamForLocale(consoleStream);
        consoleStream << timestamp << QStringLiteral(" [W] Failed to open log file \"")
                      << logFilePath << QStringLiteral("\": ") << s_logFile.errorString()
                      << Qt::endl;

        return;
    }

    s_currentDate = today;
    s_logToFile = true;
}

void appLogMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    Q_UNUSED(context);

    const QString timestamp =
            QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));
    const QString level = logSeverityToString(type);
    const QString formatted = QStringLiteral("%1 [%2] %3").arg(timestamp, level, msg);

    bool shouldLogToConsole = true;

    {
        QMutexLocker locker(&s_logMutex);

        ensureLogFile();

        if (s_logToFile && s_logFile.isOpen()) {
            QTextStream fileStream(&s_logFile);
            configureStreamForLocale(fileStream);
            fileStream << formatted << Qt::endl;
            s_logFile.flush();
        }

        // When a log file is configured and available, redirect console output to the file.
        shouldLogToConsole = !(s_logToFile && s_logFile.isOpen());
    }

    if (shouldLogToConsole) {
        QTextStream consoleStream(consoleFileForType(type));
        configureStreamForLocale(consoleStream);
        consoleStream << formatted << Qt::endl;
    }

    if (type == QtFatalMsg) {
        std::abort();
    }
}

} // namespace

void ApplicationLog::initializeLogging(const QString &filePath)
{
    QMutexLocker locker(&s_logMutex);

    s_baseLogFilePath = filePath;

    if (!s_filterRulesInstalled) {
        installDefaultFilterRules();
        s_filterRulesInstalled = true;
    }

    if (!s_messageHandlerInstalled) {
        qInstallMessageHandler(appLogMessageHandler);
        s_messageHandlerInstalled = true;
    }
}

ApplicationLog::ApplicationLog()
{
    // do nothing
}
