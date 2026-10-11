#include <QtTest>
#include "TestRegistry.h"

#include "LocalCliRegistry.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

class LocalCliRegistryTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void registersAndReadsLargePid();
    void listsEntries();
    void registersWebEntryAndLooksUpByKey();
    void missingKindIsReadAsServer();
};

void LocalCliRegistryTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void LocalCliRegistryTests::registersAndReadsLargePid()
{
    const qint64 pid = 9007199254740993LL;
    LocalCliRegistry::Entry entry;
    entry.pid = pid;
    entry.serverName = LocalCliRegistry::defaultServerName(pid);
    entry.executablePath = QStringLiteral("test-exe");
    entry.startedAtUtc = QDateTime::currentDateTimeUtc();

    QVERIFY(LocalCliRegistry::registerInstance(entry));

    LocalCliRegistry::Entry restored;
    QVERIFY(LocalCliRegistry::entryForPid(pid, &restored));
    QCOMPARE(restored.pid, pid);
    QCOMPARE(restored.serverName, entry.serverName);
    QCOMPARE(restored.executablePath, entry.executablePath);

    LocalCliRegistry::unregisterInstance(pid);
}

void LocalCliRegistryTests::listsEntries()
{
    const qint64 pidA = 9007199254740995LL;
    const qint64 pidB = 9007199254740997LL;

    LocalCliRegistry::Entry entryA;
    entryA.pid = pidA;
    entryA.serverName = LocalCliRegistry::defaultServerName(pidA);
    entryA.executablePath = QStringLiteral("test-exe-a");
    entryA.startedAtUtc = QDateTime::currentDateTimeUtc();

    LocalCliRegistry::Entry entryB;
    entryB.pid = pidB;
    entryB.serverName = LocalCliRegistry::defaultServerName(pidB);
    entryB.executablePath = QStringLiteral("test-exe-b");
    entryB.startedAtUtc = QDateTime::currentDateTimeUtc();

    QVERIFY(LocalCliRegistry::registerInstance(entryA));
    QVERIFY(LocalCliRegistry::registerInstance(entryB));

    const QVector<LocalCliRegistry::Entry> entries = LocalCliRegistry::entries();
    bool foundA = false;
    bool foundB = false;
    for (const LocalCliRegistry::Entry &entry : entries) {
        if (entry.pid == pidA)
            foundA = true;
        if (entry.pid == pidB)
            foundB = true;
    }

    QVERIFY(foundA);
    QVERIFY(foundB);

    LocalCliRegistry::unregisterInstance(pidA);
    LocalCliRegistry::unregisterInstance(pidB);
}

void LocalCliRegistryTests::registersWebEntryAndLooksUpByKey()
{
    const qint64 pid = 9007199254741101LL;
    LocalCliRegistry::Entry entry;
    entry.pid = pid;
    entry.kind = LocalCliRegistry::Kind::Web;
    entry.serverName = LocalCliRegistry::defaultServerName(pid);
    entry.executablePath = QStringLiteral("booking-resources-web");
    entry.sharedMemoryKey = QStringLiteral("booking-resources-web");
    entry.startedAtUtc = QDateTime::currentDateTimeUtc();

    QVERIFY(LocalCliRegistry::registerInstance(entry));

    LocalCliRegistry::Entry restored;
    QVERIFY(LocalCliRegistry::entryForPid(pid, &restored));
    QCOMPARE(restored.kind, LocalCliRegistry::Kind::Web);
    QCOMPARE(restored.sharedMemoryKey, QStringLiteral("booking-resources-web"));

    LocalCliRegistry::Entry byKey;
    QVERIFY(LocalCliRegistry::entryForSharedMemoryKey(QStringLiteral("booking-resources-web"),
                                                      &byKey));
    QCOMPARE(byKey.pid, pid);
    QVERIFY(byKey.isWeb());

    LocalCliRegistry::unregisterInstance(pid);
}

void LocalCliRegistryTests::missingKindIsReadAsServer()
{
    const qint64 pid = 9007199254741103LL;
    const QString path = LocalCliRegistry::entryFilePath(pid);
    QVERIFY(!path.isEmpty());

    QJsonObject obj;
    obj.insert(QStringLiteral("pid"), QString::number(pid));
    obj.insert(QStringLiteral("endpoint"), LocalCliRegistry::defaultServerName(pid));
    obj.insert(QStringLiteral("executable"), QStringLiteral("legacy-server"));
    obj.insert(QStringLiteral("started_at"),
               QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact)) > 0);
    file.close();

    LocalCliRegistry::Entry restored;
    QVERIFY(LocalCliRegistry::entryForPid(pid, &restored));
    QCOMPARE(restored.kind, LocalCliRegistry::Kind::Server);
    QVERIFY(restored.sharedMemoryKey.isEmpty());

    LocalCliRegistry::unregisterInstance(pid);
}

BOOKING_RESOURCES_REGISTER_TEST(LocalCliRegistryTests)

#include "LocalCliRegistryTests.moc"
