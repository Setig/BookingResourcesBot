#include "MessengerBot.h"

#include "User.h"
#include "WebBot.h"
#include "Resource.h"
#include "UserModel.h"
#include "TelegramBot.h"
#include "LocalCliBot.h"
#include "LocalCliProtocol.h"
#include "MessengerCommandCompletions.h"
#include "KonturTalkBot.h"
#include "ServerDatabase.h"
#include "MessengerLimiter.h"
#include "MessengerBotPoll.h"
#include "MessengerTextRender.h"
#include "MessengerMenuManager.h"
#include "ServerDateTimeFormat.h"
#include "PendingAccessUserEntry.h"
#include "ServerRuntimeSettings.h"
#include "MessengerCommandManager.h"
#include "MessengerUserFacingErrors.h"
#include "BookingResourcesTranslator.h"

#include <QList>
#include <QHash>
#include <QDateTime>
#include <QJsonObject>
#include <QStringList>
#include <QLoggingCategory>
#include <QAbstractItemModel>

Q_LOGGING_CATEGORY(lcMessengerBot, "booking.server.messenger")

namespace {

QString occupationNotifyModeToString(User::OccupationNotifyMode mode)
{
    switch (mode) {
    case User::OccupationNotifyMode::AllResources:
        return QStringLiteral("all");
    case User::OccupationNotifyMode::OwnResourcesOnly:
        return QStringLiteral("mine");
    case User::OccupationNotifyMode::Disabled:
        return QStringLiteral("off");
    }
    return QStringLiteral("mine");
}

bool occupationNotifyModeFromString(const QString &value,
                                    User::OccupationNotifyMode *modeOut,
                                    QString *errorOut)
{
    if (!modeOut)
        return false;

    const QString normalized = value.trimmed().toLower();
    if (normalized == QLatin1String("all") || normalized == QLatin1String("all_resources")) {
        *modeOut = User::OccupationNotifyMode::AllResources;
        return true;
    }
    if (normalized == QLatin1String("mine") || normalized == QLatin1String("own")
        || normalized == QLatin1String("own_resources")) {
        *modeOut = User::OccupationNotifyMode::OwnResourcesOnly;
        return true;
    }
    if (normalized == QLatin1String("off") || normalized == QLatin1String("disabled")) {
        *modeOut = User::OccupationNotifyMode::Disabled;
        return true;
    }

    if (errorOut)
        *errorOut = MessengerUserFacingErrors::invalidNotifyMode();
    return false;
}

bool localeFromSettingsValue(const QString &value, QString *localeOut, QString *errorOut)
{
    if (!localeOut)
        return false;

    const QString trimmed = value.trimmed();
    const QString normalized = trimmed.toLower();
    if (normalized == QLatin1String("default") || normalized == QLatin1String("auto")
        || normalized == QLatin1String("messenger")) {
        localeOut->clear();
        return true;
    }

    if (trimmed.isEmpty()) {
        localeOut->clear();
        return true;
    }

    *localeOut = trimmed;
    Q_UNUSED(errorOut);
    return true;
}

QString primaryLocaleForDefault(const QString &locale)
{
    const QString trimmed = locale.trimmed();
    if (trimmed.isEmpty())
        return QString();

    QString normalized = trimmed.toLower();
    normalized.replace(QLatin1Char('-'), QLatin1Char('_'));
    if (normalized == QLatin1String("ru_stand"))
        return QStringLiteral("ru_stand");

    const QString language = normalized.section(QLatin1Char('_'), 0, 0);
    if (language == QLatin1String("en") || language == QLatin1String("ru"))
        return language;

    return trimmed;
}

QJsonObject userSettingsToJson(const User &user, const QString &effectiveLocale)
{
    QJsonObject settings;
    settings.insert(QStringLiteral("locale"), user.locale().trimmed());
    settings.insert(QStringLiteral("timezone"), user.timezone().toStorageValue());
    settings.insert(QStringLiteral("occupation_notify"),
                    occupationNotifyModeToString(user.occupationNotifyMode()));

    QJsonObject response;
    response.insert(QStringLiteral("ok"), true);
    response.insert(QStringLiteral("access_active"), true);
    response.insert(QStringLiteral("effective_locale"), effectiveLocale.trimmed());
    response.insert(QStringLiteral("settings"), settings);
    return response;
}

} // namespace

class MessengerBotPrivate
{
public:
    explicit MessengerBotPrivate(MessengerBot *bot);
    ~MessengerBotPrivate();

    static AbstractBot *createBot(MessengerType type, const QString &token, QObject *parent);

    void handleWebAuthorizationRequest(WebBot *webBot,
                                       const QString &requestId,
                                       const QJsonObject &payload);
    void handleWebUserSettingsRequest(WebBot *webBot,
                                      const QString &requestId,
                                      const QString &action,
                                      const QJsonObject &payload);
    void sendReply(const AbstractBot::ChatAddress &chat,
                   const QString &text,
                   AbstractBot::MessageFormat format = AbstractBot::MessageFormat::PlainText) const;
    void sendReply(const AbstractBot::ChatAddress &chat,
                   const QString &text,
                   const AbstractBot::MessageOptions &options) const;
    void editReply(const AbstractBot::ChatAddress &chat,
                   const QString &messageId,
                   const QString &text,
                   AbstractBot::MessageFormat format = AbstractBot::MessageFormat::PlainText) const;
    void editReply(const AbstractBot::ChatAddress &chat,
                   const QString &messageId,
                   const QString &text,
                   const AbstractBot::MessageOptions &options) const;

    bool ensureDatabase(const AbstractBot::ChatAddress &chat) const;
    void rememberMessengerLocale(const MessengerUserKey &user, const QString &locale);
    QString localeOverrideForUser(const MessengerUserKey &user) const;
    UserTimeZone timeZoneOverrideForUser(const MessengerUserKey &user) const;
    QString cachedMessengerLocale(const MessengerUserKey &user) const;
    QString effectiveLocaleForUser(const MessengerUserKey &user,
                                   const QString &reportedLocale) const;
    BookingResourcesTranslator::ScopedLocaleOverride applyScopedLocale(const QString &locale);
    QString buildBlockedUserReply(const BlacklistEntry &entry,
                                  const MessengerUserKey &targetUser) const;
    AbstractBot *directBackendForUser(const MessengerUserKey &user) const;
    void sendDirectMessage(
            const MessengerUserKey &user,
            const QString &text,
            AbstractBot::MessageFormat format = AbstractBot::MessageFormat::PlainText) const;
    void sendDirectMessage(
            AbstractBot *backend,
            const MessengerUserKey &user,
            const QString &text,
            AbstractBot::MessageFormat format = AbstractBot::MessageFormat::PlainText) const;
    [[nodiscard]] bool supportsDirectMessageFormat(const MessengerUserKey &user,
                                                   AbstractBot::MessageFormat format) const;
    void configureCommands() const;
    bool statisticsCommandEnabled() const;
    void syncCommandsVisibility();
    QString resolveResourceName(const QString &resourceName,
                                const ResourceModel *resourceModel) const;
    QString displayNameForKey(const UserModel *model, const MessengerUserKey &key) const;
    void sendOccupationNotification(ServerDatabase::OccupationNotificationType type,
                                    const QString &resourceName,
                                    const MessengerUserKey &releasedBy,
                                    const MessengerUserKey &occupiedBy,
                                    const MessengerUserKey &initiator,
                                    int durationMinutes);

