#include <QtTest>
#include "TestRegistry.h"

#include "User.h"
#include "Resource.h"
#include "UserModel.h"
#include "ResourceModel.h"
#include "ResourceQueueModel.h"
#include "ServerStats.h"
#include "ServerDatabase.h"
#include "ServerSettingsModel.h"
#include "ServerDateTimeFormat.h"

#include <QDir>
#include <QMap>
#include <QSet>
#include <QFile>
#include <QUuid>
#include <QVariant>
#include <QFileInfo>
#include <QDateTime>
#include <QTimeZone>
#include <QSqlError>
#include <QSqlQuery>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QTemporaryDir>
#include <QCoreApplication>

namespace {
const MessengerUserKey kTestActor(MessengerType::LocalCli, QStringLiteral("test"));

QString readUtf8File(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(file.readAll());
}

QString csvValueAt(const QStringList &rows, int row, int column)
{
    if (row < 0 || row >= rows.size())
        return QString();
    const QStringList columns = rows.at(row).split(QLatin1Char(','));
    if (column < 0 || column >= columns.size())
        return QString();
    return columns.at(column).trimmed();
}
} // namespace

class ServerDatabaseTests : public QObject
{
    Q_OBJECT

private slots:
    void initializesDatabaseFile();
    void testDatabaseRows();
    void deleteResourceTrimsName();
    void resourceQueueLifecycle();
    void resourceStatusControlsQueueAndAvailability();
    void scheduledOccupationLifecycle();
    void dataPersistsAcrossInstances();
    void userRoleAggregatesGroupRoles();
    void persistedFixedAdminRoleLoadsAsAdmin();
    void groupAdminAccessScopesResourceMutations();
    void resourceGroupUpdateRollsBackAtomically();
    void groupAdminAccessScopesUserMutations();
    void selfUpdateCannotChangeGroups();
    void blacklistAndQueueRespectGroupScopes();
    void serverSettingsRequireRootAdmin();
    void recentPendingAccessUsersRetention();
    void occupationDurationUpdates();
    void userTimezoneSettingPersistsAcrossInstances();
    void serverDateTimeFormatRespectsExplicitTimezone();
    void statisticsReportsUseRequestedTimezone();
    void statisticsReportScopeExcludesSiblingGroups();
    void statisticsReportCanBeCancelled();
    void expiredQueueEntriesAreRemovedInBatch();
    void runtimeDateTimeFormatSetting();
    void runtimePendingAccessReplySetting();
};

void ServerDatabaseTests::initializesDatabaseFile()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for ServerDatabase");
    const QString dbPath = tempDir.filePath(QStringLiteral("server_database.sqlite"));

    {
        ServerDatabase database(dbPath);
        QVERIFY(database.isReady());
        QCOMPARE(database.databasePath(), dbPath);
        const QList<int> durations = database.occupationDurationModel()->occupationDurations();
        const QList<int> expectedDurations{ 0, 5, 30, 60, 120, 240 };
        QCOMPARE(durations, expectedDurations);
        QVERIFY(QFileInfo::exists(dbPath));
    }

    QVERIFY2(QFileInfo::exists(dbPath), "SQLite file should exist after ServerDatabase shutdown");
}

void ServerDatabaseTests::testDatabaseRows()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for ServerDatabase");
    const QString dbPath = tempDir.filePath(QStringLiteral("users.sqlite"));

    ServerDatabase database(dbPath);
    QVERIFY(database.isReady());

    const BlacklistModel *blacklistModel = database.blacklistModel();
    QVERIFY(blacklistModel);

    const QList<QString> resourceNames{
        QStringLiteral("Resource Alpha"),   QStringLiteral("Resource Beta"),
        QStringLiteral("Resource Gamma"),   QStringLiteral("Resource Delta"),
        QStringLiteral("Resource Epsilon"),
    };
    for (int i = 0; i < resourceNames.size(); ++i) {
        const QString &name = resourceNames.at(i);
        const Resource resource(name,
                                QStringLiteral("Description %1").arg(i + 1),
                                QStringLiteral("Start message %1").arg(i + 1));
        const ServerDatabaseError addError = database.addResource(resource, kTestActor);
        QVERIFY2(!addError, qPrintable(addError.debugString()));
        QVERIFY(database.resourceModel()->existsByName(name));
    }
    QCOMPARE(database.resourceModel()->items().size(), resourceNames.size());

    const QList<QString> userIds{
        QStringLiteral("user-alpha"), QStringLiteral("user-bravo"), QStringLiteral("user-charlie"),
        QStringLiteral("user-delta"), QStringLiteral("user-echo"),
    };
    for (int i = 0; i < userIds.size(); ++i) {
        const QString &userId = userIds.at(i);
        User user(MessengerType::Telegram,
                  userId,
                  QStringLiteral("First%1").arg(i + 1),
                  QStringLiteral("Last%1").arg(i + 1));
        const ServerDatabaseError addError = database.addUser(user, kTestActor);
        QVERIFY2(!addError, qPrintable(addError.debugString()));
        QVERIFY(database.userModel()->exists(user.key()));
    }

    // Same userId can exist in different messengers; MessengerUserKey must stay composite.
    {
        const QString sameId = userIds.constFirst();
        const ServerDatabaseError addLocalCliError = database.addUser(User(MessengerType::LocalCli,
                                                                           sameId,
                                                                           QStringLiteral("Local"),
                                                                           QStringLiteral("Cli")),
                                                                      kTestActor);
        QVERIFY2(!addLocalCliError, qPrintable(addLocalCliError.debugString()));
        QVERIFY(database.userModel()->exists(MessengerUserKey(MessengerType::LocalCli, sameId)));
    }

    QCOMPARE(database.userModel()->items().size(), userIds.size() + 1);

    const QList<QString> blacklistNewUsers{
        QStringLiteral("blacklist-new-1"),
        QStringLiteral("blacklist-new-2"),
    };
    const QList<QString> blacklistExisting{
        userIds.at(0),
        userIds.at(1),
        userIds.at(2),
    };

    const auto addBlacklistEntry = [&](const QString &userId) {
        const MessengerUserKey blockedBy(MessengerType::Telegram, QStringLiteral("moderator"));
        const BlacklistEntry entry(MessengerUserKey(MessengerType::Telegram, userId),
                                   QStringLiteral("Test block"),
                                   blockedBy,
                                   QDateTime::currentDateTimeUtc());
        const ServerDatabaseError saveError = database.saveBlacklistEntry(entry, kTestActor);
        QVERIFY2(!saveError, qPrintable(saveError.debugString()));
        QVERIFY(database.isBlacklisted(entry.user()));
    };

    for (const QString &userId : blacklistNewUsers) {
        addBlacklistEntry(userId);
    }
    for (const QString &userId : blacklistExisting) {
        addBlacklistEntry(userId);
    }

    // Blacklist is also scoped by messenger type.
    QVERIFY(!database.isBlacklisted(
            MessengerUserKey(MessengerType::LocalCli, userIds.constFirst())));
    QCOMPARE(blacklistModel->entries().size(), blacklistNewUsers.size() + blacklistExisting.size());

    const auto removeBlacklistEntry = [&](const QString &userId) {
        const ServerDatabaseError removeError =
                database.removeBlacklistEntry(MessengerUserKey(MessengerType::Telegram, userId),
                                              kTestActor);
        QVERIFY2(!removeError, qPrintable(removeError.debugString()));
        QVERIFY(!database.isBlacklisted(MessengerUserKey(MessengerType::Telegram, userId)));
    };
    removeBlacklistEntry(blacklistNewUsers.constFirst());
    removeBlacklistEntry(blacklistNewUsers.constLast());
    QCOMPARE(blacklistModel->entries().size(), blacklistExisting.size());

    for (int i = 3; i < userIds.size(); ++i) {
        const QString &userId = userIds.at(i);
        const ServerDatabaseError deleteError =
                database.deleteUser(MessengerUserKey(MessengerType::Telegram, userId), kTestActor);
        QVERIFY2(!deleteError, qPrintable(deleteError.debugString()));
        QVERIFY(!database.userModel()->exists(MessengerUserKey(MessengerType::Telegram, userId)));
        const User missing =
                database.userModel()->userByKey(MessengerUserKey(MessengerType::Telegram, userId));
        QVERIFY(!missing.isValid());
    }
    QCOMPARE(database.userModel()->items().size(), userIds.size() - 2 + 1);

    const auto deleteResourceAndAssert = [&](const QString &resourceName) {
        const ServerDatabaseError deleteError = database.deleteResource(resourceName, kTestActor);
        QVERIFY2(!deleteError, qPrintable(deleteError.debugString()));
        QVERIFY(!database.resourceModel()->existsByName(resourceName));
    };

    deleteResourceAndAssert(resourceNames.at(3));
    deleteResourceAndAssert(resourceNames.at(4));
    QCOMPARE(database.resourceModel()->items().size(), resourceNames.size() - 2);
}

void ServerDatabaseTests::deleteResourceTrimsName()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for ServerDatabase");
    const QString dbPath = tempDir.filePath(QStringLiteral("trimmed.sqlite"));

    ServerDatabase database(dbPath);
    QVERIFY(database.isReady());

    const QString resourceName = QStringLiteral("Trimmed Resource");
    const ServerDatabaseError addError =
            database.addResource(Resource(resourceName,
                                          QStringLiteral("Description"),
                                          QStringLiteral("Start message")),
                                 kTestActor);
    QVERIFY2(!addError, qPrintable(addError.debugString()));
    QVERIFY(database.resourceModel()->existsByName(resourceName));

    const ServerDatabaseError deleteError =
            database.deleteResource(QStringLiteral("  %1  ").arg(resourceName), kTestActor);
    QVERIFY2(!deleteError, qPrintable(deleteError.debugString()));
    QVERIFY(!database.resourceModel()->existsByName(resourceName));
}

