#include "MessengerUserSettingsMenu.h"

#include "MessengerMenuIds.h"

#include "UserModel.h"
#include "MessengerBot.h"
#include "ServerDatabase.h"
#include "ServerDateTimeFormat.h"
#include "MessengerUserFacingErrors.h"

#include <QVector>
#include <QStringList>

namespace {
constexpr char kKeyLocale[] = "locale";
constexpr char kKeyTimezone[] = "timezone";
constexpr char kKeyOccupationNotify[] = "occupation.notify";

struct PresetValue
{
    QString token;
    QString label;
};

bool isKnownKey(const QString &key)
{
    const QString trimmed = key.trimmed().toLower();
    return trimmed == QLatin1String(kKeyLocale) || trimmed == QLatin1String(kKeyTimezone)
           || trimmed == QLatin1String(kKeyOccupationNotify);
}

QString settingDisplayName(const QString &key)
{
    const QString trimmed = key.trimmed().toLower();
    if (trimmed == QLatin1String(kKeyLocale))
        return MessengerUserSettingsMenu::tr("Language");
    if (trimmed == QLatin1String(kKeyTimezone))
        return MessengerUserSettingsMenu::tr("Time zone");
    if (trimmed == QLatin1String(kKeyOccupationNotify))
        return MessengerUserSettingsMenu::tr("Occupation notifications");
    return trimmed;
}

QString settingDescription(const QString &key)
{
    const QString trimmed = key.trimmed().toLower();
    if (trimmed == QLatin1String(kKeyLocale)) {
        return MessengerUserSettingsMenu::tr(
                "Preferred language for bot replies (default uses messenger language).");
    }
    if (trimmed == QLatin1String(kKeyTimezone)) {
        return MessengerUserSettingsMenu::tr("Preferred time zone for date/time input and output "
                                             "(default uses server time zone).");
    }
    if (trimmed == QLatin1String(kKeyOccupationNotify)) {
        return MessengerUserSettingsMenu::tr(
                "Notifications about resource occupation: all visible resources, only events "
                "involving you, or off (all/mine/off).");
    }
    return QString();
}

QString formatLocaleValue(const QString &locale)
{
    const QString trimmed = locale.trimmed();
    if (trimmed.isEmpty())
        return MessengerUserSettingsMenu::tr("default (messenger)");
    return trimmed;
}

QString formatTimezoneValue(const UserTimeZone &timezone)
{
    if (!timezone.hasCustomOffset())
        return MessengerUserSettingsMenu::tr("default (server timezone)");
    const short offsetMinutes = timezone.utcOffsetMinutes();
    return MessengerUserSettingsMenu::tr("%1 (%2 min)")
            .arg(timezone.toDisplayString(), QString::number(offsetMinutes));
}

QString formatNotifyValue(User::OccupationNotifyMode mode)
{
    switch (mode) {
    case User::OccupationNotifyMode::AllResources:
        return MessengerUserSettingsMenu::tr("all resources");
    case User::OccupationNotifyMode::OwnResourcesOnly:
        return MessengerUserSettingsMenu::tr("only events involving me");
    case User::OccupationNotifyMode::Disabled:
        return MessengerUserSettingsMenu::tr("disabled");
    }
    return MessengerUserSettingsMenu::tr("only events involving me");
}

QString defaultValueForKey(const QString &key)
{
    const QString trimmed = key.trimmed().toLower();
    if (trimmed == QLatin1String(kKeyLocale))
        return MessengerUserSettingsMenu::tr("default (messenger)");
    if (trimmed == QLatin1String(kKeyTimezone))
        return MessengerUserSettingsMenu::tr("default (server timezone)");
    if (trimmed == QLatin1String(kKeyOccupationNotify))
        return MessengerUserSettingsMenu::tr("only events involving me");
    return QString();
}

QVector<PresetValue> presetsForKey(const QString &key)
{
    const QString trimmed = key.trimmed().toLower();
    if (trimmed == QLatin1String(kKeyLocale)) {
        return {
            { QStringLiteral("default"), MessengerUserSettingsMenu::tr("Default (messenger)") },
            { QStringLiteral("en"), MessengerUserSettingsMenu::tr("English") },
            { QStringLiteral("ru"), MessengerUserSettingsMenu::tr("Russian") },
            { QStringLiteral("ru_stand"), MessengerUserSettingsMenu::tr("Russian (stands)") },
        };
    }
    if (trimmed == QLatin1String(kKeyTimezone)) {
        QVector<PresetValue> presets;
        presets.append({ QStringLiteral("default"),
                         MessengerUserSettingsMenu::tr("Default (server timezone)") });

        // Keep UTC near "default", then place mirrored negative/positive pairs.
        presets.append({ QStringLiteral("0"), UserTimeZone::formatOffset(static_cast<short>(0)) });

        const QVector<int> mirroredAbsMinutes = { 720, 660, 630, 600, 570, 540, 525, 480, 420, 390,
                                                  360, 345, 330, 300, 270, 240, 210, 180, 120, 60 };
        presets.reserve(2 + mirroredAbsMinutes.size() * 2 + 2);
        for (const int absOffsetMinutes : mirroredAbsMinutes) {
            const short negativeOffset = static_cast<short>(-absOffsetMinutes);
            const short positiveOffset = static_cast<short>(absOffsetMinutes);
            presets.append({ QString::number(static_cast<int>(negativeOffset)),
                             UserTimeZone::formatOffset(negativeOffset) });
            presets.append({ QString::number(static_cast<int>(positiveOffset)),
                             UserTimeZone::formatOffset(positiveOffset) });
        }

        // UTC+13 and UTC+14 are valid upper-range offsets without matching negatives.
        const QVector<int> positiveOnlyMinutes = { 780, 840 };
        for (const int offsetMinutes : positiveOnlyMinutes) {
            const short safeOffsetMinutes = static_cast<short>(offsetMinutes);
            presets.append({ QString::number(offsetMinutes),
                             UserTimeZone::formatOffset(safeOffsetMinutes) });
        }
        return presets;
    }
    if (trimmed == QLatin1String(kKeyOccupationNotify)) {
        return {
            { QStringLiteral("all"), MessengerUserSettingsMenu::tr("All resources") },
            { QStringLiteral("mine"), MessengerUserSettingsMenu::tr("Only events involving me") },
            { QStringLiteral("off"), MessengerUserSettingsMenu::tr("Disabled") },
        };
    }
    return {};
}

QString manualInputHintForKey(const QString &key)
{
    const QString trimmed = key.trimmed().toLower();
    if (trimmed == QLatin1String(kKeyLocale)) {
        return MessengerUserSettingsMenu::tr("Send a locale code (e.g. en, ru_RU) or 'default'.");
    }
    if (trimmed == QLatin1String(kKeyTimezone)) {
        return MessengerUserSettingsMenu::tr(
                "Send a time zone value "
                "(e.g. UTC+3:00, UTC+03:00, +03:00, 03:00, -03:00, 180) or 'default'.");
    }
    if (trimmed == QLatin1String(kKeyOccupationNotify)) {
        return MessengerUserSettingsMenu::tr("Send all/mine/off.");
    }
    return QString();
}

bool parseTimezoneToken(const QString &token, UserTimeZone *timeZoneOut, QString *errorOut)
{
    if (!timeZoneOut)
        return false;

    UserTimeZone parsed;
    if (!ServerDateTimeFormat::parseTimeZoneValue(token, &parsed)) {
        if (errorOut) {
            *errorOut = MessengerUserFacingErrors::invalidTimeZoneValue();
        }
        return false;
    }

    *timeZoneOut = parsed;
    return true;
}

bool parseLocaleToken(const QString &token, QString *localeOut, QString *errorOut)
{
    if (!localeOut)
        return false;

    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty()) {
        if (errorOut)
            *errorOut = MessengerUserFacingErrors::valueCannotBeEmpty();
        return false;
    }

