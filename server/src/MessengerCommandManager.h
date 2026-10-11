#ifndef MESSENGERCOMMANDMANAGER_H
#define MESSENGERCOMMANDMANAGER_H

#include <QObject>

#include <QVector>

#include "AbstractBot.h"

class MessengerBot;
class MessengerAbstractCommand;

class MessengerCommandManagerPrivate;

/**
 * @brief Registry of bot commands with argument parsing and access control.
 *
 * MessengerCommandManager maps command names to command implementations and provides command
 * descriptors for syncing to supported messengers.
 */
class MessengerCommandManager : public QObject
{
    Q_OBJECT

public:
    explicit MessengerCommandManager(MessengerBot *bot);
    ~MessengerCommandManager() override;

    MessengerBot *bot() const;

    const QVector<MessengerAbstractCommand *> &commands() const;

    /**
     * @brief Rebuilds registered command metadata with the currently active translator.
     *
     * Command descriptions are captured when command instances are constructed, so callers that
     * render command metadata under a scoped locale should refresh the registry first.
     */
    void refreshCommandTranslations() const;

    /**
     * @brief Handles a slash-command message (starting with '/').
     *
     * Returns true if the message was a command (handled or rejected with a reply),
     * false if the message does not look like a command and should be processed elsewhere.
     */
    bool handleCommandMessage(const AbstractBot::IncomingMessage &message);

private:
    MessengerCommandManagerPrivate *d;
};

#endif // MESSENGERCOMMANDMANAGER_H
