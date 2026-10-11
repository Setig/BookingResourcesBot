#ifndef CLIHISTORY_H
#define CLIHISTORY_H

#include <QString>
#include <QStringList>

/**
 * @brief In-memory command history for the current CLI process.
 *
 * Empty lines and consecutive duplicates are ignored. Navigation keeps the original
 * entries intact until a new line is submitted. Nothing is written to disk.
 */
class CliHistory
{
public:
    CliHistory();
    ~CliHistory();

    CliHistory(const CliHistory &) = delete;
    CliHistory &operator=(const CliHistory &) = delete;
    CliHistory(CliHistory &&) = delete;
    CliHistory &operator=(CliHistory &&) = delete;

    void setMaximumCount(int count);
    [[nodiscard]] int maximumCount() const;

    void add(const QString &line);
    [[nodiscard]] QStringList entries() const;
    [[nodiscard]] int count() const;

    void beginNavigation(const QString &currentLine);
    [[nodiscard]] QString moveUp();
    [[nodiscard]] QString moveDown();
    void resetNavigation();
    [[nodiscard]] bool isNavigating() const;

private:
    void trimToMaximum();

    QStringList m_entries;
    int m_maximumCount;
    int m_navigationIndex;
    bool m_navigating;
    QString m_navigationDraft;
};

#endif // CLIHISTORY_H