    MessengerType botType = MessengerType::Telegram;
    AbstractBot *bot = nullptr;
    ServerDatabase *database = nullptr;               // non-owning
    BookingResourcesTranslator *translator = nullptr; // non-owning
    MessengerBotPoll *poll = nullptr;                 // non-owning

    QString token;
    QHash<MessengerUserKey, QString> lastMessengerLocales;
    MessengerCommandManager *commandManager = nullptr;
    bool statsCommandEnabled = false;
    MessengerLimiter *limiter = nullptr; // owned by MessengerBot (QObject parent)
    MessengerMenuManager *menuManager = nullptr;
};

MessengerBotPrivate::MessengerBotPrivate(MessengerBot *bot)
{
    // do nothing
}

MessengerBotPrivate::~MessengerBotPrivate()
{
    delete commandManager;
    delete menuManager;
}

AbstractBot *MessengerBotPrivate::createBot(MessengerType type,
                                            const QString &token,
                                            QObject *parent)
{
    switch (type) {
    case MessengerType::LocalCli:
        return new LocalCliBot(token, parent);
    case MessengerType::Telegram:
        return new TelegramBot(token, parent);
    case MessengerType::KonturTalk:
        return new KonturTalkBot(token, parent);
    case MessengerType::Web:
        return new WebBot(token, parent);
    }

    qCWarning(lcMessengerBot).nospace()
            << "Unsupported messenger type: " << qPrintable(messengerTypeToString(type));
    return nullptr;
}

void MessengerBotPrivate::handleWebAuthorizationRequest(WebBot *webBot,
                                                        const QString &requestId,
                                                        const QJsonObject &payload)
{
    QJsonObject response;
    if (!database || !database->isReady()) {
        response.insert(QStringLiteral("ok"), false);
        response.insert(QStringLiteral("error"), MessengerBot::tr("Database is not available."));
        webBot->sendAuthorizationResponse(requestId, response);
        return;
    }

    const QString userId = payload.value(QStringLiteral("user_id")).toString().trimmed();
    if (userId.isEmpty()) {
        response.insert(QStringLiteral("ok"), false);
        response.insert(QStringLiteral("error"), MessengerBot::tr("User identifier is empty."));
        webBot->sendAuthorizationResponse(requestId, response);
        return;
    }

    const MessengerUserKey userKey(MessengerType::Web, userId);
    const UserModel *model = database->userModel();
    if (model && model->userByKey(userKey).isValid()) {
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("access_active"), true);
        webBot->sendAuthorizationResponse(requestId, response);
        return;
    }

    PendingAccessUserEntry entry(userKey);
    entry.setLastMessageAtUtc(QDateTime::currentDateTimeUtc());
    entry.setFirstName(payload.value(QStringLiteral("first_name")).toString());
    entry.setLastName(payload.value(QStringLiteral("last_name")).toString());

    if (entry.firstName().isEmpty())
        entry.setFirstName(QStringLiteral("web:%1").arg(userId));

    const ServerDatabaseError error = database->recordPendingAccessUser(entry, entry.user());
    if (error) {
        response.insert(QStringLiteral("ok"), false);
        response.insert(QStringLiteral("error"), error.errorString());
        webBot->sendAuthorizationResponse(requestId, response);
        return;
    }

    response.insert(QStringLiteral("ok"), true);
    response.insert(QStringLiteral("access_active"), false);
    webBot->sendAuthorizationResponse(requestId, response);
}

void MessengerBotPrivate::handleWebUserSettingsRequest(WebBot *webBot,
                                                       const QString &requestId,
                                                       const QString &action,
                                                       const QJsonObject &payload)
{
    QJsonObject response;
    if (!database || !database->isReady()) {
        response.insert(QStringLiteral("ok"), false);
        response.insert(QStringLiteral("error"), MessengerBot::tr("Database is not available."));
        webBot->sendUserSettingsResponse(requestId, response);
        return;
    }

    const QString userId = payload.value(QStringLiteral("user_id")).toString().trimmed();
    if (userId.isEmpty()) {
        response.insert(QStringLiteral("ok"), false);
        response.insert(QStringLiteral("error"), MessengerBot::tr("User identifier is empty."));
        webBot->sendUserSettingsResponse(requestId, response);
        return;
    }

    const MessengerUserKey userKey(MessengerType::Web, userId);
    const QString reportedLocale =
            payload.value(QStringLiteral("reported_locale")).toString().trimmed();
    const UserModel *model = database->userModel();
    const User current = model ? model->userByKey(userKey) : User();
    if (!current.isValid()) {
        response.insert(QStringLiteral("ok"), action == QLatin1String("user_settings_get"));
        response.insert(QStringLiteral("access_active"), false);
        response.insert(QStringLiteral("effective_locale"),
                        primaryLocaleForDefault(reportedLocale));
        if (action == QLatin1String("user_settings_get")) {
            QJsonObject settings;
            settings.insert(QStringLiteral("locale"), QString());
            settings.insert(QStringLiteral("timezone"), QString());
            settings.insert(QStringLiteral("occupation_notify"), QStringLiteral("mine"));
            response.insert(QStringLiteral("settings"), settings);
        } else {
            response.insert(QStringLiteral("error"), MessengerBot::tr("Access is not activated."));
        }
        webBot->sendUserSettingsResponse(requestId, response);
        return;
    }

    if (action == QLatin1String("user_settings_get")) {
        webBot->sendUserSettingsResponse(
                requestId,
                userSettingsToJson(current, effectiveLocaleForUser(userKey, reportedLocale)));
        return;
    }

    if (action != QLatin1String("user_settings_set")) {
        response.insert(QStringLiteral("ok"), false);
        response.insert(QStringLiteral("error"),
                        MessengerBot::tr("Unsupported web user settings action."));
        webBot->sendUserSettingsResponse(requestId, response);
        return;
    }

    User updated = current;
    const QJsonObject settings = payload.value(QStringLiteral("settings")).toObject();
    QString parseError;
    if (settings.contains(QStringLiteral("locale"))) {
        QString locale;
        if (!localeFromSettingsValue(settings.value(QStringLiteral("locale")).toString(),
                                     &locale,
                                     &parseError)) {
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), parseError);
            webBot->sendUserSettingsResponse(requestId, response);
            return;
        }
        updated.setLocale(locale);
    }
    if (settings.contains(QStringLiteral("timezone"))) {
        const QString timezoneValue =
                settings.value(QStringLiteral("timezone")).toString().trimmed();
        UserTimeZone timezone;
        if (!timezoneValue.isEmpty()
            && !ServerDateTimeFormat::parseTimeZoneValue(timezoneValue, &timezone)) {
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), MessengerBot::tr("Invalid time zone value."));
            webBot->sendUserSettingsResponse(requestId, response);
            return;
        }
        updated.setTimezone(timezone);
    }
    if (settings.contains(QStringLiteral("occupation_notify"))) {
        User::OccupationNotifyMode notifyMode = User::OccupationNotifyMode::OwnResourcesOnly;
        if (!occupationNotifyModeFromString(settings.value(QStringLiteral("occupation_notify"))
                                                    .toString(),
                                            &notifyMode,
                                            &parseError)) {
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), parseError);
            webBot->sendUserSettingsResponse(requestId, response);
            return;
        }
        updated.setOccupationNotifyMode(notifyMode);
    }

    const ServerDatabaseError error = database->updateUser(updated, userKey);
    if (error) {
        response.insert(QStringLiteral("ok"), false);
        response.insert(QStringLiteral("error"), error.errorString());
        webBot->sendUserSettingsResponse(requestId, response);
        return;
    }

    webBot->sendUserSettingsResponse(requestId,
                                     userSettingsToJson(updated,
                                                        effectiveLocaleForUser(userKey,
                                                                               reportedLocale)));
}

