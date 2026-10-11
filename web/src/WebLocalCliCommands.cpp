#include "WebLocalCliCommands.h"

#include "WebAuthManager.h"
#include "WebAuthMethods.h"
#include "WebPasswordHasher.h"
#include "WebSessionStore.h"

#include <QJsonObject>
#include <QSet>
#include <QUrl>
#include <QVector>

namespace {

struct CommandHelp
{
    QString name;
    QString usage;
    QString description;
    QStringList details;
};

struct UserRow
{
    QString login;
    QString userKey;
    QString name;
    QString locale;
    QString provider;
    QString password;
};

QString unquote(const QString &value)
{
    if (value.size() >= 2
        && ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
            || (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\''))))) {
        return value.mid(1, value.size() - 2);
    }
    return value;
}

QVector<CommandHelp> commandHelps()
{
    QVector<CommandHelp> commands;

    CommandHelp help;
    help.name = QStringLiteral("/help");
    help.usage = QStringLiteral("/help [command]");
    help.description = WebLocalCliCommands::tr("Show available web Local CLI commands and detailed "
                                               "command usage.");
    help.details << WebLocalCliCommands::tr("Use without arguments to list available commands.");
    help.details << WebLocalCliCommands::tr(
            "Use with a command name (with or without '/') to see detailed help.");
    commands << help;

    CommandHelp listUsers;
    listUsers.name = QStringLiteral("/list_users");
    listUsers.usage = QStringLiteral("/list_users");
    listUsers.description = WebLocalCliCommands::tr(
            "List web users: login, web:<uuid>, name, locale, provider, and password state.");
    commands << listUsers;

    CommandHelp addUser;
    addUser.name = QStringLiteral("/add_user");
    addUser.usage = QStringLiteral("/add_user <login> <first_name> [last_name] [locale]");
    addUser.description = WebLocalCliCommands::tr(
            "Create a local login/password web user without a working password and print a one-time "
            "set-password link.");
    addUser.details << WebLocalCliCommands::tr(
            "Requires a login authorization method (log-in-only or log-in-with-registration).");
    addUser.details << WebLocalCliCommands::tr("The command writes only to the web SQLite store.");
    commands << addUser;

    CommandHelp deleteUser;
    deleteUser.name = QStringLiteral("/delete_user");
    deleteUser.usage = QStringLiteral("/delete_user <login|user_id|web:uuid>");
    deleteUser.description = WebLocalCliCommands::tr(
            "Delete the web-local user. The server web:<uuid> account is not changed.");
    deleteUser.details << WebLocalCliCommands::tr(
            "Removes credentials, sessions, events, files, command cache, and password tokens.");
    commands << deleteUser;

    CommandHelp resetPassword;
    resetPassword.name = QStringLiteral("/reset_password");
    resetPassword.usage = QStringLiteral("/reset_password <login|user_id|web:uuid> [login]");
    resetPassword.description = WebLocalCliCommands::tr(
            "Invalidate the current password and issue a one-time password link. "
            "A user without a login requires [login].");
    resetPassword.details << WebLocalCliCommands::tr(
            "Requires a login authorization method (log-in-only or log-in-with-registration).");
    resetPassword.details << WebLocalCliCommands::tr(
            "Existing local login/password users lose the current password and receive a "
            "/reset-password.html link.");
    resetPassword.details << WebLocalCliCommands::tr(
            "A user without a login requires [login] and receives a "
            "/set-password.html link.");
    resetPassword.details << WebLocalCliCommands::tr("The password link is valid for %n day.",
                                                     "The password link is valid for %n days.",
                                                     WebSessionStore::kPasswordResetTokenValidDays);
    commands << resetPassword;

    return commands;
}

QString formatUserColumns(const QString &login,
                          const QString &userKey,
                          const QString &name,
                          const QString &locale,
                          const QString &provider,
                          const QString &password,
                          int loginWidth,
                          int userWidth,
                          int nameWidth,
                          int localeWidth,
                          int providerWidth)
{
    return QStringLiteral("%1  %2  %3  %4  %5  %6")
            .arg(login.leftJustified(loginWidth, QLatin1Char(' ')),
                 userKey.leftJustified(userWidth, QLatin1Char(' ')),
                 name.leftJustified(nameWidth, QLatin1Char(' ')),
                 locale.leftJustified(localeWidth, QLatin1Char(' ')),
                 provider.leftJustified(providerWidth, QLatin1Char(' ')),
                 password);
}

} // namespace

