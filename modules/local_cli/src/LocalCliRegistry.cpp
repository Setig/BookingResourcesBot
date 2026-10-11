#include "LocalCliRegistry.h"

#include <QDir>
#include <QFile>
#include <QtGlobal>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QCoreApplication>

namespace {

QString baseDirectoryPath()
{
    QString root = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (root.isEmpty())
        root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (root.isEmpty())
        root = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (root.isEmpty())
        root = QDir::tempPath();

    QDir dir(root);
    return dir.filePath(QStringLiteral("booking-resources/local-cli"));
}

QString entryFileName(qint64 pid)
{
    return QStringLiteral("booking-resources-%1.json").arg(pid);
}

bool ensureDirectoryExists(const QString &path)
{
    QDir dir(path);
    if (dir.exists())
        return true;
    return dir.mkpath(QStringLiteral("."));
}

LocalCliRegistry::Entry parseEntry(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    const QByteArray data = file.readAll();
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return {};

    LocalCliRegistry::Entry entry;
    const QJsonObject obj = doc.object();
    bool pidOk = false;
    entry.pid = obj.value(QStringLiteral("pid")).toVariant().toLongLong(&pidOk);
    if (!pidOk)
        entry.pid = -1;
    entry.kind = LocalCliRegistry::parseKind(obj.value(QStringLiteral("kind")).toString());
    entry.serverName = obj.value(QStringLiteral("endpoint")).toString();
    entry.executablePath = obj.value(QStringLiteral("executable")).toString();
    entry.sharedMemoryKey = obj.value(QStringLiteral("shared_memory_key")).toString().trimmed();
    const QString started = obj.value(QStringLiteral("started_at")).toString();
    entry.startedAtUtc = QDateTime::fromString(started, Qt::ISODateWithMs);
    if (!entry.startedAtUtc.isValid())
        entry.startedAtUtc = QDateTime::fromString(started, Qt::ISODate);
    if (entry.startedAtUtc.isValid())
        entry.startedAtUtc = entry.startedAtUtc.toUTC();

    if (!entry.isValid())
        return {};

    return entry;
}

} // namespace

QString LocalCliRegistry::kindKey(Kind kind)
{
    if (kind == Kind::Web)
        return QStringLiteral("web");
    return QStringLiteral("server");
}

LocalCliRegistry::Kind LocalCliRegistry::parseKind(const QString &value)
{
    if (value.trimmed().compare(QLatin1String("web"), Qt::CaseInsensitive) == 0)
        return Kind::Web;
    return Kind::Server;
}

QString LocalCliRegistry::defaultServerName(qint64 pid)
{
    if (pid <= 0)
        pid = QCoreApplication::applicationPid();
    return QStringLiteral("booking-resources-%1").arg(pid);
}

QString LocalCliRegistry::registryDirectory()
{
    const QString dirPath = baseDirectoryPath();
    ensureDirectoryExists(dirPath);
    return dirPath;
}

QString LocalCliRegistry::entryFilePath(qint64 pid)
{
    if (pid <= 0)
        return QString();
    QDir dir(registryDirectory());
    return dir.filePath(entryFileName(pid));
}

bool LocalCliRegistry::registerInstance(const Entry &entry)
{
    if (!entry.isValid())
        return false;

    const QString dirPath = registryDirectory();
    if (dirPath.isEmpty())
        return false;

    if (!ensureDirectoryExists(dirPath))
        return false;

    const QString filePath = entryFilePath(entry.pid);
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    QJsonObject obj;
    obj.insert(QStringLiteral("pid"), QString::number(entry.pid));
    obj.insert(QStringLiteral("kind"), kindKey(entry.kind));
    obj.insert(QStringLiteral("endpoint"), entry.serverName);
    obj.insert(QStringLiteral("executable"), entry.executablePath);
    if (!entry.sharedMemoryKey.trimmed().isEmpty())
        obj.insert(QStringLiteral("shared_memory_key"), entry.sharedMemoryKey.trimmed());
    obj.insert(QStringLiteral("started_at"),
               entry.startedAtUtc.isValid() ? entry.startedAtUtc.toString(Qt::ISODateWithMs)
                                            : QString());

    const QJsonDocument doc(obj);
    const QByteArray data = doc.toJson(QJsonDocument::Compact);
    const qint64 written = file.write(data);
    if (written != data.size())
        return false;

    return file.commit();
}

void LocalCliRegistry::unregisterInstance(qint64 pid)
{
    if (pid <= 0)
        return;
    const QString path = entryFilePath(pid);
    if (path.isEmpty())
        return;
    QFile::remove(path);
}

QVector<LocalCliRegistry::Entry> LocalCliRegistry::entries()
{
    const QString dirPath = baseDirectoryPath();
    QDir dir(dirPath);
    if (!dir.exists())
        return {};

    const QFileInfoList files = dir.entryInfoList(QStringList() << QStringLiteral("*.json"),
                                                  QDir::Files | QDir::Readable,
                                                  QDir::Time);
    QVector<Entry> result;
    result.reserve(files.size());
    for (const QFileInfo &info : files) {
        const Entry entry = parseEntry(info.absoluteFilePath());
        if (entry.isValid())
            result.append(entry);
    }
    return result;
}

bool LocalCliRegistry::entryForPid(qint64 pid, Entry *entry)
{
    if (pid <= 0)
        return false;
    const QString path = entryFilePath(pid);
    if (path.isEmpty())
        return false;
    const Entry parsed = parseEntry(path);
    if (!parsed.isValid())
        return false;
    if (entry)
        *entry = parsed;
    return true;
}

bool LocalCliRegistry::entryForSharedMemoryKey(const QString &sharedMemoryKey, Entry *entry)
{
    const QString trimmedKey = sharedMemoryKey.trimmed();
    if (trimmedKey.isEmpty())
        return false;

    const QVector<Entry> all = entries();
    for (const Entry &candidate : all) {
        if (!candidate.isWeb())
            continue;
        if (candidate.sharedMemoryKey != trimmedKey)
            continue;
        if (entry)
            *entry = candidate;
        return true;
    }
    return false;
}
