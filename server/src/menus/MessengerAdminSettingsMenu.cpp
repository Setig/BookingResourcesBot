#include "MessengerAdminSettingsMenu.h"

#include "MessengerMenuIds.h"

#include "MessengerBot.h"
#include "ServerDatabase.h"
#include "ServerSettingsModel.h"
#include "ServerDateTimeFormat.h"
#include "ServerRuntimeSettings.h"
#include "MessengerUserFacingErrors.h"

#include <QVector>
#include <QStringList>

namespace {

using Setting = ServerRuntimeSettings::Setting;

struct PresetValue
{
    QString id;
    QVariant value;
    QString label;
};

QString settingDisplayName(const QString &key)
{
    const QString trimmedKey = key.trimmed();
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::MenuUpdateMode))
        return MessengerAdminSettingsMenu::tr("Menu update mode");
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::MenuSessionTimeoutMinutes))
        return MessengerAdminSettingsMenu::tr("Menu session timeout (minutes)");
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::MaxResourcesPerUser))
        return MessengerAdminSettingsMenu::tr("Max resources per user");
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::MaxOccupationDurationMinutes))
        return MessengerAdminSettingsMenu::tr("Max occupation duration (minutes)");
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::StatsEnabled))
        return MessengerAdminSettingsMenu::tr("Statistics collection");
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::StatsAdminsOnly))
        return MessengerAdminSettingsMenu::tr("Statistics access mode");
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::DateTimeFormat))
        return MessengerAdminSettingsMenu::tr("Date/time format");
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::PendingAccessUserReplyMessage))
        return MessengerAdminSettingsMenu::tr("Access activation reply");
    return trimmedKey;
}

bool defaultValueForKey(const QString &key, QVariant *valueOut)
{
    if (!valueOut)
        return false;

    const QString trimmedKey = key.trimmed();
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::MenuUpdateMode)) {
        *valueOut = ServerRuntimeSettings::defaultValue(Setting::MenuUpdateMode);
        return true;
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::MenuSessionTimeoutMinutes)) {
        *valueOut = ServerRuntimeSettings::defaultValue(Setting::MenuSessionTimeoutMinutes);
        return true;
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::MaxResourcesPerUser)) {
        *valueOut = ServerRuntimeSettings::defaultValue(Setting::MaxResourcesPerUser);
        return true;
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::MaxOccupationDurationMinutes)) {
        *valueOut = ServerRuntimeSettings::defaultValue(Setting::MaxOccupationDurationMinutes);
        return true;
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::StatsEnabled)) {
        *valueOut = ServerRuntimeSettings::defaultValue(Setting::StatsEnabled);
        return true;
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::StatsAdminsOnly)) {
        *valueOut = ServerRuntimeSettings::defaultValue(Setting::StatsAdminsOnly);
        return true;
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::DateTimeFormat)) {
        *valueOut = ServerRuntimeSettings::defaultValue(Setting::DateTimeFormat);
        return true;
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::PendingAccessUserReplyMessage)) {
        *valueOut = ServerRuntimeSettings::defaultValue(Setting::PendingAccessUserReplyMessage);
        return true;
    }
    return false;
}