WebLocalCliCommands::WebLocalCliCommands(WebAuthManager *authManager, WebSessionStore *store)
    : m_authManager(authManager)
    , m_store(store)
    , m_authMethods(WebAuthMethods::defaults())
    , m_publicOrigin()
{
    // do nothing
}

WebLocalCliCommands::~WebLocalCliCommands()
{
    // do nothing
}

void WebLocalCliCommands::setAuthMethods(const QStringList &methods)
{
    m_authMethods = WebAuthMethods::normalizeList(methods);
}

void WebLocalCliCommands::setPublicOrigin(const QString &origin)
{
    QString trimmed = origin.trimmed();
    while (trimmed.endsWith(QLatin1Char('/')))
        trimmed.chop(1);
    m_publicOrigin = trimmed;
}

namespace {

struct CompletionToken
{
    QString value;
    int start = 0;
    int end = 0;
};

QVector<CompletionToken> tokenizeWithSpans(const QString &text)
{
    QVector<CompletionToken> tokens;
    int index = 0;
    while (index < text.size()) {
        while (index < text.size() && text.at(index).isSpace())
            ++index;
        if (index >= text.size())
            break;
        CompletionToken token;
        token.start = index;
        QChar quote;
        bool inQuotes = false;
        QString current;
        while (index < text.size()) {
            const QChar ch = text.at(index);
            if (inQuotes) {
                if (ch == quote) {
                    inQuotes = false;
                    ++index;
                    continue;
                }
                current.append(ch);
                ++index;
                continue;
            }
            if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
                inQuotes = true;
                quote = ch;
                ++index;
                continue;
            }
            if (ch.isSpace())
                break;
            current.append(ch);
            ++index;
        }
        token.end = index;
        token.value = current;
        tokens.append(token);
    }
    return tokens;
}

int completionTokenIndex(const QVector<CompletionToken> &tokens, const QString &text, int cursor)
{
    for (int i = 0; i < tokens.size(); ++i) {
        const CompletionToken &token = tokens.at(i);
        const bool atEnd = cursor == token.end
                && (cursor == text.size() || !text.at(cursor).isSpace());
        if ((cursor >= token.start && cursor < token.end) || atEnd)
            return i;
        if (token.end <= cursor && (i + 1 == tokens.size() || tokens.at(i + 1).start > cursor)
            && cursor > token.end) {
            return i + 1;
        }
    }
    return tokens.size();
}

QStringList filterCompletionValues(const QStringList &values, const QString &prefix)
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
        if (result.size() >= 50)
            break;
    }
    return result;
}

} // namespace