void ServerDatabaseTests::resourceQueueLifecycle()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for ServerDatabase");
    const QString dbPath = tempDir.filePath(QStringLiteral("queue.sqlite"));

    ServerDatabase database(dbPath);
    QVERIFY(database.isReady());

    const QString resourceName = QStringLiteral("Room A");
    const ServerDatabaseError addResourceError =
            database.addResource(Resource(resourceName,
                                          QStringLiteral("Quiet room"),
                                          QStringLiteral("Welcome!")),
                                 kTestActor);
    QVERIFY2(!addResourceError, qPrintable(addResourceError.debugString()));

    const ResourceQueueModel *queueModel = database.resourceQueueModel();
    QVERIFY(queueModel);

    auto assertQueueMatches = [&](const QList<MessengerUserKey> &expectedOrder) {
        const QList<ResourceQueueEntry> queue = queueModel->queueForResource(resourceName);
        QCOMPARE(queue.size(), expectedOrder.size());
        for (int i = 0; i < expectedOrder.size(); ++i) {
            QVERIFY(queue.at(i).user() == expectedOrder.at(i));
        }
    };

    const QList<MessengerUserKey> users{
        MessengerUserKey(MessengerType::Telegram, QStringLiteral("user-alpha")),
        MessengerUserKey(MessengerType::LocalCli, QStringLiteral("user-alpha")), // same userId
        MessengerUserKey(MessengerType::Telegram, QStringLiteral("user-charlie")),
        MessengerUserKey(MessengerType::Telegram, QStringLiteral("user-delta")),
    };

    QList<MessengerUserKey> expectedQueue;

    const QDateTime baseStartUtc =
            QDateTime::fromSecsSinceEpoch(QDateTime::currentSecsSinceEpoch() + 10,
                                          QTimeZone::utc());

    for (int i = 0; i < users.size(); ++i) {
        const MessengerUserKey key = users.at(i);
        User user(key, QStringLiteral("First%1").arg(i + 1), QStringLiteral("Last%1").arg(i + 1));
        const ServerDatabaseError addUserError = database.addUser(user, kTestActor);
        QVERIFY2(!addUserError, qPrintable(addUserError.debugString()));

        const QDateTime startUtc = baseStartUtc.addSecs(i * 10);
        const QDateTime endUtc = startUtc.addSecs(5);
        const ServerDatabaseError scheduleError =
                database.scheduleResourceUser(resourceName, key, startUtc, endUtc, kTestActor);
        QVERIFY2(!scheduleError, qPrintable(scheduleError.debugString()));
        expectedQueue.append(key);
        assertQueueMatches(expectedQueue);
    }

    const ServerDatabaseError cancelFirstError =
            database.removeUserSchedulesForResource(resourceName, users.at(0), kTestActor);
    QVERIFY2(!cancelFirstError, qPrintable(cancelFirstError.debugString()));
    expectedQueue.pop_front();
    assertQueueMatches(expectedQueue);

    // Deleting telegram:user-alpha must not affect local-cli:user-alpha.
    const ServerDatabaseError deleteSameIdError = database.deleteUser(users.at(0), kTestActor);
    QVERIFY2(!deleteSameIdError, qPrintable(deleteSameIdError.debugString()));
    QVERIFY(!database.userModel()->exists(users.at(0)));
    QVERIFY(database.userModel()->exists(users.at(1)));
    assertQueueMatches(expectedQueue);

    const ServerDatabaseError deleteUserError = database.deleteUser(users.at(2), kTestActor);
    QVERIFY2(!deleteUserError, qPrintable(deleteUserError.debugString()));
    expectedQueue.removeAll(users.at(2));
    assertQueueMatches(expectedQueue); // queue updated by ServerDatabase

    const MessengerUserKey moderatorKey(MessengerType::Telegram, QStringLiteral("moderator"));
    const BlacklistEntry blockedEntry(users.at(3),
                                      QStringLiteral("Abuse"),
                                      moderatorKey,
                                      QDateTime::currentDateTimeUtc());
    const ServerDatabaseError banError = database.banUser(blockedEntry, kTestActor);
    QVERIFY2(!banError, qPrintable(banError.debugString()));
    QVERIFY(database.isBlacklisted(blockedEntry.user()));
    expectedQueue.removeAll(blockedEntry.user());
    assertQueueMatches(expectedQueue);

    const ServerDatabaseError removeRemainingError =
            database.removeUserFromQueues(users.at(1), kTestActor);
    QVERIFY2(!removeRemainingError, qPrintable(removeRemainingError.debugString()));
    expectedQueue.removeAll(users.at(1));
    assertQueueMatches(expectedQueue);

    QCOMPARE(queueModel->queueForResource(resourceName).size(), 0);
    QCOMPARE(expectedQueue.size(), 0);
    assertQueueMatches(expectedQueue);
}

void ServerDatabaseTests::resourceStatusControlsQueueAndAvailability()
{
    using Code = ServerDatabaseError::Code;

    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for ServerDatabase");
    const QString dbPath = tempDir.filePath(QStringLiteral("resource_status.sqlite"));

    ServerDatabase database(dbPath);
    QVERIFY(database.isReady());

    const QString resourceName = QStringLiteral("Room Status");
    const ServerDatabaseError addResourceError =
            database.addResource(Resource(resourceName,
                                          QStringLiteral("Status checks"),
                                          QStringLiteral("Status start message")),
                                 kTestActor);
    QVERIFY2(!addResourceError, qPrintable(addResourceError.debugString()));

    const MessengerUserKey userA(MessengerType::Telegram, QStringLiteral("status-user-a"));
    const MessengerUserKey userB(MessengerType::Telegram, QStringLiteral("status-user-b"));

    const ServerDatabaseError addUserA =
            database.addUser(User(userA, QStringLiteral("Status"), QStringLiteral("UserA")),
                             kTestActor);
    QVERIFY2(!addUserA, qPrintable(addUserA.debugString()));
    const ServerDatabaseError addUserB =
            database.addUser(User(userB, QStringLiteral("Status"), QStringLiteral("UserB")),
                             kTestActor);
    QVERIFY2(!addUserB, qPrintable(addUserB.debugString()));

    const ResourceQueueModel *queueModel = database.resourceQueueModel();
    QVERIFY(queueModel);

    const QList<int> durationOptions = database.occupationDurationModel()->minutes();
    const ServerDatabaseError occupyOnError =
            database.occupyResourceUser(resourceName, userA, 5, durationOptions, userA);
    QVERIFY2(!occupyOnError, qPrintable(occupyOnError.debugString()));

    const QDateTime scheduleStartUtc = QDateTime::currentDateTimeUtc().addSecs(10 * 60);
    const QDateTime scheduleEndUtc = scheduleStartUtc.addSecs(5 * 60);
    const ServerDatabaseError scheduleOnError = database.scheduleResourceUser(resourceName,
                                                                              userB,
                                                                              scheduleStartUtc,
                                                                              scheduleEndUtc,
                                                                              userB);
    QVERIFY2(!scheduleOnError, qPrintable(scheduleOnError.debugString()));

    QCOMPARE(queueModel->queueForResource(resourceName).size(), 2);
    QVERIFY(queueModel->currentOccupantForResource(resourceName).isValid());

    const ServerDatabaseError setTempError =
            database.setResourceStatus(resourceName,
                                       Resource::Status::TemporarilyUnavailable,
                                       kTestActor);
    QVERIFY2(!setTempError, qPrintable(setTempError.debugString()));

    const Resource tempResource = database.resourceModel()->resourceByName(resourceName);
    QVERIFY(tempResource.isValid());
    QCOMPARE(tempResource.status(), Resource::Status::TemporarilyUnavailable);
    QCOMPARE(queueModel->queueForResource(resourceName).size(), 0);
    QVERIFY(!queueModel->currentOccupantForResource(resourceName).isValid());

    const ServerDatabaseError occupyTempError =
            database.occupyResourceUser(resourceName, userA, 5, durationOptions, userA);
    QVERIFY(occupyTempError);
    QCOMPARE(occupyTempError.code(), Code::ResourceTemporarilyUnavailable);

    const ServerDatabaseError scheduleTempError = database.scheduleResourceUser(resourceName,
                                                                                userB,
                                                                                scheduleStartUtc,
                                                                                scheduleEndUtc,
                                                                                userB);
    QVERIFY(scheduleTempError);
    QCOMPARE(scheduleTempError.code(), Code::ResourceTemporarilyUnavailable);

    const ServerDatabaseError setOffError =
            database.setResourceStatus(resourceName, Resource::Status::Off, kTestActor);
    QVERIFY2(!setOffError, qPrintable(setOffError.debugString()));

    const Resource offResource = database.resourceModel()->resourceByName(resourceName);
    QVERIFY(offResource.isValid());
    QCOMPARE(offResource.status(), Resource::Status::Off);

    const ServerDatabaseError occupyOffUserError =
            database.occupyResourceUser(resourceName, userA, 5, durationOptions, userA);
    QVERIFY(occupyOffUserError);
    QCOMPARE(occupyOffUserError.code(), Code::ResourceNotFound);

    const ServerDatabaseError occupyOffAdminError =
            database.occupyResourceUser(resourceName, kTestActor, 5, durationOptions, kTestActor);
    QVERIFY(occupyOffAdminError);
    QCOMPARE(occupyOffAdminError.code(), Code::ResourceOff);

    const ServerDatabaseError setOnError =
            database.setResourceStatus(resourceName, Resource::Status::On, kTestActor);
    QVERIFY2(!setOnError, qPrintable(setOnError.debugString()));

    const Resource onResource = database.resourceModel()->resourceByName(resourceName);
    QVERIFY(onResource.isValid());
    QCOMPARE(onResource.status(), Resource::Status::On);

    const ServerDatabaseError occupyAfterEnableError =
            database.occupyResourceUser(resourceName, userA, 5, durationOptions, userA);
    QVERIFY2(!occupyAfterEnableError, qPrintable(occupyAfterEnableError.debugString()));
}

