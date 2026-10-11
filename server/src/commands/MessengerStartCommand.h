#ifndef MESSENGERSTARTCOMMAND_H
#define MESSENGERSTARTCOMMAND_H

#include "MessengerAbstractCommand.h"

class MessengerStartCommand : public MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerStartCommand)

public:
    explicit MessengerStartCommand(MessengerCommandManager *manager);

    void execute(const AbstractBot::IncomingMessage &message, const QString &args) override;
    QStringList details() const override;
    QStringList examples() const override;
};

BOOKING_RESOURCES_REGISTER_COMMAND(MessengerStartCommand)

#endif // MESSENGERSTARTCOMMAND_H