WebLocalCliCommands::Completion WebLocalCliCommands::complete(const QString &text, int cursor) const
{
    Completion result;
    result.appendSpace = true;
    const int clamped = qBound(0, cursor, text.size());
    result.replaceFrom = clamped;
    result.replaceTo = clamped;

    const QVector<CompletionToken> tokens = tokenizeWithSpans(text);
    const int index = completionTokenIndex(tokens, text, clamped);

    QStringList commandNames;
    const QVector<CommandHelp> commands = commandHelps();
    for (const CommandHelp &command : commands)
        commandNames.append(command.name);

    if (tokens.isEmpty() || index <= 0) {
        if (tokens.isEmpty()) {
            result.values = filterCompletionValues(commandNames, QString());
            return result;
        }
        if (index >= tokens.size()) {
            result.values = filterCompletionValues(commandNames, QString());
            return result;
        }
        const CompletionToken &active = tokens.at(index);
        result.replaceFrom = active.start;
        result.replaceTo = active.end;
        result.values = filterCompletionValues(commandNames, text.mid(active.start, clamped - active.start));
        return result;
    }

    QString command = tokens.first().value.trimmed().toLower();
    if (!command.startsWith(QLatin1Char('/')))
        command.prepend(QLatin1Char('/'));

    const CompletionToken active = index < tokens.size() ? tokens.at(index) : CompletionToken();
    result.replaceFrom = index < tokens.size() ? active.start : clamped;
    result.replaceTo = index < tokens.size() ? active.end : clamped;
    const QString prefix = index < tokens.size()
                                   ? text.mid(active.start, qMax(0, clamped - active.start))
                                   : QString();

    if (command == QLatin1String("/help") && index == 1) {
        QStringList values = commandNames;
        for (const QString &name : commandNames) {
            if (name.startsWith(QLatin1Char('/')))
                values.append(name.mid(1));
        }
        result.values = filterCompletionValues(values, prefix);
        return result;
    }

    if (command == QLatin1String("/add_user") && index == 4) {
        result.values = filterCompletionValues(QStringList() << QStringLiteral("en")
                                                             << QStringLiteral("ru")
                                                             << QStringLiteral("ru_stand"),
                                               prefix);
        return result;
    }

    if ((command == QLatin1String("/delete_user") || command == QLatin1String("/reset_password"))
        && index == 1 && m_store) {
        QStringList values;
        const QStringList userIds = m_store->userIds();
        for (const QString &userId : userIds) {
            values.append(userId);
            values.append(QStringLiteral("web:%1").arg(userId));
            const WebSessionStore::Credential credential = m_store->credentialForUser(userId);
            if (credential.isValid())
                values.append(credential.login);
        }
        result.values = filterCompletionValues(values, prefix);
        return result;
    }

    return result;
}

WebLocalCliCommands::Result WebLocalCliCommands::execute(const QString &command) const
{
    const QStringList tokens = tokenize(command);
    Result result;
    if (tokens.isEmpty()) {
        result.text = tr("Command payload is empty.");
        return result;
    }

    const QString name = tokens.first().trimmed().toLower();
    const QStringList args = tokens.mid(1);
    if (name == QLatin1String("/help") || name == QLatin1String("help"))
        return help(args);
    if (name == QLatin1String("/list_users") || name == QLatin1String("list_users"))
        return listUsers();
    if (name == QLatin1String("/add_user") || name == QLatin1String("add_user"))
        return addUser(args);
    if (name == QLatin1String("/delete_user") || name == QLatin1String("delete_user"))
        return deleteUser(args);
    if (name == QLatin1String("/reset_password") || name == QLatin1String("reset_password"))
        return resetPassword(args);

    result.text = tr("Unknown command: %1\nUse /help to list web Local CLI commands.").arg(name);
    return result;
}

WebLocalCliCommands::Result WebLocalCliCommands::help(const QStringList &args) const
{
    Result result;
    const QVector<CommandHelp> commands = commandHelps();

    if (!args.isEmpty()) {
        QString normalized = args.first().trimmed().toLower();
        if (!normalized.startsWith(QLatin1Char('/')))
            normalized.prepend(QLatin1Char('/'));

        for (const CommandHelp &command : commands) {
            if (command.name != normalized)
                continue;

            QStringList lines;
            if (!command.description.isEmpty())
                lines << command.description;
            if (!command.usage.isEmpty())
                lines << tr("Usage: %1").arg(command.usage);
            for (const QString &detail : command.details) {
                if (!detail.trimmed().isEmpty())
                    lines << detail;
            }
            result.ok = true;
            result.text = lines.join(QLatin1Char('\n'));
            return result;
        }

        result.text = tr("Unknown command: %1\nUse /help to list web Local CLI commands.")
                              .arg(normalized);
        return result;
    }

    QStringList lines;
    lines << tr("Web Local CLI commands:");
    for (const CommandHelp &command : commands)
        lines << QStringLiteral("%1 - %2").arg(command.usage, command.description);
    lines << QString();
    lines << tr("Tip: use /help <command> to see arguments and examples.");

    result.ok = true;
    result.text = lines.join(QLatin1Char('\n'));
    return result;
}

