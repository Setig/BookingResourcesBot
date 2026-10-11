#ifndef MESSENGERABSTRACTCOMMAND_H
#define MESSENGERABSTRACTCOMMAND_H

#include "AbstractBot.h"

#include "User.h"
#include "Group.h"
#include "UserTimeZone.h"
#include "MessengerType.h"
#include "MessengerCommandParser.h"
#include "MessengerCommandAudience.h"
#include "MessengerCommandStaticRegistration.h"

#include <QMap>
#include <QSet>
#include <QList>
#include <QString>
#include <QStringList>
#include <QCoreApplication>

class QDateTime;

class User;
class Resource;
class MessengerBot;
class MessengerCommandManager;

/**
 * @brief Base abstraction for a messenger command.
 *
 * Each command implementation owns its parser, access rules and execution handler.
 * MessengerAbstractCommand provides access to the shared manager API and a set of common helpers
 * to reduce duplication across commands.
 */
class MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerAbstractCommand)

public:
    explicit MessengerAbstractCommand(MessengerCommandManager *manager,
                                      MessengerCommandAudience audience,
                                      uint priority);
    virtual ~MessengerAbstractCommand();

    MessengerCommandManager *manager() const;
    MessengerBot *bot() const;

    const MessengerCommandParser &parser() const;
    MessengerCommandAudience audience() const;
    QString command() const;
    /**
     * @brief Returns command priority used by /help sorting.
     *
     * Lower value means higher priority.
     */
    uint priority() const;

    virtual bool isEnabled(const AbstractBot::IncomingMessage *message = nullptr) const;
    /**
     * @brief Returns a user-facing reason when the command exists but is currently disabled.
     *
     * Empty means /help should treat the command as unknown.
     */
    virtual QString disabledUserFacingReason(const AbstractBot::IncomingMessage &message) const;

    MessengerCommandParser::ParseResult parseArguments(const QString &args) const;
    static bool parseDurationMinutesToken(const QString &token, int *durationMinutes);

    virtual void execute(const AbstractBot::IncomingMessage &message, const QString &args) = 0;
    virtual QStringList details() const = 0;
    virtual QStringList examples() const = 0;

protected:
    void initParser(QString command, QString description = QString());
    virtual void configureParser(MessengerCommandParser &parser) const;

    bool fetchMessengerUserProfile(MessengerType messenger,
                                   const QString &userId,
                                   AbstractBot::UserProfile *profileOut,
                                   QString *errorOut) const;

    static bool parseBoolToken(const QString &token, bool defaultValue = false);
    static bool parseBoolTokenValue(const QString &token, bool *valueOut);
    static QString supportedMessengerList();
    static bool parseMessengerToken(const QString &token, MessengerType *messengerOut);
    static bool parseGroupListToken(const QString &token,
                                    QSet<Group> *groupsOut,
                                    QString *errorOut = nullptr);
    static bool parseUserGroupRolesToken(const QString &token,
                                         QMap<Group, User::Role> *rolesOut,
                                         QString *errorOut = nullptr);
    static QString formatGroupsList(const QSet<Group> &groups);
    static QString formatUserGroupRoles(const QMap<Group, User::Role> &roles);

    static QString formatUserLine(const User &user, bool html = false);
    static QString formatResourceLine(const Resource &resource);

    static bool parseBlockDurationToken(const QString &token, QDateTime *untilUtcOut);
    static bool parseDurationChoice(const QString &token,
                                    const QList<int> &options,
                                    int *durationMinutes);
    static bool isAfterReleaseStartToken(const QString &token);
    static bool parseScheduleDateTimeToken(const QString &token,
                                           const QString &dateTimeFormat,
                                           QDateTime *utcOut,
                                           const UserTimeZone &timeZone = UserTimeZone());
    static QString buildDurationPrompt(const QString &resourceName, const QList<int> &options);

    void setParserCommand(const QString &command);

private:
    MessengerCommandManager *m_manager; // non-owning
    MessengerCommandParser m_parser;
    MessengerCommandAudience m_audience;
    uint m_priority;
};

#endif // MESSENGERABSTRACTCOMMAND_H