void ServerDatabaseTests::scheduledOccupationLifecycle()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for ServerDatabase");
    const QString dbPath = tempDir.filePath(QStringLiteral("schedule.sqlite"));

    ServerDatabase database(dbPath);
    QVERIFY(database.isReady());

    const QString resourceName = QStringLiteral("Room S");
    const ServerDatabaseError addResourceError =
            database.addResource(Resource(resourceName,
                                          QStringLiteral("Schedule room"),
                                          QStringLiteral("Welcome!")),
                                 kTestActor);
    QVERIFY2(!addResourceError, qPrintable(addResourceError.debugString()));

    const QString userA = QStringLiteral("user-a");
    const QString userB = QStringLiteral("user-b");
    const MessengerUserKey userKeyA(MessengerType::Telegram, userA);
    const MessengerUserKey userKeyB(MessengerType::Telegram, userB);

    const ServerDatabaseError addUserA =
            database.addUser(User(userKeyA, QStringLiteral("User"), QStringLiteral("A")),
                             kTestActor);
    QVERIFY2(!addUserA, qPrintable(addUserA.debugString()));
    const ServerDatabaseError addUserB =
            database.addUser(User(userKeyB, QStringLiteral("User"), QStringLiteral("B")),
                             kTestActor);
    QVERIFY2(!addUserB, qPrintable(addUserB.debugString()));

    const ResourceQueueModel *queueModel = database.resourceQueueModel();
    QVERIFY(queueModel);

    QSignalSpy startedSpy(queueModel, &ResourceQueueModel::occupationStarted);
    QVERIFY(startedSpy.isValid());
    QSignalSpy endedSpy(queueModel, &ResourceQueueModel::occupationEnded);
    QVERIFY(endedSpy.isValid());

    const QDateTime baseUtc =
            QDateTime::fromSecsSinceEpoch(QDateTime::currentSecsSinceEpoch(), QTimeZone::utc());

    const QDateTime aStartUtc = baseUtc;
    const QDateTime aEndUtc = baseUtc.addSecs(2);
    const QDateTime bStartUtc = aEndUtc;
    const QDateTime bEndUtc = bStartUtc.addSecs(2);

    const ServerDatabaseError scheduleA =
            database.scheduleResourceUser(resourceName, userKeyA, aStartUtc, aEndUtc, kTestActor);
    QVERIFY2(!scheduleA, qPrintable(scheduleA.debugString()));

    // Overlapping schedule should be rejected.
    const ServerDatabaseError overlapError = database.scheduleResourceUser(resourceName,
                                                                           userKeyB,
                                                                           aStartUtc.addSecs(1),
                                                                           bEndUtc,
                                                                           kTestActor);
    QVERIFY(overlapError);
    QVERIFY(!overlapError.errorString().trimmed().isEmpty());

    const ServerDatabaseError scheduleB =
            database.scheduleResourceUser(resourceName, userKeyB, bStartUtc, bEndUtc, kTestActor);
    QVERIFY2(!scheduleB, qPrintable(scheduleB.debugString()));

    // A should occupy immediately.
    {
        const ResourceQueueEntry occupant = queueModel->currentOccupantForResource(resourceName);
        QVERIFY(occupant.isValid());
        QVERIFY(occupant.user() == userKeyA);
    }

    // Wait for A to end and B to start.
    QTest::qWait(2500);
    {
        const ResourceQueueEntry occupant = queueModel->currentOccupantForResource(resourceName);
        QVERIFY(occupant.isValid());
        QVERIFY(occupant.user() == userKeyB);
    }

    // Wait for B to end.
    QTest::qWait(2500);
    {
        const ResourceQueueEntry occupant = queueModel->currentOccupantForResource(resourceName);
        QVERIFY(!occupant.isValid());
    }
}

void ServerDatabaseTests::dataPersistsAcrossInstances()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for ServerDatabase");
    const QString dbPath = tempDir.filePath(QStringLiteral("persist.sqlite"));

    const QString userId = QStringLiteral("persistent-user");
    const QString resourceName = QStringLiteral("Focus Room");
    const QString blockedUserId = QStringLiteral("blacklisted-user");
    const MessengerUserKey userKey(MessengerType::Telegram, userId);
    const MessengerUserKey blockedUserKey(MessengerType::Telegram, blockedUserId);
    const MessengerUserKey moderatorKey(MessengerType::Telegram, QStringLiteral("moderator"));

    {
        ServerDatabase database(dbPath);
        QVERIFY(database.isReady());

        User user(userKey, QStringLiteral("Persist"), QStringLiteral("Test"), true);
        user.setLocale(QStringLiteral("ru_RU"));
        const ServerDatabaseError addUserError = database.addUser(user, kTestActor);
        QVERIFY2(!addUserError, qPrintable(addUserError.debugString()));

        Resource resource(resourceName,
                          QStringLiteral("Soundproof space"),
                          QStringLiteral("Please stay focused."));
        const ServerDatabaseError addResourceError = database.addResource(resource, kTestActor);
        QVERIFY2(!addResourceError, qPrintable(addResourceError.debugString()));

        const ServerDatabaseError enqueueError =
                database.occupyResourceUser(resourceName,
                                            userKey,
                                            30,
                                            database.occupationDurationModel()->minutes(),
                                            kTestActor);
        QVERIFY2(!enqueueError, qPrintable(enqueueError.debugString()));

        const BlacklistEntry entry(blockedUserKey,
                                   QStringLiteral("Abuse"),
                                   moderatorKey,
                                   QDateTime::currentDateTimeUtc());
        const ServerDatabaseError saveBlacklistError =
                database.saveBlacklistEntry(entry, kTestActor);
        QVERIFY2(!saveBlacklistError, qPrintable(saveBlacklistError.debugString()));

        // ServerDatabase destructor flushes dirty models.
    }

    {
        ServerDatabase reloaded(dbPath);
        QVERIFY(reloaded.isReady());

        const User restoredUser = reloaded.userModel()->userByKey(userKey);
        QVERIFY(restoredUser.isValid());
        QCOMPARE(restoredUser.firstName(), QStringLiteral("Persist"));
        QVERIFY(restoredUser.isAdmin());
        QCOMPARE(restoredUser.locale(), QStringLiteral("ru_RU"));

        bool resourceFound = false;
        const QList<Resource> resources = reloaded.resourceModel()->items();
        for (const Resource &resource : resources) {
            if (resource.name() == resourceName) {
                resourceFound = true;
                QCOMPARE(resource.description(), QStringLiteral("Soundproof space"));
                break;
            }
        }
        QVERIFY2(resourceFound, "Resource should exist after reopening database");

        const QList<ResourceQueueEntry> queue =
                reloaded.resourceQueueModel()->queueForResource(resourceName);
        QCOMPARE(queue.size(), 1);
        const ResourceQueueEntry entry = queue.constFirst();
        QVERIFY(entry.isValid());
        QVERIFY(entry.user() == userKey);
        QVERIFY(entry.endAtUtc().isValid());
        QCOMPARE(entry.startAtUtc().secsTo(entry.endAtUtc()), qint64(30 * 60));

        BlacklistEntry restoredEntry;
        QVERIFY(reloaded.isBlacklisted(blockedUserKey, &restoredEntry));
        QCOMPARE(restoredEntry.reason(), QStringLiteral("Abuse"));
        QVERIFY(restoredEntry.blockedBy() == moderatorKey);
    }
}

void ServerDatabaseTests::userRoleAggregatesGroupRoles()
{
    User user(MessengerType::Telegram,
              QStringLiteral("grouped-user"),
              QStringLiteral("Grouped"),
              QStringLiteral("User"),
              User::Role::UserRole);

    QVERIFY(user.role() == User::Role::UserRole);
    QVERIFY(!user.isAdmin());
    QVERIFY(!user.isFixedAdmin());
    QVERIFY(user.groupRoles().contains(Group::root()));
    QVERIFY(user.groups().contains(Group::root()));
    QVERIFY(user.hasAccessToGroup(Group(QStringLiteral("alpha"))));

    QMap<Group, User::Role> roles;
    roles.insert(Group(QStringLiteral("alpha")), User::Role::UserRole);
    user.setGroupRoles(roles);

    QVERIFY(user.role() == User::Role::UserRole);
    QVERIFY(user.roleForGroup(Group(QStringLiteral("alpha/child"))) == User::Role::UserRole);
    QVERIFY(user.hasAccessToGroup(Group(QStringLiteral("alpha/child"))));
    QVERIFY(!user.canAdministerGroup(Group(QStringLiteral("alpha"))));

    roles.insert(Group(QStringLiteral("beta")), User::Role::AdminRole);
    user.setGroupRoles(roles);

    QVERIFY(user.role() == User::Role::AdminRole);
    QVERIFY(user.roleForGroup(Group(QStringLiteral("beta/child"))) == User::Role::AdminRole);
    QVERIFY(user.isAdmin());
    QVERIFY(!user.isFixedAdmin());
    QVERIFY(user.canAdministerGroup(Group(QStringLiteral("beta/child"))));
    QVERIFY(!user.canAdministerGroup(Group(QStringLiteral("gamma"))));

    roles.insert(Group(QStringLiteral("gamma")), User::Role::FixedAdminRole);
    user.setGroupRoles(roles);

    QVERIFY(user.role() == User::Role::FixedAdminRole);
    QVERIFY(user.roleForGroup(Group(QStringLiteral("anything"))) == User::Role::FixedAdminRole);
    QVERIFY(user.isAdmin());
    QVERIFY(user.isFixedAdmin());
    QVERIFY(user.canAdministerGroup(Group(QStringLiteral("outside"))));

    user.setIsAdmin(true);
    QVERIFY(user.role() == User::Role::FixedAdminRole);
    QVERIFY(user.isFixedAdmin());
    QVERIFY(!user.groupRoles().isEmpty());

    user.setIsAdmin(false);
    QVERIFY(user.role() == User::Role::UserRole);
    QVERIFY(!user.isAdmin());
    QVERIFY(!user.isFixedAdmin());
    QVERIFY(user.groupRoles().contains(Group::root()));
    QVERIFY(user.hasAccessToGroup(Group(QStringLiteral("outside"))));

    user.setGroupRoles(QMap<Group, User::Role>());
    QVERIFY(user.role() == User::Role::UserRole);
    QVERIFY(user.groupRoles().contains(Group::root()));
    QVERIFY(user.groups().contains(Group::root()));
    QVERIFY(user.hasAccessToGroup(Group(QStringLiteral("alpha/child"))));
}

