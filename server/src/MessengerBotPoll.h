#ifndef MESSENGERBOTPOLL_H
#define MESSENGERBOTPOLL_H

#include "MessengerType.h"

#include <QList>
#include <QObject>
#include <QString>

class AbstractBot;
class MessengerBot;
class ServerDatabase;
class MessengerBotPollPrivate;
class BookingResourcesTranslator;

/**
 * @brief Stores messenger bot wrappers and their backend lookup by messenger type.
 */
class MessengerBotPoll : public QObject
{
    Q_OBJECT

public:
    explicit MessengerBotPoll(QObject *parent = nullptr);
    ~MessengerBotPoll() override;

    void setDatabase(ServerDatabase *database);
    ServerDatabase *database() const;

    void setTranslator(BookingResourcesTranslator *translator);
    BookingResourcesTranslator *translator() const;

    MessengerBot *createBot(MessengerType type, const QString &token, QObject *parent = nullptr);
    MessengerBot *bot(MessengerType type, const QString &token) const;
    QList<MessengerBot *> bots() const;

    AbstractBot *backendBot(MessengerType type) const;

private:
    MessengerBotPollPrivate *d;
};

#endif // MESSENGERBOTPOLL_H