WebLocalCliCommands::Result WebLocalCliCommands::listUsers() const
{
    Result result;
    if (!m_store) {
        result.text = tr("Web authentication is not initialized.");
        return result;
    }

    const QStringList userIds = m_store->userIds();
    if (userIds.isEmpty()) {
        result.ok = true;
        result.text = tr("No web users.");
        return result;
    }

    const QString headerLogin = tr("Login");
    const QString headerUserId = tr("User ID");
    const QString headerName = tr("Name");
    const QString headerLocale = tr("Locale");
    const QString headerProvider = tr("Provider");
    const QString headerPassword = tr("Password");

    int loginWidth = qMax(20, int(headerLogin.size()));
    int userWidth = qMax(40, int(headerUserId.size()));
    int nameWidth = qMax(14, int(headerName.size()));
    int localeWidth = qMax(7, int(headerLocale.size()));
    int providerWidth = qMax(15, int(headerProvider.size()));

    QVector<UserRow> rows;
    rows.reserve(userIds.size());
    for (const QString &userId : userIds) {
        const WebSessionStore::User user = m_store->user(userId);
        const WebSessionStore::Credential credential = m_store->credentialForUser(userId);
        UserRow row;
        row.login = credential.isValid() ? credential.login : QStringLiteral("-");
        row.userKey = QStringLiteral("web:%1").arg(user.userId);
        row.name = QStringLiteral("%1 %2").arg(user.firstName, user.lastName).simplified();
        row.locale = user.locale;
        row.provider = user.provider;
        row.password = QStringLiteral("-");
        if (credential.isValid()) {
            row.password = WebPasswordHasher::isPendingPasswordHash(credential.passwordHash)
                                   ? tr("pending")
                                   : tr("set");
        }
        loginWidth = qMax(loginWidth, int(row.login.size()));
        userWidth = qMax(userWidth, int(row.userKey.size()));
        nameWidth = qMax(nameWidth, int(row.name.size()));
        localeWidth = qMax(localeWidth, int(row.locale.size()));
        providerWidth = qMax(providerWidth, int(row.provider.size()));
        rows << row;
    }

    QStringList lines;
    lines << formatUserColumns(headerLogin,
                               headerUserId,
                               headerName,
                               headerLocale,
                               headerProvider,
                               headerPassword,
                               loginWidth,
                               userWidth,
                               nameWidth,
                               localeWidth,
                               providerWidth);
    for (const UserRow &row : rows) {
        lines << formatUserColumns(row.login,
                                   row.userKey,
                                   row.name,
                                   row.locale,
                                   row.provider,
                                   row.password,
                                   loginWidth,
                                   userWidth,
                                   nameWidth,
                                   localeWidth,
                                   providerWidth);
    }

    result.ok = true;
    result.text = lines.join(QLatin1Char('\n'));
    return result;
}

WebLocalCliCommands::Result WebLocalCliCommands::addUser(const QStringList &args) const
{
    Result result;
    if (!localCredentialsEnabled()) {
        result.text = tr("A login authorization method is not enabled.");
        return result;
    }
    if (!m_authManager) {
        result.text = tr("Web authentication is not initialized.");
        return result;
    }
    if (args.size() < 2) {
        result.text = tr("Usage: /add_user <login> <first_name> [last_name] [locale]");
        return result;
    }

    QJsonObject request;
    request.insert(QStringLiteral("login"), args.at(0));
    request.insert(QStringLiteral("first_name"), args.at(1));
    if (args.size() > 2)
        request.insert(QStringLiteral("last_name"), args.at(2));
    if (args.size() > 3)
        request.insert(QStringLiteral("locale"), args.at(3));

    const WebAuthManager::AuthResult auth =
            m_authManager->addLocalPasswordUserWithoutPassword(request);
    if (!auth.ok) {
        result.text = auth.error;
        return result;
    }

    result.ok = true;
    result.text = tr("Created web user web:%1 with login \"%2\".\nSet-password link:\n%3%4")
                          .arg(auth.userId,
                               args.at(0),
                               passwordUrl(QStringLiteral("/set-password.html"), auth.sessionToken),
                               originNote());
    return result;
}