    const QString normalized = trimmed.toLower();
    if (normalized == QStringLiteral("default") || normalized == QStringLiteral("auto")
        || normalized == QStringLiteral("messenger")) {
        localeOut->clear();
        return true;
    }

    *localeOut = trimmed;
    return true;
}

bool parseNotifyModeToken(const QString &token,
                          User::OccupationNotifyMode *modeOut,
                          QString *errorOut)
{
    if (!modeOut)
        return false;

    const QString trimmed = token.trimmed().toLower();
    if (trimmed.isEmpty()) {
        if (errorOut)
            *errorOut = MessengerUserFacingErrors::valueCannotBeEmpty();
        return false;
    }

    if (trimmed == QStringLiteral("all") || trimmed == QStringLiteral("all_resources")) {
        *modeOut = User::OccupationNotifyMode::AllResources;
        return true;
    }
    if (trimmed == QStringLiteral("mine") || trimmed == QStringLiteral("own")
        || trimmed == QStringLiteral("own_resources")) {
        *modeOut = User::OccupationNotifyMode::OwnResourcesOnly;
        return true;
    }
    if (trimmed == QStringLiteral("off") || trimmed == QStringLiteral("disabled")) {
        *modeOut = User::OccupationNotifyMode::Disabled;
        return true;
    }

    if (errorOut) {
        *errorOut = MessengerUserFacingErrors::invalidNotifyMode();
    }
    return false;
}
} // namespace

