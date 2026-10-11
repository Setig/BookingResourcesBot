#ifndef CLICOMPLETION_H
#define CLICOMPLETION_H

#include <QString>
#include <QStringList>
#include <QVector>

#include "LocalCliRegistry.h"

/**
 * @brief Local CLI completion helpers for meta-commands and token insertion.
 */
namespace CliCompletion {

constexpr int kMaxListedValues = 50;

/**
 * @brief Result of a completion query for the current input line.
 */
struct Result
{
    int replaceFrom = 0;
    int replaceTo = 0;
    QStringList values;
    bool appendSpace = true;
};

[[nodiscard]] Result meta(const QString &line,
                          int cursor,
                          const QVector<LocalCliRegistry::Entry> &entries);
[[nodiscard]] QString quoteToken(const QString &value);
[[nodiscard]] QString commonPrefix(const QStringList &values);
[[nodiscard]] QStringList filterPrefix(const QStringList &values, const QString &prefix);
[[nodiscard]] Result merge(const Result &left, const Result &right);
[[nodiscard]] bool firstTokenIsMeta(const QString &line, int cursor);
[[nodiscard]] bool firstTokenIsSlash(const QString &line, int cursor);

} // namespace CliCompletion

#endif // CLICOMPLETION_H
