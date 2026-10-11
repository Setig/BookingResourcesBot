#include "LocalCliHelper.h"

#include <QHostInfo>

qint64 LocalCliHelper::parsePid(const QString &value, bool *ok)
{
    bool success = false;
    const qint64 pid = value.toLongLong(&success);
    if (ok)
        *ok = success;
    return pid;
}

QString LocalCliHelper::localHostLabel()
{
    const QString host = QHostInfo::localHostName().trimmed();
    if (!host.isEmpty())
        return host;
    return QStringLiteral("local-cli");
}
