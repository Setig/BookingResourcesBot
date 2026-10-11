#include "CliHistory.h"

namespace {

constexpr int kDefaultMaximumCount = 1000;

} // namespace

CliHistory::CliHistory()
    : m_maximumCount(kDefaultMaximumCount)
    , m_navigationIndex(0)
    , m_navigating(false)
{
    resetNavigation();
}

CliHistory::~CliHistory()
{
    // do nothing
}

void CliHistory::setMaximumCount(int count)
{
    m_maximumCount = qMax(1, count);
    trimToMaximum();
}

int CliHistory::maximumCount() const
{
    return m_maximumCount;
}

void CliHistory::add(const QString &line)
{
    if (line.trimmed().isEmpty())
        return;
    if (!m_entries.isEmpty() && m_entries.last() == line)
        return;

    m_entries.append(line);
    trimToMaximum();
    resetNavigation();
}

QStringList CliHistory::entries() const
{
    return m_entries;
}

int CliHistory::count() const
{
    return m_entries.size();
}

void CliHistory::beginNavigation(const QString &currentLine)
{
    if (m_navigating)
        return;

    m_navigationDraft = currentLine;
    m_navigationIndex = m_entries.size();
    m_navigating = true;
}

QString CliHistory::moveUp()
{
    if (m_entries.isEmpty())
        return isNavigating() ? m_navigationDraft : QString();

    if (!isNavigating())
        beginNavigation(QString());

    if (m_navigationIndex > 0)
        --m_navigationIndex;
    return m_entries.at(m_navigationIndex);
}

QString CliHistory::moveDown()
{
    if (!isNavigating())
        return QString();

    if (m_navigationIndex < m_entries.size())
        ++m_navigationIndex;
    if (m_navigationIndex >= m_entries.size())
        return m_navigationDraft;
    return m_entries.at(m_navigationIndex);
}

void CliHistory::resetNavigation()
{
    m_navigationIndex = m_entries.size();
    m_navigating = false;
    m_navigationDraft.clear();
}

bool CliHistory::isNavigating() const
{
    return m_navigating;
}

void CliHistory::trimToMaximum()
{
    if (m_maximumCount <= 0)
        return;
    while (m_entries.size() > m_maximumCount)
        m_entries.removeFirst();
    if (m_navigationIndex > m_entries.size())
        m_navigationIndex = m_entries.size();
}