void MessengerBotPrivate::sendReply(const AbstractBot::ChatAddress &chat,
                                    const QString &text,
                                    AbstractBot::MessageFormat format) const
{
    AbstractBot::MessageOptions options;
    options.format = format;
    sendReply(chat, text, options);
}

void MessengerBotPrivate::sendReply(const AbstractBot::ChatAddress &chat,
                                    const QString &text,
                                    const AbstractBot::MessageOptions &options) const
{
    if (!bot || !chat.isValid())
        return;

    AbstractBot::MessageOptions resolvedOptions = options;
    AbstractBot::MessageFormat resolvedFormat = resolvedOptions.format;
    const AbstractBot::Capabilities caps = bot->capabilities();
    if (resolvedFormat == AbstractBot::MessageFormat::Html
        && !(caps.testFlag(AbstractBot::CapabilityFlag::HtmlFormatting))) {
        resolvedFormat = AbstractBot::MessageFormat::PlainText;
    } else if (resolvedFormat == AbstractBot::MessageFormat::Markdown
               && !(caps.testFlag(AbstractBot::CapabilityFlag::MarkdownFormatting))) {
        resolvedFormat = AbstractBot::MessageFormat::PlainText;
    }

    resolvedOptions.format = resolvedFormat;
    if (!resolvedOptions.inlineKeyboard.isEmpty()
        && !(caps.testFlag(AbstractBot::CapabilityFlag::InlineKeyboards))) {
        resolvedOptions.inlineKeyboard.clear();
    }
    if (resolvedOptions.threadId.isEmpty())
        resolvedOptions.threadId = chat.threadId;

    bot->sendMessage(chat, text, resolvedOptions);
}

void MessengerBotPrivate::editReply(const AbstractBot::ChatAddress &chat,
                                    const QString &messageId,
                                    const QString &text,
                                    AbstractBot::MessageFormat format) const
{
    AbstractBot::MessageOptions options;
    options.format = format;
    editReply(chat, messageId, text, options);
}

void MessengerBotPrivate::editReply(const AbstractBot::ChatAddress &chat,
                                    const QString &messageId,
                                    const QString &text,
                                    const AbstractBot::MessageOptions &options) const
{
    if (!bot || !chat.isValid())
        return;

    const QString trimmedMessageId = messageId.trimmed();
    if (trimmedMessageId.isEmpty())
        return;

    AbstractBot::MessageOptions resolvedOptions = options;
    AbstractBot::MessageFormat resolvedFormat = resolvedOptions.format;
    const AbstractBot::Capabilities caps = bot->capabilities();
    if (resolvedFormat == AbstractBot::MessageFormat::Html
        && !(caps.testFlag(AbstractBot::CapabilityFlag::HtmlFormatting))) {
        resolvedFormat = AbstractBot::MessageFormat::PlainText;
    } else if (resolvedFormat == AbstractBot::MessageFormat::Markdown
               && !(caps.testFlag(AbstractBot::CapabilityFlag::MarkdownFormatting))) {
        resolvedFormat = AbstractBot::MessageFormat::PlainText;
    }

    resolvedOptions.format = resolvedFormat;
    if (!resolvedOptions.inlineKeyboard.isEmpty()
        && !(caps.testFlag(AbstractBot::CapabilityFlag::InlineKeyboards))) {
        resolvedOptions.inlineKeyboard.clear();
    }
    if (resolvedOptions.threadId.isEmpty())
        resolvedOptions.threadId = chat.threadId;

    bot->editMessage(chat, trimmedMessageId, text, resolvedOptions);
}

bool MessengerBotPrivate::ensureDatabase(const AbstractBot::ChatAddress &chat) const
{
    if (!database || !database->isReady()) {
        sendReply(chat, MessengerBot::tr("Database is not available right now."));
        return false;
    }
    return true;
}

void MessengerBotPrivate::rememberMessengerLocale(const MessengerUserKey &user,
                                                  const QString &locale)
{
    const QString trimmedLocale = locale.trimmed();
    if (trimmedLocale.isEmpty())
        return;

    if (!user.isValid())
        return;

    const QString existingLocale = lastMessengerLocales.value(user);
    if (existingLocale == trimmedLocale)
        return;

    lastMessengerLocales.insert(user, trimmedLocale);
}

QString MessengerBotPrivate::localeOverrideForUser(const MessengerUserKey &user) const
{
    if (!database)
        return QString();

    if (!user.isValid())
        return QString();

    const UserModel *model = database->userModel();
    if (!model)
        return QString();

    const User userEntry = model->userByKey(user);
    return userEntry.locale().trimmed();
}

UserTimeZone MessengerBotPrivate::timeZoneOverrideForUser(const MessengerUserKey &user) const
{
    if (!database)
        return UserTimeZone();

    if (!user.isValid())
        return UserTimeZone();

    const UserModel *model = database->userModel();
    if (!model)
        return UserTimeZone();

    const User userEntry = model->userByKey(user);
    if (!userEntry.isValid())
        return UserTimeZone();

    return userEntry.timezone();
}

QString MessengerBotPrivate::cachedMessengerLocale(const MessengerUserKey &user) const
{
    if (!user.isValid())
        return QString();
    return lastMessengerLocales.value(user);
}

QString MessengerBotPrivate::effectiveLocaleForUser(const MessengerUserKey &user,
                                                    const QString &reportedLocale) const
{
    const QString overrideLocale = localeOverrideForUser(user);
    if (!overrideLocale.isEmpty())
        return overrideLocale;

    const QString trimmedReported = reportedLocale.trimmed();
    if (!trimmedReported.isEmpty())
        return primaryLocaleForDefault(trimmedReported);

    return primaryLocaleForDefault(cachedMessengerLocale(user));
}

