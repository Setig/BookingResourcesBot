#include "MessengerBotPoll.h"

#include "AbstractBot.h"
#include "MessengerBot.h"
#include "ServerDatabase.h"
#include "BookingResourcesTranslator.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QPointer>

class MessengerBotPollPrivate
{
public:
    MessengerBotPollPrivate();
    ~MessengerBotPollPrivate();

    QString normalizeToken(const QString &token) const;

    QPointer<ServerDatabase> database;
    BookingResourcesTranslator *translator; // non-owning
    QHash<MessengerType, QPointer<MessengerBot>> bots;
};

MessengerBotPollPrivate::MessengerBotPollPrivate()
    : translator(nullptr)
{
    // do nothing
}

MessengerBotPollPrivate::~MessengerBotPollPrivate()
{
    // do nothing
}

QString MessengerBotPollPrivate::normalizeToken(const QString &token) const
{
    return token.trimmed();
}

MessengerBotPoll::MessengerBotPoll(QObject *parent)
    : QObject(parent)
    , d(new MessengerBotPollPrivate)
{
    // do nothing
}

MessengerBotPoll::~MessengerBotPoll()
{
    const QList<MessengerBot *> storedBots = bots();
    for (MessengerBot *bot : storedBots)
        delete bot;

    delete d;
}

void MessengerBotPoll::setDatabase(ServerDatabase *database)
{
    d->database = database;
}

ServerDatabase *MessengerBotPoll::database() const
{
    return d->database.data();
}

void MessengerBotPoll::setTranslator(BookingResourcesTranslator *translator)
{
    d->translator = translator;
}

BookingResourcesTranslator *MessengerBotPoll::translator() const
{
    return d->translator;
}

MessengerBot *MessengerBotPoll::createBot(MessengerType type, const QString &token, QObject *parent)
{
    const QString normalizedToken = d->normalizeToken(token);
    if (normalizedToken.isEmpty())
        return nullptr;

    if (MessengerBot *existing = d->bots.value(type).data())
        return existing;

    QObject *botParent = parent ? parent : this;
    MessengerBot *newBot = new MessengerBot(type,
                                            normalizedToken,
                                            d->database.data(),
                                            d->translator,
                                            botParent,
                                            this);
    d->bots.insert(type, newBot);
    return newBot;
}

MessengerBot *MessengerBotPoll::bot(MessengerType type, const QString &token) const
{
    const QString normalizedToken = d->normalizeToken(token);
    if (normalizedToken.isEmpty())
        return nullptr;

    return d->bots.value(type).data();
}

QList<MessengerBot *> MessengerBotPoll::bots() const
{
    QList<MessengerBot *> result;

    for (QHash<MessengerType, QPointer<MessengerBot>>::const_iterator it = d->bots.cbegin();
         it != d->bots.cend();
         ++it) {
        MessengerBot *bot = it.value().data();
        if (!bot)
            continue;

        result.append(bot);
    }

    return result;
}

AbstractBot *MessengerBotPoll::backendBot(MessengerType type) const
{
    MessengerBot *bot = d->bots.value(type).data();
    return bot ? bot->backendBot() : nullptr;
}
