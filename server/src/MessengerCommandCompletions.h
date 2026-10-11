#ifndef MESSENGERCOMMANDCOMPLETIONS_H
#define MESSENGERCOMMANDCOMPLETIONS_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "MessengerUserKey.h"

class MessengerCommandManager;

/**
 * @brief Shared slash-command completion tables for Web UI and Local CLI.
 */
namespace MessengerCommandCompletions {

/**
 * @brief Result of evaluating a command line at a cursor.
 */
struct Result
{
    int replaceFrom = 0;
    int replaceTo = 0;
    QStringList values;
    bool appendSpace = true;
};

[[nodiscard]] QJsonArray argumentCompletionsToJson(const QString &commandName,
                                                   const MessengerCommandManager *manager,
                                                   const MessengerUserKey &user,
                                                   const QStringList &availableCommandNames);
[[nodiscard]] bool conditionsMatch(const QJsonObject &when, const QStringList &previousValues);
[[nodiscard]] Result complete(const MessengerCommandManager *manager,
                              const MessengerUserKey &user,
                              const QString &text,
                              int cursor,
                              bool localCliOnly);

} // namespace MessengerCommandCompletions

#endif // MESSENGERCOMMANDCOMPLETIONS_H