QVector<PresetValue> presetValuesForKey(const QString &key)
{
    QVector<PresetValue> presets;
    const QString trimmedKey = key.trimmed();
    auto appendUIntPreset = [&presets](uint value) {
        PresetValue preset;
        preset.id = QString::number(value);
        preset.value = QVariant::fromValue(value);
        presets.append(preset);
    };
    auto appendBoolPreset = [&presets](const QString &id, bool value, const QString &label) {
        PresetValue preset;
        preset.id = id;
        preset.value = QVariant::fromValue(value);
        preset.label = label;
        presets.append(preset);
    };

    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::MenuUpdateMode)) {
        appendUIntPreset(0u);
        appendUIntPreset(1u);
        return presets;
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::MaxResourcesPerUser)) {
        for (uint value : { 0u, 1u, 2u, 3u, 5u, 10u }) {
            appendUIntPreset(value);
        }
        return presets;
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::MaxOccupationDurationMinutes)) {
        for (uint value : { 0u, 30u, 60u, 120u }) {
            appendUIntPreset(value);
        }
        return presets;
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::MenuSessionTimeoutMinutes)) {
        for (uint value : { 0u, 5u, 15u, 30u, 60u }) {
            appendUIntPreset(value);
        }
        return presets;
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::StatsEnabled)) {
        appendBoolPreset(QStringLiteral("0"), false, MessengerAdminSettingsMenu::tr("disabled"));
        appendBoolPreset(QStringLiteral("1"), true, MessengerAdminSettingsMenu::tr("enabled"));
        return presets;
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::StatsAdminsOnly)) {
        appendBoolPreset(QStringLiteral("all_users"),
                         false,
                         MessengerAdminSettingsMenu::tr("admins and users"));
        appendBoolPreset(QStringLiteral("admins_only"),
                         true,
                         MessengerAdminSettingsMenu::tr("admins only"));
        return presets;
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::DateTimeFormat)) {
        PresetValue autoPreset;
        autoPreset.id = QStringLiteral("auto");
        autoPreset.value = QVariant::fromValue(QString());
        autoPreset.label = MessengerAdminSettingsMenu::tr("auto (%1)")
                                   .arg(ServerDateTimeFormat::defaultLocalizedFormat());
        presets.append(autoPreset);

        auto appendFormatPreset = [&presets](const QString &id, const QString &format) {
            PresetValue preset;
            preset.id = id;
            preset.value = QVariant::fromValue(format);
            preset.label = format;
            presets.append(preset);
        };
        appendFormatPreset(QStringLiteral("iso_sec"), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        appendFormatPreset(QStringLiteral("iso_min"), QStringLiteral("yyyy-MM-dd HH:mm"));
        appendFormatPreset(QStringLiteral("ru_sec"), QStringLiteral("dd.MM.yyyy HH:mm:ss"));
        appendFormatPreset(QStringLiteral("ru_min"), QStringLiteral("dd.MM.yyyy HH:mm"));
        return presets;
    }
    return presets;
}

bool presetValueByIdForKey(const QString &key, const QString &presetId, QVariant *valueOut)
{
    if (!valueOut)
        return false;
    const QString normalizedId = presetId.trimmed();
    if (normalizedId.isEmpty())
        return false;

    const QVector<PresetValue> presets = presetValuesForKey(key);
    for (const PresetValue &preset : presets) {
        if (preset.id == normalizedId) {
            *valueOut = preset.value;
            return true;
        }
    }
    return false;
}

QString manualInputHintForKey(const QString &key)
{
    const QString trimmedKey = key.trimmed();
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::MenuUpdateMode))
        return MessengerAdminSettingsMenu::tr("Send 0/1 or active/passive.");
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::MenuSessionTimeoutMinutes)) {
        return MessengerAdminSettingsMenu::tr("Send a non-negative integer (0 = disabled).");
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::StatsEnabled)) {
        return MessengerAdminSettingsMenu::tr("Send 0/1 or enabled/disabled.");
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::StatsAdminsOnly)) {
        return MessengerAdminSettingsMenu::tr("Send 0/1 or admins_only/all_users.");
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::DateTimeFormat)) {
        return MessengerAdminSettingsMenu::tr(
                "Send date/time format like dd.MM.yyyy HH:mm:ss or yyyy-MM-dd HH:mm, or "
                "default/auto.");
    }
    if (ServerRuntimeSettings::isSettingKey(trimmedKey, Setting::PendingAccessUserReplyMessage)) {
        return MessengerAdminSettingsMenu::tr(
                "Send reply text for users awaiting access activation, or default/auto.");
    }
    return MessengerAdminSettingsMenu::tr("Send a non-negative integer (0 = unlimited).");
}

} // namespace

MessengerAdminSettingsMenu::MessengerAdminSettingsMenu(MessengerMenuManager *manager)
    : MessengerAbstractMenu(manager)
{
    if (manager) {
        QObject::connect(manager,
                         &MessengerMenuManager::sessionEnded,
                         this,
                         [this](const QString &sessionId) {
                             m_stateBySessionId.remove(sessionId);
                         });
    }
}

QString MessengerAdminSettingsMenu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuAdminSettings);
}

bool MessengerAdminSettingsMenu::adminsOnly() const
{
    return true;
}

MessengerMenu::AccessLevel MessengerAdminSettingsMenu::accessLevel() const
{
    return MessengerMenu::AccessLevel::ServerAdminsOnly;
}

QStringList MessengerAdminSettingsMenu::eventKeys() const
{
    return { QStringLiteral("as") };
}