void ServerDatabaseTests::persistedFixedAdminRoleLoadsAsAdmin()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for ServerDatabase");
    const QString dbPath = tempDir.filePath(QStringLiteral("legacy_fixed_admin.sqlite"));
    const MessengerUserKey userKey(MessengerType::Telegram, QStringLiteral("legacy-fixed-admin"));

    {
        ServerDatabase database(dbPath);
        QVERIFY(database.isReady());
    }

    const QString connectionName = QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        QSqlDatabase sql = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        sql.setDatabaseName(dbPath);
        QVERIFY2(sql.open(), qPrintable(sql.lastError().text()));

        QSqlQuery query(sql);
        QVERIFY2(query.exec(QStringLiteral("INSERT INTO users(messenger_type, user_id, first_name, "
                                           "last_name, is_admin) "
                                           "VALUES ('telegram', 'legacy-fixed-admin', '', '', 1)")),
                 qPrintable(query.lastError().text()));
        QVERIFY2(query.exec(QStringLiteral("INSERT INTO user_groups(messenger_type, user_id, "
                                           "group_path, role) "
                                           "VALUES ('telegram', 'legacy-fixed-admin', '', "
                                           "'fixed_admin')")),
                 qPrintable(query.lastError().text()));
        sql.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    ServerDatabase database(dbPath);
    QVERIFY(database.isReady());

    const User user = database.userModel()->userByKey(userKey);
    QVERIFY(user.isValid());
    QVERIFY(user.isAdmin());
    QVERIFY(!user.isFixedAdmin());
    QVERIFY(user.role() == User::Role::AdminRole);
}

void ServerDatabaseTests::groupAdminAccessScopesResourceMutations()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for ServerDatabase");
    const QString dbPath = tempDir.filePath(QStringLiteral("group_admin_access.sqlite"));

    ServerDatabase database(dbPath);
    QVERIFY(database.isReady());

    const MessengerUserKey alphaAdminKey(MessengerType::Telegram, QStringLiteral("alpha-admin"));
    User alphaAdmin(alphaAdminKey,
                    QStringLiteral("Alpha"),
                    QStringLiteral("Admin"),
                    User::Role::UserRole);
    QMap<Group, User::Role> alphaAdminGroups;
    alphaAdminGroups.insert(Group(QStringLiteral("alpha")), User::Role::AdminRole);
    alphaAdmin.setGroupRoles(alphaAdminGroups);

    const ServerDatabaseError addAdminError = database.addUser(alphaAdmin, kTestActor);
    QVERIFY2(!addAdminError, qPrintable(addAdminError.debugString()));

    Resource alphaChild(QStringLiteral("Alpha Child"),
                        QStringLiteral("Allowed"),
                        QStringLiteral("Start"));
    alphaChild.setGroups(QSet<Group>({ Group(QStringLiteral("alpha/child")) }));
    const ServerDatabaseError addAlphaChildError = database.addResource(alphaChild, alphaAdminKey);
    QVERIFY2(!addAlphaChildError, qPrintable(addAlphaChildError.debugString()));
    QVERIFY(database.resourceModel()->existsByName(alphaChild.name()));

    Resource beta(QStringLiteral("Beta"), QStringLiteral("Forbidden"), QStringLiteral("Start"));
    beta.setGroups(QSet<Group>({ Group(QStringLiteral("beta")) }));
    const ServerDatabaseError addBetaByAlphaError = database.addResource(beta, alphaAdminKey);
    QVERIFY(addBetaByAlphaError);
    QVERIFY(!database.resourceModel()->existsByName(beta.name()));

    const ServerDatabaseError addBetaByRootError = database.addResource(beta, kTestActor);
    QVERIFY2(!addBetaByRootError, qPrintable(addBetaByRootError.debugString()));

    Resource movedToBeta(QStringLiteral("Alpha Child"),
                         QStringLiteral("Move denied"),
                         QStringLiteral("Start"));
    movedToBeta.setGroups(QSet<Group>({ Group(QStringLiteral("beta")) }));
    const ServerDatabaseError moveToBetaError = database.updateResource(movedToBeta, alphaAdminKey);
    QVERIFY(moveToBetaError);

    Resource updatedAlpha(QStringLiteral("Alpha Child"),
                          QStringLiteral("Allowed update"),
                          QStringLiteral("Updated start"));
    updatedAlpha.setGroups(QSet<Group>({ Group(QStringLiteral("alpha")) }));
    const ServerDatabaseError updateAlphaError =
            database.updateResource(updatedAlpha, alphaAdminKey);
    QVERIFY2(!updateAlphaError, qPrintable(updateAlphaError.debugString()));
    const Resource restoredAlpha = database.resourceModel()->resourceByName(updatedAlpha.name());
    QVERIFY(restoredAlpha.isValid());
    QVERIFY(restoredAlpha.groups().contains(Group(QStringLiteral("alpha"))));
    QCOMPARE(restoredAlpha.description(), QStringLiteral("Allowed update"));

    const ServerDatabaseError deleteBetaError = database.deleteResource(beta.name(), alphaAdminKey);
    QVERIFY(deleteBetaError);
    QVERIFY(database.resourceModel()->existsByName(beta.name()));

    const ServerDatabaseError deleteAlphaError =
            database.deleteResource(updatedAlpha.name(), alphaAdminKey);
    QVERIFY2(!deleteAlphaError, qPrintable(deleteAlphaError.debugString()));
    QVERIFY(!database.resourceModel()->existsByName(updatedAlpha.name()));
}

void ServerDatabaseTests::resourceGroupUpdateRollsBackAtomically()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for ServerDatabase");
    const QString dbPath = tempDir.filePath(QStringLiteral("resource_group_rollback.sqlite"));
    ServerDatabase database(dbPath);
    QVERIFY(database.isReady());

    Resource resource(QStringLiteral("Shared"), QStringLiteral("before"), QString());
    const QSet<Group> originalGroups{ Group(QStringLiteral("alpha")),
                                      Group(QStringLiteral("beta")) };
    resource.setGroups(originalGroups);
    const ServerDatabaseError addError = database.addResource(resource, kTestActor);
    QVERIFY2(!addError, qPrintable(addError.debugString()));

    const QString connectionName = QStringLiteral("resource_group_rollback_%1")
                                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase sql = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        sql.setDatabaseName(dbPath);
        QVERIFY2(sql.open(), qPrintable(sql.lastError().text()));
        QSqlQuery trigger(sql);
        QVERIFY2(trigger.exec(QStringLiteral(
                         "CREATE TRIGGER fail_forbidden_resource_group "
                         "BEFORE INSERT ON resource_groups "
                         "WHEN NEW.group_path = 'forbidden' "
                         "BEGIN SELECT RAISE(ABORT, 'injected group failure'); END")),
                 qPrintable(trigger.lastError().text()));
        sql.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    Resource failedUpdate(QStringLiteral("Shared"), QStringLiteral("after"), QString());
    failedUpdate.setGroups({ Group(QStringLiteral("alpha")), Group(QStringLiteral("forbidden")) });
    QVERIFY(database.updateResource(failedUpdate, kTestActor));

    const Resource restored = database.resourceModel()->resourceByName(resource.name());
    QCOMPARE(restored.description(), QStringLiteral("before"));
    QCOMPARE(restored.groups(), originalGroups);

    const QString verifyConnectionName =
            QStringLiteral("resource_group_verify_%1")
                    .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase sql =
                QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), verifyConnectionName);
        sql.setDatabaseName(dbPath);
        QVERIFY2(sql.open(), qPrintable(sql.lastError().text()));
        QSqlQuery query(sql);
        query.prepare(QStringLiteral("SELECT group_path FROM resource_groups "
                                     "WHERE resource_uuid = :uuid ORDER BY group_path"));
        query.bindValue(QStringLiteral(":uuid"), restored.uuidString());
        QVERIFY2(query.exec(), qPrintable(query.lastError().text()));
        QStringList persistedGroups;
        while (query.next())
            persistedGroups.append(query.value(0).toString());
        QCOMPARE(persistedGroups, QStringList({ QStringLiteral("alpha"), QStringLiteral("beta") }));
        sql.close();
    }
    QSqlDatabase::removeDatabase(verifyConnectionName);
}

void ServerDatabaseTests::groupAdminAccessScopesUserMutations()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for ServerDatabase");
    ServerDatabase database(tempDir.filePath(QStringLiteral("group_user_access.sqlite")));
    QVERIFY(database.isReady());

    const MessengerUserKey alphaAdminKey(MessengerType::Telegram, QStringLiteral("alpha-admin"));
    User alphaAdmin(alphaAdminKey,
                    QStringLiteral("Alpha"),
                    QStringLiteral("Admin"),
                    User::Role::UserRole);
    alphaAdmin.setGroupRoles({ { Group(QStringLiteral("alpha")), User::Role::AdminRole } });
    const ServerDatabaseError addAdminError = database.addUser(alphaAdmin, kTestActor);
    QVERIFY2(!addAdminError, qPrintable(addAdminError.debugString()));

    const MessengerUserKey betaUserKey(MessengerType::Telegram, QStringLiteral("beta-user"));
    User betaUser(betaUserKey,
                  QStringLiteral("Beta"),
                  QStringLiteral("User"),
                  User::Role::UserRole);
    betaUser.setGroupRoles({ { Group(QStringLiteral("beta")), User::Role::UserRole } });
    const ServerDatabaseError addBetaError = database.addUser(betaUser, kTestActor);
    QVERIFY2(!addBetaError, qPrintable(addBetaError.debugString()));

    const MessengerUserKey alphaChildKey(MessengerType::Telegram, QStringLiteral("alpha-child"));
    User alphaChild(alphaChildKey,
                    QStringLiteral("Alpha"),
                    QStringLiteral("Child"),
                    User::Role::UserRole);
    alphaChild.setGroupRoles({ { Group(QStringLiteral("alpha/child")), User::Role::UserRole } });
    const ServerDatabaseError addAlphaError = database.addUser(alphaChild, alphaAdminKey);
    QVERIFY2(!addAlphaError, qPrintable(addAlphaError.debugString()));

    User forbiddenRoot(MessengerUserKey(MessengerType::Telegram, QStringLiteral("root-user")),
                       QStringLiteral("Root"),
                       QStringLiteral("User"),
                       User::Role::UserRole);
    QVERIFY(database.addUser(forbiddenRoot, alphaAdminKey));

    User movedBeta = betaUser;
    movedBeta.setGroupRoles({ { Group(QStringLiteral("alpha/child")), User::Role::UserRole } });
    QVERIFY(database.updateUser(movedBeta, alphaAdminKey));

    User promotedAlpha = alphaChild;
    promotedAlpha.setGroupRoles(
            { { Group(QStringLiteral("alpha/child")), User::Role::AdminRole } });
    const ServerDatabaseError promoteError = database.updateUser(promotedAlpha, alphaAdminKey);
    QVERIFY2(!promoteError, qPrintable(promoteError.debugString()));

    QVERIFY(database.deleteUser(betaUserKey, alphaAdminKey));
    QVERIFY(database.userModel()->exists(betaUserKey));

    const ServerDatabaseError deleteAlphaError = database.deleteUser(alphaChildKey, alphaAdminKey);
    QVERIFY2(!deleteAlphaError, qPrintable(deleteAlphaError.debugString()));
    QVERIFY(!database.userModel()->exists(alphaChildKey));
}

