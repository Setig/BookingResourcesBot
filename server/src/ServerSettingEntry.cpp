#include "ServerSettingEntry.h"

ServerSettingEntry::ServerSettingEntry()
{
    // do nothing
}

ServerSettingEntry::ServerSettingEntry(const QString &key, const QVariant &value)
    : m_key(key.trimmed())
    , m_value(value)
{
    // do nothing
}

ServerSettingEntry::ServerSettingEntry(const ServerSettingEntry &other)
    : m_key(other.m_key)
    , m_value(other.m_value)
{
    // do nothing
}

ServerSettingEntry::ServerSettingEntry(ServerSettingEntry &&other) noexcept
    : m_key(std::move(other.m_key))
    , m_value(std::move(other.m_value))
{
    // do nothing
}

ServerSettingEntry &ServerSettingEntry::operator=(const ServerSettingEntry &other)
{
    if (this != &other) {
        m_key = other.m_key;
        m_value = other.m_value;
    }
    return *this;
}

ServerSettingEntry &ServerSettingEntry::operator=(ServerSettingEntry &&other) noexcept
{
    if (this != &other) {
        m_key = std::move(other.m_key);
        m_value = std::move(other.m_value);
    }
    return *this;
}

ServerSettingEntry::~ServerSettingEntry()
{
    // do nothing
}

QString ServerSettingEntry::key() const
{
    return m_key;
}

QVariant ServerSettingEntry::value() const
{
    return m_value;
}

void ServerSettingEntry::setKey(const QString &key)
{
    m_key = key.trimmed();
}

void ServerSettingEntry::setValue(const QVariant &value)
{
    m_value = value;
}

bool ServerSettingEntry::isValid() const
{
    return !m_key.trimmed().isEmpty();
}