MessengerMenu::MenuPage MessengerAdminSettingsMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuAdminSettings);
    page.adminsOnly = true;
    page.accessLevel = MessengerMenu::AccessLevel::ServerAdminsOnly;
    page.title = tr("Server settings");
    page.body = tr("Runtime server settings.");

    MessengerMenuManager *mgr = menuManager();
    MessengerBot *bot = mgr ? mgr->bot() : nullptr;
    const ServerDatabase *db = bot ? bot->database() : nullptr;
    const ServerSettingsModel *model = db ? db->serverSettingsModel() : nullptr;
    if (!mgr || !bot || !db || !model) {
        page.body = tr("Menu session is not available right now.");
        return page;
    }

    const QString sid = mgr->sessionId(context);
    SessionState *state = ensureState(sid);
    if (!state) {
        page.body = tr("Menu session is not available right now.");
        return page;
    }

    const QString currentMenuId = mgr->currentMenuId(context);
    if (currentMenuId.trimmed() != page.id) {
        state->reset();
    }

    mgr->setCurrentMenuId(context, page.id, false);

    bool showList = (state->view == SettingsViewMode::List);
    ServerSettingEntry selectedEntry;
    if (!showList) {
        const QString key = state->selectedKey.trimmed();
        if (key.isEmpty()) {
            state->errorText = tr("Select a setting.");
            state->view = SettingsViewMode::List;
            showList = true;
        } else {
            selectedEntry = model->entryByKey(key);
            if (!selectedEntry.isValid()) {
                state->errorText = tr("Unknown setting key: %1").arg(key);
                state->view = SettingsViewMode::List;
                state->selectedKey.clear();
                showList = true;
            }
        }
    }

    if (showList) {
        page.body = tr("Select a setting to view or change.");
        page.dynamicText = [model, mgr](const MessengerMenu::RenderContext &ctx) {
            Q_UNUSED(ctx);
            MessengerMenu::MenuPage::DynamicText out;
            out.format = AbstractBot::MessageFormat::PlainText;

            QStringList lines;
            const QList<ServerSettingEntry> entries = model->entriesCopy();
            for (const ServerSettingEntry &entry : entries) {
                const QString k = entry.key();
                const QString v = ServerRuntimeSettings::formatSettingValue(
                        k, entry.value(), ServerRuntimeSettings::ValueFormatStyle::HumanReadable);
                const QString d = model->descriptionForKey(k);
                if (d.trimmed().isEmpty()) {
                    lines << QStringLiteral("%1 = %2").arg(k, v);
                } else {
                    lines << QStringLiteral("%1 = %2 - %3").arg(k, v, d);
                }
            }
            if (lines.isEmpty())
                lines << MessengerAdminSettingsMenu::tr("No runtime settings are available.");

            const QString text = lines.join(QStringLiteral("\n")).trimmed();
            const bool showAsOf =
                    mgr && mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive;
            out.text = showAsOf ? mgr->buildTextWithAsOfCurrentFormat(text, out.format) : text;
            return out;
        };

        if (mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive) {
            page.rows.append({ mgr->makeNavigateButton(tr("Refresh list"), page.id, true) });
        }

        const QList<ServerSettingEntry> entries = model->entriesCopy();
        for (const ServerSettingEntry &entry : entries) {
            const QString key = entry.key().trimmed();
            if (key.isEmpty())
                continue;
            page.rows.append({ mgr->makeEventButton(settingDisplayName(key),
                                                    QStringLiteral("as:select:%1").arg(key),
                                                    true) });
        }

        page.rows.append({ mgr->makeNavigateButton(tr("Back to admin home"),
                                                   QLatin1String(MessengerMenuIds::kMenuAdminHome),
                                                   true) });

        if (!state->errorText.trimmed().isEmpty()) {
            MessengerMenuManager::prependErrorBlock(&page, state->errorText);
        }
        return page;
    }

    const QString key = selectedEntry.key().trimmed();
    const QVector<PresetValue> presets = presetValuesForKey(key);
    QVariant defaultValue;
    const bool hasDefault = defaultValueForKey(key, &defaultValue);

    switch (state->view) {
    case SettingsViewMode::Presets:
        page.body = presets.isEmpty() ? tr("No predefined values for this setting.")
                                      : tr("Select a predefined value.");
        break;
    case SettingsViewMode::ManualInput:
        page.body = tr("Send a new value for this setting.");
        break;
    case SettingsViewMode::Details:
    default:
        page.body = tr("Setting details.");
        break;
    }

    QStringList lines;
    lines << tr("Key: %1").arg(selectedEntry.key());
    lines << tr("Current value: %1")
                     .arg(ServerRuntimeSettings::formatSettingValue(
                             selectedEntry.key(),
                             selectedEntry.value(),
                             ServerRuntimeSettings::ValueFormatStyle::HumanReadable));
    const QString description = model->descriptionForKey(selectedEntry.key());
    if (!description.trimmed().isEmpty()) {
        lines << tr("Description: %1").arg(description);
    }
    if (hasDefault) {
        lines << tr("Default value: %1")
                         .arg(ServerRuntimeSettings::formatSettingValue(
                                 key,
                                 defaultValue,
                                 ServerRuntimeSettings::ValueFormatStyle::HumanReadable));
    }
    if (!presets.isEmpty()) {
        QStringList presetLabels;
        for (const PresetValue &preset : presets) {
            if (!preset.label.trimmed().isEmpty()) {
                presetLabels << preset.label;
            } else {
                presetLabels << ServerRuntimeSettings::formatSettingValue(
                        key, preset.value, ServerRuntimeSettings::ValueFormatStyle::HumanReadable);
            }
        }
        lines << tr("Preset values: %1").arg(presetLabels.join(QStringLiteral(", ")));
    }
    if (state->view == SettingsViewMode::ManualInput) {
        lines << tr("Manual input: %1").arg(manualInputHintForKey(key));
    }

    const QString detailsText = lines.join(QStringLiteral("\n")).trimmed();
    page.dynamicText = [detailsText, mgr](const MessengerMenu::RenderContext &ctx) {
        Q_UNUSED(ctx);
        MessengerMenu::MenuPage::DynamicText out;
        out.format = AbstractBot::MessageFormat::PlainText;
        const bool showAsOf =
                mgr && mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive;
        out.text = showAsOf ? mgr->buildTextWithAsOfCurrentFormat(detailsText, out.format)
                            : detailsText;
        return out;
    };

    if (state->view == SettingsViewMode::Presets) {
        static constexpr int kButtonsPerRow = 4;
        QVector<MessengerMenu::MenuButton> row;
        row.reserve(kButtonsPerRow);
        for (const PresetValue &preset : presets) {
            const QString label =
                    preset.label.trimmed().isEmpty()
                            ? ServerRuntimeSettings::formatSettingValue(
                                      key,
                                      preset.value,
                                      ServerRuntimeSettings::ValueFormatStyle::HumanReadable)
                            : preset.label;
            row.append(mgr->makeEventButton(label,
                                            QStringLiteral("as:set:%1:%2").arg(key, preset.id),
                                            true));
            if (row.size() >= kButtonsPerRow) {
                page.rows.append(row);
                row.clear();
            }
        }
        if (!row.isEmpty())
            page.rows.append(row);
        page.rows.append({ mgr->makeEventButton(tr("Back to setting details"),
                                                QStringLiteral("as:detail:%1").arg(key),
                                                true) });
    } else {
        page.rows.append({ mgr->makeEventButton(tr("Set default"),
                                                QStringLiteral("as:default:%1").arg(key),
                                                true),
                           mgr->makeEventButton(tr("Choose from predefined"),
                                                QStringLiteral("as:presets:%1").arg(key),
                                                true) });
        page.rows.append({ mgr->makeEventButton(tr("Set manually"),
                                                QStringLiteral("as:manual:%1").arg(key),
                                                true) });

        if (state->view == SettingsViewMode::ManualInput) {
            page.rows.append({ mgr->makeEventButton(tr("Back to setting details"),
                                                    QStringLiteral("as:detail:%1").arg(key),
                                                    true) });
        }
    }

    page.rows.append(
            { mgr->makeEventButton(tr("Back to settings list"), QStringLiteral("as:back"), true) });
    page.rows.append({ mgr->makeNavigateButton(tr("Back to admin home"),
                                               QLatin1String(MessengerMenuIds::kMenuAdminHome),
                                               true) });

    if (!state->errorText.trimmed().isEmpty()) {
        MessengerMenuManager::prependErrorBlock(&page, state->errorText);
    }

    return page;
}

