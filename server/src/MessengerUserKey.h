#ifndef MESSENGERUSERKEY_H
#define MESSENGERUSERKEY_H

#include <QString>

#include <QMetaType>
#include <QHashFunctions>

#include "MessengerType.h"

/**
 * @brief Composite key identifying a user within a specific messenger.
 *
 * The stable textual representation is "<messenger>:<user_id>", e.g. "telegram:123".
 * Use fromString() to parse user-provided text.
 */
class MessengerUserKey
{
public:
    MessengerUserKey();
    explicit MessengerUserKey(MessengerType messenger, const QString &userId);
    ~MessengerUserKey();

    MessengerUserKey(const MessengerUserKey &);
    MessengerUserKey(MessengerUserKey &&) noexcept;
    MessengerUserKey &operator=(const MessengerUserKey &);
    MessengerUserKey &operator=(MessengerUserKey &&) noexcept;

    [[nodiscard]] MessengerType messenger() const;
    void setMessenger(MessengerType messenger);

    [[nodiscard]] const QString &userId() const;
    void setUserId(const QString &userId);

    [[nodiscard]] bool isValid() const;

    /**
     * @brief Converts the key to a stable textual representation.
     *
     * @return String in the form "<messenger>:<user_id>" or empty string if the key is invalid.
     */
    [[nodiscard]] QString toString() const;

    /**
     * @brief Parses a user key from text.
     *
     * Accepts "<messenger>:<user_id>" (e.g. "telegram:123").
     *
     * The function does not try to extract a key from a longer text. The trimmed input string
     * must be exactly the supported format.
     *
     * @param text User-provided text.
     * @param isOk Optional output flag indicating whether parsing succeeded.
     * @return Parsed key. If parsing fails, returns an invalid key (empty userId).
     */
    static MessengerUserKey fromString(const QString &text, bool *isOk = nullptr);

    bool operator==(const MessengerUserKey &other) const;
    bool operator!=(const MessengerUserKey &other) const;

private:
    MessengerType m_messenger;
    QString m_userId;
};

uint qHash(const MessengerUserKey &key, uint seed = 0) noexcept;

Q_DECLARE_METATYPE(MessengerUserKey)

#endif // MESSENGERUSERKEY_H