void ServerDatabaseTests::selfUpdateCannotChangeGroups()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for ServerDatabase");
    ServerDatabase database(tempDir.filePath(QStringLiteral("self_update_groups.sqlite")));
    QVERIFY(database.isReady());

    const MessengerUserKey userKey(MessengerType::Telegram, QStringLiteral("alpha-user"));
    User user(userKey, QStringLiteral("Alpha"), QStringLiteral("User"), User::Role::UserRole);
    user.setGroupRoles({ { Group(QStringLiteral("alpha")), User::Role::UserRole } });
    const ServerDatabaseError addError = database.addUser(user, kTestActor);
    QVERIFY2(!addError, qPrintable(addError.debugString()));

    User escalated = user;
    escalated.setGroupRoles({ { Group::root(), User::Role::UserRole } });
    QVERIFY(database.updateUser(escalated, userKey));
    QCOMPARE(database.userModel()->userByKey(userKey).groupRoles(), user.groupRoles());

    User preferences = user;
    preferences.setLocale(QStringLiteral("ru"));
    const ServerDatabaseError preferenceError = database.updateUser(preferences, userKey);
    QVERIFY2(!preferenceError, qPrintable(preferenceError.debugString()));
    QCOMPARE(database.userModel()->userByKey(userKey).locale(), QStringLiteral("ru"));

    const MessengerUserKey mixedAdminKey(MessengerType::Telegram, QStringLiteral("mixed-admin"));
    User mixedAdmin(mixedAdminKey,
                    QStringLiteral("Mixed"),
                    QStringLiteral("Admin"),
                    User::Role::UserRole);
    mixedAdmin.setGroupRoles({ { Group(QStringLiteral("alpha")), User::Role::AdminRole },
                               { Group(QStringLiteral("beta")), User::Role::UserRole } });
    QVERIFY(!database.addUser(mixedAdmin, kTestActor));
    User droppedExternalGroup = mixedAdmin;
    droppedExternalGroup.setGroupRoles(
            { { Group(QStringLiteral("alpha")), User::Role::AdminRole } });
    QVERIFY(database.updateUser(droppedExternalGroup, mixedAdminKey));
    QCOMPARE(database.userModel()->userByKey(mixedAdminKey).groupRoles(), mixedAdmin.groupRoles());
}

void ServerDatabaseTests::blacklistAndQueueRespectGroupScopes()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for ServerDatabase");
    ServerDatabase database(tempDir.filePath(QStringLiteral("group_blacklist_queue.sqlite")));
    QVERIFY(database.isReady());

    const MessengerUserKey rootAdminKey(MessengerType::Telegram, QStringLiteral("root-admin"));
    const MessengerUserKey alphaAdminKey(MessengerType::Telegram, QStringLiteral("alpha-admin"));
    const MessengerUserKey alphaUserKey(MessengerType::Telegram, QStringLiteral("alpha-user"));
    const MessengerUserKey betaUserKey(MessengerType::Telegram, QStringLiteral("beta-user"));

    const ServerDatabaseError addRootError = database.addUser(User(rootAdminKey,
                                                                   QStringLiteral("Root"),
                                                                   QStringLiteral("Admin"),
                                                                   User::Role::AdminRole),
                                                              kTestActor);
    QVERIFY2(!addRootError, qPrintable(addRootError.debugString()));

    const auto addGroupedUser =
            [&database](const MessengerUserKey &key, const Group &group, User::Role role) {
                User user(key, key.userId(), QString(), User::Role::UserRole);
                user.setGroupRoles({ { group, role } });
                return database.addUser(user, kTestActor);
            };
    QVERIFY(!addGroupedUser(alphaAdminKey, Group(QStringLiteral("alpha")), User::Role::AdminRole));
    QVERIFY(!addGroupedUser(alphaUserKey,
                            Group(QStringLiteral("alpha/child")),
                            User::Role::UserRole));
    QVERIFY(!addGroupedUser(betaUserKey, Group(QStringLiteral("beta")), User::Role::UserRole));

    const BlacklistEntry betaBlock(betaUserKey, QStringLiteral("beta"), alphaAdminKey);
    QVERIFY(database.banUser(betaBlock, alphaAdminKey));
    QVERIFY(!database.isBlacklisted(betaUserKey));

    const BlacklistEntry alphaBlock(alphaUserKey, QStringLiteral("alpha"), alphaAdminKey);
    const ServerDatabaseError banAlphaError = database.banUser(alphaBlock, alphaAdminKey);
    QVERIFY2(!banAlphaError, qPrintable(banAlphaError.debugString()));
    QVERIFY(database.isBlacklisted(alphaUserKey));
    QVERIFY(!database.unbanUser(alphaUserKey, alphaAdminKey));

    const MessengerUserKey unknownKey(MessengerType::Telegram, QStringLiteral("unknown"));
    QVERIFY(database.banUser(BlacklistEntry(unknownKey), alphaAdminKey));
    QVERIFY(!database.banUser(BlacklistEntry(unknownKey), rootAdminKey));
    QVERIFY(!database.unbanUser(unknownKey, rootAdminKey));

    const MessengerUserKey expiredKey(MessengerType::Telegram, QStringLiteral("expired"));
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    QVERIFY(!database.saveBlacklistEntry(BlacklistEntry(expiredKey,
                                                        QStringLiteral("expired"),
                                                        kTestActor,
                                                        nowUtc.addSecs(-120),
                                                        nowUtc.addSecs(-60)),
                                         kTestActor));
    User expiredUser(expiredKey,
                     QStringLiteral("Expired"),
                     QStringLiteral("User"),
                     User::Role::UserRole);
    expiredUser.setGroupRoles({ { Group(QStringLiteral("alpha")), User::Role::UserRole } });
    QVERIFY(!database.addUser(expiredUser, alphaAdminKey));

    Resource alphaResource(QStringLiteral("Alpha Resource"), QString(), QString());
    alphaResource.setGroups({ Group(QStringLiteral("alpha/child")) });
    Resource betaResource(QStringLiteral("Beta Resource"), QString(), QString());
    betaResource.setGroups({ Group(QStringLiteral("beta")) });
    QVERIFY(!database.addResource(alphaResource, kTestActor));
    QVERIFY(!database.addResource(betaResource, kTestActor));

    const QList<int> durations = database.occupationDurationModel()->minutes();
    QVERIFY(database.occupyResourceUser(betaResource.name(),
                                        alphaUserKey,
                                        5,
                                        durations,
                                        alphaUserKey));
    const ServerDatabaseError occupyAlphaError = database.occupyResourceUser(alphaResource.name(),
                                                                             alphaUserKey,
                                                                             5,
                                                                             durations,
                                                                             alphaUserKey);
    QVERIFY2(!occupyAlphaError, qPrintable(occupyAlphaError.debugString()));
    QVERIFY(database.releaseUserOccupationForResource(betaResource.name(),
                                                      betaUserKey,
                                                      alphaAdminKey));
    QVERIFY(!database.releaseUserOccupationForResource(alphaResource.name(),
                                                       alphaUserKey,
                                                       alphaAdminKey));

    QVERIFY(!database.occupyResourceUser(alphaResource.name(),
                                         alphaUserKey,
                                         5,
                                         durations,
                                         alphaUserKey));
    Resource movedResource = database.resourceModel()->resourceByName(alphaResource.name());
    movedResource.setGroups({ Group(QStringLiteral("beta")) });
    QVERIFY(!database.updateResource(movedResource, kTestActor));
    QVERIFY(database.resourceQueueModel()->queueForResource(alphaResource.name()).isEmpty());
}

void ServerDatabaseTests::serverSettingsRequireRootAdmin()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for ServerDatabase");
    const QString dbPath = tempDir.filePath(QStringLiteral("server_settings_access.sqlite"));

    ServerDatabase database(dbPath);
    QVERIFY(database.isReady());

    const MessengerUserKey rootAdminKey(MessengerType::Telegram, QStringLiteral("root-admin"));
    const MessengerUserKey groupAdminKey(MessengerType::Telegram, QStringLiteral("group-admin"));

    const ServerDatabaseError addRootAdminError = database.addUser(User(rootAdminKey,
                                                                        QStringLiteral("Root"),
                                                                        QStringLiteral("Admin"),
                                                                        User::Role::AdminRole),
                                                                   kTestActor);
    QVERIFY2(!addRootAdminError, qPrintable(addRootAdminError.debugString()));

    User groupAdmin(groupAdminKey,
                    QStringLiteral("Group"),
                    QStringLiteral("Admin"),
                    User::Role::UserRole);
    QMap<Group, User::Role> groupRoles;
    groupRoles.insert(Group(QStringLiteral("alpha")), User::Role::AdminRole);
    groupAdmin.setGroupRoles(groupRoles);
    const ServerDatabaseError addGroupAdminError = database.addUser(groupAdmin, kTestActor);
    QVERIFY2(!addGroupAdminError, qPrintable(addGroupAdminError.debugString()));

    QVERIFY(database.canManageServerSettings(kTestActor));
    QVERIFY(database.canManageServerSettings(rootAdminKey));
    QVERIFY(!database.canManageServerSettings(groupAdminKey));

    const ServerDatabaseError deniedError =
            database.setServerSetting(ServerRuntimeSettings::Setting::MaxResourcesPerUser,
                                      QVariant::fromValue(uint(2)),
                                      groupAdminKey);
    QVERIFY(deniedError);

    const ServerDatabaseError allowedError =
            database.setServerSetting(ServerRuntimeSettings::Setting::MaxResourcesPerUser,
                                      QVariant::fromValue(uint(2)),
                                      rootAdminKey);
    QVERIFY2(!allowedError, qPrintable(allowedError.debugString()));
    QCOMPARE(database.serverSettingValue(ServerRuntimeSettings::Setting::MaxResourcesPerUser)
                     .toUInt(),
             uint(2));

    QVERIFY(database.addOccupationDuration(15, groupAdminKey));
    QVERIFY(!database.addOccupationDuration(15, rootAdminKey));

    const MessengerUserKey pendingKey(MessengerType::Telegram, QStringLiteral("pending"));
    QVERIFY(!database.recordPendingAccessUser(
            PendingAccessUserEntry(pendingKey,
                                   QStringLiteral("Pending"),
                                   QStringLiteral("User"),
                                   QDateTime::currentDateTimeUtc()),
            kTestActor));
    QVERIFY(database.clearPendingAccessUser(pendingKey, groupAdminKey));
    QVERIFY(!database.clearPendingAccessUser(pendingKey, rootAdminKey));
}