BookingResourcesTranslator::ScopedLocaleOverride MessengerBotPrivate::applyScopedLocale(
        const QString &locale)
{
    if (!translator)
        return BookingResourcesTranslator::ScopedLocaleOverride();

    const QString trimmed = locale.trimmed();
    if (trimmed.isEmpty())
        return BookingResourcesTranslator::ScopedLocaleOverride();

    return translator->useLocale(trimmed);
}

QString MessengerBotPrivate::buildBlockedUserReply(const BlacklistEntry &entry,
                                                   const MessengerUserKey &targetUser) const
{
    const QString dateTimeFormat = ServerDateTimeFormat::effectiveFormat(database);
    const UserTimeZone timeZone = timeZoneOverrideForUser(targetUser);
    QStringList lines;
    lines << MessengerBot::tr("Your account is blocked.");
    lines << MessengerBot::tr("Status: %1")
                     .arg(MessengerTextRender::describeBlacklistWindow(entry,
                                                                       dateTimeFormat,
                                                                       timeZone));

    const QString reason = MessengerTextRender::collapseWhitespace(entry.reason());
    if (!reason.isEmpty()) {
        lines << MessengerBot::tr("Reason: %1").arg(reason);
    }

    return lines.join(QStringLiteral("\n"));
}

void MessengerBotPrivate::sendDirectMessage(const MessengerUserKey &user,
                                            const QString &text,
                                            AbstractBot::MessageFormat format) const
{
    sendDirectMessage(directBackendForUser(user), user, text, format);
}

void MessengerBotPrivate::sendDirectMessage(AbstractBot *backend,
                                            const MessengerUserKey &user,
                                            const QString &text,
                                            AbstractBot::MessageFormat format) const
{
    const QString &userId = user.userId();
    if (!backend || userId.isEmpty())
        return;

    AbstractBot::ChatAddress chat;
    chat.messenger = user.messenger();
    chat.chatId = userId;
    if (!chat.isValid())
        return;

    AbstractBot::MessageOptions options;
    options.format = supportsDirectMessageFormat(user, format)
                             ? format
                             : AbstractBot::MessageFormat::PlainText;
    backend->sendMessage(chat, text, options);
}

bool MessengerBotPrivate::supportsDirectMessageFormat(const MessengerUserKey &user,
                                                      AbstractBot::MessageFormat format) const
{
    if (format == AbstractBot::MessageFormat::PlainText)
        return true;

    AbstractBot *backend = directBackendForUser(user);
    if (!backend)
        return false;

    const AbstractBot::Capabilities caps = backend->capabilities();
    if (format == AbstractBot::MessageFormat::Html)
        return caps.testFlag(AbstractBot::CapabilityFlag::HtmlFormatting);
    if (format == AbstractBot::MessageFormat::Markdown)
        return caps.testFlag(AbstractBot::CapabilityFlag::MarkdownFormatting);

    return false;
}

AbstractBot *MessengerBotPrivate::directBackendForUser(const MessengerUserKey &user) const
{
    if (!user.isValid())
        return nullptr;

    if (poll) {
        AbstractBot *backend = poll->backendBot(user.messenger());
        if (backend)
            return backend;
    }

    if (botType == user.messenger())
        return bot;

    return nullptr;
}

void MessengerBotPrivate::configureCommands() const
{
    if (bot) {
        bot->setCommandManager(commandManager);
    }
    if (auto *localCli = qobject_cast<LocalCliBot *>(bot)) {
        localCli->setCompletionHandler([this](const QString &text,
                                              int cursor,
                                              const QString &userId) {
            const MessengerUserKey user(MessengerType::LocalCli, userId);
            const MessengerCommandCompletions::Result result =
                    MessengerCommandCompletions::complete(commandManager, user, text, cursor, true);
            LocalCliProtocol::CompletionResult out;
            out.replaceFrom = result.replaceFrom;
            out.replaceTo = result.replaceTo;
            out.values = result.values;
            out.appendSpace = result.appendSpace;
            return out;
        });
    }
}

bool MessengerBotPrivate::statisticsCommandEnabled() const
{
    bool statsEnabled =
            ServerRuntimeSettings::defaultValue(ServerRuntimeSettings::Setting::StatsEnabled)
                    .toBool();
    bool statsAdminsOnly =
            ServerRuntimeSettings::defaultValue(ServerRuntimeSettings::Setting::StatsAdminsOnly)
                    .toBool();
    if (database && database->isReady()) {
        statsEnabled =
                database->serverSettingValue(ServerRuntimeSettings::Setting::StatsEnabled).toBool();
        statsAdminsOnly =
                database->serverSettingValue(ServerRuntimeSettings::Setting::StatsAdminsOnly)
                        .toBool();
    }

    return statsEnabled && !statsAdminsOnly;
}

void MessengerBotPrivate::syncCommandsVisibility()
{
    if (!bot || !commandManager)
        return;

    const bool enabled = statisticsCommandEnabled();
    if (statsCommandEnabled == enabled)
        return;

    statsCommandEnabled = enabled;
    configureCommands();
}

QString MessengerBotPrivate::resolveResourceName(const QString &resourceName,
                                                 const ResourceModel *resourceModel) const
{
    const QString trimmed = resourceName.trimmed();
    if (trimmed.isEmpty())
        return trimmed;
    if (!resourceModel)
        return trimmed;
    const Resource resource = resourceModel->resourceByName(trimmed);
    return resource.isValid() ? resource.name() : trimmed;
}

QString MessengerBotPrivate::displayNameForKey(const UserModel *model,
                                               const MessengerUserKey &key) const
{
    if (!key.isValid())
        return MessengerBot::tr("unknown", "Placeholder for missing user");
    if (model) {
        const User user = model->userByKey(key);
        if (user.isValid())
            return MessengerTextRender::formatUserDisplayName(user, false);
    }
    return key.toString();
}