MessengerUserSettingsMenu::MessengerUserSettingsMenu(MessengerMenuManager *manager)
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

QString MessengerUserSettingsMenu::menuId() const
{
    return QLatin1String(MessengerMenuIds::kMenuUserSettings);
}

QStringList MessengerUserSettingsMenu::eventKeys() const
{
    return { QStringLiteral("us") };
}

MessengerMenu::MenuPage MessengerUserSettingsMenu::buildPage(
        const MessengerMenu::RenderContext &context) const
{
    MessengerMenu::MenuPage page;
    page.id = QLatin1String(MessengerMenuIds::kMenuUserSettings);
    page.title = tr("User settings");
    page.body = tr("Personal language, time zone, and notification preferences.");

    MessengerMenuManager *mgr = menuManager();
    MessengerBot *bot = mgr ? mgr->bot() : nullptr;
    ServerDatabase *db = bot ? bot->database() : nullptr;
    const UserModel *model = db ? db->userModel() : nullptr;
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

    const MessengerUserKey self = AbstractBot::senderKey(context.chat, context.sender);
    const User current = model->userByKey(self);
    if (!current.isValid()) {
        page.body = MessengerUserFacingErrors::accessDeniedAccessNotActivated(self);
        return page;
    }

    bool showList = (state->view == SettingsViewMode::List);
    QString selectedKey;
    if (!showList) {
        selectedKey = state->selectedKey.trimmed();
        if (selectedKey.isEmpty() || !isKnownKey(selectedKey)) {
            state->errorText = tr("Select a setting.");
            state->view = SettingsViewMode::List;
            state->selectedKey.clear();
            showList = true;
        }
    }

    if (showList) {
        page.body = tr("Select a setting to view or change.");
        page.dynamicText = [model, self, mgr](const MessengerMenu::RenderContext &ctx) {
            Q_UNUSED(ctx);
            MessengerMenu::MenuPage::DynamicText out;
            out.format = AbstractBot::MessageFormat::PlainText;

            QStringList lines;
            const User user = model ? model->userByKey(self) : User();
            if (!user.isValid()) {
                lines << MessengerUserFacingErrors::accessDeniedAccessNotActivated(self);
            } else {
                lines << QStringLiteral("%1 = %2 - %3")
                                 .arg(QLatin1String(kKeyLocale),
                                      formatLocaleValue(user.locale()),
                                      settingDescription(kKeyLocale));
                lines << QStringLiteral("%1 = %2 - %3")
                                 .arg(QLatin1String(kKeyTimezone),
                                      formatTimezoneValue(user.timezone()),
                                      settingDescription(kKeyTimezone));
                lines << QStringLiteral("%1 = %2 - %3")
                                 .arg(QLatin1String(kKeyOccupationNotify),
                                      formatNotifyValue(user.occupationNotifyMode()),
                                      settingDescription(kKeyOccupationNotify));
            }

            const QString text = lines.join(QStringLiteral("\n")).trimmed();
            const bool showAsOf =
                    mgr && mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive;
            out.text = showAsOf ? mgr->buildTextWithAsOfCurrentFormat(text, out.format) : text;
            return out;
        };

        if (mgr->menuUpdateMode() == MessengerMenuManager::MenuUpdateMode::Passive) {
            page.rows.append({ mgr->makeNavigateButton(tr("Refresh list"), page.id, false) });
        }

        page.rows.append({ mgr->makeEventButton(settingDisplayName(kKeyLocale),
                                                QStringLiteral("us:select:%1").arg(kKeyLocale),
                                                false) });
        page.rows.append({ mgr->makeEventButton(settingDisplayName(kKeyTimezone),
                                                QStringLiteral("us:select:%1").arg(kKeyTimezone),
                                                false) });
        page.rows.append(
                { mgr->makeEventButton(settingDisplayName(kKeyOccupationNotify),
                                       QStringLiteral("us:select:%1").arg(kKeyOccupationNotify),
                                       false) });

        page.rows.append({ mgr->makeNavigateButton(tr("Back to main"),
                                                   QLatin1String(MessengerMenuIds::kMenuMain),
                                                   false) });

        if (!state->errorText.trimmed().isEmpty()) {
            MessengerMenuManager::prependErrorBlock(&page, state->errorText);
        }
        return page;
    }

    const QVector<PresetValue> presets = presetsForKey(selectedKey);
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
    lines << tr("Setting: %1").arg(settingDisplayName(selectedKey));
    if (selectedKey.trimmed().toLower() == QLatin1String(kKeyLocale)) {
        lines << tr("Current value: %1").arg(formatLocaleValue(current.locale()));
    } else if (selectedKey.trimmed().toLower() == QLatin1String(kKeyTimezone)) {
        lines << tr("Current value: %1").arg(formatTimezoneValue(current.timezone()));
    } else {
        lines << tr("Current value: %1").arg(formatNotifyValue(current.occupationNotifyMode()));
    }
    const QString description = settingDescription(selectedKey);
    if (!description.trimmed().isEmpty())
        lines << tr("Description: %1").arg(description);
    const QString defaultValue = defaultValueForKey(selectedKey);
    if (!defaultValue.trimmed().isEmpty())
        lines << tr("Default value: %1").arg(defaultValue);
    if (!presets.isEmpty()) {
        QStringList presetLabels;
        for (const PresetValue &preset : presets)
            presetLabels << preset.label;
        lines << tr("Preset values: %1").arg(presetLabels.join(QStringLiteral(", ")));
    }
    if (state->view == SettingsViewMode::ManualInput) {
        const QString hint = manualInputHintForKey(selectedKey);
        if (!hint.trimmed().isEmpty())
            lines << tr("Manual input: %1").arg(hint);
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
        static constexpr int kButtonsPerRow = 2;
        QVector<MessengerMenu::MenuButton> row;
        row.reserve(kButtonsPerRow);
        for (const PresetValue &preset : presets) {
            row.append(mgr->makeEventButton(preset.label,
                                            QStringLiteral("us:set:%1:%2")
                                                    .arg(selectedKey, preset.token),
                                            false));
            if (row.size() >= kButtonsPerRow) {
                page.rows.append(row);
                row.clear();
            }
        }
        if (!row.isEmpty())
            page.rows.append(row);
        page.rows.append({ mgr->makeEventButton(tr("Back to setting details"),
                                                QStringLiteral("us:detail:%1").arg(selectedKey),
                                                false) });
    } else {
        QVector<MessengerMenu::MenuButton> row;
        row.append(mgr->makeEventButton(tr("Set default"),
                                        QStringLiteral("us:default:%1").arg(selectedKey),
                                        false));
        if (!presets.isEmpty()) {
            row.append(mgr->makeEventButton(tr("Choose from predefined"),
                                            QStringLiteral("us:presets:%1").arg(selectedKey),
                                            false));
        }
        if (!row.isEmpty())
            page.rows.append(row);
        page.rows.append({ mgr->makeEventButton(tr("Set manually"),
                                                QStringLiteral("us:manual:%1").arg(selectedKey),
                                                false) });
        if (state->view == SettingsViewMode::ManualInput) {
            page.rows.append({ mgr->makeEventButton(tr("Back to setting details"),
                                                    QStringLiteral("us:detail:%1").arg(selectedKey),
                                                    false) });
        }
    }

    page.rows.append({ mgr->makeEventButton(tr("Back to settings list"),
                                            QStringLiteral("us:back"),
                                            false) });
    page.rows.append({ mgr->makeNavigateButton(tr("Back to main"),
                                               QLatin1String(MessengerMenuIds::kMenuMain),
                                               false) });

    if (!state->errorText.trimmed().isEmpty()) {
        MessengerMenuManager::prependErrorBlock(&page, state->errorText);
    }

    return page;
}