bool MessengerAdminSettingsMenu::handleEvent(const QString &event,
                                             const QString &sessionId,
                                             const MessengerMenu::RenderContext &context,
                                             MessengerMenuManager::ActionResult *result) const
{
    if (!result)
        return false;

    MessengerMenuManager *mgr = menuManager();
    MessengerBot *bot = mgr ? mgr->bot() : nullptr;
    ServerDatabase *db = bot ? bot->database() : nullptr;
    if (!mgr || !bot || !db)
        return false;

    const QString trimmed = event.trimmed();
    if (!trimmed.startsWith(QStringLiteral("as:")))
        return false;

    const QStringList parts = trimmed.split(QLatin1Char(':'), Qt::KeepEmptyParts);
    if (parts.size() < 2)
        return false;

    const QString action = parts.value(1).trimmed();
    const QString key = parts.value(2).trimmed();

    SessionState *state = ensureState(sessionId);
    if (!state)
        return false;

    const auto applySettingValue = [&](const QString &targetKey,
                                       const QVariant &settingValue) -> bool {
        Setting setting = Setting::MaxResourcesPerUser;
        if (!ServerRuntimeSettings::settingFromKey(targetKey.trimmed(), &setting)) {
            state->errorText = tr("Unknown setting key: %1").arg(targetKey);
            return false;
        }
        const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
        const ServerDatabaseError error = db->setServerSetting(setting, settingValue, actor);
        if (error) {
            state->errorText = error.errorString();
            return false;
        }
        state->errorText.clear();
        return true;
    };

    if (action == QStringLiteral("back")) {
        state->reset();
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (action == QStringLiteral("detail")) {
        if (!key.isEmpty())
            state->selectedKey = key;
        if (state->selectedKey.trimmed().isEmpty()) {
            state->errorText = tr("Select a setting.");
            state->view = SettingsViewMode::List;
        } else {
            state->view = SettingsViewMode::Details;
            state->errorText.clear();
        }
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (action == QStringLiteral("select")) {
        if (key.isEmpty()) {
            state->errorText = tr("Select a setting.");
            state->view = SettingsViewMode::List;
            state->selectedKey.clear();
        } else {
            Setting setting = Setting::MaxResourcesPerUser;
            if (!ServerRuntimeSettings::settingFromKey(key.trimmed(), &setting)) {
                state->errorText = tr("Unknown setting key: %1").arg(key);
                state->view = SettingsViewMode::List;
                state->selectedKey.clear();
            } else {
                state->selectedKey = QLatin1String(ServerRuntimeSettings::key(setting));
                state->view = SettingsViewMode::Details;
                state->errorText.clear();
            }
        }
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (action == QStringLiteral("presets")) {
        if (key.isEmpty()) {
            state->errorText = tr("Select a setting.");
            state->view = SettingsViewMode::List;
            state->selectedKey.clear();
        } else {
            Setting setting = Setting::MaxResourcesPerUser;
            if (!ServerRuntimeSettings::settingFromKey(key.trimmed(), &setting)) {
                state->errorText = tr("Unknown setting key: %1").arg(key);
                state->view = SettingsViewMode::List;
                state->selectedKey.clear();
            } else if (presetValuesForKey(QLatin1String(ServerRuntimeSettings::key(setting)))
                               .isEmpty()) {
                state->errorText = tr("No predefined values for this setting.");
                state->selectedKey = QLatin1String(ServerRuntimeSettings::key(setting));
                state->view = SettingsViewMode::Details;
            } else {
                state->selectedKey = QLatin1String(ServerRuntimeSettings::key(setting));
                state->view = SettingsViewMode::Presets;
                state->errorText.clear();
            }
        }
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (action == QStringLiteral("manual")) {
        if (key.isEmpty()) {
            state->errorText = tr("Select a setting.");
            state->view = SettingsViewMode::List;
            state->selectedKey.clear();
        } else {
            Setting setting = Setting::MaxResourcesPerUser;
            if (!ServerRuntimeSettings::settingFromKey(key.trimmed(), &setting)) {
                state->errorText = tr("Unknown setting key: %1").arg(key);
                state->view = SettingsViewMode::List;
                state->selectedKey.clear();
            } else {
                state->selectedKey = QLatin1String(ServerRuntimeSettings::key(setting));
                state->view = SettingsViewMode::ManualInput;
                state->errorText.clear();
            }
        }
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (action == QStringLiteral("default")) {
        QVariant defaultValue;
        if (key.isEmpty() || !defaultValueForKey(key, &defaultValue)) {
            state->errorText = tr("Default value is not available for this setting.");
            state->view = SettingsViewMode::Details;
            result->handled = true;
            result->sendReply = true;
            return true;
        }
        state->selectedKey = key;
        applySettingValue(key, defaultValue);
        state->view = SettingsViewMode::Details;
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (action == QStringLiteral("set")) {
        if (parts.size() < 4) {
            state->errorText = MessengerUserFacingErrors::valueCannotBeEmpty();
            result->handled = true;
            result->sendReply = true;
            return true;
        }
        QVariant presetValue;
        const QString presetId = parts.value(3).trimmed();
        if (!presetValueByIdForKey(key, presetId, &presetValue)) {
            state->errorText = tr("Invalid preset value.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }
        if (!key.isEmpty()) {
            state->selectedKey = key;
        }
        applySettingValue(key, presetValue);
        state->view = SettingsViewMode::Details;
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (action == QStringLiteral("toggle")) {
        if (key.isEmpty())
            return false;
        Setting setting = Setting::MaxResourcesPerUser;
        if (!ServerRuntimeSettings::settingFromKey(key.trimmed(), &setting)) {
            state->errorText = tr("Unknown setting key: %1").arg(key);
            result->handled = true;
            result->sendReply = true;
            return true;
        }
        if (setting == Setting::DateTimeFormat) {
            state->errorText = tr("Toggle action is not supported for this setting.");
            result->handled = true;
            result->sendReply = true;
            return true;
        }
        const QVariant currentValue = db->serverSettingValue(setting);
        QVariant newValue;
        if (setting == Setting::StatsEnabled || setting == Setting::StatsAdminsOnly) {
            newValue = QVariant::fromValue(!currentValue.toBool());
        } else {
            bool ok = false;
            const uint v = currentValue.toUInt(&ok);
            const uint toggledValue = (ok && v == 0u) ? 1u : 0u;
            newValue = QVariant::fromValue(toggledValue);
        }
        state->selectedKey = QLatin1String(ServerRuntimeSettings::key(setting));
        applySettingValue(state->selectedKey, newValue);
        state->view = SettingsViewMode::Details;
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    return false;
}

bool MessengerAdminSettingsMenu::handleTextInput(const QString &text,
                                                 const QString &sessionId,
                                                 const MessengerMenu::RenderContext &context,
                                                 MessengerMenuManager::ActionResult *result) const
{
    if (!result)
        return false;

    MessengerMenuManager *mgr = menuManager();
    MessengerBot *bot = mgr ? mgr->bot() : nullptr;
    ServerDatabase *db = bot ? bot->database() : nullptr;
    if (!mgr || !bot || !db)
        return false;

    SessionState *state = ensureState(sessionId);
    if (!state)
        return false;

    if (state->view != SettingsViewMode::ManualInput)
        return false;

    const QString key = state->selectedKey.trimmed();
    if (key.isEmpty()) {
        state->errorText = tr("Select a setting.");
        state->view = SettingsViewMode::List;
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    Setting setting = Setting::MaxResourcesPerUser;
    if (!ServerRuntimeSettings::settingFromKey(key, &setting)) {
        state->errorText = tr("Unknown setting key: %1").arg(key);
        state->view = SettingsViewMode::List;
        state->selectedKey.clear();
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    QVariant newValue;
    QString parseError;
    const QString normalizedKey = QLatin1String(ServerRuntimeSettings::key(setting));
    if (!ServerRuntimeSettings::parseSettingValueToken(normalizedKey,
                                                       text,
                                                       &newValue,
                                                       &parseError)) {
        state->errorText =
                parseError.isEmpty() ? MessengerUserFacingErrors::valueCannotBeEmpty() : parseError;
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
    const ServerDatabaseError error = db->setServerSetting(setting, newValue, actor);
    if (error) {
        state->errorText = error.errorString();
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    state->selectedKey = normalizedKey;
    state->errorText.clear();
    state->view = SettingsViewMode::Details;
    result->handled = true;
    result->sendReply = true;
    return true;
}

MessengerAdminSettingsMenu::SessionState *MessengerAdminSettingsMenu::ensureState(
        const QString &sessionId) const
{
    const QString sid = sessionId.trimmed();
    if (sid.isEmpty())
        return nullptr;

    QHash<QString, SessionState>::iterator it = m_stateBySessionId.find(sid);
    if (it == m_stateBySessionId.end()) {
        SessionState created;
        it = m_stateBySessionId.insert(sid, created);
    }
    return &it.value();
}

BOOKING_RESOURCES_REGISTER_MENU(MessengerAdminSettingsMenu)