void MessengerBotPrivate::sendOccupationNotification(
        ServerDatabase::OccupationNotificationType type,
        const QString &resourceName,
        const MessengerUserKey &releasedBy,
        const MessengerUserKey &occupiedBy,
        const MessengerUserKey &initiator,
        int durationMinutes)
{
    if (!database || !database->isReady())
        return;

    const UserModel *userModel = database->userModel();
    if (!userModel)
        return;

    const QList<User> users = userModel->items();
    if (users.isEmpty())
        return;

    const ResourceModel *resourceModel = database->resourceModel();
    const QString normalizedResource = resolveResourceName(resourceName, resourceModel);
    const Resource resource =
            resourceModel ? resourceModel->resourceByName(normalizedResource) : Resource();

    for (const User &target : users) {
        const MessengerUserKey targetKey = target.key();
        if (!targetKey.isValid())
            continue;
        if (targetKey.messenger() != botType)
            continue;
        if (initiator.isValid() && targetKey == initiator)
            continue;

        const User::OccupationNotifyMode notifyMode = target.occupationNotifyMode();
        if (notifyMode == User::OccupationNotifyMode::Disabled)
            continue;

        if (resource.isValid()
            && !MessengerTextRender::isResourceVisibleToViewer(resource,
                                                               target,
                                                               target.isAdmin())) {
            continue;
        }

        if (notifyMode == User::OccupationNotifyMode::OwnResourcesOnly) {
            bool involved = false;
            switch (type) {
            case ServerDatabase::OccupationNotificationType::Occupied:
                involved = (targetKey == occupiedBy);
                break;
            case ServerDatabase::OccupationNotificationType::Handover:
                involved = (targetKey == occupiedBy || targetKey == releasedBy);
                break;
            case ServerDatabase::OccupationNotificationType::Released:
                involved = (targetKey == releasedBy);
                break;
            }
            if (!involved)
                continue;
        }

        const QString targetLocale = effectiveLocaleForUser(targetKey, QString());
        BookingResourcesTranslator::ScopedLocaleOverride guard = applyScopedLocale(targetLocale);
        Q_UNUSED(guard);

        QString text;
        switch (type) {
        case ServerDatabase::OccupationNotificationType::Occupied: {
            const QString occupiedName = displayNameForKey(userModel, occupiedBy);
            const QString durationLabel =
                    MessengerTextRender::formatDurationMinutes(durationMinutes);
            text = MessengerBot::tr("Resource '%1' occupied by %2 for %3.")
                           .arg(normalizedResource, occupiedName, durationLabel);
            break;
        }
        case ServerDatabase::OccupationNotificationType::Handover: {
            const QString releasedName = displayNameForKey(userModel, releasedBy);
            const QString occupiedName = displayNameForKey(userModel, occupiedBy);
            const QString durationLabel =
                    MessengerTextRender::formatDurationMinutes(durationMinutes);
            text = MessengerBot::tr(
                           "Resource '%1' handed over: %2 released it, %3 occupied for %4.")
                           .arg(normalizedResource, releasedName, occupiedName, durationLabel);
            break;
        }
        case ServerDatabase::OccupationNotificationType::Released: {
            const QString releasedName = displayNameForKey(userModel, releasedBy);
            text = MessengerBot::tr("Resource '%1' released by %2.")
                           .arg(normalizedResource, releasedName);
            break;
        }
        }

        sendDirectMessage(targetKey, text);
    }
}

