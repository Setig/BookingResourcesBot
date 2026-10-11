#ifndef SERVERSETTINGENTRY_H
#define SERVERSETTINGENTRY_H

#include <QString>
#include <QVariant>

/**
 * @brief Represents a runtime server setting.
 *
 * The value is always stored in normalized form:
 * - key is trimmed
 */
class ServerSettingEntry final
{
public:
    ServerSettingEntry();
    explicit ServerSettingEntry(const QString &key, const QVariant &value = QVariant());

    ServerSettingEntry(const ServerSettingEntry &other);
    ServerSettingEntry(ServerSettingEntry &&other) noexcept;
    ServerSettingEntry &operator=(const ServerSettingEntry &other);
    ServerSettingEntry &operator=(ServerSettingEntry &&other) noexcept;

    ~ServerSettingEntry();

    [[nodiscard]] QString key() const;
    [[nodiscard]] QVariant value() const;

    void setKey(const QString &key);
    void setValue(const QVariant &value);

    [[nodiscard]] bool isValid() const;

private:
    QString m_key;
    QVariant m_value;
};

#endif // SERVERSETTINGENTRY_H
