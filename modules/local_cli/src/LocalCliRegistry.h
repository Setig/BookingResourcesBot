#ifndef LOCALCLIREGISTRY_H
#define LOCALCLIREGISTRY_H

#include <QVector>
#include <QDateTime>

#include <QString>

/**
 * @brief Discovers and advertises running Local CLI server instances.
 *
 * The registry stores per-process JSON files in a per-user directory so that the CLI can list
 * running servers and connect to a selected process by PID or web shared-memory key.
 */
class LocalCliRegistry
{
public:
    /**
     * @brief Process kind advertised in a registry entry.
     */
    enum class Kind {
        Server,
        Web,
    };

    /**
     * @brief Registry entry describing one running Local CLI host process.
     */
    struct Entry
    {
        qint64 pid = -1;
        Kind kind = Kind::Server;
        QString serverName;
        QString executablePath;
        QString sharedMemoryKey;
        QDateTime startedAtUtc;

        [[nodiscard]] bool isValid() const { return pid > 0 && !serverName.isEmpty(); }

        [[nodiscard]] bool isWeb() const { return kind == Kind::Web; }
    };

    [[nodiscard]] static QString kindKey(Kind kind);
    [[nodiscard]] static Kind parseKind(const QString &value);
    [[nodiscard]] static QString defaultServerName(qint64 pid);
    [[nodiscard]] static QString registryDirectory();
    [[nodiscard]] static QString entryFilePath(qint64 pid);
    [[nodiscard]] static bool registerInstance(const Entry &entry);
    static void unregisterInstance(qint64 pid);
    [[nodiscard]] static QVector<Entry> entries();
    [[nodiscard]] static bool entryForPid(qint64 pid, Entry *entry);
    [[nodiscard]] static bool entryForSharedMemoryKey(const QString &sharedMemoryKey, Entry *entry);
};

#endif // LOCALCLIREGISTRY_H