bool MessengerUserSettingsMenu::handleEvent(const QString &event,
                                            const QString &sessionId,
                                            const MessengerMenu::RenderContext &context,
                                            MessengerMenuManager::ActionResult *result) const
{
    if (!result)
        return false;

    MessengerMenuManager *mgr = menuManager();
    MessengerBot *bot = mgr ? mgr->bot() : nullptr;
    ServerDatabase *db = bot ? bot->database() : nullptr;
    const UserModel *model = db ? db->userModel() : nullptr;
    if (!mgr || !bot || !db || !model)
        return false;

    const QString trimmed = event.trimmed();
    if (!trimmed.startsWith(QStringLiteral("us:")))
        return false;

    const QStringList parts = trimmed.split(QLatin1Char(':'), Qt::KeepEmptyParts);
    if (parts.size() < 2)
        return false;

    const QString action = parts.value(1).trimmed();
    const QString key = parts.value(2).trimmed();

    SessionState *state = ensureState(sessionId);
    if (!state)
        return false;

    const auto applySettingValue = [&](const QString &targetKey, const QString &token) -> bool {
        const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
        const User current = model->userByKey(actor);
        if (!current.isValid()) {
            state->errorText = MessengerUserFacingErrors::accessDeniedAccessNotActivated(actor);
            return false;
        }

        User updated = current;
        QString parseError;
        const QString normalized = targetKey.trimmed().toLower();
        if (normalized == QLatin1String(kKeyLocale)) {
            QString parsedLocale;
            if (!parseLocaleToken(token, &parsedLocale, &parseError)) {
                state->errorText = parseError;
                return false;
            }
            updated.setLocale(parsedLocale);
        } else if (normalized == QLatin1String(kKeyTimezone)) {
            UserTimeZone parsedTimeZone;
            if (!parseTimezoneToken(token, &parsedTimeZone, &parseError)) {
                state->errorText = parseError;
                return false;
            }
            updated.setTimezone(parsedTimeZone);
        } else if (normalized == QLatin1String(kKeyOccupationNotify)) {
            User::OccupationNotifyMode mode = User::OccupationNotifyMode::OwnResourcesOnly;
            if (!parseNotifyModeToken(token, &mode, &parseError)) {
                state->errorText = parseError;
                return false;
            }
            updated.setOccupationNotifyMode(mode);
        } else {
            state->errorText = tr("Unknown setting key: %1").arg(targetKey);
            return false;
        }

        const ServerDatabaseError error = db->updateUser(updated, actor);
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
        if (key.isEmpty() || !isKnownKey(key)) {
            state->errorText = tr("Select a setting.");
            state->view = SettingsViewMode::List;
            state->selectedKey.clear();
        } else {
            state->selectedKey = key;
            state->view = SettingsViewMode::Details;
            state->errorText.clear();
        }
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (action == QStringLiteral("presets")) {
        if (key.isEmpty() || !isKnownKey(key)) {
            state->errorText = tr("Select a setting.");
            state->view = SettingsViewMode::List;
            state->selectedKey.clear();
        } else if (presetsForKey(key).isEmpty()) {
            state->errorText = tr("No predefined values for this setting.");
            state->selectedKey = key;
            state->view = SettingsViewMode::Details;
        } else {
            state->selectedKey = key;
            state->view = SettingsViewMode::Presets;
            state->errorText.clear();
        }
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (action == QStringLiteral("manual")) {
        if (key.isEmpty() || !isKnownKey(key)) {
            state->errorText = tr("Select a setting.");
            state->view = SettingsViewMode::List;
            state->selectedKey.clear();
        } else {
            state->selectedKey = key;
            state->view = SettingsViewMode::ManualInput;
            state->errorText.clear();
        }
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    if (action == QStringLiteral("default")) {
        if (key.isEmpty() || !isKnownKey(key)) {
            state->errorText = tr("Default value is not available for this setting.");
            state->view = SettingsViewMode::Details;
            result->handled = true;
            result->sendReply = true;
            return true;
        }

        const QString normalized = key.trimmed().toLower();
        const QString defaultToken =
                normalized == QLatin1String(kKeyLocale)     ? QStringLiteral("default")
                : normalized == QLatin1String(kKeyTimezone) ? QStringLiteral("default")
                                                            : QStringLiteral("mine");
        applySettingValue(key, defaultToken);
        state->selectedKey = key;
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
        const QString token = parts.value(3).trimmed();
        if (!key.isEmpty())
            state->selectedKey = key;
        applySettingValue(key, token);
        state->view = SettingsViewMode::Details;
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    return false;
}

bool MessengerUserSettingsMenu::handleTextInput(const QString &text,
                                                const QString &sessionId,
                                                const MessengerMenu::RenderContext &context,
                                                MessengerMenuManager::ActionResult *result) const
{
    if (!result)
        return false;

    MessengerMenuManager *mgr = menuManager();
    MessengerBot *bot = mgr ? mgr->bot() : nullptr;
    ServerDatabase *db = bot ? bot->database() : nullptr;
    const UserModel *model = db ? db->userModel() : nullptr;
    if (!mgr || !bot || !db || !model)
        return false;

    SessionState *state = ensureState(sessionId);
    if (!state)
        return false;

    if (state->view != SettingsViewMode::ManualInput)
        return false;

    const QString key = state->selectedKey.trimmed();
    if (key.isEmpty() || !isKnownKey(key)) {
        state->errorText = tr("Select a setting.");
        state->view = SettingsViewMode::List;
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    const MessengerUserKey actor = AbstractBot::senderKey(context.chat, context.sender);
    const User current = model->userByKey(actor);
    if (!current.isValid()) {
        state->errorText = MessengerUserFacingErrors::accessDeniedAccessNotActivated(actor);
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    User updated = current;
    QString parseError;
    const QString normalized = key.trimmed().toLower();
    if (normalized == QLatin1String(kKeyLocale)) {
        QString parsedLocale;
        if (!parseLocaleToken(text, &parsedLocale, &parseError)) {
            state->errorText = parseError;
            result->handled = true;
            result->sendReply = true;
            return true;
        }
        updated.setLocale(parsedLocale);
    } else if (normalized == QLatin1String(kKeyTimezone)) {
        UserTimeZone parsedTimeZone;
        if (!parseTimezoneToken(text, &parsedTimeZone, &parseError)) {
            state->errorText = parseError;
            result->handled = true;
            result->sendReply = true;
            return true;
        }
        updated.setTimezone(parsedTimeZone);
    } else if (normalized == QLatin1String(kKeyOccupationNotify)) {
        User::OccupationNotifyMode mode = User::OccupationNotifyMode::OwnResourcesOnly;
        if (!parseNotifyModeToken(text, &mode, &parseError)) {
            state->errorText = parseError;
            result->handled = true;
            result->sendReply = true;
            return true;
        }
        updated.setOccupationNotifyMode(mode);
    } else {
        state->errorText = tr("Unknown setting key: %1").arg(key);
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    const ServerDatabaseError error = db->updateUser(updated, actor);
    if (error) {
        state->errorText = error.errorString();
        result->handled = true;
        result->sendReply = true;
        return true;
    }

    state->errorText.clear();
    state->view = SettingsViewMode::Details;
    result->handled = true;
    result->sendReply = true;
    return true;
}

MessengerUserSettingsMenu::SessionState *MessengerUserSettingsMenu::ensureState(
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

BOOKING_RESOURCES_REGISTER_MENU(MessengerUserSettingsMenu)