void ServerDatabaseTests::recentPendingAccessUsersRetention()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for ServerDatabase");
    const QString dbPath = tempDir.filePath(QStringLiteral("pending_access.sqlite"));

    ServerDatabase database(dbPath);
    QVERIFY(database.isReady());

    const PendingAccessUserModel *pendingAccessModel = database.pendingAccessUserModel();
    QVERIFY(pendingAccessModel);

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const int expectedCap = 100;

    for (int i = 0; i < 120; ++i) {
        const PendingAccessUserEntry entry(MessengerUserKey(MessengerType::Telegram,
                                                            QStringLiteral("pending-%1").arg(i)),
                                           QStringLiteral("First%1").arg(i),
                                           QStringLiteral("Last%1").arg(i),
                                           now.addSecs(-i * 60));
        const ServerDatabaseError recordError = database.recordPendingAccessUser(entry, kTestActor);
        QVERIFY2(!recordError, qPrintable(recordError.debugString()));
    }

    const PendingAccessUserEntry expired(MessengerUserKey(MessengerType::Telegram,
                                                          QStringLiteral("expired-user")),
                                         QStringLiteral("Expired"),
                                         QStringLiteral("User"),
                                         now.addDays(-5));
    const ServerDatabaseError recordExpiredError =
            database.recordPendingAccessUser(expired, kTestActor);
    QVERIFY2(!recordExpiredError, qPrintable(recordExpiredError.debugString()));

    const QList<PendingAccessUserEntry> recentAll = pendingAccessModel->recentUsers(150);
    QCOMPARE(recentAll.size(), expectedCap);

    bool expiredPresent = false;
    for (const PendingAccessUserEntry &entry : recentAll) {
        if (entry.user() == expired.user()) {
            expiredPresent = true;
            break;
        }
    }
    QVERIFY(!expiredPresent);
    QVERIFY(!recentAll.isEmpty());
    QVERIFY(recentAll.constFirst().user()
            == MessengerUserKey(MessengerType::Telegram, QStringLiteral("pending-0")));

    const MessengerUserKey removedUserKey = recentAll.constFirst().user();
    const ServerDatabaseError clearError =
            database.clearPendingAccessUser(removedUserKey, kTestActor);
    QVERIFY2(!clearError, qPrintable(clearError.debugString()));

    const int requestedLimit = 50;
    const QList<PendingAccessUserEntry> afterRemoval =
            pendingAccessModel->recentUsers(requestedLimit);
    QCOMPARE(afterRemoval.size(), requestedLimit);
    bool removedStillPresent = false;
    for (const PendingAccessUserEntry &entry : afterRemoval) {
        if (entry.user() == removedUserKey) {
            removedStillPresent = true;
            break;
        }
    }
    QVERIFY(!removedStillPresent);
}

void ServerDatabaseTests::occupationDurationUpdates()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for ServerDatabase");
    const QString dbPath = tempDir.filePath(QStringLiteral("durations.sqlite"));

    ServerDatabase database(dbPath);
    QVERIFY(database.isReady());

    const QList<int> baseline = database.occupationDurationModel()->occupationDurations();
    QVERIFY(baseline.contains(0));
    QVERIFY(!baseline.contains(45));

    const ServerDatabaseError addError = database.addOccupationDuration(45, kTestActor);
    QVERIFY2(!addError, qPrintable(addError.debugString()));
    QVERIFY(database.occupationDurationModel()->occupationDurations().contains(45));

    const ServerDatabaseError duplicateError = database.addOccupationDuration(45, kTestActor);
    QVERIFY(duplicateError);

    const ServerDatabaseError removeError = database.removeOccupationDuration(45, kTestActor);
    QVERIFY2(!removeError, qPrintable(removeError.debugString()));
    QVERIFY(!database.occupationDurationModel()->occupationDurations().contains(45));

    const ServerDatabaseError missingError = database.removeOccupationDuration(45, kTestActor);
    QVERIFY(missingError);

    const ServerDatabaseError removeIndefiniteError =
            database.removeOccupationDuration(0, kTestActor);
    QVERIFY(removeIndefiniteError);
}

void ServerDatabaseTests::userTimezoneSettingPersistsAcrossInstances()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for ServerDatabase");
    const QString dbPath = tempDir.filePath(QStringLiteral("user_timezone.sqlite"));
    const MessengerUserKey userKey(MessengerType::Telegram, QStringLiteral("tz-user"));

    {
        ServerDatabase database(dbPath);
        QVERIFY(database.isReady());

        const User user(userKey.messenger(),
                        userKey.userId(),
                        QStringLiteral("Time"),
                        QStringLiteral("Zone"));
        const ServerDatabaseError addError = database.addUser(user, kTestActor);
        QVERIFY2(!addError, qPrintable(addError.debugString()));

        User editable = database.userModel()->userByKey(userKey);
        QVERIFY(editable.isValid());
        QVERIFY(!editable.timezone().hasCustomOffset());

        editable.setTimezone(UserTimeZone(180));
        const ServerDatabaseError updateError = database.updateUser(editable, kTestActor);
        QVERIFY2(!updateError, qPrintable(updateError.debugString()));

        const User updated = database.userModel()->userByKey(userKey);
        QVERIFY(updated.isValid());
        QCOMPARE(updated.timezone().toStorageValue(), QStringLiteral("180"));
    }

    {
        ServerDatabase reopened(dbPath);
        QVERIFY(reopened.isReady());
        User persisted = reopened.userModel()->userByKey(userKey);
        QVERIFY(persisted.isValid());
        QCOMPARE(persisted.timezone().toStorageValue(), QStringLiteral("180"));

        persisted.setTimezone(UserTimeZone());
        const ServerDatabaseError resetError = reopened.updateUser(persisted, kTestActor);
        QVERIFY2(!resetError, qPrintable(resetError.debugString()));
    }

    {
        ServerDatabase reopened(dbPath);
        QVERIFY(reopened.isReady());
        const User resetUser = reopened.userModel()->userByKey(userKey);
        QVERIFY(resetUser.isValid());
        QVERIFY(!resetUser.timezone().hasCustomOffset());
    }
}

void ServerDatabaseTests::serverDateTimeFormatRespectsExplicitTimezone()
{
    const QString format = QStringLiteral("yyyy-MM-dd HH:mm");
    const UserTimeZone explicitTimeZone(180);
    QDateTime utcDateTime;
    QVERIFY(ServerDateTimeFormat::parseUserDateTime(QStringLiteral("2026-01-02 10:15"),
                                                    format,
                                                    ServerDateTimeFormat::ParseMode::LocalToUtc,
                                                    &utcDateTime,
                                                    explicitTimeZone));
    QCOMPARE(utcDateTime, QDateTime(QDate(2026, 1, 2), QTime(7, 15), QTimeZone::utc()));

    const QString rendered = ServerDateTimeFormat::formatLocalDateTime(utcDateTime,
                                                                       format,
                                                                       QStringLiteral("invalid"),
                                                                       explicitTimeZone);
    QCOMPARE(rendered, QStringLiteral("2026-01-02 10:15"));

    UserTimeZone parsedTimeZone;
    QVERIFY(ServerDateTimeFormat::parseTimeZoneValue(QStringLiteral("system"), &parsedTimeZone));
    QVERIFY(!parsedTimeZone.hasCustomOffset());

    const auto expectParsedOffset = [&parsedTimeZone](const QString &token,
                                                      const QString &expectedMinutesText) {
        QVERIFY(ServerDateTimeFormat::parseTimeZoneValue(token, &parsedTimeZone));
        QVERIFY(parsedTimeZone.hasCustomOffset());
        QCOMPARE(parsedTimeZone.toStorageValue(), expectedMinutesText);
    };

    expectParsedOffset(QStringLiteral("UTC+3:00"), QStringLiteral("180"));
    expectParsedOffset(QStringLiteral("UTC+03:00"), QStringLiteral("180"));
    expectParsedOffset(QStringLiteral("+03:00"), QStringLiteral("180"));
    expectParsedOffset(QStringLiteral("03:00"), QStringLiteral("180"));
    expectParsedOffset(QStringLiteral("-03:00"), QStringLiteral("-180"));
    expectParsedOffset(QStringLiteral("UTC+05:30"), QStringLiteral("330"));

    QVERIFY(!ServerDateTimeFormat::parseTimeZoneValue(QStringLiteral("UTC+25:00"),
                                                      &parsedTimeZone));

    QCOMPARE(ServerDateTimeFormat::formatUtcOffset(static_cast<short>(0)), QStringLiteral("UTC"));
    QCOMPARE(ServerDateTimeFormat::formatUtcOffset(static_cast<short>(180)),
             QStringLiteral("UTC+03:00"));
    QCOMPARE(ServerDateTimeFormat::formatUtcOffset(static_cast<short>(-180)),
             QStringLiteral("UTC-03:00"));

    const QDateTime sampleUtc(QDate(2026, 1, 2), QTime(12, 0), QTimeZone::utc());
    const QString fallbackRendered = ServerDateTimeFormat::formatLocalDateTime(sampleUtc,
                                                                               format,
                                                                               QStringLiteral("-"),
                                                                               UserTimeZone());
    const QString expectedFallback =
            sampleUtc.toTimeZone(QTimeZone::systemTimeZone()).toString(format);
    QCOMPARE(fallbackRendered, expectedFallback);
}

