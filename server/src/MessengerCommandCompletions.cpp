#include "MessengerCommandCompletions.h"

#include "BlacklistModel.h"
#include "Group.h"
#include "MessengerAbstractCommand.h"
#include "MessengerBot.h"
#include "MessengerCommandAudience.h"
#include "MessengerCommandManager.h"
#include "MessengerCommandParser.h"
#include "MessengerType.h"
#include "MessengerTextRender.h"
#include "User.h"
#include "OccupationDurationModel.h"
#include "PendingAccessUserModel.h"
#include "Resource.h"
#include "ResourceModel.h"
#include "ServerDatabase.h"
#include "ServerRuntimeSettings.h"
#include "ServerStats.h"
#include "UserModel.h"

#include <QJsonValue>
#include <QPair>
#include <QSet>
#include <QVector>

#include <initializer_list>

namespace {

constexpr int kMaxCompletionValues = 50;

QJsonArray stringsToJson(const QStringList &values)
{
    QJsonArray result;
    for (const QString &value : values)
        result.append(value);
    return result;
}

QJsonObject whenEquals(std::initializer_list<QPair<int, QStringList>> conditions)
{
    QJsonObject result;
    for (const QPair<int, QStringList> &condition : conditions)
        result.insert(QString::number(condition.first), stringsToJson(condition.second));
    return result;
}

void addCompletionEntry(QJsonArray *entries,
                        int argumentIndex,
                        const QStringList &values,
                        const QJsonObject &when = QJsonObject())
{
    if (!entries || values.isEmpty())
        return;

    QJsonObject entry;
    entry.insert(QStringLiteral("argument"), argumentIndex);
    entry.insert(QStringLiteral("values"), stringsToJson(values));
    if (!when.isEmpty())
        entry.insert(QStringLiteral("when"), when);
    entries->append(entry);
}

ServerDatabase *databaseForManager(const MessengerCommandManager *manager)
{
    const MessengerBot *owner = manager ? manager->bot() : nullptr;
    return owner ? owner->database() : nullptr;
}

QStringList resourceCompletionValues(const MessengerCommandManager *manager,
                                     const MessengerUserKey &user,
                                     bool visibleOnly)
{
    QStringList values;
    ServerDatabase *db = databaseForManager(manager);
    const ResourceModel *model = db ? db->resourceModel() : nullptr;
    if (!model)
        return values;

    const MessengerType viewerMessenger =
            user.isValid() ? user.messenger() : MessengerType::Web;
    const QList<Resource> resources = model->items();
    for (const Resource &resource : resources) {
        if (!resource.isValid())
            continue;
        if (visibleOnly) {
            if (!MessengerTextRender::isResourceVisibleToViewer(resource,
                                                                db,
                                                                viewerMessenger,
                                                                user)) {
                continue;
            }
        } else if (!db->canAdministerResource(user, resource)) {
            continue;
        }
        values.append(resource.name());
    }
    values.sort(Qt::CaseInsensitive);
    return values;
}

QStringList activeAccessUserCompletionValues(const MessengerCommandManager *manager,
                                             const MessengerUserKey &actor,
                                             MessengerType currentMessenger,
                                             bool currentMessengerOnly)
{
    QStringList values;
    const ServerDatabase *db = databaseForManager(manager);
    const UserModel *model = db ? db->userModel() : nullptr;
    if (!model)
        return values;

    const QList<User> users = model->items();
    for (const User &user : users) {
        const MessengerUserKey key = user.key();
        if (!key.isValid())
            continue;
        if (currentMessengerOnly && key.messenger() != currentMessenger)
            continue;
        if (key == actor) {
            values.append(key.toString());
            continue;
        }
        if (!db->canAdministerUser(actor, user))
            continue;
        values.append(key.toString());
    }
    values.sort(Qt::CaseInsensitive);
    return values;
}

QStringList pendingAccessUserCompletionValues(const MessengerCommandManager *manager,
                                              const MessengerUserKey &actor)
{
    QStringList values;
    const ServerDatabase *db = databaseForManager(manager);
    if (!db || !db->canManageServerSettings(actor))
        return values;
    const PendingAccessUserModel *model = db ? db->pendingAccessUserModel() : nullptr;
    if (!model)
        return values;

    const QList<PendingAccessUserEntry> users = model->recentUsers(50);
    for (const PendingAccessUserEntry &entry : users) {
        const MessengerUserKey key = entry.user();
        if (key.isValid())
            values.append(key.toString());
    }
    return values;
}

QStringList blacklistedUserCompletionValues(const MessengerCommandManager *manager,
                                            const MessengerUserKey &actor)
{
    QStringList values;
    const ServerDatabase *db = databaseForManager(manager);
    const BlacklistModel *model = db ? db->blacklistModel() : nullptr;
    if (!model)
        return values;

    const QList<BlacklistEntry> entries = model->entries();
    for (const BlacklistEntry &entry : entries) {
        const MessengerUserKey key = entry.user();
        if (key.isValid() && db->canManageBlacklistTarget(actor, key))
            values.append(key.toString());
    }
    values.sort(Qt::CaseInsensitive);
    return values;
}

QStringList occupationDurationCompletionValues(const MessengerCommandManager *manager)
{
    QStringList values;
    const ServerDatabase *db = databaseForManager(manager);
    const OccupationDurationModel *model = db ? db->occupationDurationModel() : nullptr;
    if (!model)
        return values;

    const QList<int> minutes = model->minutes();
    for (int value : minutes)
        values.append(QString::number(value));
    return values;
}

QStringList groupCompletionValues(const MessengerCommandManager *manager,
                                  const MessengerUserKey &actor)
{
    QStringList values;
    QSet<Group> groups;
    const ServerDatabase *db = databaseForManager(manager);
    if (!db)
        return values;

    groups.unite(db->administeredGroupRoots(actor));
    if (const ResourceModel *resourceModel = db->resourceModel()) {
        const QList<Resource> resources = resourceModel->items();
        for (const Resource &resource : resources) {
            for (const Group &group : resource.groups()) {
                if (db->canAdministerGroups(actor, QSet<Group>({ group })))
                    groups.insert(group);
            }
        }
    }
    if (const UserModel *userModel = db->userModel()) {
        const QList<User> users = userModel->items();
        for (const User &user : users) {
            for (const Group &group : user.groups()) {
                if (db->canAdministerGroups(actor, QSet<Group>({ group })))
                    groups.insert(group);
            }
        }
    }

    values = Group::toStringList(groups, false);
    if (groups.contains(Group::root())) {
        values.removeAll(QString());
        values.prepend(QStringLiteral("-"));
    }
    values.sort(Qt::CaseInsensitive);
    return values;
}

QStringList groupRoleCompletionValues(const MessengerCommandManager *manager,
                                      const MessengerUserKey &actor)
{
    QStringList values;
    const QStringList groups = groupCompletionValues(manager, actor);
    for (const QString &group : groups) {
        if (group == QStringLiteral("-")) {
            values << group;
        } else {
            values << QStringLiteral("%1=user").arg(group);
            values << QStringLiteral("%1=admin").arg(group);
        }
    }
    return values;
}

QStringList serverSettingKeys()
{
    QStringList keys;
    const QList<ServerRuntimeSettings::Setting> settings = ServerRuntimeSettings::allSettings();
    for (ServerRuntimeSettings::Setting setting : settings)
        keys.append(QLatin1String(ServerRuntimeSettings::key(setting)));
    return keys;
}

QStringList serverSettingValues(ServerRuntimeSettings::Setting setting)
{
    QStringList values;
    values << QStringLiteral("default") << QStringLiteral("auto");
    switch (setting) {
    case ServerRuntimeSettings::Setting::MaxResourcesPerUser:
    case ServerRuntimeSettings::Setting::MaxOccupationDurationMinutes:
    case ServerRuntimeSettings::Setting::MenuSessionTimeoutMinutes:
        values << QStringLiteral("0") << QStringLiteral("1") << QStringLiteral("60")
               << QStringLiteral("unlimited") << QStringLiteral("disabled");
        break;
    case ServerRuntimeSettings::Setting::MenuUpdateMode:
        values << QStringLiteral("active") << QStringLiteral("passive");
        break;
    case ServerRuntimeSettings::Setting::StatsEnabled:
        values << QStringLiteral("true") << QStringLiteral("false") << QStringLiteral("1")
               << QStringLiteral("0");
        break;
    case ServerRuntimeSettings::Setting::StatsAdminsOnly:
        values << QStringLiteral("admins_only") << QStringLiteral("all_users")
               << QStringLiteral("true") << QStringLiteral("false");
        break;
    case ServerRuntimeSettings::Setting::DateTimeFormat:
        values << QStringLiteral("dd.MM.yyyy HH:mm:ss") << QStringLiteral("yyyy-MM-dd HH:mm");
        break;
    case ServerRuntimeSettings::Setting::PendingAccessUserReplyMessage:
        break;
    }
    return values;
}

QStringList filterPrefix(const QStringList &values, const QString &prefix)
{
    QStringList result;
    QSet<QString> seen;
    const QString normalized = prefix.trimmed();
    for (const QString &raw : values) {
        const QString value = raw.trimmed();
        if (value.isEmpty())
            continue;
        if (!normalized.isEmpty() && !value.startsWith(normalized, Qt::CaseInsensitive))
            continue;
        const QString key = value.toLower();
        if (seen.contains(key))
            continue;
        seen.insert(key);
        result.append(value);
        if (result.size() >= kMaxCompletionValues)
            break;
    }
    return result;
}

QVector<const MessengerAbstractCommand *> availableCommands(const MessengerCommandManager *manager,
                                                            const MessengerUserKey &user,
                                                            bool localCliOnly)
{
    QVector<const MessengerAbstractCommand *> result;
    if (!manager)
        return result;

    const MessengerBot *ownerBot = manager->bot();
    AbstractBot::IncomingMessage availabilityMessage;
    const AbstractBot::IncomingMessage *availabilityContext = nullptr;
    if (user.isValid()) {
        availabilityMessage.chat.messenger = user.messenger();
        availabilityMessage.chat.chatId = user.userId();
        availabilityMessage.sender.userId = user.userId();
        availabilityContext = &availabilityMessage;
    }

    const QVector<MessengerAbstractCommand *> commands = manager->commands();
    for (const MessengerAbstractCommand *command : commands) {
        if (!command || !command->isEnabled(availabilityContext))
            continue;
        const MessengerCommandAudience audience = command->audience();
        if (localCliOnly && audience == MessengerCommandAudience::UsersOnly)
            continue;
        if (audience == MessengerCommandAudience::AdminsOnly) {
            if (!ownerBot || !ownerBot->isAdminUser(user))
                continue;
        }
        if (audience == MessengerCommandAudience::ServerAdminsOnly) {
            if (!ownerBot || !ownerBot->canManageServerSettings(user))
                continue;
        }
        result.append(command);
    }
    return result;
}

const MessengerAbstractCommand *findCommand(const QVector<const MessengerAbstractCommand *> &commands,
                                            const QString &name)
{
    QString normalized = name.trimmed().toLower();
    if (normalized.isEmpty())
        return nullptr;
    if (!normalized.startsWith(QLatin1Char('/')))
        normalized.prepend(QLatin1Char('/'));
    for (const MessengerAbstractCommand *command : commands) {
        if (command && command->command().trimmed().toLower() == normalized)
            return command;
    }
    return nullptr;
}

} // namespace

