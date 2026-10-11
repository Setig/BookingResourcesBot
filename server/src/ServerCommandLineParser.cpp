#include "ServerCommandLineParser.h"

#include "ServerConfig.h"
#include "MessengerUserKey.h"

#include <QSet>
#include <QTextStream>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QCommandLineOption>
#include <QCommandLineParser>

Q_LOGGING_CATEGORY(lcServerCmdLine, "booking.server.cmdline")

class ServerCommandLineParserPrivate
{
public:
    ServerCommandLineParserPrivate();

    QCommandLineParser parser;
    QCommandLineOption configFileOption;
    QCommandLineOption logFileOption;
    QCommandLineOption dbFileOption;
    QCommandLineOption webSharedMemoryKeyOption;
    QCommandLineOption webSharedMemorySecretOption;
    QCommandLineOption telegramTokenOption;
    QCommandLineOption konturTalkTokenOption;
    QCommandLineOption genericAdminOption;
};

namespace {

QString lookupSourceDescription(const ServerConfig::LookupResult &lookup)
{
    QString description = LayeredIniSettings::sourceName(lookup.source);
    if (!lookup.fileName.isEmpty())
        description += QStringLiteral(" %1").arg(lookup.fileName);
    return description;
}

QStringList botTokenConfigKeys(MessengerType type)
{
    QStringList keys;
    keys.append(QStringLiteral("bots/%1/token").arg(messengerTypeToString(type)));
    if (type == MessengerType::Telegram)
        keys.append(QStringLiteral("telegram/token"));
    return keys;
}

QStringList adminIdConfigKeys(MessengerType type)
{
    QStringList keys;
    keys.append(QStringLiteral("bots/%1/admin_ids").arg(messengerTypeToString(type)));
    if (type == MessengerType::Telegram)
        keys.append(QStringLiteral("telegram/admin_ids"));
    return keys;
}

} // namespace

ServerCommandLineParserPrivate::ServerCommandLineParserPrivate()
    : configFileOption({ QStringLiteral("config-file") },
                       QStringLiteral("Path to the INI configuration file."),
                       QStringLiteral("file"))
    , logFileOption({ QStringLiteral("l"), QStringLiteral("log-file") },
                    QStringLiteral("Path to log file for redirecting stdout and stderr."),
                    QStringLiteral("file"))
    , dbFileOption({ QStringLiteral("db-file") },
                   QStringLiteral("Path to SQLite database file."),
                   QStringLiteral("file"))
    , webSharedMemoryKeyOption({ QStringLiteral("web-shared-memory-key") },
                               QStringLiteral("Shared memory key for the web IPC transport."),
                               QStringLiteral("key"))
    , webSharedMemorySecretOption({ QStringLiteral("web-shared-memory-secret") },
                                  QStringLiteral(
                                          "Shared secret used to authenticate web IPC frames."),
                                  QStringLiteral("secret"))
    , telegramTokenOption({ QStringLiteral("telegram-token") },
                          QStringLiteral("Telegram bot API token."),
                          QStringLiteral("token"))
    , konturTalkTokenOption({ QStringLiteral("kontur-talk-token") },
                            QStringLiteral("Kontur.Talk bot JWT token."),
                            QStringLiteral("token"))
    , genericAdminOption({ QStringLiteral("admin") },
                         QStringLiteral("Admin account in the form '<messenger>:<user_id>'. Can be "
                                        "provided multiple times."),
                         QStringLiteral("messenger:user_id"))
{
    parser.setApplicationDescription(
            QStringLiteral("Resources bot that can connect to multiple messengers."));
    parser.addHelpOption();
    parser.addVersionOption();

    parser.addOption(configFileOption);
    parser.addOption(logFileOption);
    parser.addOption(dbFileOption);
    parser.addOption(webSharedMemoryKeyOption);
    parser.addOption(webSharedMemorySecretOption);
    parser.addOption(telegramTokenOption);
    parser.addOption(konturTalkTokenOption);
    parser.addOption(genericAdminOption);
}

ServerCommandLineParser::ServerCommandLineParser()
    : d(new ServerCommandLineParserPrivate())
{
    // do nothing
}

ServerCommandLineParser::~ServerCommandLineParser()
{
    delete d;
}

void ServerCommandLineParser::process(const QCoreApplication &app)
{
    process(app.arguments());
}

void ServerCommandLineParser::process(const QStringList &arguments)
{
    if (!d->parser.parse(arguments)) {
        QTextStream ts(stderr);
        ts << QCoreApplication::applicationName() << ": " << d->parser.errorText() << Qt::endl;
        ts << "Use '--help' to see available options." << Qt::endl;
        QCoreApplication::exit(1);
        return;
    }

    if (d->parser.isSet(QStringLiteral("help"))) {
        d->parser.showHelp();
    }

    if (d->parser.isSet(QStringLiteral("version"))) {
        d->parser.showVersion();
    }
}

QString ServerCommandLineParser::configFilePath() const
{
    if (!d->parser.isSet(d->configFileOption))
        return QString();

    return d->parser.value(d->configFileOption).trimmed();
}

QString ServerCommandLineParser::logFilePath(const ServerConfig &config) const
{
    if (d->parser.isSet(d->logFileOption)) {
        return d->parser.value(d->logFileOption);
    }

    return config.logFile();
}