void ServerDatabaseTests::statisticsReportsUseRequestedTimezone()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for stats tests");
    const QString baseDbPath = tempDir.filePath(QStringLiteral("server.sqlite"));
    const QString outputDir = tempDir.filePath(QStringLiteral("reports"));
    QVERIFY(QDir().mkpath(outputDir));

    ServerStats stats(baseDbPath);
    stats.setEnabled(true);

    const MessengerUserKey actor(MessengerType::Telegram, QStringLiteral("stats-user"));
    const QString resourceUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString resourceName = QStringLiteral("Projector");

    const QDateTime janStartUtc(QDate(2026, 1, 1), QTime(21, 0), QTimeZone::utc());
    const QDateTime janEndUtc(QDate(2026, 1, 1), QTime(22, 0), QTimeZone::utc());
    stats.recordOccupationEvent(resourceName,
                                resourceUuid,
                                actor,
                                actor,
                                QStringLiteral("occupation_start"),
                                false,
                                60,
                                janStartUtc);
    stats.recordOccupationEvent(resourceName,
                                resourceUuid,
                                actor,
                                actor,
                                QStringLiteral("occupation_end"),
                                false,
                                60,
                                janEndUtc);

    ServerStats::ReportRequest peakRequest;
    peakRequest.reportKind = ServerStats::ReportRequest::ReportKind::PeakHours;
    peakRequest.format = ServerStats::ReportRequest::OutputFormat::Csv;
    peakRequest.startUtc = janStartUtc;
    peakRequest.endUtc = janStartUtc.addSecs(2 * 60 * 60);
    peakRequest.timeZone = UserTimeZone(180);

    const ServerStats::ReportResult peakResult =
            ServerStats::generateReport(baseDbPath, peakRequest, outputDir);
    QVERIFY2(peakResult.ok, qPrintable(peakResult.errorText));
    QVERIFY(!peakResult.filePath.isEmpty());

    const QString peakFileName = QFileInfo(peakResult.filePath).fileName();
    QVERIFY(peakFileName.contains(QStringLiteral("20260102_000000_to_20260102_020000")));

    const QString peakCsv = readUtf8File(peakResult.filePath);
    QVERIFY(!peakCsv.isEmpty());
    const QStringList peakRows = peakCsv.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QVERIFY(peakRows.size() >= 25);
    QVERIFY(csvValueAt(peakRows, 0, 0).contains(QStringLiteral("Hour (UTC+03:00)")));
    QCOMPARE(csvValueAt(peakRows, 1, 0), QStringLiteral("00"));
    QCOMPARE(csvValueAt(peakRows, 1, 1), QStringLiteral("1.00"));
    QCOMPARE(csvValueAt(peakRows, 22, 0), QStringLiteral("21"));
    QCOMPARE(csvValueAt(peakRows, 22, 1), QStringLiteral("0.00"));

    ServerStats::ReportRequest weekdayRequest = peakRequest;
    weekdayRequest.reportKind = ServerStats::ReportRequest::ReportKind::WeekdayPattern;

    const ServerStats::ReportResult weekdayResult =
            ServerStats::generateReport(baseDbPath, weekdayRequest, outputDir);
    QVERIFY2(weekdayResult.ok, qPrintable(weekdayResult.errorText));
    const QString weekdayCsv = readUtf8File(weekdayResult.filePath);
    QVERIFY(!weekdayCsv.isEmpty());
    const QStringList weekdayRows = weekdayCsv.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QVERIFY(weekdayRows.size() >= 8);
    QCOMPARE(csvValueAt(weekdayRows, 4, 1), QStringLiteral("0.00"));
    QCOMPARE(csvValueAt(weekdayRows, 5, 1), QStringLiteral("1.00"));

    ServerStats::ReportRequest eventsRequest;
    eventsRequest.reportKind = ServerStats::ReportRequest::ReportKind::EventsLog;
    eventsRequest.format = ServerStats::ReportRequest::OutputFormat::Csv;
    eventsRequest.startUtc = janStartUtc;
    eventsRequest.endUtc = janEndUtc;
    eventsRequest.timeZone = UserTimeZone(180);

    const ServerStats::ReportResult eventsResult =
            ServerStats::generateReport(baseDbPath, eventsRequest, outputDir);
    QVERIFY2(eventsResult.ok, qPrintable(eventsResult.errorText));
    const QString eventsCsv = readUtf8File(eventsResult.filePath);
    QVERIFY(!eventsCsv.isEmpty());
    const QStringList eventsRows = eventsCsv.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QVERIFY(eventsRows.size() >= 3);
    QVERIFY(csvValueAt(eventsRows, 0, 0).contains(QStringLiteral("Event time (UTC+03:00)")));
    QCOMPARE(csvValueAt(eventsRows, 1, 0), QStringLiteral("2026-01-02 00:00:00"));
    QCOMPARE(csvValueAt(eventsRows, 1, 1), QStringLiteral("occupation_start"));
    QCOMPARE(csvValueAt(eventsRows, 1, 2), resourceName);
    QCOMPARE(csvValueAt(eventsRows, 1, 3), actor.toString());
    QCOMPARE(csvValueAt(eventsRows, 1, 4), actor.toString());
    QCOMPARE(csvValueAt(eventsRows, 1, 5), QStringLiteral("0"));
    QCOMPARE(csvValueAt(eventsRows, 1, 6), QStringLiteral("60"));
    QCOMPARE(csvValueAt(eventsRows, 1, 9), QStringLiteral("0"));
    QCOMPARE(csvValueAt(eventsRows, 2, 1), QStringLiteral("occupation_end"));

    const QDateTime dstStartUtc(QDate(2026, 3, 29), QTime(0, 30), QTimeZone::utc());
    const QDateTime dstEndUtc(QDate(2026, 3, 29), QTime(2, 30), QTimeZone::utc());
    stats.recordOccupationEvent(resourceName,
                                resourceUuid,
                                actor,
                                actor,
                                QStringLiteral("occupation_start"),
                                false,
                                120,
                                dstStartUtc);
    stats.recordOccupationEvent(resourceName,
                                resourceUuid,
                                actor,
                                actor,
                                QStringLiteral("occupation_end"),
                                false,
                                120,
                                dstEndUtc);

    ServerStats::ReportRequest dstRequest;
    dstRequest.reportKind = ServerStats::ReportRequest::ReportKind::PeakHours;
    dstRequest.format = ServerStats::ReportRequest::OutputFormat::Csv;
    dstRequest.startUtc = QDateTime(QDate(2026, 3, 29), QTime(0, 0), QTimeZone::utc());
    dstRequest.endUtc = QDateTime(QDate(2026, 3, 29), QTime(3, 0), QTimeZone::utc());
    dstRequest.timeZone = UserTimeZone(60);

    const ServerStats::ReportResult dstResult =
            ServerStats::generateReport(baseDbPath, dstRequest, outputDir);
    QVERIFY2(dstResult.ok, qPrintable(dstResult.errorText));
    const QString dstCsv = readUtf8File(dstResult.filePath);
    QVERIFY(!dstCsv.isEmpty());
    const QStringList dstRows = dstCsv.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QVERIFY(dstRows.size() >= 25);
    QCOMPARE(csvValueAt(dstRows, 2, 0), QStringLiteral("01"));
    QCOMPARE(csvValueAt(dstRows, 2, 1), QStringLiteral("0.50"));
    QCOMPARE(csvValueAt(dstRows, 3, 0), QStringLiteral("02"));
    QCOMPARE(csvValueAt(dstRows, 3, 1), QStringLiteral("1.00"));
    QCOMPARE(csvValueAt(dstRows, 4, 0), QStringLiteral("03"));
    QCOMPARE(csvValueAt(dstRows, 4, 1), QStringLiteral("0.50"));
    QCOMPARE(csvValueAt(dstRows, 5, 0), QStringLiteral("04"));
    QCOMPARE(csvValueAt(dstRows, 5, 1), QStringLiteral("0.00"));
}