MessengerBot::MessengerBot(MessengerType type,
                           const QString &token,
                           ServerDatabase *database,
                           BookingResourcesTranslator *translator,
                           QObject *parent,
                           MessengerBotPoll *poll)
    : QObject(parent)
    , d(new MessengerBotPrivate(this))
{
    d->botType = type;
    d->token = token;
    d->database = database;
    d->translator = translator;
    d->poll = poll;

    d->limiter = new MessengerLimiter(d->database, this);

    d->bot = MessengerBotPrivate::createBot(d->botType, d->token, this);
    if (d->bot) {
        if (WebBot *webBot = qobject_cast<WebBot *>(d->bot))
            webBot->setTranslator(d->translator);

        const AbstractBot::Capabilities caps = d->bot->capabilities();
        const bool supportsInlineMenus =
                caps.testFlag(AbstractBot::CapabilityFlag::InlineKeyboards);
        if (supportsInlineMenus)
            d->menuManager = new MessengerMenuManager(this);

        QObject::connect(d->bot,
                         &AbstractBot::messageReceived,
                         this,
                         &MessengerBot::handleIncomingMessage);

        if (supportsInlineMenus && d->menuManager) {
            QObject::connect(d->bot,
                             &AbstractBot::callbackQueryReceived,
                             this,
                             &MessengerBot::handleIncomingCallbackQuery);

            QObject::connect(d->bot,
                             &AbstractBot::messageSent,
                             d->menuManager,
                             &MessengerMenuManager::onMenuMessageSent);
        }

        QObject::connect(d->bot, &AbstractBot::errorOccurred, this, &MessengerBot::errorOccurred);

        if (WebBot *webBot = qobject_cast<WebBot *>(d->bot)) {
            QObject::connect(webBot,
                             &WebBot::userAuthorizationRequested,
                             this,
                             [this, webBot](const QString &requestId, const QJsonObject &payload) {
                                 d->handleWebAuthorizationRequest(webBot, requestId, payload);
                             });

            QObject::connect(webBot,
                             &WebBot::userSettingsRequested,
                             this,
                             [this, webBot](const QString &requestId,
                                            const QString &action,
                                            const QJsonObject &payload) {
                                 d->handleWebUserSettingsRequest(webBot,
                                                                 requestId,
                                                                 action,
                                                                 payload);
                             });

            if (d->database) {
                QObject::connect(d->database,
                                 &ServerDatabase::userChanged,
                                 this,
                                 [webBot](const MessengerUserKey &user) {
                                     webBot->sendCommandCacheInvalidated(user);
                                 });

                auto sendGlobalCommandCacheInvalidated = [webBot]() {
                    webBot->sendCommandCacheInvalidated();
                };
                QObject::connect(d->database,
                                 &ServerDatabase::serverSettingChanged,
                                 this,
                                 [sendGlobalCommandCacheInvalidated](ServerRuntimeSettings::Setting,
                                                                     const QVariant &) {
                                     sendGlobalCommandCacheInvalidated();
                                 });
                auto connectCommandCacheModelSignals =
                        [this, sendGlobalCommandCacheInvalidated](const QAbstractItemModel *model) {
                            if (!model)
                                return;
                            QObject::connect(model,
                                             &QAbstractItemModel::modelReset,
                                             this,
                                             sendGlobalCommandCacheInvalidated);
                            QObject::connect(model,
                                             &QAbstractItemModel::layoutChanged,
                                             this,
                                             sendGlobalCommandCacheInvalidated);
                            QObject::connect(model,
                                             &QAbstractItemModel::rowsInserted,
                                             this,
                                             sendGlobalCommandCacheInvalidated);
                            QObject::connect(model,
                                             &QAbstractItemModel::rowsRemoved,
                                             this,
                                             sendGlobalCommandCacheInvalidated);
                            QObject::connect(model,
                                             &QAbstractItemModel::dataChanged,
                                             this,
                                             sendGlobalCommandCacheInvalidated);
                        };
                connectCommandCacheModelSignals(d->database->resourceModel());
                connectCommandCacheModelSignals(d->database->userModel());
                connectCommandCacheModelSignals(d->database->occupationDurationModel());
                connectCommandCacheModelSignals(d->database->blacklistModel());
                connectCommandCacheModelSignals(d->database->pendingAccessUserModel());
            }
        }
    } else {
        qCWarning(lcMessengerBot).nospace()
                << metaObject()->className() << ":: Failed to initialize bot of type "
                << qPrintable(messengerTypeToString(d->botType)) << "!";
    }

    // Create commandManager before menuManager
    d->commandManager = new MessengerCommandManager(this);
    d->statsCommandEnabled = d->statisticsCommandEnabled();

    if (d->database) {
        QObject::connect(d->database,
                         &ServerDatabase::errorOccurred,
                         this,
                         &MessengerBot::errorOccurred);

        QObject::connect(d->database,
                         &ServerDatabase::serverSettingChanged,
                         this,
                         [this](ServerRuntimeSettings::Setting setting, const QVariant &) {
                             if (setting == ServerRuntimeSettings::Setting::StatsEnabled
                                 || setting == ServerRuntimeSettings::Setting::StatsAdminsOnly) {
                                 d->syncCommandsVisibility();
                             }
                         });

        if (const ResourceQueueModel *queueModel = d->database->resourceQueueModel()) {
            QObject::connect(
                    queueModel,
                    &ResourceQueueModel::occupationStarted,
                    this,
                    [this, queueModel](const QString &resourceName,
                                       const MessengerUserKey &user,
                                       bool scheduled,
                                       const QDateTime &handoverAtUtc) {
                        if (d->botType != user.messenger())
                            return;

                        const QString targetLocale = d->effectiveLocaleForUser(user, QString());
                        BookingResourcesTranslator::ScopedLocaleOverride targetGuard =
                                d->applyScopedLocale(targetLocale);
                        Q_UNUSED(targetGuard);

                        const ResourceModel *resourceModel =
                                d->database ? d->database->resourceModel() : nullptr;
                        const Resource resourceDetails =
                                resourceModel ? resourceModel->resourceByName(resourceName)
                                              : Resource();

                        QStringList lines;
                        if (scheduled) {
                            lines << tr("Scheduled time for '%1' has started.").arg(resourceName);
                        } else {
                            lines << tr("It's your turn to use '%1'.").arg(resourceName);
                        }

                        if (handoverAtUtc.isValid()) {
                            const QString dateTimeFormat =
                                    ServerDateTimeFormat::effectiveFormat(d->database);
                            const UserTimeZone targetTimeZone = d->timeZoneOverrideForUser(user);
                            const QString handoverLocal =
                                    MessengerTextRender::formatLocalDateTime(handoverAtUtc,
                                                                             QString(),
                                                                             dateTimeFormat,
                                                                             targetTimeZone);

                            if (scheduled) {
                                lines << tr("Scheduled until: %1").arg(handoverLocal);
                            } else {
                                const QDateTime nextScheduleStart =
                                        queueModel ? queueModel->nextScheduleStartUtc(resourceName)
                                                   : QDateTime();
                                if (nextScheduleStart.isValid()) {
                                    const QString scheduleLocal =
                                            MessengerTextRender::formatLocalDateTime(
                                                    nextScheduleStart,
                                                    QString(),
                                                    dateTimeFormat,
                                                    targetTimeZone);
                                    lines << tr("Attention: the resource will be "
                                                "handed over automatically due to a "
                                                "schedule at %1.")
                                                     .arg(scheduleLocal);
                                } else {
                                    lines << tr("Expected handover: %1").arg(handoverLocal);
                                }
                            }
                        }

                        const QString normalizedStart = MessengerTextRender::normalizeNewlines(
                                                                resourceDetails.startMessage())
                                                                .trimmed();
                        if (!normalizedStart.isEmpty()) {
                            lines << tr("Start instructions:");
                            lines << normalizedStart;
                        }

                        d->sendDirectMessage(user, lines.join(QStringLiteral("\n")));
                    });

            QObject::connect(
                    queueModel,
                    &ResourceQueueModel::occupationEnded,
                    this,
                    [this](const QString &resourceName,
                           const MessengerUserKey &user,
                           bool scheduled,
                           const QString &reason,
                           const QDateTime &endedAtUtc) {
                        if (d->botType != user.messenger())
                            return;
                        const bool preemptedBySchedule =
                                reason == QStringLiteral("preempted_by_schedule");
                        const bool scheduledEnded = scheduled && reason == QStringLiteral("ended");
                        if (!preemptedBySchedule && !scheduledEnded)
                            return;

                        const QString targetLocale = d->effectiveLocaleForUser(user, QString());
                        BookingResourcesTranslator::ScopedLocaleOverride targetGuard =
                                d->applyScopedLocale(targetLocale);
                        Q_UNUSED(targetGuard);

                        const QString endedLocal =
                                endedAtUtc.isValid()
                                        ? MessengerTextRender::formatLocalDateTime(
                                                  endedAtUtc,
                                                  QString(),
                                                  ServerDateTimeFormat::effectiveFormat(
                                                          d->database),
                                                  d->timeZoneOverrideForUser(user))
                                        : tr("(unknown)");
                        QString text;
                        if (preemptedBySchedule) {
                            text = tr("Your occupation of '%1' has ended because a scheduled "
                                      "reservation started.\nEnded at: %2")
                                           .arg(resourceName, endedLocal);
                        } else {
                            text = tr("Your occupation of '%1' has ended.\nEnded at: %2")
                                           .arg(resourceName, endedLocal);
                        }
                        d->sendDirectMessage(user, text);
                    });
        }

        QObject::connect(d->database,
                         &ServerDatabase::occupationNotification,
                         this,
                         [this](ServerDatabase::OccupationNotificationType type,
                                const QString &resourceName,
                                const MessengerUserKey &releasedBy,
                                const MessengerUserKey &occupiedBy,
                                const MessengerUserKey &initiator,
                                int durationMinutes) {
                             d->sendOccupationNotification(type,
                                                           resourceName,
                                                           releasedBy,
                                                           occupiedBy,
                                                           initiator,
                                                           durationMinutes);
                         });
    }

    d->configureCommands();
}

MessengerBot::~MessengerBot()
{
    delete d;
}

bool MessengerBot::isRunning() const
{
    return d->bot && d->bot->isRunning();
}

MessengerType MessengerBot::botType() const
{
    return d->botType;
}

ServerDatabase *MessengerBot::database() const
{
    return d->database;
}

AbstractBot *MessengerBot::backendBot() const
{
    return d->bot;
}

MessengerBotPoll *MessengerBot::poll() const
{
    return d->poll;
}

BookingResourcesTranslator *MessengerBot::translator() const
{
    return d->translator;
}

MessengerMenuManager *MessengerBot::menuManager() const
{
    return d->menuManager;
}

void MessengerBot::sendReply(const AbstractBot::ChatAddress &chat,
                             const QString &text,
                             AbstractBot::MessageFormat format) const
{
    AbstractBot::MessageOptions options;
    options.format = format;
    sendReply(chat, text, options);
}

void MessengerBot::sendReply(const AbstractBot::ChatAddress &chat,
                             const QString &text,
                             const AbstractBot::MessageOptions &options) const
{
    if (!d->bot || !chat.isValid())
        return;

    AbstractBot::MessageOptions resolvedOptions = options;
    AbstractBot::MessageFormat resolvedFormat = resolvedOptions.format;
    const AbstractBot::Capabilities caps = d->bot->capabilities();
    if (resolvedFormat == AbstractBot::MessageFormat::Html
        && !(caps.testFlag(AbstractBot::CapabilityFlag::HtmlFormatting))) {
        resolvedFormat = AbstractBot::MessageFormat::PlainText;
    } else if (resolvedFormat == AbstractBot::MessageFormat::Markdown
               && !(caps.testFlag(AbstractBot::CapabilityFlag::MarkdownFormatting))) {
        resolvedFormat = AbstractBot::MessageFormat::PlainText;
    }

    resolvedOptions.format = resolvedFormat;
    if (!resolvedOptions.inlineKeyboard.isEmpty()
        && !(caps.testFlag(AbstractBot::CapabilityFlag::InlineKeyboards))) {
        resolvedOptions.inlineKeyboard.clear();
    }
    if (resolvedOptions.threadId.isEmpty())
        resolvedOptions.threadId = chat.threadId;

    d->bot->sendMessage(chat, text, resolvedOptions);
}