WebLocalCliCommands::Result WebLocalCliCommands::deleteUser(const QStringList &args) const
{
    Result result;
    if (!m_store || !m_authManager) {
        result.text = tr("Web authentication is not initialized.");
        return result;
    }
    if (args.isEmpty()) {
        result.text = tr("Usage: /delete_user <login|user_id|web:uuid>");
        return result;
    }

    const QString userId = m_store->resolveUserId(args.at(0));
    if (userId.isEmpty()) {
        result.text = tr("Web user was not found.");
        return result;
    }

    if (!m_authManager->deleteUser(userId)) {
        result.text = m_store->lastError().isEmpty() ? tr("Failed to delete web user.")
                                                     : m_store->lastError();
        return result;
    }

    result.ok = true;
    result.text = tr("Deleted web user web:%1.").arg(userId);
    return result;
}

WebLocalCliCommands::Result WebLocalCliCommands::resetPassword(const QStringList &args) const
{
    Result result;
    if (!localCredentialsEnabled()) {
        result.text = tr("A login authorization method is not enabled.");
        return result;
    }
    if (!m_store || !m_authManager) {
        result.text = tr("Web authentication is not initialized.");
        return result;
    }
    if (args.isEmpty()) {
        result.text = tr("Usage: /reset_password <login|user_id|web:uuid> [login]");
        return result;
    }

    const QString userId = m_store->resolveUserId(args.at(0));
    if (userId.isEmpty()) {
        result.text = tr("Web user was not found.");
        return result;
    }

    const WebSessionStore::Credential credential = m_store->credentialForUser(userId);
    WebAuthManager::AuthResult auth;
    QString page = QStringLiteral("/reset-password.html");
    QString purposeLabel = tr("Reset-password");
    if (credential.isValid()) {
        auth = m_authManager->issuePasswordToken(userId, QStringLiteral("reset_password"));
    } else {
        if (args.size() < 2) {
            result.text = tr("Users without a login require a login: "
                             "/reset_password <user_id|web:uuid> <login>");
            return result;
        }
        auth = m_authManager->attachLocalPasswordLogin(userId, args.at(1));
        page = QStringLiteral("/set-password.html");
        purposeLabel = tr("Set-password");
    }

    if (!auth.ok) {
        result.text = auth.error;
        return result;
    }

    result.ok = true;
    result.text = tr("%1 link for web:%2:\n%3%4")
                          .arg(purposeLabel,
                               auth.userId,
                               passwordUrl(page, auth.sessionToken),
                               QStringLiteral("\n")
                                       + tr("The link is valid for %n day.",
                                            "The link is valid for %n days.",
                                            WebSessionStore::kPasswordResetTokenValidDays)
                                       + originNote());
    return result;
}

bool WebLocalCliCommands::localCredentialsEnabled() const
{
    return WebAuthMethods::allowsLocalCredentials(m_authMethods);
}

QString WebLocalCliCommands::passwordUrl(const QString &page, const QString &token) const
{
    const QString origin =
            m_publicOrigin.isEmpty() ? QStringLiteral("http://127.0.0.1") : m_publicOrigin;
    QUrl url(origin + page);
    url.setQuery(QStringLiteral("token=%1").arg(QString::fromUtf8(QUrl::toPercentEncoding(token))));
    return url.toString(QUrl::FullyEncoded);
}

QString WebLocalCliCommands::originNote() const
{
    const QString origin = m_publicOrigin.toLower();
    if (origin.contains(QLatin1String("127.0.0.1")) || origin.contains(QLatin1String("localhost"))
        || m_publicOrigin.isEmpty()) {
        return QStringLiteral("\n")
               + tr("This link uses a local origin. Set web/public_https_host to share it "
                    "outside this machine.");
    }
    return QString();
}

QStringList WebLocalCliCommands::tokenize(const QString &text)
{
    QStringList tokens;
    QString current;
    QChar quote;
    bool inQuotes = false;

    for (const QChar ch : text) {
        if (inQuotes) {
            if (ch == quote) {
                inQuotes = false;
                continue;
            }
            current.append(ch);
            continue;
        }

        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            inQuotes = true;
            quote = ch;
            continue;
        }
        if (ch.isSpace()) {
            if (!current.isEmpty()) {
                tokens.append(unquote(current));
                current.clear();
            }
            continue;
        }
        current.append(ch);
    }

    if (!current.isEmpty())
        tokens.append(unquote(current));
    return tokens;
}
