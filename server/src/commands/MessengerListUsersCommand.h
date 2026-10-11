#ifndef MESSENGERLISTUSERSCOMMAND_H
#define MESSENGERLISTUSERSCOMMAND_H

#include "MessengerAbstractCommand.h"

class MessengerListUsersCommand : public MessengerAbstractCommand
{
    Q_DECLARE_TR_FUNCTIONS(MessengerListUsersCommand)

public:
    explicit MessengerListUsersCommand(MessengerCommandManager *manager);

    void execute(const AbstractBot::IncomingMessage &message, const QString &args) override;
    QStringList details() const override;
    QStringList examples() const override;
};

BOOKING_RESOURCES_REGISTER_COMMAND(MessengerListUsersCommand)

#endif // MESSENGERLISTUSERSCOMMAND_H