void MessengerBot::sendFile(const AbstractBot::ChatAddress &chat,
                            const QString &filePath,
                            const QString &caption,
                            AbstractBot::MessageFormat captionFormat) const
{
    if (!d->bot || !chat.isValid())
        return;

    AbstractBot::MessageFormat resolvedFormat = captionFormat;
    const AbstractBot::Capabilities caps = d->bot->capabilities();
    if (resolvedFormat == AbstractBot::MessageFormat::Html
        && !(caps.testFlag(AbstractBot::CapabilityFlag::HtmlFormatting))) {
        resolvedFormat = AbstractBot::MessageFormat::PlainText;
    } else if (resolvedFormat == AbstractBot::MessageFormat::Markdown
               && !(caps.testFlag(AbstractBot::CapabilityFlag::MarkdownFormatting))) {
        resolvedFormat = AbstractBot::MessageFormat::PlainText;
    }

    d->bot->sendFile(chat, filePath, caption, resolvedFormat);
}

void MessengerBot::editReply(const AbstractBot::ChatAddress &chat,
                             const QString &messageId,
                             const QString &text,
                             AbstractBot::MessageFormat format) const
{
    AbstractBot::MessageOptions options;
    options.format = format;
    editReply(chat, messageId, text, options);
}

void MessengerBot::editReply(const AbstractBot::ChatAddress &chat,
                             const QString &messageId,
                             const QString &text,
                             const AbstractBot::MessageOptions &options) const
{
    if (!d->bot || !chat.isValid())
        return;

    const QString trimmedMessageId = messageId.trimmed();
    if (trimmedMessageId.isEmpty())
        return;

    AbstractBot::MessageOptions resolvedOptions = options;
    AbstractBot::MessageFormat resolvedFormat = resolvedOptions.format;
    const AbstractBot::Capabilities caps = d->bot->capabilities();
    if (resolvedFormat == AbstractBot::MessageFormat::Html
        && !(caps.testFlag(AbstractBot::CapabilityFlag::HtmlFormatting))) {
        resolvedFormat = AbstractBot::MessageFormat::PlainText;
    } else if (resolvedFormat == AbstractBot::MessageFormat::Markdown
               && !(caps.testFlag(AbstractBot::CapabilityFlag::MarkdownFormatting))) {
        resolvedFormat = AbstractBot::MessageFormat::PlainText;
    }

    resolvedOptions.format = resolvedFormat;
    if (!resolvedOptions.inlineKeyboard.isEmpty()
        && !(caps.testFlag(AbstractBot::CapabilityFlag::InlineKeyboards))) {
        resolvedOptions.inlineKeyboard.clear();
    }
    if (resolvedOptions.threadId.isEmpty())
        resolvedOptions.threadId = chat.threadId;

    d->bot->editMessage(chat, trimmedMessageId, text, resolvedOptions);
}

bool MessengerBot::ensureDatabase(const AbstractBot::ChatAddress &chat) const
{
    if (!d->database || !d->database->isReady()) {
        sendReply(chat, tr("Database is not available right now."));
        return false;
    }
    return true;
}

BookingResourcesTranslator::ScopedLocaleOverride MessengerBot::applyScopedLocale(
        const QString &locale) const
{
    if (!d->translator)
        return BookingResourcesTranslator::ScopedLocaleOverride();

    const QString trimmed = locale.trimmed();
    if (trimmed.isEmpty())
        return BookingResourcesTranslator::ScopedLocaleOverride();

    return d->translator->useLocale(trimmed);
}

QString MessengerBot::effectiveLocaleForUser(const MessengerUserKey &user,
                                             const QString &reportedLocale) const
{
    return d->effectiveLocaleForUser(user, reportedLocale);
}

UserTimeZone MessengerBot::effectiveTimeZoneForUser(const MessengerUserKey &user) const
{
    return d->timeZoneOverrideForUser(user);
}

void MessengerBot::sendDirectMessage(const MessengerUserKey &user,
                                     const QString &text,
                                     AbstractBot::MessageFormat format) const
{
    d->sendDirectMessage(user, text, format);
}

bool MessengerBot::supportsDirectMessageFormat(const MessengerUserKey &user,
                                               AbstractBot::MessageFormat format) const
{
    return d->supportsDirectMessageFormat(user, format);
}

void MessengerBot::notifyAccessActivated(const MessengerUserKey &user) const
{
    if (!user.isValid())
        return;

    const QString targetLocale = d->effectiveLocaleForUser(user, QString());
    BookingResourcesTranslator::ScopedLocaleOverride guard = d->applyScopedLocale(targetLocale);
    Q_UNUSED(guard);

    d->sendDirectMessage(d->directBackendForUser(user),
                         user,
                         tr("Access has been activated. You can now use the bot. "
                            "Send /start to begin with commands."));
}

void MessengerBot::notifyNextUserQueueStatus(const QString &resourceName,
                                             const MessengerUserKey &actor) const
{
    if (!d->database || !d->database->isReady())
        return;

    QString normalizedStart;
    if (const ResourceModel *resourceModel = d->database->resourceModel()) {
        const Resource resourceDetails = resourceModel->resourceByName(resourceName);
        normalizedStart =
                MessengerTextRender::normalizeNewlines(resourceDetails.startMessage()).trimmed();
    }

    const ResourceQueueModel *queueModel = d->database->resourceQueueModel();
    if (!queueModel)
        return;

    const QList<ResourceQueueEntry> queue = queueModel->queueForResource(resourceName);
    for (const ResourceQueueEntry &entry : queue) {
        const MessengerUserKey targetUser = entry.user();
        if (!targetUser.isValid() || targetUser == actor)
            continue;
        if (targetUser.messenger() != botType())
            continue;

        AbstractBot::ChatAddress directChat;
        directChat.messenger = targetUser.messenger();
        directChat.chatId = targetUser.userId();
        if (!directChat.isValid())
            continue;

        const QString targetLocale = d->effectiveLocaleForUser(targetUser, QString());
        BookingResourcesTranslator::ScopedLocaleOverride targetGuard =
                d->applyScopedLocale(targetLocale);
        Q_UNUSED(targetGuard);

        MessengerTextRender::RenderedText rendered =
                MessengerTextRender::renderResourceQueueStatus(d->database,
                                                               directChat.messenger,
                                                               resourceName,
                                                               targetUser,
                                                               backendBot());
        if (!normalizedStart.isEmpty()) {
            const QString label = tr("Start instructions:");
            if (rendered.format == AbstractBot::MessageFormat::Html) {
                const QString htmlBlock =
                        QStringLiteral("<b>%1</b>\n<pre><code>%2</code></pre>")
                                .arg(label.toHtmlEscaped(), normalizedStart.toHtmlEscaped());
                rendered.text = QStringLiteral("%1\n\n%2").arg(rendered.text.trimmed(), htmlBlock);
            } else {
                const QString plainBlock = QStringLiteral("%1\n%2").arg(label, normalizedStart);
                rendered.text = QStringLiteral("%1\n\n%2").arg(rendered.text.trimmed(), plainBlock);
            }
        }
        sendReply(directChat, rendered.text, rendered.format);
        break;
    }
}

