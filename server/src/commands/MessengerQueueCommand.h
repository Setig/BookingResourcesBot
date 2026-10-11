#ifndef MESSENGERQUEUECOMMAND_H
#define MESSENGERQUEUECOMMAND_H

#include "MessengerAbstractCommand.h"

class MessengerQueueCommand : public MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerQueueCommand)

public:
    explicit MessengerQueueCommand(MessengerCommandManager *manager);

    void execute(const AbstractBot::IncomingMessage &message, const QString &args) override;
    QStringList details() const override;
    QStringList examples() const override;

protected:
    void configureParser(MessengerCommandParser &parser) const override;
};

BOOKING_RESOURCES_REGISTER_COMMAND(MessengerQueueCommand)

#endif // MESSENGERQUEUECOMMAND_H