namespace MessengerCommandCompletions {

QJsonArray argumentCompletionsToJson(const QString &commandName,
                                     const MessengerCommandManager *manager,
                                     const MessengerUserKey &user,
                                     const QStringList &availableCommandNames)
{
    QJsonArray entries;
    const QString name = commandName.trimmed();
    const QStringList visibleResources = resourceCompletionValues(manager, user, true);
    const QStringList allResources = resourceCompletionValues(manager, user, false);
    const MessengerType viewerMessenger =
            user.isValid() ? user.messenger() : MessengerType::Web;
    const QStringList activeAccessUsers =
            activeAccessUserCompletionValues(manager, user, viewerMessenger, false);

    if (name == QLatin1String("/add_resource")) {
        addCompletionEntry(&entries, 3, groupCompletionValues(manager, user));
    } else if (name == QLatin1String("/add_user")) {
        addCompletionEntry(&entries, 0, pendingAccessUserCompletionValues(manager, user));
        addCompletionEntry(&entries,
                           3,
                           QStringList() << QStringLiteral("true") << QStringLiteral("false")
                                         << QStringLiteral("admin"));
        addCompletionEntry(&entries, 4, groupRoleCompletionValues(manager, user));
    } else if (name == QLatin1String("/ban_user")) {
        QStringList values = activeAccessUsers;
        values.append(pendingAccessUserCompletionValues(manager, user));
        addCompletionEntry(&entries, 0, values);
        addCompletionEntry(&entries,
                           1,
                           QStringList() << QStringLiteral("60") << QStringLiteral("1h")
                                         << QStringLiteral("1d") << QStringLiteral("1w")
                                         << QStringLiteral("permanent"));
    } else if (name == QLatin1String("/delete_resource")) {
        addCompletionEntry(&entries, 0, allResources);
    } else if (name == QLatin1String("/delete_user")) {
        addCompletionEntry(&entries, 0, activeAccessUsers);
    } else if (name == QLatin1String("/help")) {
        QStringList values;
        for (const QString &command : availableCommandNames) {
            const QString trimmed = command.trimmed();
            if (trimmed.isEmpty())
                continue;
            values.append(trimmed);
            if (trimmed.startsWith(QLatin1Char('/')))
                values.append(trimmed.mid(1));
        }
        addCompletionEntry(&entries, 0, values);
    } else if (name == QLatin1String("/occupation_durations")) {
        addCompletionEntry(&entries,
                           0,
                           QStringList() << QStringLiteral("list") << QStringLiteral("add")
                                         << QStringLiteral("remove"));
        addCompletionEntry(&entries,
                           1,
                           occupationDurationCompletionValues(manager),
                           whenEquals({ { 0, QStringList() << QStringLiteral("remove") } }));
    } else if (name == QLatin1String("/occupy")) {
        addCompletionEntry(&entries, 0, visibleResources);
        addCompletionEntry(&entries, 1, occupationDurationCompletionValues(manager));
    } else if (name == QLatin1String("/queue") || name == QLatin1String("/release")
               || name == QLatin1String("/schedule") || name == QLatin1String("/unschedule")) {
        addCompletionEntry(&entries, 0, visibleResources);
    } else if (name == QLatin1String("/send_notification")) {
        addCompletionEntry(&entries,
                           0,
                           activeAccessUserCompletionValues(manager,
                                                            user,
                                                            viewerMessenger,
                                                            viewerMessenger == MessengerType::Web));
    } else if (name == QLatin1String("/server_settings")) {
        addCompletionEntry(&entries,
                           0,
                           QStringList() << QStringLiteral("list") << QStringLiteral("get")
                                         << QStringLiteral("set"));
        addCompletionEntry(&entries, 1, serverSettingKeys());
        const QList<ServerRuntimeSettings::Setting> settings = ServerRuntimeSettings::allSettings();
        for (ServerRuntimeSettings::Setting setting : settings) {
            const QString key = QLatin1String(ServerRuntimeSettings::key(setting));
            addCompletionEntry(&entries,
                               2,
                               serverSettingValues(setting),
                               whenEquals({ { 0, QStringList() << QStringLiteral("set") },
                                            { 1, QStringList() << key } }));
        }
    } else if (name == QLatin1String("/soonest")) {
        QStringList values;
        for (int count = 1; count <= 20; ++count)
            values.append(QString::number(count));
        addCompletionEntry(&entries, 0, values);
    } else if (name == QLatin1String("/statistics")) {
        QStringList resources;
        resources << QStringLiteral("all") << QStringLiteral("-") << QStringLiteral("*");
        resources.append(visibleResources);
        addCompletionEntry(&entries, 0, resources);

        QStringList users;
        users << QStringLiteral("all") << QStringLiteral("-") << QStringLiteral("*");
        users.append(activeAccessUsers);
        addCompletionEntry(&entries, 1, users);
        addCompletionEntry(&entries, 3, QStringList() << QStringLiteral("now"));
        addCompletionEntry(&entries,
                           4,
                           QStringList() << QStringLiteral("csv") << QStringLiteral("table")
                                         << QStringLiteral("all"));
        addCompletionEntry(&entries, 5, ServerStats::reportKindUsageText().split(QLatin1Char('|')));
    } else if (name == QLatin1String("/unban_user")) {
        addCompletionEntry(&entries, 0, blacklistedUserCompletionValues(manager, user));
    } else if (name == QLatin1String("/update_resource")) {
        addCompletionEntry(&entries, 0, allResources);
        addCompletionEntry(&entries,
                           3,
                           QStringList() << Resource::statusToToken(Resource::Status::On)
                                         << Resource::statusToToken(
                                                    Resource::Status::TemporarilyUnavailable)
                                         << Resource::statusToToken(Resource::Status::Off));
        addCompletionEntry(&entries, 4, groupCompletionValues(manager, user));
    } else if (name == QLatin1String("/update_user")) {
        addCompletionEntry(&entries, 0, activeAccessUsers);
        addCompletionEntry(&entries,
                           3,
                           QStringList() << QStringLiteral("true") << QStringLiteral("false")
                                         << QStringLiteral("admin"));
        addCompletionEntry(&entries, 4, groupRoleCompletionValues(manager, user));
    } else if (name == QLatin1String("/user_settings")) {
        addCompletionEntry(&entries,
                           0,
                           QStringList() << QStringLiteral("list") << QStringLiteral("get")
                                         << QStringLiteral("set"));
        addCompletionEntry(&entries,
                           1,
                           QStringList() << QStringLiteral("locale") << QStringLiteral("timezone")
                                         << QStringLiteral("occupation.notify"));
        addCompletionEntry(&entries,
                           2,
                           QStringList() << QStringLiteral("default") << QStringLiteral("auto")
                                         << QStringLiteral("messenger") << QStringLiteral("en")
                                         << QStringLiteral("ru") << QStringLiteral("ru_stand"),
                           whenEquals({ { 0, QStringList() << QStringLiteral("set") },
                                        { 1, QStringList() << QStringLiteral("locale") } }));
        addCompletionEntry(&entries,
                           2,
                           QStringList() << QStringLiteral("default") << QStringLiteral("auto")
                                         << QStringLiteral("system") << QStringLiteral("UTC")
                                         << QStringLiteral("UTC+03:00"),
                           whenEquals({ { 0, QStringList() << QStringLiteral("set") },
                                        { 1, QStringList() << QStringLiteral("timezone") } }));
        addCompletionEntry(&entries,
                           2,
                           QStringList() << QStringLiteral("all") << QStringLiteral("mine")
                                         << QStringLiteral("off"),
                           whenEquals(
                                   { { 0, QStringList() << QStringLiteral("set") },
                                     { 1,
                                       QStringList() << QStringLiteral("occupation.notify") } }));
    }

    return entries;
}

bool conditionsMatch(const QJsonObject &when, const QStringList &previousValues)
{
    if (when.isEmpty())
        return true;

    for (auto it = when.begin(); it != when.end(); ++it) {
        bool ok = false;
        const int index = it.key().toInt(&ok);
        if (!ok)
            continue;

        QStringList expected;
        if (it.value().isArray()) {
            const QJsonArray values = it.value().toArray();
            for (const QJsonValue &value : values)
                expected.append(value.toString());
        } else {
            expected.append(it.value().toString());
        }

        const QString actual = (index >= 0 && index < previousValues.size())
                                       ? previousValues.at(index).trimmed().toLower()
                                       : QString();
        bool match = false;
        for (const QString &item : expected) {
            if (item.trimmed().toLower() == actual) {
                match = true;
                break;
            }
        }
        if (!match)
            return false;
    }
    return true;
}

Result complete(const MessengerCommandManager *manager,
                const MessengerUserKey &user,
                const QString &text,
                int cursor,
                bool localCliOnly)
{
    Result result;
    result.appendSpace = true;
    if (!manager)
        return result;

    manager->refreshCommandTranslations();
    const QVector<const MessengerAbstractCommand *> commands =
            availableCommands(manager, user, localCliOnly);
    QStringList availableCommandNames;
    for (const MessengerAbstractCommand *command : commands) {
        if (command)
            availableCommandNames.append(command->command());
    }

    const MessengerCommandParser::CompletionContext commandContext =
            MessengerCommandParser::contextAtCursor(text,
                                                    cursor,
                                                    QVector<MessengerCommandParser::ArgumentInfo>());
    if (!commandContext.ok)
        return result;

    result.replaceFrom = commandContext.replaceFrom;
    result.replaceTo = commandContext.replaceTo;

    if (commandContext.completingCommand) {
        QStringList names;
        const QString prefix = commandContext.prefix;
        for (const QString &name : availableCommandNames) {
            const QString trimmed = name.trimmed();
            if (trimmed.isEmpty())
                continue;
            if (prefix.isEmpty() || trimmed.startsWith(prefix, Qt::CaseInsensitive)
                || (!prefix.startsWith(QLatin1Char('/'))
                    && trimmed.mid(1).startsWith(prefix, Qt::CaseInsensitive))) {
                names.append(trimmed);
            }
        }
        result.values = filterPrefix(names, QString());
        return result;
    }

    const MessengerAbstractCommand *command = findCommand(commands, commandContext.commandName);
    if (!command)
        return result;

    const MessengerCommandParser::CompletionContext argumentContext =
            MessengerCommandParser::contextAtCursor(text, cursor, command->parser().arguments());
    if (!argumentContext.ok)
        return result;

    result.replaceFrom = argumentContext.replaceFrom;
    result.replaceTo = argumentContext.replaceTo;

    const QJsonArray entries = argumentCompletionsToJson(command->command(),
                                                         manager,
                                                         user,
                                                         availableCommandNames);
    QStringList values;
    for (const QJsonValue &entryValue : entries) {
        const QJsonObject entry = entryValue.toObject();
        if (entry.value(QStringLiteral("argument")).toInt(-1) != argumentContext.argumentIndex)
            continue;
        if (!conditionsMatch(entry.value(QStringLiteral("when")).toObject(),
                             argumentContext.previousValues)) {
            continue;
        }
        const QJsonArray rawValues = entry.value(QStringLiteral("values")).toArray();
        for (const QJsonValue &rawValue : rawValues)
            values.append(rawValue.toString());
    }
    result.values = filterPrefix(values, argumentContext.prefix);
    return result;
}

} // namespace MessengerCommandCompletions