bool MessengerBot::isAdminUser(const MessengerUserKey &user) const
{
    if (user.messenger() == MessengerType::LocalCli)
        return true;

    if (!d->database)
        return false;

    if (!user.isValid())
        return false;

    const UserModel *model = d->database->userModel();
    if (!model)
        return false;

    const User userEntry = model->userByKey(user);
    return userEntry.isValid() && userEntry.isAdmin();
}

bool MessengerBot::canManageServerSettings(const MessengerUserKey &user) const
{
    if (!d->database)
        return false;

    return d->database->canManageServerSettings(user);
}

bool MessengerBot::hasActiveAccess(const MessengerUserKey &user) const
{
    if (user.messenger() == MessengerType::LocalCli)
        return true;

    if (!d->database)
        return false;

    if (!user.isValid())
        return false;

    const UserModel *model = d->database->userModel();
    if (!model)
        return false;

    return model->exists(user);
}

void MessengerBot::start()
{
    if (!d->bot)
        return;

    d->bot->start();
}

void MessengerBot::stop()
{
    if (!d->bot)
        return;

    d->bot->stop();
}

void MessengerBot::replaceBackendBot(AbstractBot *bot)
{
    if (d->bot == bot)
        return;

    if (d->bot) {
        if (d->menuManager)
            d->bot->disconnect(d->menuManager);
        d->bot->disconnect(this);
        delete d->bot;
    }

    d->bot = bot;
    if (!d->bot)
        return;

    d->bot->setParent(this);

    const AbstractBot::Capabilities caps = d->bot->capabilities();
    const bool supportsInlineMenus = caps.testFlag(AbstractBot::CapabilityFlag::InlineKeyboards);
    if (supportsInlineMenus && !d->menuManager) {
        d->menuManager = new MessengerMenuManager(this);
    } else if (!supportsInlineMenus && d->menuManager) {
        delete d->menuManager;
        d->menuManager = nullptr;
    }

    QObject::connect(d->bot,
                     &AbstractBot::messageReceived,
                     this,
                     &MessengerBot::handleIncomingMessage);

    if (supportsInlineMenus && d->menuManager) {
        QObject::connect(d->bot,
                         &AbstractBot::callbackQueryReceived,
                         this,
                         &MessengerBot::handleIncomingCallbackQuery);
        QObject::connect(d->bot,
                         &AbstractBot::messageSent,
                         d->menuManager,
                         &MessengerMenuManager::onMenuMessageSent);
    }

    QObject::connect(d->bot, &AbstractBot::errorOccurred, this, &MessengerBot::errorOccurred);
    d->configureCommands();
}

void MessengerBot::handleIncomingMessage(const AbstractBot::IncomingMessage &message)
{
    const MessengerUserKey senderKey = AbstractBot::senderKey(message);

    qCDebug(lcMessengerBot).nospace()
            << "Incoming message from \"" << qPrintable(senderKey.toString())
            << "\" chatId=" << message.chat.chatId << " : " << message.text;

    const QString reportedLocale = message.languageCode.trimmed();
    if (!reportedLocale.isEmpty())
        d->rememberMessengerLocale(senderKey, reportedLocale);

    const QString effectiveLocale = d->effectiveLocaleForUser(senderKey, reportedLocale);

    BookingResourcesTranslator::ScopedLocaleOverride localeGuard =
            d->applyScopedLocale(effectiveLocale);
    Q_UNUSED(localeGuard);

    processMessage(message);
}

void MessengerBot::handleIncomingCallbackQuery(const AbstractBot::CallbackQuery &query)
{
    if (!query.isValid())
        return;

    const MessengerUserKey senderKey = AbstractBot::senderKey(query);

    qCDebug(lcMessengerBot).nospace()
            << "Incoming callback query from \"" << qPrintable(senderKey.toString())
            << "\" chatId=" << query.chat.chatId << " : " << query.data;

    const QString reportedLocale = query.languageCode.trimmed();
    if (!reportedLocale.isEmpty())
        d->rememberMessengerLocale(senderKey, reportedLocale);

    const QString effectiveLocale = d->effectiveLocaleForUser(senderKey, reportedLocale);

    BookingResourcesTranslator::ScopedLocaleOverride localeGuard =
            d->applyScopedLocale(effectiveLocale);
    Q_UNUSED(localeGuard);

    if (!applyUserAccessPolicy(senderKey, query.chat, query.sender))
        return;

    if (d->menuManager)
        d->menuManager->processCallbackQuery(query);
}

bool MessengerBot::applyUserAccessPolicy(const MessengerUserKey &senderKey,
                                         const AbstractBot::ChatAddress &chat,
                                         const AbstractBot::UserAddress &sender)
{
    if (!d->limiter)
        return true;

    const MessengerLimiter::AccessDecision decision = d->limiter->evaluate(senderKey, sender);
    if (decision.allow)
        return true;

    if (decision.showBlockedUserReply && decision.blacklistEntry.isValid()) {
        d->sendReply(chat, d->buildBlockedUserReply(decision.blacklistEntry, senderKey));
        return false;
    }

    if (decision.shouldReply && !decision.replyText.trimmed().isEmpty()) {
        d->sendReply(chat, decision.replyText);
        return false;
    }

    return false;
}

void MessengerBot::processMessage(const AbstractBot::IncomingMessage &message)
{
    const bool isLocalCli = message.chat.messenger == MessengerType::LocalCli;
    const MessengerUserKey senderKey = AbstractBot::senderKey(message);

    if (!applyUserAccessPolicy(senderKey, message.chat, message.sender))
        return;

    // If the message starts with '/', treat it as a command.
    if (d->commandManager->handleCommandMessage(message))
        return;

    if (isLocalCli) {
        d->sendReply(message.chat,
                     tr("Local CLI accepts admin slash commands only.\n"
                        "Use /help for admin commands or \\help for CLI help."));
        return;
    }

    if (d->menuManager && d->menuManager->processTextMessage(message)) {
        return;
    }

    // Non-command messages: simple echo behavior with a gentle hint.
    QStringList replyLines;
    replyLines << tr("You wrote: %1").arg(message.text);
    replyLines << tr("Need help? Send /help to see available commands.");
    d->sendReply(message.chat, replyLines.join(QStringLiteral("\n")));
}