QString ServerCommandLineParser::databaseFilePath(const ServerConfig &config) const
{
    if (d->parser.isSet(d->dbFileOption)) {
        qCInfo(lcServerCmdLine) << "Using database file from command line option '--db-file'.";
        return d->parser.value(d->dbFileOption);
    }

    const ServerConfig::LookupResult lookup = config.lookupValue(QStringLiteral("database/file"));
    const QString configured = lookup.value.toString();
    if (!configured.isEmpty()) {
        qCInfo(lcServerCmdLine).nospace()
                << "Using database file from " << lookupSourceDescription(lookup) << ".";
    }
    return configured;
}

QString ServerCommandLineParser::webSharedMemoryKey(const ServerConfig &config) const
{
    if (d->parser.isSet(d->webSharedMemoryKeyOption)) {
        const QString key = d->parser.value(d->webSharedMemoryKeyOption).trimmed();
        qCInfo(lcServerCmdLine).nospace()
                << "Using web shared memory key from command line option, key: \""
                << qPrintable(key) << "\".";
        return key;
    }

    const ServerConfig::LookupResult lookup =
            config.lookupValue(QStringLiteral("web/shared_memory_key"));
    const QString configured = lookup.value.toString().trimmed();
    if (!configured.isEmpty()) {
        qCInfo(lcServerCmdLine).nospace()
                << "Using web shared memory key from " << lookupSourceDescription(lookup)
                << ", key: \"" << qPrintable(configured) << "\".";
    }
    return configured;
}

QString ServerCommandLineParser::webSharedMemorySecret(const ServerConfig &config) const
{
    if (d->parser.isSet(d->webSharedMemorySecretOption)) {
        qCInfo(lcServerCmdLine) << "Using web shared memory secret from command line option.";
        return d->parser.value(d->webSharedMemorySecretOption);
    }

    const ServerConfig::LookupResult lookup =
            config.lookupValue(QStringLiteral("web/shared_memory_secret"));
    const QString configured = lookup.value.toString();
    if (!configured.isEmpty()) {
        qCInfo(lcServerCmdLine).nospace()
                << "Using web shared memory secret from " << lookupSourceDescription(lookup) << ".";
    }
    return configured;
}

QHash<MessengerType, QString> ServerCommandLineParser::botTokens(const ServerConfig &config) const
{
    QHash<MessengerType, QString> tokens;

    const auto optionForMessenger = [&](MessengerType type) -> const QCommandLineOption * {
        switch (type) {
        case MessengerType::LocalCli:
            return nullptr;
        case MessengerType::Telegram:
            return &d->telegramTokenOption;
        case MessengerType::KonturTalk:
            return &d->konturTalkTokenOption;
        case MessengerType::Web:
            return nullptr;
        }
        return nullptr;
    };

    const auto optionLabel = [](const QCommandLineOption *option) -> QString {
        if (!option)
            return QString();
        const QStringList names = option->names();
        if (names.isEmpty())
            return QString();
        const QString name = names.constFirst();
        if (name.size() == 1)
            return QStringLiteral("-%1").arg(name);
        return QStringLiteral("--%1").arg(name);
    };

    for (MessengerType type : allMessengerTypes()) {
        const QCommandLineOption *option = optionForMessenger(type);
        if (!option)
            continue;

        QString token;

        if (d->parser.isSet(*option)) {
            token = d->parser.value(*option).trimmed();
            qCInfo(lcServerCmdLine).nospace() << "Using " << messengerTypeDisplayName(type)
                                              << " token from command line option \""
                                              << qPrintable(optionLabel(option)) << "\".";
        } else {
            const ServerConfig::LookupResult lookup = config.lookupAnyValue(botTokenConfigKeys(type));
            token = lookup.value.toString().trimmed();
            if (!token.isEmpty()) {
                qCInfo(lcServerCmdLine).nospace()
                        << "Using " << messengerTypeDisplayName(type)
                        << " token from " << lookupSourceDescription(lookup) << ".";
            }
        }

        if (!token.isEmpty())
            tokens.insert(type, token);
    }

    return tokens;
}

QHash<MessengerType, QStringList> ServerCommandLineParser::adminUserIds(
        const ServerConfig &config) const
{
    QHash<MessengerType, QStringList> admins;
    QHash<MessengerType, QSet<QString>> seen;

    const auto addAdmin = [&](MessengerType messenger, const QString &rawId) {
        const QString id = rawId.trimmed();
        if (id.isEmpty())
            return;
        QSet<QString> &unique = seen[messenger];
        if (unique.contains(id))
            return;
        unique.insert(id);
        admins[messenger].append(id);
    };

    for (MessengerType messenger : allMessengerTypes()) {
        const ServerConfig::LookupResult lookup = config.lookupAnyValue(adminIdConfigKeys(messenger));
        const QStringList stored = config.adminUserIds(messenger);
        if (!stored.isEmpty()) {
            qCInfo(lcServerCmdLine).nospace()
                    << "Using " << messengerTypeDisplayName(messenger)
                    << " admin ids from " << lookupSourceDescription(lookup) << ".";
        }
        for (const QString &id : stored) {
            addAdmin(messenger, id);
        }
    }

    if (d->parser.isSet(d->genericAdminOption)) {
        const QStringList values = d->parser.values(d->genericAdminOption);
        qCInfo(lcServerCmdLine) << "Parsing admin descriptors from '--admin' options.";
        for (const QString &entry : values) {
            bool ok = false;
            MessengerUserKey key = MessengerUserKey::fromString(entry, &ok);
            if (!ok) {
                qCWarning(lcServerCmdLine).nospace()
                        << "Invalid admin descriptor \"" << qPrintable(entry)
                        << "\". Expected format '<messenger>:<user_id>'.";
                continue;
            }

            addAdmin(key.messenger(), key.userId());
        }
    }

    return admins;
}