void ServerDatabaseTests::statisticsReportScopeExcludesSiblingGroups()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for stats scope test");
    const QString dbPath = tempDir.filePath(QStringLiteral("stats_scope.sqlite"));
    ServerDatabase database(dbPath);
    QVERIFY(database.isReady());

    const ServerDatabaseError enableError =
            database.setServerSetting(ServerRuntimeSettings::Setting::StatsEnabled,
                                      true,
                                      kTestActor);
    QVERIFY2(!enableError, qPrintable(enableError.debugString()));

    const MessengerUserKey alphaAdminKey(MessengerType::Telegram, QStringLiteral("alpha-admin"));
    const MessengerUserKey alphaUserKey(MessengerType::Telegram, QStringLiteral("alpha-user"));
    const MessengerUserKey betaUserKey(MessengerType::Telegram, QStringLiteral("beta-user"));

    User alphaAdmin(alphaAdminKey,
                    QStringLiteral("Alpha"),
                    QStringLiteral("Admin"),
                    User::Role::UserRole);
    alphaAdmin.setGroupRoles({ { Group(QStringLiteral("alpha")), User::Role::AdminRole } });
    User alphaUser(alphaUserKey,
                   QStringLiteral("Alpha"),
                   QStringLiteral("User"),
                   User::Role::UserRole);
    alphaUser.setGroupRoles({ { Group(QStringLiteral("alpha")), User::Role::UserRole } });
    User betaUser(betaUserKey,
                  QStringLiteral("Beta"),
                  QStringLiteral("User"),
                  User::Role::UserRole);
    betaUser.setGroupRoles({ { Group(QStringLiteral("beta")), User::Role::UserRole } });
    QVERIFY(!database.addUser(alphaAdmin, kTestActor));
    QVERIFY(!database.addUser(alphaUser, kTestActor));
    QVERIFY(!database.addUser(betaUser, kTestActor));

    Resource alphaResource(QStringLiteral("Alpha Stats"), QString(), QString());
    alphaResource.setGroups({ Group(QStringLiteral("alpha")) });
    Resource betaResource(QStringLiteral("Beta Stats"), QString(), QString());
    betaResource.setGroups({ Group(QStringLiteral("beta")) });
    QVERIFY(!database.addResource(alphaResource, kTestActor));
    QVERIFY(!database.addResource(betaResource, kTestActor));

    const QDateTime startUtc = QDateTime::currentDateTimeUtc().addSecs(-5);
    const QList<int> durations = database.occupationDurationModel()->minutes();
    QVERIFY(!database.occupyResourceUser(alphaResource.name(),
                                         alphaUserKey,
                                         5,
                                         durations,
                                         alphaUserKey));
    QVERIFY(!database.occupyResourceUser(betaResource.name(),
                                         betaUserKey,
                                         5,
                                         durations,
                                         betaUserKey));

    ServerStats::ReportRequest request;
    request.reportKind = ServerStats::ReportRequest::ReportKind::EventsLog;
    request.format = ServerStats::ReportRequest::OutputFormat::Table;
    request.startUtc = startUtc;
    request.endUtc = QDateTime::currentDateTimeUtc().addSecs(5);
    ServerStats::configureActorScope(&request, alphaAdmin, false);
    request.allowedResourceUuids.append(
            database.resourceModel()->resourceByName(alphaResource.name()).uuidString());
    request.allowedUsers.append(alphaAdminKey);
    request.allowedUsers.append(alphaUserKey);

    const ServerStats::ReportResult result =
            ServerStats::generateReport(dbPath, request, tempDir.path());
    QVERIFY2(result.ok, qPrintable(result.errorText));
    QVERIFY(result.tableText.contains(alphaResource.name()));
    QVERIFY(!result.tableText.contains(betaResource.name()));
    QVERIFY(!result.tableText.contains(betaUserKey.toString()));

    Resource moved = database.resourceModel()->resourceByName(alphaResource.name());
    moved.setGroups({ Group(QStringLiteral("beta")) });
    QVERIFY(!database.updateResource(moved, kTestActor));
    QVERIFY(!ServerStats::actorScopeStillValid(request,
                                               alphaAdmin,
                                               false,
                                               database.resourceModel(),
                                               database.userModel()));
}

void ServerDatabaseTests::runtimeDateTimeFormatSetting()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for ServerDatabase");
    const QString dbPath = tempDir.filePath(QStringLiteral("runtime_datetime_format.sqlite"));
    const QString settingKey = QStringLiteral("ui.date_time_format");

    {
        ServerDatabase database(dbPath);
        QVERIFY(database.isReady());

        const ServerDatabaseError setError =
                database.setServerSetting(settingKey,
                                          QVariant::fromValue(
                                                  QStringLiteral("dd.MM.yyyy HH:mm:ss")),
                                          kTestActor);
        QVERIFY2(!setError, qPrintable(setError.debugString()));

        const ServerSettingEntry configured =
                database.serverSettingsModel()->entryByKey(settingKey);
        QVERIFY(configured.isValid());
        QCOMPARE(configured.value().toString(), QStringLiteral("dd.MM.yyyy HH:mm:ss"));

        const ServerDatabaseError invalidError =
                database.setServerSetting(settingKey,
                                          QVariant::fromValue(QStringLiteral("QQQQ")),
                                          kTestActor);
        QVERIFY(invalidError);

        const ServerSettingEntry unchanged = database.serverSettingsModel()->entryByKey(settingKey);
        QVERIFY(unchanged.isValid());
        QCOMPARE(unchanged.value().toString(), QStringLiteral("dd.MM.yyyy HH:mm:ss"));
    }

    {
        ServerDatabase reopened(dbPath);
        QVERIFY(reopened.isReady());
        const ServerSettingEntry persisted = reopened.serverSettingsModel()->entryByKey(settingKey);
        QVERIFY(persisted.isValid());
        QCOMPARE(persisted.value().toString(), QStringLiteral("dd.MM.yyyy HH:mm:ss"));

        const ServerDatabaseError resetError =
                reopened.setServerSetting(settingKey,
                                          QVariant::fromValue(QStringLiteral("auto")),
                                          kTestActor);
        QVERIFY2(!resetError, qPrintable(resetError.debugString()));
        const ServerSettingEntry resetEntry =
                reopened.serverSettingsModel()->entryByKey(settingKey);
        QVERIFY(resetEntry.isValid());
        QCOMPARE(resetEntry.value().toString(), QString());
    }

    {
        ServerDatabase reopened(dbPath);
        QVERIFY(reopened.isReady());
        const ServerSettingEntry entry = reopened.serverSettingsModel()->entryByKey(settingKey);
        QVERIFY(entry.isValid());
        QCOMPARE(entry.value().toString(), QString());
    }
}

void ServerDatabaseTests::runtimePendingAccessReplySetting()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for ServerDatabase");
    const QString dbPath = tempDir.filePath(QStringLiteral("runtime_pending_access_reply.sqlite"));
    const QString settingKey = QStringLiteral("access.pending_access_user_reply");
    const QString customReply = QStringLiteral("Contact the service desk to request access.");

    {
        ServerDatabase database(dbPath);
        QVERIFY(database.isReady());

        const ServerSettingEntry defaultEntry =
                database.serverSettingsModel()->entryByKey(settingKey);
        QVERIFY(defaultEntry.isValid());
        QCOMPARE(defaultEntry.value().toString(), QString());

        const ServerDatabaseError setError =
                database.setServerSetting(settingKey, QVariant::fromValue(customReply), kTestActor);
        QVERIFY2(!setError, qPrintable(setError.debugString()));

        const ServerSettingEntry configured =
                database.serverSettingsModel()->entryByKey(settingKey);
        QVERIFY(configured.isValid());
        QCOMPARE(configured.value().toString(), customReply);
    }

    {
        ServerDatabase reopened(dbPath);
        QVERIFY(reopened.isReady());
        const ServerSettingEntry persisted = reopened.serverSettingsModel()->entryByKey(settingKey);
        QVERIFY(persisted.isValid());
        QCOMPARE(persisted.value().toString(), customReply);

        const ServerDatabaseError resetError =
                reopened.setServerSetting(settingKey,
                                          QVariant::fromValue(QStringLiteral("default")),
                                          kTestActor);
        QVERIFY2(!resetError, qPrintable(resetError.debugString()));
        const ServerSettingEntry resetEntry =
                reopened.serverSettingsModel()->entryByKey(settingKey);
        QVERIFY(resetEntry.isValid());
        QCOMPARE(resetEntry.value().toString(), QString());
    }

    {
        ServerDatabase reopened(dbPath);
        QVERIFY(reopened.isReady());
        const ServerSettingEntry entry = reopened.serverSettingsModel()->entryByKey(settingKey);
        QVERIFY(entry.isValid());
        QCOMPARE(entry.value().toString(), QString());
    }
}

void ServerDatabaseTests::statisticsReportCanBeCancelled()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    ServerStats::ReportRequest request;
    request.startUtc = QDateTime::currentDateTimeUtc().addDays(-1);
    request.endUtc = QDateTime::currentDateTimeUtc();

    int cancellationChecks = 0;
    const ServerStats::ReportResult result =
            ServerStats::generateReport(tempDir.filePath(QStringLiteral("database.sqlite")),
                                        request,
                                        tempDir.path(),
                                        [&cancellationChecks]() {
                                            ++cancellationChecks;
                                            return true;
                                        });

    QVERIFY(!result.ok);
    QVERIFY(!result.errorText.isEmpty());
    QVERIFY(cancellationChecks > 0);
    QVERIFY(result.filePath.isEmpty());
}

void ServerDatabaseTests::expiredQueueEntriesAreRemovedInBatch()
{
    UserModel users;
    ResourceModel resources;
    ResourceQueueModel queue(&users, &resources);
    int singleRemoveCalls = 0;
    int batchRemoveCalls = 0;
    int batchRemovedKeys = 0;
    ResourceQueueModel::StorageAdapter storage;
    storage.remove = [&singleRemoveCalls](const ResourceQueueKey &) {
        ++singleRemoveCalls;
        return true;
    };
    storage.removeEntries = [&batchRemoveCalls,
                             &batchRemovedKeys](const QList<ResourceQueueKey> &keys) {
        ++batchRemoveCalls;
        batchRemovedKeys += keys.size();
        return true;
    };
    queue.setStorageAdapter(storage);
    QSignalSpy rowsRemovedSpy(&queue, &QAbstractItemModel::rowsRemoved);
    QSignalSpy modelResetSpy(&queue, &QAbstractItemModel::modelReset);

    const QString resourceUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    QList<ResourceQueueEntry> entries;
    constexpr int kEntryCount = 500;
    entries.reserve(kEntryCount);
    for (int i = 0; i < kEntryCount; ++i) {
        const MessengerUserKey user(MessengerType::Telegram,
                                    QStringLiteral("expired-user-%1").arg(i));
        const QDateTime startUtc = nowUtc.addSecs(-kEntryCount - i - 10);
        entries.append(ResourceQueueEntry(resourceUuid,
                                          user,
                                          startUtc,
                                          startUtc,
                                          nowUtc.addSecs(-1),
                                          ResourceQueueEntry::StartMode::AtTime,
                                          1));
    }

    queue.replaceAll(entries);

    QCOMPARE(queue.items().size(), 0);
    QCOMPARE(rowsRemovedSpy.count(), 0);
    QCOMPARE(modelResetSpy.count(), 2);
    QCOMPARE(singleRemoveCalls, 0);
    QCOMPARE(batchRemoveCalls, 1);
    QCOMPARE(batchRemovedKeys, kEntryCount);
}

BOOKING_RESOURCES_REGISTER_TEST(ServerDatabaseTests)

#include "ServerDatabaseTests.moc"
