#ifndef MESSENGERFREERESOURCESCOMMAND_H
#define MESSENGERFREERESOURCESCOMMAND_H

#include "MessengerAbstractCommand.h"

class MessengerFreeResourcesCommand : public MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerFreeResourcesCommand)

public:
    explicit MessengerFreeResourcesCommand(MessengerCommandManager *manager);

    void execute(const AbstractBot::IncomingMessage &message, const QString &args) override;
    QStringList details() const override;
    QStringList examples() const override;
};

BOOKING_RESOURCES_REGISTER_COMMAND(MessengerFreeResourcesCommand)

#endif // MESSENGERFREERESOURCESCOMMAND_H
